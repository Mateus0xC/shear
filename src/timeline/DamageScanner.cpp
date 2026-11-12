#include "DamageScanner.h"
#include "core/CleanStart.h"
#include "util/Metrics.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <algorithm>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

namespace shear {

namespace {

// Alguns arquivos quebrados (clipes do ShadowPlay com o começo cortado) decodificam "sem erro" para o FFmpeg: os quadros
// não saem marcados como corrompidos, mas o decodificador avisa ("Frame num change", "reference count overflow"...).
// Os avisos de cada thread são contados enquanto ela decodifica uma amostra.
thread_local int t_decodeWarnings = 0;
void countDecodeWarnings(void *, int level, const char *fmt, va_list vl)
{
    if (level > AV_LOG_WARNING)
        return;
    char buf[256];
    vsnprintf(buf, sizeof buf, fmt, vl);
    static const char *const kPatterns[] = {"error while decoding", "Frame num change", "overflow", "illegal",
                                            "concealing", "corrupt", "missing picture", "no frame!"};
    for (const char *p : kPatterns)
        if (strstr(buf, p)) {
            ++t_decodeWarnings;
            return;
        }
}

// Decodifica uma janelinha do vídeo com um decodificador NOVO (um quadro corrompido pode "envenenar" o estado do
// decodificador e fazer tudo depois dele falhar, como se o arquivo inteiro estivesse quebrado).
struct Sampler {
    AVFormatContext *fmt;
    AVStream *st;
    int vi;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    ~Sampler()
    {
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }

    double offset() const { return st->start_time != AV_NOPTS_VALUE ? double(st->start_time) * av_q2d(st->time_base) : 0; }

    // Fração (0..1) dos quadros que saem BONS entre os `n` primeiros quadros a partir de `sec`. O decodificador começa
    // no keyframe anterior e os quadros antes de `sec` são só preparação (como faz um seek exato do player).
    double goodFraction(double sec, int n)
    {
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) return 0;
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, st->codecpar);
        ctx->thread_count = 1;
        // Não mexer em error_concealment / skip_loop_filter: sem o conserto de erros ligado, o decodificador deixa de
        // marcar os quadros quebrados (decode_error_flags) e o dano passa despercebido.
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            return 0;
        }
        const double tb = av_q2d(st->time_base);
        const int64_t target = int64_t((sec + offset()) / tb);
        int ok = 0, total = 0;
        t_decodeWarnings = 0;
        auto drain = [&] {
            while (avcodec_receive_frame(ctx, frame) == 0) {
                const int64_t ts = frame->best_effort_timestamp;
                if (ts != AV_NOPTS_VALUE && double(ts) * tb - offset() >= sec - 1e-3 && total < n) {
                    ++total;
                    if (frame->decode_error_flags == 0 && !(frame->flags & AV_FRAME_FLAG_CORRUPT))
                        ++ok;
                }
                av_frame_unref(frame);
            }
        };
        if (av_seek_frame(fmt, vi, target, AVSEEK_FLAG_BACKWARD) >= 0) {
            for (int guard = 0; guard < 300 && total < n; ++guard) {
                if (av_read_frame(fmt, pkt) < 0) break;
                if (pkt->stream_index == vi) {
                    avcodec_send_packet(ctx, pkt);
                    drain();
                }
                av_packet_unref(pkt);
            }
        }
        avcodec_free_context(&ctx);
        if (t_decodeWarnings > 0)
            return 0; // o decodificador reclamou: o trecho está quebrado, mesmo com os quadros "sem erro"
        return total > 0 ? double(ok) / n : 0; // quadro que nunca saiu conta como ruim
    }
};

// Abre o arquivo e acha o stream de vídeo (cada thread tem o seu).
struct Source {
    AVFormatContext *fmt = nullptr;
    int vi = -1;
    ~Source() { avformat_close_input(&fmt); }
    bool open(const QString &path, bool probe = true)
    {
        if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return false;
        if (probe && avformat_find_stream_info(fmt, nullptr) < 0) return false; // mp4 já traz os parâmetros no moov
        vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        return vi >= 0;
    }
};

// Devolve a partir de quando o vídeo está sadio (> 0 = há começo corrompido a pular; 0 = nada a pular). `decided`
// fica falso se não deu para concluir (erro ao abrir, cancelado, nenhum trecho sadio): não vale guardar o resultado.
double analyze(const QString &path, unsigned maxThreads, const std::function<bool()> &cancelled, bool *decided)
{
    *decided = false;
    av_log_set_level(AV_LOG_WARNING); // arquivo quebrado enche o log de avisos: o callback conta e não imprime nada
    av_log_set_callback(countDecodeWarnings);
    QElapsedTimer clock;
    clock.start();
    Source main;
    if (!main.open(path))
        return 0;
    const double duration = main.fmt->duration > 0 ? double(main.fmt->duration) / AV_TIME_BASE : 0;
    if (duration < 4)
        return 0;
    AVStream *st = main.fmt->streams[main.vi];
    double fps = av_q2d(st->avg_frame_rate);
    if (fps <= 1) fps = av_q2d(st->r_frame_rate);
    if (fps <= 1) fps = 30;

    Sampler sm{main.fmt, st, main.vi};
    constexpr int kFrames = 12; // cada amostra decodifica 12 quadros a partir de um keyframe
    auto perSecond = [&](Sampler &s, double sec) { return int(s.goodFraction(sec, kFrames) * fps); }; // "quadros bons por segundo"

    // Teste rápido: o início está sadio? (é o que roda em todo vídeo; o resto só se houver problema)
    if (perSecond(sm, 0) >= 0.5 * fps) {
        *decided = true;
        return 0;
    }

    const qint64 tProbe = clock.elapsed();
    // Há dano no começo. Amostra o vídeo de 6 em 6 s, em paralelo (cada amostra usa um decodificador novo).
    constexpr int kStride = 6;
    const int n = int(duration) / kStride;
    std::vector<int> counts(size_t(n), 0);
    std::vector<char> done(size_t(n), 0);
    std::mutex mu;
    std::atomic<bool> enough{false}; // já há amostras sadias suficientes depois do dano: não precisa olhar o resto
    std::atomic<int> next{0};
    const unsigned nthreads = std::clamp(std::thread::hardware_concurrency(), 2u, maxThreads);
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < nthreads; ++t)
        pool.emplace_back([&] {
            Source src;
            if (!src.open(path, false)) return;
            Sampler s{src.fmt, src.fmt->streams[src.vi], src.vi};
            for (int i; (i = next++) < n && !cancelled() && !enough;) {
                const int c = perSecond(s, double(i * kStride));
                std::lock_guard<std::mutex> lock(mu);
                counts[size_t(i)] = c;
                done[size_t(i)] = 1;
                // o dano é um começo: 3 amostras sadias seguidas (18 s) depois de alguma ruim bastam
                int j = 0, good = 0;
                while (j < n && done[size_t(j)]) ++j; // prefixo já medido
                for (int k = j - 1; k >= 0 && counts[size_t(k)] >= 0.5 * fps; --k) ++good;
                if (good >= 3 && j - good > 0) enough = true;
            }
        });
    for (auto &t : pool) t.join();
    if (cancelled())
        return 0;

    // Dano = amostras ruins seguidas; o vídeo é sadio depois da última sequência delas.
    if (enough) { // só o prefixo medido vale
        int j = 0;
        while (j < n && done[size_t(j)]) ++j;
        counts.resize(size_t(j));
    }
    const double clean = firstCleanSecond(counts, fps, 2); // em amostras (cada uma = 6 s)
    if (clean <= 0)
        return 0; // dano sem fim (ou só quedas isoladas): nada a pular
    // Refina entre a última amostra ruim e a primeira boa: os pontos intermediários (a cada ~1,2 s) são medidos em
    // paralelo, numa rodada só (a bisseção em série levava 3 a 4 medições seguidas).
    const int firstGood = int(clean); // índice da primeira amostra depois do dano
    const double lo = double((firstGood - 1) * kStride);
    double hi = double(firstGood * kStride);
    {
        constexpr int kSteps = 4; // 6 s em 5 intervalos de 1,2 s = 4 pontos internos
        std::vector<char> good(size_t(kSteps), 0);
        std::vector<std::thread> refine;
        for (int k = 0; k < kSteps; ++k)
            refine.emplace_back([&, k] {
                Source src;
                if (!src.open(path, false)) return;
                Sampler s{src.fmt, src.fmt->streams[src.vi], src.vi};
                const double t = lo + (hi - lo) * double(k + 1) / double(kSteps + 1);
                good[size_t(k)] = perSecond(s, t) >= 0.5 * fps ? 1 : 0;
            });
        for (auto &t : refine) t.join();
        const double step = (hi - lo) / double(kSteps + 1);
        for (int k = 0; k < kSteps; ++k)
            if (good[size_t(k)]) { hi = lo + step * double(k + 1); break; }
    }
    metrics::log(QString("análise do começo de %1: pronta em %2 ms (teste rápido %3 ms); vídeo sadio a partir de ~%4 s")
                     .arg(QFileInfo(path).fileName()).arg(clock.elapsed()).arg(tProbe).arg(hi, 0, 'f', 1));
    *decided = true;
    return hi;
}

} // namespace

DamageScanner::DamageScanner(QString path, QObject *parent) : QThread(parent), m_path(std::move(path)) {}

void DamageScanner::run()
{
    bool decided = false;
    const double clean = analyze(m_path, 6, [this] { return isInterruptionRequested(); }, &decided);
    if (clean > 0)
        emit damaged(clean);
}

DamagePrefetch::DamagePrefetch(QStringList paths, QObject *parent) : QThread(parent), m_paths(std::move(paths)) {}

void DamagePrefetch::run()
{
    for (const QString &p : std::as_const(m_paths)) {
        if (isInterruptionRequested())
            return;
        bool decided = false;
        // poucas threads: é trabalho de fundo, não pode atrapalhar quem está usando o app
        const double clean = analyze(p, 2, [this] { return isInterruptionRequested(); }, &decided);
        if (decided)
            emit analyzed(p, clean);
    }
}

} // namespace shear

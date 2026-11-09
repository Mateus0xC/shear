#include "DamageScanner.h"
#include "core/CleanStart.h"
#include "util/Metrics.h"
#include <QElapsedTimer>
#include <atomic>
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

DamageScanner::DamageScanner(QString path, QObject *parent) : QThread(parent), m_path(std::move(path)) {}

namespace {

// Decodifica uma janelinha do vídeo com um decodificador NOVO (um quadro corrompido pode "envenenar" o estado do
// decodificador e fazer tudo depois dele falhar, como se o arquivo inteiro estivesse quebrado). Conta os quadros que
// saíram bons com instante em [sec, sec + window).
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

    int okFrames(double sec, double window, bool *keyOk = nullptr)
    {
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) return 0;
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, st->codecpar);
        ctx->thread_count = 1;
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            return 0;
        }
        const double tb = av_q2d(st->time_base);
        const int64_t target = int64_t((sec + offset()) / tb);
        int ok = 0;
        if (av_seek_frame(fmt, vi, target, AVSEEK_FLAG_BACKWARD) >= 0) {
            bool done = false;
            for (int guard = 0; guard < 400 && !done; ++guard) {
                if (av_read_frame(fmt, pkt) < 0) break;
                if (pkt->stream_index == vi) {
                    avcodec_send_packet(ctx, pkt);
                    while (avcodec_receive_frame(ctx, frame) == 0) {
                        const int64_t ts = frame->best_effort_timestamp;
                        if (ts != AV_NOPTS_VALUE) {
                            const double t = double(ts) * tb - offset();
                            if (t >= sec + window) done = true;
                            else if (t >= sec && frame->decode_error_flags == 0 && !(frame->flags & AV_FRAME_FLAG_CORRUPT))
                                ++ok;
                        }
                        av_frame_unref(frame);
                    }
                }
                av_packet_unref(pkt);
            }
        }
        if (keyOk) *keyOk = ok > 0;
        avcodec_free_context(&ctx);
        return ok;
    }
};

} // namespace

// Abre o arquivo e prepara um Sampler (cada thread tem o seu).
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

void DamageScanner::run()
{
    av_log_set_level(AV_LOG_QUIET); // arquivo quebrado enche o log de avisos
    QElapsedTimer clock;
    clock.start();
    Source main;
    if (!main.open(m_path))
        return;
    const double duration = main.fmt->duration > 0 ? double(main.fmt->duration) / AV_TIME_BASE : 0;
    if (duration < 4)
        return;
    AVStream *st = main.fmt->streams[main.vi];
    double fps = av_q2d(st->avg_frame_rate);
    if (fps <= 1) fps = av_q2d(st->r_frame_rate);
    if (fps <= 1) fps = 30;

    Sampler sm{main.fmt, st, main.vi};
    constexpr double kWindow = 0.25; // cada amostra decodifica um quarto de segundo de vídeo
    auto perSecond = [&](Sampler &s, double sec) { return int(s.okFrames(sec, kWindow) / kWindow); };

    // Teste rápido: o início está sadio? (é o que roda em todo vídeo; o resto só se houver problema)
    if (perSecond(sm, 0) >= 0.5 * fps)
        return;

    const qint64 tProbe = clock.elapsed();
    // Há dano no começo. Amostra o vídeo todo de 3 em 3 s, em paralelo (cada amostra usa um decodificador novo).
    constexpr int kStride = 3;
    const int n = int(duration) / kStride;
    std::vector<int> counts(size_t(n), 0);
    std::vector<char> done(size_t(n), 0);
    std::mutex mu;
    std::atomic<bool> enough{false}; // já há amostras sadias suficientes depois do dano: não precisa olhar o resto
    std::atomic<int> next{0};
    const unsigned nthreads = std::clamp(std::thread::hardware_concurrency(), 2u, 6u);
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < nthreads; ++t)
        pool.emplace_back([&] {
            Source src;
            if (!src.open(m_path, false)) return;
            Sampler s{src.fmt, src.fmt->streams[src.vi], src.vi};
            for (int i; (i = next++) < n && !isInterruptionRequested() && !enough;) {
                const int c = perSecond(s, double(i * kStride));
                std::lock_guard<std::mutex> lock(mu);
                counts[size_t(i)] = c;
                done[size_t(i)] = 1;
                // o dano é um começo: 5 amostras sadias seguidas (15 s) depois de alguma ruim bastam
                int j = 0, good = 0;
                while (j < n && done[size_t(j)]) ++j; // prefixo já medido
                for (int k = j - 1; k >= 0 && counts[size_t(k)] >= 0.5 * fps; --k) ++good;
                if (good >= 5 && j - good > 0) enough = true;
            }
        });
    for (auto &t : pool) t.join();
    if (isInterruptionRequested())
        return;

    // Dano = amostras ruins seguidas; o vídeo é sadio depois da última sequência delas.
    if (enough) { // só o prefixo medido vale
        int j = 0;
        while (j < n && done[size_t(j)]) ++j;
        counts.resize(size_t(j));
    }
    const double clean = firstCleanSecond(counts, fps, 2); // em amostras (cada uma = 3 s)
    if (clean <= 0)
        return;
    // Refina entre a última amostra ruim e a primeira boa: os dois segundos de permeio.
    const int firstGood = int(clean); // índice da primeira amostra depois do dano
    double start = double(firstGood * kStride);
    for (int k = 1; k < kStride; ++k) {
        const double sec = double((firstGood - 1) * kStride + k);
        if (perSecond(sm, sec) >= 0.5 * fps) { start = sec; break; }
    }
    metrics::log(QString("análise do começo: pronta em %1 ms (teste rápido %2 ms); vídeo sadio a partir de ~%3 s")
                     .arg(clock.elapsed()).arg(tProbe).arg(start, 0, 'f', 1));
    emit damaged(start);
}

} // namespace shear

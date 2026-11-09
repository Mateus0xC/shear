#include "DamageScanner.h"
#include "core/CleanStart.h"
#include <algorithm>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

namespace shear {

DamageScanner::DamageScanner(QString path, QObject *parent) : QThread(parent), m_path(std::move(path)) {}

void DamageScanner::run()
{
    av_log_set_level(AV_LOG_QUIET); // arquivo quebrado enche o log de avisos
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, m_path.toUtf8().constData(), nullptr, nullptr) < 0)
        return;
    AVCodecContext *ctx = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    auto cleanup = [&] {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
    };
    if (avformat_find_stream_info(fmt, nullptr) < 0) { cleanup(); return; }
    const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vi < 0) { cleanup(); return; }
    AVStream *st = fmt->streams[vi];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) { cleanup(); return; }
    ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    ctx->thread_count = 2; // sem competir demais com o player
    if (avcodec_open2(ctx, codec, nullptr) < 0) { cleanup(); return; }

    double fps = av_q2d(st->avg_frame_rate);
    if (fps <= 1) fps = av_q2d(st->r_frame_rate);
    if (fps <= 1) fps = 30;
    const double duration = fmt->duration > 0 ? double(fmt->duration) / AV_TIME_BASE : 0;
    if (duration < 4) { cleanup(); return; }
    const double tb = av_q2d(st->time_base);

    std::vector<double> ok;      // quadros que decodificaram bem
    std::vector<double> okKeys;  // keyframes que decodificaram bem
    bool checked = false;        // o teste rápido dos 3 primeiros segundos já foi feito
    constexpr double kProbeSec = 0.5;

    auto drain = [&] {
        while (avcodec_receive_frame(ctx, frame) == 0) {
            const int64_t ts = frame->best_effort_timestamp;
            if (ts != AV_NOPTS_VALUE && frame->decode_error_flags == 0 && !(frame->flags & AV_FRAME_FLAG_CORRUPT)) {
                const double t = double(ts) * tb - (st->start_time != AV_NOPTS_VALUE ? double(st->start_time) * tb : 0);
                ok.push_back(t);
                if (frame->flags & AV_FRAME_FLAG_KEY) okKeys.push_back(t);
            }
            av_frame_unref(frame);
        }
    };

    bool eof = false;
    while (!isInterruptionRequested()) {
        const int r = av_read_frame(fmt, pkt);
        if (r < 0) { avcodec_send_packet(ctx, nullptr); drain(); eof = true; break; }
        if (pkt->stream_index == vi) {
            const double pt = pkt->pts != AV_NOPTS_VALUE
                                  ? double(pkt->pts) * tb - (st->start_time != AV_NOPTS_VALUE ? double(st->start_time) * tb : 0)
                                  : 0;
            avcodec_send_packet(ctx, pkt);
            drain();
            if (!checked && pt > kProbeSec + 0.25) {
                checked = true;
                const double expected = kProbeSec * fps;
                const long good = std::count_if(ok.begin(), ok.end(), [](double t) { return t < kProbeSec; });
                if (double(good) >= 0.5 * expected) { av_packet_unref(pkt); cleanup(); return; } // início sadio
            }
        }
        av_packet_unref(pkt);
    }
    if (isInterruptionRequested() || !eof) { cleanup(); return; }
    cleanup();

    const double clean = firstCleanTime(ok, fps, duration);
    if (clean <= 0)
        return;
    std::sort(okKeys.begin(), okKeys.end());
    const auto it = std::lower_bound(okKeys.begin(), okKeys.end(), clean);
    emit damaged(it != okKeys.end() ? *it : clean);
}

} // namespace shear

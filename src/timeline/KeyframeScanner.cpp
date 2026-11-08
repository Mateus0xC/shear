#include "KeyframeScanner.h"
#include <QElapsedTimer>

extern "C" {
#include <libavformat/avformat.h>
}

namespace shear {

KeyframeScanner::KeyframeScanner(QString path, QObject *parent) : QThread(parent), m_path(std::move(path)) {}

void KeyframeScanner::run()
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, m_path.toUtf8().constData(), nullptr, nullptr) < 0) {
        emit failed();
        return;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        emit failed();
        return;
    }
    int video = -1, audio = 0;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const auto type = fmt->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && video < 0
            && !(fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
            video = int(i);
        else if (type == AVMEDIA_TYPE_AUDIO)
            ++audio;
    }
    const double dur = fmt->duration > 0 ? double(fmt->duration) / AV_TIME_BASE : 0.0;
    emit probed(dur, audio);
    if (video < 0) {
        avformat_close_input(&fmt);
        return;
    }

    const AVRational tb = fmt->streams[video]->time_base;
    const int64_t origin = fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time : 0;
    AVPacket *pkt = av_packet_alloc();
    QVector<double> batch;
    QElapsedTimer flush;
    flush.start();
    while (!isInterruptionRequested() && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == video && (pkt->flags & AV_PKT_FLAG_KEY)) {
            const int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
            if (ts != AV_NOPTS_VALUE) {
                const double t = double(ts) * av_q2d(tb) - double(origin) / AV_TIME_BASE;
                batch.append(t < 0 ? 0.0 : t);
            }
        }
        av_packet_unref(pkt);
        if (!batch.isEmpty() && flush.elapsed() > 60) {
            emit keyframes(batch);
            batch.clear();
            flush.restart();
        }
    }
    if (!batch.isEmpty())
        emit keyframes(batch);
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
}

} // namespace shear

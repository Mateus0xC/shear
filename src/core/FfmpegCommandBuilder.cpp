#include "FfmpegCommandBuilder.h"
#include <algorithm>

namespace shear {

QString fmtSeconds(double s)
{
    return QString::number(std::max(0.0, s), 'f', 3);
}

static QString num(double v) { return QString::number(v, 'g', 6); }

static QString gainFilter(double g)
{
    return g == 0.0 ? QString("anull") : QString("volume=%1dB").arg(num(g));
}

int enabledTrackCount(const std::vector<AudioTrack> &tracks)
{
    return int(std::count_if(tracks.begin(), tracks.end(), [](const AudioTrack &t) { return t.enabled; }));
}

QString buildAudioMixGraph(const std::vector<AudioTrack> &tracks, bool forMpv, const QString &outLabel)
{
    const bool recipe = enabledTrackCount(tracks) >= 2; // junta: receita do usuário
    struct In { QString pad, chain, label; };
    std::vector<In> ins;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (!tracks[i].enabled)
            continue;
        const double g = tracks[i].gainDb;
        In in;
        in.pad = forMpv ? QString("[aid%1]").arg(i + 1) : QString("[0:a:%1]").arg(i);
        if (recipe && i == 1) {
            in.chain = QString("highpass=f=100,lowpass=f=8000,volume=%1dB,agate=range=0:threshold=0.03:release=200")
                           .arg(num(20 + g));
            in.label = "mic";
        } else if (recipe && i == 0) {
            in.chain = QString("volume=%1dB").arg(num(10 + g));
            in.label = "desktop";
        } else {
            in.chain = gainFilter(g);
            in.label = QString("t%1").arg(i);
        }
        ins.push_back(in);
    }
    if (ins.empty())
        return {};
    if (ins.size() == 1)
        return QString("%1%2[%3]").arg(ins[0].pad, ins[0].chain, outLabel);

    QStringList parts;
    QString mixIn;
    for (const auto &in : ins) {
        parts << QString("%1%2[%3]").arg(in.pad, in.chain, in.label);
        mixIn += QString("[%1]").arg(in.label);
    }
    parts << QString("%1amix=%2:longest[%3]").arg(mixIn).arg(ins.size()).arg(outLabel);
    return parts.join(';');
}

static void videoEncodeArgs(QStringList &a, const ExportRequest &r)
{
    if (r.hasNvenc)
        a << "-c:v" << "h264_nvenc" << "-preset" << "p3" << "-rc" << "vbr" << "-cq" << "19" << "-b:v" << "0";
    else
        a << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "18";
}

QStringList buildFfmpegArgs(const ExportRequest &r, const CutPlan &plan)
{
    QStringList a;
    a << "-hide_banner" << "-n" << "-nostats" << "-progress" << "pipe:1";

    const double start = plan.actualStartSec;
    const double dur = std::max(0.001, r.endSec - start);
    a << "-ss" << fmtSeconds(start) << "-t" << fmtSeconds(dur) << "-i" << r.inputPath;
    a << "-map" << "0:V:0";

    QStringList audio; // opções de áudio, depois das de vídeo
    const int n = enabledTrackCount(r.audio);
    if (n == 0) {
        audio << "-an";
    } else if (n == 1) {
        size_t idx = 0;
        while (!r.audio[idx].enabled) ++idx;
        if (r.audio[idx].gainDb == 0.0) { // cópia direta
            a << "-map" << QString("0:a:%1").arg(idx);
            audio << "-c:a" << "copy";
        } else {
            a << "-filter_complex" << buildAudioMixGraph(r.audio, false, "aout") << "-map" << "[aout]";
            audio << "-c:a" << "aac" << "-b:a" << "320k";
        }
    } else {
        a << "-filter_complex" << buildAudioMixGraph(r.audio, false, "aout") << "-map" << "[aout]";
        audio << "-c:a" << "aac" << "-b:a" << "320k";
    }

    if (plan.copyVideo)
        a << "-c:v" << "copy" << "-avoid_negative_ts" << "make_zero";
    else
        videoEncodeArgs(a, r);
    a << audio << "-movflags" << "+faststart" << r.outputPath;
    return a;
}

} // namespace shear

#include "FfmpegCommandBuilder.h"
#include <algorithm>

namespace shear {

const char *const kMixFilter =
    "[0:a:1]highpass=f=100,lowpass=f=8000,volume=20dB,agate=range=0:threshold=0.03:release=200[mic];"
    "[0:a:0]volume=10dB[desktop];"
    "[desktop][mic]amix=2:longest[aout]";

const char *const kDesktopFilter = "[0:a:0]volume=10dB[aout]";

QString fmtSeconds(double s)
{
    return QString::number(std::max(0.0, s), 'f', 3);
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

    if (r.preset == Preset::MixMic) {
        a << "-filter_complex" << (r.hasMicTrack ? kMixFilter : kDesktopFilter)
          << "-map" << "0:V:0" << "-map" << "[aout]";
        if (plan.copyVideo)
            a << "-c:v" << "copy" << "-avoid_negative_ts" << "make_zero";
        else
            videoEncodeArgs(a, r);
        a << "-c:a" << "aac" << "-b:a" << "320k";
    } else { // QuickCut
        if (r.tracks == AudioTracks::All)
            a << "-map" << "0";
        else
            a << "-map" << "0:V:0" << "-map" << "0:a:0";
        if (plan.copyVideo)
            a << "-c" << "copy" << "-avoid_negative_ts" << "make_zero";
        else {
            videoEncodeArgs(a, r);
            a << "-c:a" << "copy";
        }
    }
    a << "-movflags" << "+faststart" << r.outputPath;
    return a;
}

} // namespace shear

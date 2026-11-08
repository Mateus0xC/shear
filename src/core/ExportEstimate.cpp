#include "ExportEstimate.h"
#include "FfmpegCommandBuilder.h"
#include <algorithm>
#include <cmath>

namespace shear {

// Tempos fixos de arranque do ffmpeg/abertura/fechamento, medidos nos testes.
static constexpr double kCopyOverhead = 0.25, kAudioOverhead = 0.4, kVideoOverhead = 0.6;

ExportKind exportKind(const ExportRequest &r, const CutPlan &plan)
{
    if (!plan.copyVideo)
        return ExportKind::VideoReencode;
    const int n = enabledTrackCount(r.audio);
    if (n >= 2)
        return ExportKind::AudioReencode;
    if (n == 1) {
        for (const auto &t : r.audio)
            if (t.enabled && t.gainDb != 0.0)
                return ExportKind::AudioReencode;
    }
    return ExportKind::Copy;
}

double estimateSeconds(ExportKind k, bool nvenc, double clipSec, double clipBytes, const ExportRates &rt)
{
    clipSec = std::max(0.0, clipSec);
    clipBytes = std::max(0.0, clipBytes);
    const double transfer = clipBytes / (std::max(1.0, rt.copyMBps) * 1e6);
    switch (k) {
    case ExportKind::Copy:
        return kCopyOverhead + transfer;
    case ExportKind::AudioReencode:
        return kAudioOverhead + transfer + clipSec / std::max(0.1, rt.audioRealtime);
    case ExportKind::VideoReencode:
        return kVideoOverhead + clipSec / std::max(0.1, nvenc ? rt.nvencRealtime : rt.x264Realtime);
    }
    return 0;
}

ExportRates calibrate(ExportRates rt, ExportKind k, bool nvenc, double clipSec, double clipBytes, double elapsed)
{
    auto blend = [](double oldV, double observed, double lo, double hi) {
        return std::clamp(0.5 * oldV + 0.5 * std::clamp(observed, lo, hi), lo, hi);
    };
    if (clipSec <= 0 || elapsed <= 0)
        return rt;
    switch (k) {
    case ExportKind::Copy:
        if (clipBytes > 0)
            rt.copyMBps = blend(rt.copyMBps, (clipBytes / 1e6) / std::max(0.05, elapsed - kCopyOverhead), 20, 3000);
        break;
    case ExportKind::AudioReencode: {
        const double transfer = clipBytes / (std::max(1.0, rt.copyMBps) * 1e6);
        rt.audioRealtime = blend(rt.audioRealtime, clipSec / std::max(0.05, elapsed - kAudioOverhead - transfer), 2, 500);
        break;
    }
    case ExportKind::VideoReencode: {
        const double observed = clipSec / std::max(0.05, elapsed - kVideoOverhead);
        if (nvenc)
            rt.nvencRealtime = blend(rt.nvencRealtime, observed, 0.3, 80);
        else
            rt.x264Realtime = blend(rt.x264Realtime, observed, 0.3, 80);
        break;
    }
    }
    return rt;
}

QString formatDuration(double sec)
{
    if (sec < 1.0)
        return QStringLiteral("menos de 1 s");
    const int s = int(std::lround(sec));
    if (s < 60)
        return QString("~%1 s").arg(s);
    return QString("~%1 min %2 s").arg(s / 60).arg(s % 60);
}

} // namespace shear

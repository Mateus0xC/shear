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

QString buildAudioMixGraph(const std::vector<AudioTrack> &tracks, bool forMpv, const QString &outLabel, int inputIndex)
{
    const bool recipe = enabledTrackCount(tracks) >= 2; // junta: receita do usuário
    struct In { QString pad, chain, label; };
    std::vector<In> ins;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (!tracks[i].enabled)
            continue;
        const double g = tracks[i].gainDb;
        In in;
        in.pad = forMpv ? QString("[aid%1]").arg(i + 1) : QString("[%1:a:%2]").arg(inputIndex).arg(i);
        if (recipe && i == 1) {
            in.chain = QString("highpass=f=100,lowpass=f=8000,volume=%1dB,agate=range=0:threshold=0.03:release=200")
                           .arg(num(20 + g));
            in.label = "mic";
        } else if (recipe && i == 0) {
            in.chain = gainFilter(g); // desktop mantém o nível original (sem boost)
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
    // normalize=0: o amix não divide cada entrada pelo nº de faixas, então o desktop NÃO muda de volume ao
    // ligar o mic. O alimiter (sem auto-level) só impede clipping quando as duas faixas estão altas.
    parts << QString("%1amix=inputs=%2:duration=longest:normalize=0,alimiter=limit=0.95:level=0[%3]")
                 .arg(mixIn).arg(ins.size()).arg(outLabel);
    return parts.join(';');
}

static void videoEncodeArgs(QStringList &a, const ExportRequest &r)
{
    if (r.hasNvenc)
        a << "-c:v" << "h264_nvenc" << "-preset" << "p3" << "-rc" << "vbr" << "-cq" << "19" << "-b:v" << "0";
    else
        a << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "18";
}

// Vários pedaços (trechos do meio apagados) e/ou fade: cada pedaço é um `-ss/-t -i` (o ffmpeg pula direto, sem decodificar
// o que foi apagado) e um filter_complex emenda tudo com `concat`. Recodifica o vídeo e o áudio (AAC).
static QStringList buildAssembledArgs(const ExportRequest &r)
{
    const std::vector<Range> segs = keptSegments(r);
    QStringList a;
    a << "-hide_banner" << "-n" << "-nostats" << "-progress" << "pipe:1";
    for (const Range &s : segs)
        a << "-ss" << fmtSeconds(s.first) << "-t" << fmtSeconds(s.second - s.first) << "-i" << r.inputPath;

    const bool hasAudio = enabledTrackCount(r.audio) > 0;
    QStringList graph, concatIn;
    for (size_t k = 0; k < segs.size(); ++k) {
        const double len = segs[k].second - segs[k].first;
        const double f = std::min(r.fadeSec, len / 2); // o fade nunca passa da metade do pedaço
        // Fade só nas EMENDAS: o pedaço que vem depois de um corte entra com fade, o que vem antes de um corte sai com
        // fade. O começo e o fim do vídeo ficam como estão.
        const bool fadeIn = k > 0, fadeOut = k + 1 < segs.size();
        QStringList vf, af;
        if (f > 0.001) {
            if (fadeIn) { vf << QString("fade=t=in:st=0:d=%1").arg(num(f)); af << QString("afade=t=in:st=0:d=%1").arg(num(f)); }
            if (fadeOut) { vf << QString("fade=t=out:st=%1:d=%2").arg(num(len - f), num(f)); af << QString("afade=t=out:st=%1:d=%2").arg(num(len - f), num(f)); }
        }
        QString v = QString("[%1:V:0]setpts=PTS-STARTPTS").arg(k);
        if (!vf.isEmpty())
            v += "," + vf.join(',');
        graph << v + QString("[v%1]").arg(k);
        concatIn << QString("[v%1]").arg(k);
        if (hasAudio) {
            graph << buildAudioMixGraph(r.audio, false, QString("m%1").arg(k), int(k));
            QString au = QString("[m%1]asetpts=PTS-STARTPTS").arg(k);
            if (!af.isEmpty())
                au += "," + af.join(',');
            graph << au + QString("[a%1]").arg(k);
            concatIn << QString("[a%1]").arg(k);
        }
    }
    graph << QString("%1concat=n=%2:v=1:a=%3[vout]%4")
                 .arg(concatIn.join(QString())).arg(segs.size()).arg(hasAudio ? 1 : 0).arg(hasAudio ? "[aout]" : "");
    a << "-filter_complex" << graph.join(';') << "-map" << "[vout]";
    if (hasAudio)
        a << "-map" << "[aout]";
    videoEncodeArgs(a, r);
    if (hasAudio)
        a << "-c:a" << "aac" << "-b:a" << "320k";
    else
        a << "-an";
    a << "-movflags" << "+faststart" << r.outputPath;
    return a;
}

QStringList buildFfmpegArgs(const ExportRequest &r, const CutPlan &plan)
{
    if (plan.assembled && !keptSegments(r).empty())
        return buildAssembledArgs(r);
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

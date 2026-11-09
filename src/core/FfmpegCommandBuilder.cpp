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

Compression planCompression(const ExportRequest &r, double seconds)
{
    Compression c;
    if (r.maxBytes <= 0 || seconds <= 0)
        return c;
    const bool hasAudio = enabledTrackCount(r.audio) > 0;
    // 6% de folga para o contêiner (cabeçalhos, índice) e para o bitrate não ser 100% exato
    const double totalKbps = double(r.maxBytes) * 8.0 * 0.94 / 1000.0 / seconds;
    c.audioKbps = hasAudio ? (totalKbps < 450 ? 64 : 96) : 0;
    c.videoKbps = std::max(60, int((totalKbps - c.audioKbps) * r.bitrateScale));
    // quanto menos bitrate, menor a resolução (mais nítido do que esticar poucos bits num quadro grande)
    c.height = c.videoKbps >= 4000 ? 0 : c.videoKbps >= 2000 ? 720 : c.videoKbps >= 1000 ? 540 : 360;
    c.cap30fps = c.videoKbps < 2500;
    return c;
}

// Filtros de vídeo da compressão (vazio = nenhum). A vírgula dentro de min() é escapada para o ffmpeg.
static QString compressionFilters(const Compression &c)
{
    QStringList f;
    if (c.height > 0)
        f << QString("scale=-2:min(%1\\,ih)").arg(c.height); // nunca aumenta o vídeo
    if (c.cap30fps)
        f << "fps=fps=min(30\\,source_fps)";
    return f.join(',');
}

static void videoEncodeArgs(QStringList &a, const ExportRequest &r, const Compression &c = {})
{
    if (c.videoKbps > 0) { // compressão: bitrate fixo, perto do alvo
        const QString k = QString::number(c.videoKbps) + "k", buf = QString::number(c.videoKbps * 2) + "k";
        if (r.hasNvenc)
            a << "-c:v" << "h264_nvenc" << "-preset" << "p5" << "-rc" << "cbr" << "-b:v" << k << "-maxrate" << k << "-bufsize" << buf;
        else
            a << "-c:v" << "libx264" << "-preset" << "veryfast" << "-b:v" << k << "-maxrate" << k << "-bufsize" << buf;
        return;
    }
    if (r.hasNvenc)
        a << "-c:v" << "h264_nvenc" << "-preset" << "p3" << "-rc" << "vbr" << "-cq" << "19" << "-b:v" << "0";
    else
        a << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "18";
}

// Vários pedaços (trechos do meio apagados): cada pedaço é um `-ss/-t -i` (o ffmpeg pula direto, sem decodificar o que
// foi apagado) e um filter_complex emenda tudo. Recodifica o vídeo e o áudio (AAC).
//  - Vídeo: concat; com fade, o pedaço antes da emenda sai com fade e o depois entra com fade (começo e fim do vídeo não).
//  - Áudio com fade: CROSSFADE nas emendas (acrossfade). Para a duração total continuar igual à do vídeo, o áudio de cada
//    pedaço que vem antes de uma emenda é lido um pouco além do fim (a mesma duração do crossfade): a sobreposição consome
//    esse trecho extra e os dois ficam em sincronia.
static QStringList buildAssembledArgs(const ExportRequest &r)
{
    const std::vector<Range> segs = keptSegments(r);
    const size_t n = segs.size();
    std::vector<double> len(n), join(n, 0.0); // join[k] = duração do fade na emenda entre os pedaços k e k+1
    for (size_t k = 0; k < n; ++k)
        len[k] = segs[k].second - segs[k].first;
    for (size_t k = 0; k + 1 < n; ++k)
        join[k] = r.fadeSec > 0.001 ? std::min({r.fadeSec, len[k] / 2, len[k + 1] / 2}) : 0.0;
    const bool crossfade = std::any_of(join.begin(), join.end(), [](double d) { return d > 0.001; });
    const bool hasAudio = enabledTrackCount(r.audio) > 0;
    const Compression comp = planCompression(r, keptSeconds(r));
    const QString vf = compressionFilters(comp);
    const QString vlabel = vf.isEmpty() ? "[vout]" : "[vcat]"; // com compressão, os filtros vêm depois do concat

    QStringList a;
    a << "-hide_banner" << "-n" << "-nostats" << "-progress" << "pipe:1";
    for (size_t k = 0; k < n; ++k) {
        const double ext = (hasAudio && crossfade && k + 1 < n) ? join[k] : 0.0; // áudio lido além do fim, para o crossfade
        a << "-ss" << fmtSeconds(segs[k].first) << "-t" << fmtSeconds(len[k] + ext) << "-i" << r.inputPath;
    }

    QStringList graph, vIn, aIn;
    for (size_t k = 0; k < n; ++k) {
        QString v = QString("[%1:V:0]trim=duration=%2,setpts=PTS-STARTPTS").arg(k).arg(num(len[k]));
        if (k > 0 && join[k - 1] > 0.001)
            v += QString(",fade=t=in:st=0:d=%1").arg(num(join[k - 1]));
        if (k + 1 < n && join[k] > 0.001)
            v += QString(",fade=t=out:st=%1:d=%2").arg(num(len[k] - join[k]), num(join[k]));
        graph << v + QString("[v%1]").arg(k);
        vIn << QString("[v%1]").arg(k);
        if (hasAudio) {
            graph << buildAudioMixGraph(r.audio, false, QString("m%1").arg(k), int(k));
            graph << QString("[m%1]asetpts=PTS-STARTPTS[a%1]").arg(k);
            aIn << QString("[a%1]").arg(k);
        }
    }
    if (hasAudio && !crossfade) { // sem fade: vídeo e áudio entram juntos no concat
        QString both;
        for (size_t k = 0; k < n; ++k)
            both += vIn[int(k)] + aIn[int(k)];
        graph << QString("%1concat=n=%2:v=1:a=1%3[aout]").arg(both).arg(n).arg(vlabel);
    } else {
        graph << QString("%1concat=n=%2:v=1:a=0%3").arg(vIn.join(QString())).arg(n).arg(vlabel);
        if (hasAudio) { // crossfade encadeado: ((a0 x a1) x a2) ...
            QString cur = "[a0]";
            for (size_t k = 1; k < n; ++k) {
                const QString out = k + 1 == n ? "[aout]" : QString("[x%1]").arg(k);
                if (join[k - 1] > 0.001)
                    graph << QString("%1[a%2]acrossfade=d=%3:c1=tri:c2=tri%4").arg(cur).arg(k).arg(num(join[k - 1])).arg(out);
                else
                    graph << QString("%1[a%2]concat=n=2:v=0:a=1%3").arg(cur).arg(k).arg(out);
                cur = out;
            }
        }
    }
    if (!vf.isEmpty())
        graph << QString("[vcat]%1[vout]").arg(vf);
    a << "-filter_complex" << graph.join(';') << "-map" << "[vout]";
    if (hasAudio)
        a << "-map" << "[aout]";
    videoEncodeArgs(a, r, comp);
    if (hasAudio)
        a << "-c:a" << "aac" << "-b:a" << (comp.audioKbps > 0 ? QString::number(comp.audioKbps) + "k" : QString("320k"));
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

    const Compression comp = planCompression(r, dur);
    const QString aacRate = comp.audioKbps > 0 ? QString::number(comp.audioKbps) + "k" : QString("320k");
    QStringList audio; // opções de áudio, depois das de vídeo
    const int n = enabledTrackCount(r.audio);
    if (n == 0) {
        audio << "-an";
    } else if (n == 1) {
        size_t idx = 0;
        while (!r.audio[idx].enabled) ++idx;
        if (r.audio[idx].gainDb == 0.0) {
            a << "-map" << QString("0:a:%1").arg(idx);
            if (comp.audioKbps > 0) // comprimindo: o áudio é recodificado (a cópia pode ocupar mais do que o limite)
                audio << "-c:a" << "aac" << "-b:a" << aacRate;
            else // cópia direta
                audio << "-c:a" << "copy";
        } else {
            a << "-filter_complex" << buildAudioMixGraph(r.audio, false, "aout") << "-map" << "[aout]";
            audio << "-c:a" << "aac" << "-b:a" << aacRate;
        }
    } else {
        a << "-filter_complex" << buildAudioMixGraph(r.audio, false, "aout") << "-map" << "[aout]";
        audio << "-c:a" << "aac" << "-b:a" << aacRate;
    }

    if (plan.copyVideo) {
        a << "-c:v" << "copy" << "-avoid_negative_ts" << "make_zero";
    } else {
        if (const QString vf = compressionFilters(comp); !vf.isEmpty())
            a << "-vf" << vf;
        videoEncodeArgs(a, r, comp);
    }
    a << audio << "-movflags" << "+faststart" << r.outputPath;
    return a;
}

} // namespace shear

// Mede o tempo de exportação de cada preset usando o mesmo core/ do app.
// Uso: bench_export <ffmpeg.exe> <entrada.mp4> <início> <fim> <keyframe_anterior>
#include "core/CutPlanner.h"
#include "core/FfmpegCommandBuilder.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <cstdio>

using namespace shear;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 6) { fprintf(stderr, "uso: bench_export ffmpeg in start end kf\n"); return 2; }
    const QString ff = argv[1];
    ExportRequest base;
    base.inputPath = argv[2];
    base.startSec = QString(argv[3]).toDouble();
    base.endSec = QString(argv[4]).toDouble();
    base.keyframeBeforeStart = QString(argv[5]).toDouble();
    base.audio = {AudioTrack{}, AudioTrack{}}; // desktop + mic

    struct Case { const char *name; Preset p; CutMode m; double micGain = 0; };
    const Case cases[] = {
        {"Corte rápido (cópia, modo Rápido)", Preset::QuickCut, CutMode::Fast},
        {"Corte rápido (modo Exato = recodifica)", Preset::QuickCut, CutMode::Exact},
        {"Mixar mic+desktop (vídeo copiado)", Preset::MixMic, CutMode::Fast},
        {"Corte rápido + mic com +6 dB (áudio recodificado)", Preset::QuickCut, CutMode::Fast, 6},
    };
    for (const auto &c : cases) {
        ExportRequest r = base;
        r.preset = c.p;
        r.mode = c.m;
        r.audio[1].gainDb = c.micGain;
        r.outputPath = QString("bench_%1.mp4").arg(int(c.p) * 10 + int(c.m));
        QFile::remove(r.outputPath);
        const auto args = buildFfmpegArgs(r, planCut(r));
        QElapsedTimer t;
        t.start();
        QProcess p;
        p.start(ff, args);
        p.waitForFinished(-1);
        const double sec = t.elapsed() / 1000.0;
        printf("%-42s %6.2f s  exit=%d  %lld MB\n", c.name, sec, p.exitCode(),
               QFile(r.outputPath).size() / 1048576);
        if (p.exitCode() != 0) printf("%s\n", p.readAllStandardError().right(400).constData());
        fflush(stdout);
    }
    return 0;
}

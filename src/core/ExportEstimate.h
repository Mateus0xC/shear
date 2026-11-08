#pragma once
#include "CutPlanner.h"
#include "CutTypes.h"
#include <QString>

namespace shear {

enum class ExportKind {
    Copy,          // vídeo e áudio copiados: só I/O
    AudioReencode, // vídeo copiado, áudio recodificado (volume alterado ou faixas juntadas)
    VideoReencode  // vídeo recodificado (início exato): NVENC ou libx264
};

// Velocidades medidas nesta máquina. Começam com valores típicos e são recalibradas a cada exportação real.
struct ExportRates {
    double copyMBps = 400;     // vazão da cópia (disco), MB/s
    double audioRealtime = 17; // áudio recodificado: quantas vezes mais rápido que o tempo real
    double nvencRealtime = 6;  // vídeo no NVENC
    double x264Realtime = 2.5; // vídeo no libx264
};

ExportKind exportKind(const ExportRequest &r, const CutPlan &plan);

// Segundos previstos para exportar `clipSec` segundos de vídeo (`clipBytes` ≈ tamanho do trecho no arquivo).
double estimateSeconds(ExportKind k, bool nvenc, double clipSec, double clipBytes, const ExportRates &rates);

// Atualiza as taxas com uma exportação real (média de 50% antiga + 50% observada, com limites sensatos).
ExportRates calibrate(ExportRates rates, ExportKind k, bool nvenc, double clipSec, double clipBytes,
                      double elapsedSec);

// "menos de 1 s", "~3 s", "~1 min 20 s"
QString formatDuration(double sec);

} // namespace shear

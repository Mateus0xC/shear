#pragma once
#include "CutTypes.h"
#include "CutPlanner.h"

namespace shear {

extern const char *const kMixFilter;      // mic (a:1) + desktop (a:0) -> [aout]
extern const char *const kDesktopFilter;  // sem mic: só volume do desktop -> [aout]

// Argumentos do ffmpeg (sem o executável). Função pura.
QStringList buildFfmpegArgs(const ExportRequest &r, const CutPlan &plan);

// Formata segundos com 3 casas, sempre com ponto.
QString fmtSeconds(double s);

} // namespace shear

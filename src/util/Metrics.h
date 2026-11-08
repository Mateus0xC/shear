#pragma once
#include <QString>

namespace shear::metrics {

// Tempo (ms) desde a criação do processo até agora.
double processAgeMs();
// Escreve em stderr e em %LOCALAPPDATA%/Shear/metrics.log
void log(const QString &line);

} // namespace shear::metrics

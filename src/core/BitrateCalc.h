#pragma once

namespace shear {

constexpr int kDiscordAudioKbps = 128;
constexpr double kSafetyMargin = 0.95; // ~5% de margem

// Bitrate de vídeo em kbps: (alvoMB*8*1024/duração - áudio) * margem.
// Retorna 0 se o alvo for inviável (duração <= 0 ou bitrate <= mínimo).
int videoBitrateKbps(double targetMB, double durationSec, int audioKbps = kDiscordAudioKbps);

} // namespace shear

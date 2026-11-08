#pragma once
#include "CutTypes.h"
#include "CutPlanner.h"

namespace shear {

// Argumentos do ffmpeg (sem o executável). Função pura.
QStringList buildFfmpegArgs(const ExportRequest &r, const CutPlan &plan);

// Filtro que mixa as faixas em uma só, terminando em [outLabel]. Vazio se nenhuma faixa entra.
// useListen=true usa as faixas "ouvidas" (preview); false usa as "exportadas".
// forMpv=true usa as entradas [aidN] do mpv (lavfi-complex); false usa [0:a:N] do ffmpeg.
// No preset MixMic aplica o filtro do usuário: faixa 0 = desktop (+10 dB), faixa 1 = mic (filtros + 20 dB).
QString buildAudioMixGraph(const std::vector<AudioTrack> &tracks, Preset preset, bool useListen,
                           bool forMpv, const QString &outLabel);

// Corte rápido: o áudio pode ser copiado se nenhuma faixa exportada teve o volume alterado.
bool quickCutAudioIsCopy(const std::vector<AudioTrack> &tracks);
int exportedTrackCount(const std::vector<AudioTrack> &tracks);

// Formata segundos com 3 casas, sempre com ponto.
QString fmtSeconds(double s);

} // namespace shear

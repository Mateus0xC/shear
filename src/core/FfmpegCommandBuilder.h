#pragma once
#include "CutTypes.h"
#include "CutPlanner.h"

namespace shear {

// Argumentos do ffmpeg (sem o executável). Função pura.
//  - 0 faixas ligadas: sem áudio.
//  - 1 faixa ligada: copiada sem recodificar (ou recodificada só se o volume foi alterado).
//  - 2+ faixas ligadas: juntadas em uma só (AAC 320k) com a receita de mixagem.
QStringList buildFfmpegArgs(const ExportRequest &r, const CutPlan &plan);

// Filtro que leva as faixas ligadas a uma só, terminando em [outLabel]. Vazio se nenhuma está ligada.
// forMpv=true usa as entradas [aidN] do mpv (lavfi-complex); false usa [0:a:N] do ffmpeg.
// Com 2+ faixas ligadas aplica a receita: faixa 0 = desktop (nível original), faixa 1 = mic (highpass/lowpass/
// gate + 20 dB), amix sem normalização + limitador; o volume de cada faixa soma-se a esses ganhos.
// inputIndex: qual -i do ffmpeg tem as faixas ([N:a:i]); só para a exportação com vários pedaços.
QString buildAudioMixGraph(const std::vector<AudioTrack> &tracks, bool forMpv, const QString &outLabel,
                           int inputIndex = 0);

int enabledTrackCount(const std::vector<AudioTrack> &tracks);

// Compressão "até X MB": bitrates e resolução para caber em maxBytes em `seconds` de vídeo. O áudio sai em AAC (todas as
// faixas ligadas juntadas em uma). height 0 = mantém a resolução; sem o limite (maxBytes == 0) devolve zeros.
struct Compression {
    int videoKbps = 0;
    int audioKbps = 0;
    int height = 0;       // altura máxima do vídeo (0 = a original)
    bool cap30fps = false; // com pouco bitrate, 30 quadros/s rendem mais qualidade que 60
};
Compression planCompression(const ExportRequest &r, double seconds);

// Formata segundos com 3 casas, sempre com ponto.
QString fmtSeconds(double s);

} // namespace shear

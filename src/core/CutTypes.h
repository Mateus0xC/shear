#pragma once
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

namespace shear {

enum class CutMode { Fast, Exact };

// Uma faixa de áudio do arquivo. Ligada = você ouve no preview e ela vai no arquivo exportado.
struct AudioTrack {
    bool enabled = true;
    double gainDb = 0.0; // ajuste de volume; com 2+ faixas ligadas soma-se ao ganho-base da receita de mixagem
};

using Range = std::pair<double, double>; // [início, fim] em segundos

struct ExportRequest {
    QString inputPath;
    QString outputPath;
    double startSec = 0;          // início escolhido pelo usuário
    double endSec = 0;
    double keyframeBeforeStart = 0; // keyframe anterior ao início (modo Rápido)
    CutMode mode = CutMode::Fast;
    std::vector<AudioTrack> audio; // uma entrada por faixa do arquivo (a:0, a:1, ...)
    bool hasNvenc = true;
    std::vector<Range> gaps;      // trechos do MEIO a apagar (o que sobra é emendado); dentro de [startSec, endSec]
    double fadeSec = 0;           // > 0: esmaece cada EMENDA entre pedaços (o começo e o fim do vídeo não)
};

inline constexpr double kJoinFadeSec = 0.2; // duração do fade nas emendas: rápido, só para não "dar tranco"

} // namespace shear

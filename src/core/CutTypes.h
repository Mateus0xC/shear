#pragma once
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace shear {

enum class CutMode { Fast, Exact, Auto };
enum class Preset { QuickCut, MixMic };
enum class AudioTracks { DesktopOnly, All };

// Uma faixa de áudio do arquivo: se vai na exportação, se é ouvida no preview e o ajuste de volume.
struct AudioTrack {
    bool exportIt = true;
    bool listen = true;
    double gainDb = 0.0; // no preset de mixagem, soma-se ao ganho-base do filtro
};

struct ExportRequest {
    QString inputPath;
    QString outputPath;
    double startSec = 0;          // início escolhido pelo usuário
    double endSec = 0;
    double keyframeBeforeStart = 0; // keyframe anterior ao início (para modo Auto/Rápido)
    CutMode mode = CutMode::Auto;
    double autoThresholdSec = 0.5;
    Preset preset = Preset::QuickCut;
    std::vector<AudioTrack> audio; // uma entrada por faixa do arquivo (a:0, a:1, ...)
    bool hasNvenc = true;
};

} // namespace shear

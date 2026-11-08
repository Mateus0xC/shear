#pragma once
#include <QString>
#include <QStringList>
#include <optional>

namespace shear {

enum class CutMode { Fast, Exact, Auto };
enum class Preset { QuickCut, MixMic, DiscordLimit };
enum class AudioTracks { DesktopOnly, All };

struct ExportRequest {
    QString inputPath;
    QString outputPath;
    double startSec = 0;          // início escolhido pelo usuário
    double endSec = 0;
    double keyframeBeforeStart = 0; // keyframe anterior ao início (para modo Auto/Rápido)
    CutMode mode = CutMode::Auto;
    double autoThresholdSec = 0.5;
    Preset preset = Preset::QuickCut;
    AudioTracks tracks = AudioTracks::DesktopOnly;
    double targetMB = 10.0;
    bool hasNvenc = true;
    bool hasMicTrack = true;
};

} // namespace shear

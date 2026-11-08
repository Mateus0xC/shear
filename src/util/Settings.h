#pragma once
#include "core/CutTypes.h"
#include <QString>

namespace shear {

struct Config {
    CutMode mode = CutMode::Auto;
    Preset preset = Preset::QuickCut;
    AudioTracks tracks = AudioTracks::DesktopOnly;
    double autoThresholdSec = 0.5;
    QString outputDir;      // vazio = pasta do original
    QString shadowPlayDir;  // vazio = Vídeos do usuário

    static Config load();
    void save() const;
    QString effectiveShadowPlayDir() const;
};

} // namespace shear

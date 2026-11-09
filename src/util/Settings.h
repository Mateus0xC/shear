#pragma once
#include "core/CutTypes.h"
#include "core/ExportEstimate.h"
#include <QString>

namespace shear {

struct Config {
    CutMode mode = CutMode::Fast;
    bool scrubAudio = true; // ouvir o som enquanto arrasta na timeline
    bool gpuDecode = true; // preview decodificado no NVDEC (GPU NVIDIA); false = CPU
    bool fadeJoins = true; // suaviza as emendas dos trechos apagados (fade no vídeo, crossfade no áudio)
    QString outputDir;      // vazio = pasta do original
    QString shadowPlayDir;  // vazio = Vídeos do usuário

    static Config load();
    void save() const;
    QString effectiveShadowPlayDir() const;
};

// Velocidades de exportação medidas (calibram a previsão de tempo).
ExportRates loadRates();
void saveRates(const ExportRates &r);

} // namespace shear

#pragma once
#include "CutTypes.h"

namespace shear {

struct CutPlan {
    bool copyVideo = true;     // false => recodifica o vídeo
    double actualStartSec = 0; // onde o clipe realmente começa
    double earlyBySec = 0;     // quanto antes do pedido (modo rápido)
};

// Rápido: copia o vídeo (o início cai num keyframe). Exato: recodifica para cortar no frame certo.
CutPlan planCut(const ExportRequest &r);

// "o clipe vai começar 1,4 s antes"; vazio se não houver aviso.
QString earlyStartWarning(double earlyBySec);

} // namespace shear

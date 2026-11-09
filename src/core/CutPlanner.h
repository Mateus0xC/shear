#pragma once
#include "CutTypes.h"

namespace shear {

struct CutPlan {
    bool copyVideo = true;     // false => recodifica o vídeo
    double actualStartSec = 0; // onde o clipe realmente começa
    double earlyBySec = 0;     // quanto antes do pedido (modo rápido)
    bool assembled = false;    // há trechos apagados do meio e/ou fade: vários pedaços emendados e recodificados
};

// Pedaços que sobram de [startSec, endSec] depois de tirar os `gaps` (em ordem, sem sobrepor; pedaços < 0,05 s somem).
std::vector<Range> keptSegments(const ExportRequest &r);
// Duração total do que vai para o arquivo.
double keptSeconds(const ExportRequest &r);

// Rápido: copia o vídeo (o início cai num keyframe). Exato: recodifica para cortar no frame certo.
CutPlan planCut(const ExportRequest &r);

// "o clipe vai começar 1,4 s antes"; vazio se não houver aviso.
QString earlyStartWarning(double earlyBySec);

} // namespace shear

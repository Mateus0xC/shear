#include "CutPlanner.h"
#include <algorithm>

namespace shear {

std::vector<Range> keptSegments(const ExportRequest &r)
{
    std::vector<Range> gaps = r.gaps;
    std::sort(gaps.begin(), gaps.end());
    std::vector<Range> out;
    double cursor = r.startSec;
    for (const Range &g : gaps) {
        const double a = std::max(g.first, r.startSec), b = std::min(g.second, r.endSec);
        if (b <= a) continue; // fora do trecho escolhido
        if (a - cursor >= 0.05) out.push_back({cursor, a});
        cursor = std::max(cursor, b);
    }
    if (r.endSec - cursor >= 0.05) out.push_back({cursor, r.endSec});
    return out;
}

double keptSeconds(const ExportRequest &r)
{
    double s = 0;
    for (const Range &g : keptSegments(r))
        s += g.second - g.first;
    return s;
}

CutPlan planCut(const ExportRequest &r)
{
    CutPlan p;
    if (!r.gaps.empty()) { // trechos apagados (e o fade das emendas) pedem recodificar: o corte já sai exato no quadro
        p.assembled = true;
        p.copyVideo = false;
        p.actualStartSec = r.startSec;
        p.earlyBySec = 0;
        return p;
    }
    const double gap = std::max(0.0, r.startSec - r.keyframeBeforeStart);

    if (r.maxBytes > 0) { // comprimir para caber num tamanho: recodifica (o corte já sai exato)
        p.copyVideo = false;
        p.actualStartSec = r.startSec;
        p.earlyBySec = 0;
        return p;
    }

    const bool fast = r.mode == CutMode::Fast;

    p.copyVideo = fast;
    p.actualStartSec = fast ? r.keyframeBeforeStart : r.startSec;
    p.earlyBySec = fast ? gap : 0.0;
    return p;
}

QString earlyStartWarning(double earlyBySec)
{
    if (earlyBySec < 0.05)
        return {};
    return QStringLiteral("O clipe vai começar %1 s antes")
        .arg(QString::number(earlyBySec, 'f', 1).replace('.', ','));
}

} // namespace shear

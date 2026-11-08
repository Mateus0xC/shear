#include "CutPlanner.h"
#include <algorithm>

namespace shear {

CutPlan planCut(const ExportRequest &r)
{
    CutPlan p;
    const double gap = std::max(0.0, r.startSec - r.keyframeBeforeStart);

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

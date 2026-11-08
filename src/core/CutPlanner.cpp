#include "CutPlanner.h"
#include <algorithm>

namespace shear {

CutPlan planCut(const ExportRequest &r)
{
    CutPlan p;
    const double gap = std::max(0.0, r.startSec - r.keyframeBeforeStart);

    bool fast = false;
    switch (r.mode) {
    case CutMode::Fast:  fast = true; break;
    case CutMode::Exact: fast = false; break;
    case CutMode::Auto:  fast = gap < r.autoThresholdSec; break;
    }

    // O preset do Discord sempre recodifica o vídeo: já corta no frame exato.
    if (r.preset == Preset::DiscordLimit) {
        p.copyVideo = false;
        p.actualStartSec = r.startSec;
        return p;
    }
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

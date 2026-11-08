#include "BitrateCalc.h"
#include <cmath>

namespace shear {

int videoBitrateKbps(double targetMB, double durationSec, int audioKbps)
{
    if (durationSec <= 0.0 || targetMB <= 0.0)
        return 0;
    // 1 MB = 8192 kbit (MiB, como o Discord conta)
    const double totalKbps = targetMB * 8.0 * 1024.0 / durationSec;
    const double video = (totalKbps - audioKbps) * kSafetyMargin;
    if (video < 50.0)
        return 0;
    return static_cast<int>(std::floor(video));
}

} // namespace shear

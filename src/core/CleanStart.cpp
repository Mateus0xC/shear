#include "CleanStart.h"
#include <cmath>

namespace shear {

double firstCleanTime(const std::vector<double> &okFrames, double fps, double duration)
{
    const int full = int(std::floor(duration)); // só segundos inteiros (o último pode vir pela metade)
    if (full < 4 || fps <= 0)
        return 0;
    std::vector<int> count(size_t(full), 0);
    for (double t : okFrames) {
        const int s = int(std::floor(t));
        if (s >= 0 && s < full)
            ++count[size_t(s)];
    }
    const double need = 0.5 * fps;
    int lastRunEnd = -1; // último segundo da última sequência ruim (>= 3 seguidos)
    int run = 0;
    for (int s = 0; s < full; ++s) {
        if (count[size_t(s)] < need) {
            ++run;
            if (run >= 3)
                lastRunEnd = s;
        } else {
            run = 0;
        }
    }
    if (lastRunEnd < 0 || lastRunEnd >= full - 1)
        return 0; // sem dano, ou dano até o fim
    return double(lastRunEnd + 1);
}

} // namespace shear

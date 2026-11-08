#include "Settings.h"
#include <QSettings>
#include <QStandardPaths>

namespace shear {

Config Config::load()
{
    QSettings s;
    Config c;
    c.mode = static_cast<CutMode>(s.value("cut/mode", int(c.mode)).toInt());
    c.autoThresholdSec = s.value("cut/autoThreshold", c.autoThresholdSec).toDouble();
    c.gpuDecode = s.value("player/gpuDecode", true).toBool();
    c.outputDir = s.value("paths/output").toString();
    c.shadowPlayDir = s.value("paths/shadowplay").toString();
    return c;
}

void Config::save() const
{
    QSettings s;
    s.setValue("cut/mode", int(mode));
    s.setValue("cut/autoThreshold", autoThresholdSec);
    s.setValue("player/gpuDecode", gpuDecode);
    s.setValue("paths/output", outputDir);
    s.setValue("paths/shadowplay", shadowPlayDir);
}

QString Config::effectiveShadowPlayDir() const
{
    return shadowPlayDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                                   : shadowPlayDir;
}

} // namespace shear

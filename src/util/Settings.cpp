#include "Settings.h"
#include <QSettings>
#include <QStandardPaths>

namespace shear {

Config Config::load()
{
    QSettings s;
    Config c;
    c.mode = static_cast<CutMode>(s.value("cut/mode", int(c.mode)).toInt());
    c.preset = s.value("export/preset", 0).toInt() == 1 ? Preset::MixMic : Preset::QuickCut;
    c.tracks = static_cast<AudioTracks>(s.value("export/tracks", int(c.tracks)).toInt());
    c.autoThresholdSec = s.value("cut/autoThreshold", c.autoThresholdSec).toDouble();
    c.outputDir = s.value("paths/output").toString();
    c.shadowPlayDir = s.value("paths/shadowplay").toString();
    return c;
}

void Config::save() const
{
    QSettings s;
    s.setValue("cut/mode", int(mode));
    s.setValue("export/preset", int(preset));
    s.setValue("export/tracks", int(tracks));
    s.setValue("cut/autoThreshold", autoThresholdSec);
    s.setValue("paths/output", outputDir);
    s.setValue("paths/shadowplay", shadowPlayDir);
}

QString Config::effectiveShadowPlayDir() const
{
    return shadowPlayDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                                   : shadowPlayDir;
}

} // namespace shear

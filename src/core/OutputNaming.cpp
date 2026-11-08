#include "OutputNaming.h"

namespace shear {

QString uniqueOutputPath(const QString &dir, const QString &baseName, const QTime &now,
                         const std::function<bool(const QString &)> &exists)
{
    const QString d = dir.endsWith('/') || dir.endsWith('\\') ? dir : dir + '/';
    const QString stem = QString("%1_shear_%2").arg(baseName, now.toString("hh-mm-ss"));
    QString path = d + stem + ".mp4";
    for (int i = 2; exists(path); ++i)
        path = d + QString("%1_%2.mp4").arg(stem).arg(i);
    return path;
}

} // namespace shear

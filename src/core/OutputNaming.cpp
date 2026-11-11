#include "OutputNaming.h"

namespace shear {

QString fallbackStem(const QString &original)
{
    return original + "_shear";
}

QString uniquePath(const QString &dir, const QString &stem, const std::function<bool(const QString &)> &exists)
{
    const QString d = dir.endsWith('/') || dir.endsWith('\\') ? dir : dir + '/';
    QString path = d + stem + ".mp4";
    for (int i = 2; exists(path); ++i)
        path = d + QString("%1_%2.mp4").arg(stem).arg(i);
    return path;
}

} // namespace shear

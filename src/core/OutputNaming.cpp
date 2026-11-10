#include "OutputNaming.h"

namespace shear {

static QString fileSafe(QString s)
{
    for (const QChar c : QStringLiteral("\\/:*?\"<>|"))
        s.remove(c);
    return s.simplified();
}

QString clipBaseName(const QString &original, const QString &map, const QString &agent, const QString &tag)
{
    QStringList parts;
    for (const QString &p : {map, agent, tag})
        if (const QString s = fileSafe(p); !s.isEmpty())
            parts << s;
    return parts.isEmpty() ? original : parts.join('_');
}

QString uniqueOutputPath(const QString &dir, const QString &baseName, const QDateTime &now,
                         const std::function<bool(const QString &)> &exists)
{
    const QString d = dir.endsWith('/') || dir.endsWith('\\') ? dir : dir + '/';
    const QString stem = QString("%1_%2").arg(baseName, now.toString("yyyy-MM-dd_HH-mm"));
    QString path = d + stem + ".mp4";
    for (int i = 2; exists(path); ++i)
        path = d + QString("%1_%2.mp4").arg(stem).arg(i);
    return path;
}

} // namespace shear

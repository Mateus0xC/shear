#include "OutputNaming.h"
#include <QStringList>

namespace shear {

static QString fileSafe(QString s)
{
    for (const QChar c : QStringLiteral("\\/:*?\"<>|"))
        s.remove(c);
    return s.simplified();
}

QString valorantStem(const QDate &date, const QString &map, const QString &agent)
{
    QStringList parts;
    for (const QString &p : {map, agent})
        if (const QString s = fileSafe(p); !s.isEmpty())
            parts << s;
    if (parts.isEmpty())
        return {};
    return QString("Valorant %1 - %2").arg(date.toString("yyyy.MM.dd"), parts.join('_'));
}

QString fallbackStem(const QString &original, const QDateTime &now)
{
    return QString("%1_%2").arg(original, now.toString("yyyy-MM-dd_HH-mm"));
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

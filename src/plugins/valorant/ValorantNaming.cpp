#include "ValorantNaming.h"
#include <QStringList>

namespace shear::valorant {

static QString fileSafe(QString s)
{
    for (const QChar c : QStringLiteral("\\/:*?\"<>|"))
        s.remove(c);
    return s.simplified();
}

QString valorantStem(const QDate &date, const QString &map, const QString &agent, const QString &tag)
{
    QStringList parts;
    for (const QString &p : {map, agent})
        if (const QString s = fileSafe(p); !s.isEmpty())
            parts << s;
    if (parts.isEmpty())
        return {};
    if (const QString t = fileSafe(tag); !t.isEmpty())
        parts << t;
    return QString("Valorant %1 - %2").arg(date.toString("yyyy.MM.dd"), parts.join('_'));
}

} // namespace shear::valorant

#include "ValorantStore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace shear::valorant::store {

QString dir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/valorant"; // junto do metrics.log
}

static bool writeAtomic(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

QJsonObject loadClips()
{
    QFile f(dir() + "/clips.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool saveClip(const QString &key, const QJsonObject &entry)
{
    QJsonObject all = loadClips();
    all.insert(key, entry);
    return writeAtomic(dir() + "/clips.json", QJsonDocument(all).toJson(QJsonDocument::Indented));
}

bool saveMatch(const QString &matchId, const QJsonObject &match)
{
    // o ID vem da rede: só aceita caracteres de um UUID, para nunca virar caminho
    for (const QChar ch : matchId)
        if (!ch.isLetterOrNumber() && ch != '-')
            return false;
    return !matchId.isEmpty() &&
           writeAtomic(dir() + "/matches/" + matchId + ".json", QJsonDocument(match).toJson(QJsonDocument::Indented));
}

QByteArray readCache(const QString &name)
{
    QFile f(dir() + "/" + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeCache(const QString &name, const QByteArray &data) { return writeAtomic(dir() + "/" + name, data); }

} // namespace shear::valorant::store

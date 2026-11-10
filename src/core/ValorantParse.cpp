#include "ValorantParse.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace shear::valorant {

Lockfile parseLockfile(const QByteArray &content)
{
    const auto parts = content.trimmed().split(':');
    Lockfile l;
    if (parts.size() < 5)
        return l;
    bool ok = false;
    const int port = parts[2].toInt(&ok);
    if (!ok || port <= 0 || port > 65535)
        return l;
    l.port = port;
    l.password = QString::fromUtf8(parts[3]);
    return l;
}

RegionInfo parseShooterLog(const QString &text)
{
    RegionInfo r;
    static const QRegularExpression host(R"(https://glz-([a-z0-9]+)-1\.([a-z0-9]+)\.a\.pvp\.net)");
    static const QRegularExpression ver(R"(CI server version:\s*(\S+))");
    if (const auto m = host.match(text); m.hasMatch()) {
        r.region = m.captured(1);
        r.shard = m.captured(2);
    }
    if (const auto m = ver.match(text); m.hasMatch())
        r.version = m.captured(1);
    return r;
}

QString parseMatchId(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object().value("MatchID").toString();
}

CoreGameInfo parseCoreGame(const QByteArray &json, const QString &puuid)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    CoreGameInfo c;
    c.mapId = o.value("MapID").toString();
    c.modeId = o.value("ModeID").toString();
    for (const auto &v : o.value("Players").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("Subject").toString().compare(puuid, Qt::CaseInsensitive) == 0) {
            c.agentId = p.value("CharacterID").toString();
            break;
        }
    }
    return c;
}

QString pickMatchForClip(const QByteArray &historyJson, qint64 clipEndMs)
{
    QString best;
    qint64 bestStart = -1;
    for (const auto &v : QJsonDocument::fromJson(historyJson).object().value("History").toArray()) {
        const QJsonObject o = v.toObject();
        const qint64 start = qint64(o.value("GameStartTime").toDouble());
        if (start > 0 && start <= clipEndMs && start > bestStart) {
            bestStart = start;
            best = o.value("MatchID").toString();
        }
    }
    return best;
}

MatchSummary parseMatchDetails(const QByteArray &json, const QString &puuid)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const QJsonObject info = o.value("matchInfo").toObject();
    MatchSummary m;
    m.mapId = info.value("mapId").toString();
    m.startMs = qint64(info.value("gameStartMillis").toDouble());
    m.lengthMs = qint64(info.value("gameLengthMillis").toDouble());
    for (const auto &v : o.value("players").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("subject").toString().compare(puuid, Qt::CaseInsensitive) == 0) {
            m.agentId = p.value("characterId").toString();
            break;
        }
    }
    return m;
}

static QString findName(const QByteArray &json, const char *keyField, const QString &id)
{
    if (id.isEmpty())
        return {};
    for (const auto &v : QJsonDocument::fromJson(json).object().value("data").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(keyField).toString().compare(id, Qt::CaseInsensitive) == 0)
            return o.value("displayName").toString();
    }
    return {};
}

QString mapNameFor(const QByteArray &json, const QString &mapUrl) { return findName(json, "mapUrl", mapUrl); }
QString agentNameFor(const QByteArray &json, const QString &uuid) { return findName(json, "uuid", uuid); }

QString clipKey(const QString &fileName, qint64 size)
{
    return fileName + "|" + QString::number(size);
}

bool looksLikeValorantClip(const QString &path)
{
    return path.contains("valorant", Qt::CaseInsensitive);
}

} // namespace shear::valorant

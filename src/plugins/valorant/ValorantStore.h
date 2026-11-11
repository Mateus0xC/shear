#pragma once
#include <QJsonObject>
#include <QString>

// Armazenamento em JSON: <AppLocalData>/valorant/clips.json e matches/<matchId>.json (gravados com QSaveFile).
namespace shear::valorant::store {

QString dir();
QJsonObject loadClips();                                   // chave do clipe -> entrada
bool saveClip(const QString &key, const QJsonObject &entry); // lê, troca a entrada e regrava
bool saveMatch(const QString &matchId, const QJsonObject &fields);   // junta os campos ao que já existe
QJsonObject loadMatch(const QString &matchId);
bool eraseMatchField(const QString &matchId, const QString &key); // tira um campo (ex.: offsetSec)
QByteArray readCache(const QString &name);                 // maps.json / agents.json
bool writeCache(const QString &name, const QByteArray &data);

} // namespace shear::valorant::store

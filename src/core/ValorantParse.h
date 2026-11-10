#pragma once
#include <QByteArray>
#include <QString>

// Funções puras (sem rede e sem disco) que interpretam o que a API local do Valorant devolve.
namespace shear::valorant {

struct Lockfile {
    int port = 0;
    QString password;
    bool valid() const { return port > 0 && !password.isEmpty(); }
};
// `nome:pid:porta:senha:protocolo`
Lockfile parseLockfile(const QByteArray &content);

struct RegionInfo {
    QString region, shard, version;
    bool valid() const { return !region.isEmpty() && !shard.isEmpty(); }
};
// Lê o começo do ShooterGame.log: host glz-{região}-1.{shard}.a.pvp.net e "CI server version: ...".
RegionInfo parseShooterLog(const QString &text);

// /core-game/v1/players/{puuid} -> MatchID ("" se não houver)
QString parseMatchId(const QByteArray &json);

struct CoreGameInfo {
    QString mapId, agentId, modeId;
    bool valid() const { return !mapId.isEmpty() && !agentId.isEmpty(); }
};
// /core-game/v1/matches/{id}: mapa e o agente do jogador `puuid`.
CoreGameInfo parseCoreGame(const QByteArray &json, const QString &puuid);

// /match-history/v1/history/{puuid}: a partida mais recente que COMEÇOU antes de `clipEndMs` (ms desde 1970), ou "".
QString pickMatchForClip(const QByteArray &historyJson, qint64 clipEndMs);

struct MatchSummary {
    QString mapId, agentId;
    qint64 startMs = 0, lengthMs = 0;
    bool valid() const { return !mapId.isEmpty() && !agentId.isEmpty() && startMs > 0; }
    // O clipe (que termina em `clipEndMs`) cabe nessa partida? `slackMs` cobre clipe salvo depois do fim da partida.
    bool covers(qint64 clipEndMs, qint64 slackMs) const
    {
        return clipEndMs >= startMs && clipEndMs <= startMs + lengthMs + slackMs;
    }
};
// /match-details/v1/matches/{id}: mapa, meu agente (characterId) e horário/duração da partida.
MatchSummary parseMatchDetails(const QByteArray &json, const QString &puuid);

// Nomes a partir das listas do valorant-api.com (/v1/maps casa por mapUrl; /v1/agents por uuid, sem diferenciar maiúsculas).
// Devolve "" se o ID for desconhecido (nunca o ID cru).
QString mapNameFor(const QByteArray &json, const QString &mapUrl);
QString agentNameFor(const QByteArray &json, const QString &uuid);

// Chave do clipe que sobrevive a mover o arquivo de pasta: nome + tamanho.
QString clipKey(const QString &fileName, qint64 size);

// O clipe é do Valorant? (nome do arquivo ou alguma pasta do caminho contém "valorant")
bool looksLikeValorantClip(const QString &path);

} // namespace shear::valorant

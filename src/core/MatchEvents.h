#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>
#include <utility>

// Eventos do jogador numa partida do Valorant e sua conversão para o tempo do clipe (puro: sem rede nem disco).
namespace shear::valorant {

// Os horários da API ficam 60 a 110 s antes do que aparece no vídeo e esse tanto muda de partida para partida. Normalmente
// o shear acha o valor exato no ShooterGame.log (autoOffsetSec); este padrão (média de partidas medidas) só vale quando o
// log da partida não existe mais. A tecla K do editor ainda acerta à mão (fica salvo em matches/<id>.json).
inline constexpr double kDefaultClipOffsetSec = 104.0;

struct MatchEvent {
    enum Kind { Kill, Death } kind = Kill;
    qint64 absMs = 0; // horário absoluto (ms desde 1970): gameStartMillis + gameTime
    int round = 0;
    QString weaponId; // damageItem; vazio para spike/queda/corpo a corpo
};

struct ClipEvent {
    MatchEvent::Kind kind = MatchEvent::Kill;
    double t = 0; // segundos no tempo do clipe
    int round = 0;
    QString weaponId;
};

// Tudo o que se guarda de uma partida para os clipes dela.
struct MatchData {
    QString matchId;
    QVector<MatchEvent> events;
    QVector<qint64> roundStartsMs; // início de cada round (ms absoluto), em ordem; o fim de um round é o início do seguinte
    qint64 endMs = 0;              // fim da partida (início + duração)
    double offsetSec = qQNaN();    // ajuste que VOCÊ calibrou com a tecla K (NaN = nunca calibrada)
    double autoOffsetSec = qQNaN(); // ajuste calculado sozinho a partir do ShooterGame.log (NaN = não achou)
};

// Início de cada round em /match-details: gameStartMillis + (gameTime − roundTime) de qualquer kill do round.
// Rounds sem nenhuma kill não aparecem.
QVector<qint64> extractRoundStarts(const QByteArray &matchDetailsJson);

// "Último round capturado": o round que contém o fim do clipe. Se o trecho dele dentro do clipe tiver menos de `minSec`,
// vale o round anterior. O fim de cada round é o início do seguinte (o do último é o fim da partida). Devolve {I, O} em
// segundos do clipe (já limitados a [0, duração]) ou nada se não houver round que cubra o clipe.
std::optional<std::pair<double, double>> pickLastRoundSpan(const QVector<qint64> &roundStartsMs, qint64 matchEndMs,
                                                           qint64 clipEndMs, double durationSec, double offsetSec,
                                                           double minSec = 10.0);

// Os MEUS eventos em /match-details: kills (killer == puuid) e mortes (victim == puuid), em ordem de horário.
QVector<MatchEvent> extractMyEvents(const QByteArray &matchDetailsJson, const QString &puuid);

// O clipe cobre aproximadamente [clipEnd − duração, clipEnd]; `offsetSec` ajusta essa suposição (positivo = eventos
// aparecem mais tarde no clipe). Só devolve os que caem dentro do clipe.
QVector<ClipEvent> alignToClip(const QVector<MatchEvent> &events, qint64 clipEndMs, double durationSec,
                               double offsetSec = 0);

// Do tempo do clipe original para o do clipe exportado: tira o que fica antes de `start`, depois de `end` e dentro dos
// trechos apagados (`gaps`), e desconta os trechos apagados anteriores.
QVector<ClipEvent> toExported(const QVector<ClipEvent> &events, double start, double end,
                              const QVector<std::pair<double, double>> &gaps);

// Kills no clipe: "2k", "3k", "4k", "5k"... = o maior número de kills num mesmo round dentro do clipe (a partir de 2);
// mortes não geram tag (aparecem só na timeline).
QStringList makeTags(const QVector<ClipEvent> &events);

} // namespace shear::valorant

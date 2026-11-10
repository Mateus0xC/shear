#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <utility>

// Eventos do jogador numa partida do Valorant e sua conversão para o tempo do clipe (puro: sem rede nem disco).
namespace shear::valorant {

// Os horários da API ficam ~100 s antes do que aparece no vídeo (gameStartMillis parece marcar o começo da seleção de
// agentes/carregamento, não o do primeiro round) e esse tanto muda de partida para partida: medido 109,5 s e 98,4 s em duas
// partidas. O padrão é a média; a tecla K do editor acerta o offset de cada partida (fica salvo em matches/<id>.json).
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

// Kills no clipe: "2k", "3k", "4k"; "ace" = 5 kills no mesmo round; "morreu" se morri nele.
QStringList makeTags(const QVector<ClipEvent> &events);

} // namespace shear::valorant

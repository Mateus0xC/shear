#pragma once
#include "core/MatchEvents.h"
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace shear::valorant {

// Descobre mapa e agente de um clipe do Valorant pela API local não oficial do cliente (somente GET, somente leitura).
// Tudo assíncrono; qualquer falha termina em silêncio (sem sinal). Tokens ficam só na memória, nunca em disco/log.
class ValorantService : public QObject {
    Q_OBJECT
public:
    explicit ValorantService(QObject *parent = nullptr);
    // Clipes até esta idade (s) tentam primeiro a partida em andamento; os mais antigos são achados pelo histórico.
    static constexpr qint64 kAssociateMaxAgeSec = 300;

    void identify(const QString &clipPath, const QDateTime &created, qint64 size);
    // Baixa (uma por vez, em segundo plano) os detalhes das partidas já associadas que ainda não os têm. Sem jogo, não faz nada.
    void backfill();
    // Guarda as tags (2k, 3k, 4k...) do clipe no clips.json.
    void saveTags(const QString &clipPath, qint64 size, const QStringList &tags);
    // Dados digitados à mão ({map, agent, kills, comment}); objeto vazio apaga. Valem para qualquer clipe, com ou sem Riot.
    void saveManual(const QString &clipPath, qint64 size, const QJsonObject &manual);
    // Guarda o ajuste de alinhamento da partida (vale para todos os clipes dela).
    void saveMatchOffset(const QString &matchId, double offsetSec);

signals:
    void identified(const QString &clipPath, const QString &map, const QString &agent);
    // Dados digitados à mão que já estavam guardados para o clipe (objeto vazio se não há).
    void manualLoaded(const QString &clipPath, const QJsonObject &manual);
    // Meus kills e mortes da partida do clipe (horário absoluto).
    // Meus eventos, início dos rounds e ajuste guardado da partida do clipe.
    void matchDataReady(const QString &clipPath, const shear::valorant::MatchData &data);

private:
    struct Ctx;
    using Reply = std::function<void(int status, const QByteArray &body)>;
    void get(const QString &url, const QList<QPair<QByteArray, QByteArray>> &headers, bool local, Reply cb);
    void step1(std::shared_ptr<Ctx> c);
    void step2(std::shared_ptr<Ctx> c);
    void step3(std::shared_ptr<Ctx> c);
    void step4(std::shared_ptr<Ctx> c);
    void stepHistory(std::shared_ptr<Ctx> c);
    void stepDetails(std::shared_ptr<Ctx> c);
    void startAutoOffset(std::shared_ptr<Ctx> c, const QString &matchId);
    void loadKnownEvents(std::shared_ptr<Ctx> c);
    void proceed(std::shared_ptr<Ctx> c);
    void applyDetails(std::shared_ptr<Ctx> c, const QString &matchId, const QByteArray &body, std::function<void()> then);
    bool stale(const Ctx &c) const;
    void finish(std::shared_ptr<Ctx> c, const QString &why);
    QList<QPair<QByteArray, QByteArray>> remoteHeaders(const Ctx &c) const;
    void resolveNames(std::shared_ptr<Ctx> c);

    QNetworkAccessManager *m_nam = nullptr; // criado só na primeira consulta
    int m_gen = 0;
};

} // namespace shear::valorant

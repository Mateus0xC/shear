#pragma once
#include <QDateTime>
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

signals:
    void identified(const QString &clipPath, const QString &map, const QString &agent);

private:
    struct Ctx;
    using Reply = std::function<void(int status, const QByteArray &body)>;
    void get(const QString &url, const QList<QPair<QByteArray, QByteArray>> &headers, bool local, Reply cb);
    void step1(std::shared_ptr<Ctx> c);
    void step2(std::shared_ptr<Ctx> c);
    void step3(std::shared_ptr<Ctx> c);
    void step4(std::shared_ptr<Ctx> c);
    void stepHistory(std::shared_ptr<Ctx> c);
    void finish(std::shared_ptr<Ctx> c, const QString &why);
    QList<QPair<QByteArray, QByteArray>> remoteHeaders(const Ctx &c) const;
    void resolveNames(std::shared_ptr<Ctx> c);

    QNetworkAccessManager *m_nam = nullptr; // criado só na primeira consulta
    int m_gen = 0;
};

} // namespace shear::valorant

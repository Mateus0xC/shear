#pragma once
#include <QLocalServer>
#include <QObject>

namespace shear {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    // Se já há uma instância, envia o caminho (pode ser vazio) e retorna true.
    static bool sendToRunning(const QString &path);
    explicit SingleInstance(QObject *parent = nullptr);
    bool listen();

signals:
    void openRequested(const QString &path); // path vazio = só trazer à frente

private:
    QLocalServer m_server;
};

} // namespace shear

#include "SingleInstance.h"
#include <QLocalSocket>

namespace shear {

static const QString kName = QStringLiteral("shear-single-instance-v1");

bool SingleInstance::sendToRunning(const QString &path)
{
    QLocalSocket s;
    s.connectToServer(kName);
    if (!s.waitForConnected(150))
        return false;
    s.write(path.toUtf8() + '\n');
    s.waitForBytesWritten(1000);
    s.disconnectFromServer();
    return true;
}

SingleInstance::SingleInstance(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *sock = m_server.nextPendingConnection()) {
            connect(sock, &QLocalSocket::readyRead, this, [this, sock] {
                while (sock->canReadLine())
                    emit openRequested(QString::fromUtf8(sock->readLine()).trimmed());
            });
            connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        }
    });
}

bool SingleInstance::listen()
{
    QLocalServer::removeServer(kName);
    return m_server.listen(kName);
}

} // namespace shear

#pragma once
#include <QThread>
#include <QVector>

namespace shear {

// Lê pacotes com libavformat (sem decodificar) e emite os instantes dos keyframes.
class KeyframeScanner : public QThread {
    Q_OBJECT
public:
    explicit KeyframeScanner(QString path, QObject *parent = nullptr);

signals:
    void probed(double durationSec, int audioTracks);
    void keyframes(QVector<double> batch); // lotes incrementais
    void failed();

protected:
    void run() override;

private:
    QString m_path;
};

} // namespace shear

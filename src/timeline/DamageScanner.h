#pragma once
#include <QThread>

namespace shear {

// Detecta vídeo com o começo corrompido. Primeiro decodifica só os 3 primeiros segundos (rápido): se saírem quadros
// suficientes, termina sem fazer nada. Se não, decodifica o resto e descobre a partir de quando o vídeo está sadio.
class DamageScanner : public QThread {
    Q_OBJECT
public:
    explicit DamageScanner(QString path, QObject *parent = nullptr);

signals:
    // Só é emitido se houver dano: segundos iniciais a pular (já encaixado num keyframe que decodifica bem).
    void damaged(double cleanStartSec);

protected:
    void run() override;

private:
    QString m_path;
};

} // namespace shear

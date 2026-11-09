#pragma once
#include <QStringList>
#include <QThread>

namespace shear {

// Detecta vídeo com o começo corrompido. Primeiro decodifica só 12 quadros do começo (rápido): se saírem bons,
// termina sem fazer nada. Se não, amostra o vídeo em paralelo e descobre a partir de quando ele está sadio.
class DamageScanner : public QThread {
    Q_OBJECT
public:
    explicit DamageScanner(QString path, QObject *parent = nullptr);

signals:
    // Só é emitido se houver dano: segundos iniciais a pular.
    void damaged(double cleanStartSec);

protected:
    void run() override;

private:
    QString m_path;
};

// Analisa vários arquivos em segundo plano (os clipes recentes), com poucas threads, para o resultado já estar
// guardado quando o usuário abrir um deles.
class DamagePrefetch : public QThread {
    Q_OBJECT
public:
    explicit DamagePrefetch(QStringList paths, QObject *parent = nullptr);

signals:
    void analyzed(const QString &path, double cleanStartSec); // 0 = vídeo sadio

protected:
    void run() override;

private:
    QStringList m_paths;
};

} // namespace shear

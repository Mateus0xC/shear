#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

struct mpv_handle;

namespace shear {

// Segundo player (libmpv) SÓ DE ÁUDIO, para o "áudio ao arrastar" na timeline. Não decodifica vídeo, então um seek
// leva milissegundos e acompanha o mouse em qualquer velocidade. O vídeo principal fica pausado e parado: quem toca é
// este player, com o mesmo grafo de áudio (faixas ligadas, mixagem) e o mesmo volume do preview.
class AudioScrubber : public QObject {
    Q_OBJECT
public:
    explicit AudioScrubber(QObject *parent = nullptr);
    ~AudioScrubber() override;

    void loadFile(const QString &path);
    void setAudioGraph(const QString &graph); // o mesmo grafo do preview; vazio = mudo
    void resetAudio();
    void setVolume(int percent);

    // Toca a partir desta posição. O som segue o mouse: cada movimento reposiciona, e o áudio some sozinho
    // um instante depois do último movimento.
    // soundMs: quanto o som dura depois do último movimento (mais curto quando há mais zoom).
    void scrub(double sec, int soundMs = 140);
    void stop();

private slots:
    void handleEvents();

private:
    void command(const QStringList &args);
    static void wakeup(void *ctx);

    mpv_handle *m_mpv = nullptr;
    QTimer m_idle;
    QElapsedTimer m_last;
    QString m_graph;
    bool m_muted = false;
    bool m_playing = false;
    int m_volume = 100;
};

} // namespace shear

#pragma once
#include <QImage>
#include <QWidget>

struct mpv_handle;
struct mpv_render_context;

namespace shear {

// Preview com libmpv pintando por SOFTWARE num buffer de CPU (sem OpenGL/D3D na janela).
// Motivo: o overlay da NVIDIA/ShadowPlay se injeta em processos que apresentam com OpenGL/D3D e trata o app
// como jogo. A decodificação usa a GPU só via NVDEC ("nvdec-copy"), que nos testes não aciona o overlay.
class MpvWidget : public QWidget {
    Q_OBJECT
public:
    // gpuDecode=true (padrão): decodifica no NVDEC; false: tudo na CPU.
    explicit MpvWidget(bool gpuDecode = false, QWidget *parent = nullptr);
    ~MpvWidget() override;

    void loadFile(const QString &path);
    void seek(double sec);
    void setPaused(bool p);
    void togglePause();
    void stepFrame(bool forward);
    // Áudio do preview: grafo lavfi-complex terminando em [ao]; vazio = sem áudio.
    void setAudioGraph(const QString &graph);
    void resetAudio(); // volta ao padrão do mpv (faixa 1), sem grafo
    // Áudio ao arrastar (scrub): depois de um seek, toca um trecho curto e pausa de novo. O mpv não toca
    // áudio em seeks com o vídeo pausado; o trecho começa quando o seek termina e dura `ms`.
    void scrubBurst(int ms = 110);
    void cancelScrub(); // cancela o trecho pendente (quem chamou decide se fica tocando ou pausado)
    void setVolume(int percent); // só o preview, não afeta a exportação
    void showText(const QString &text, int ms = 900); // OSD sobre o vídeo
    bool paused() const { return m_paused; }
    double position() const { return m_pos; }
    double duration() const { return m_dur; }

signals:
    void positionChanged(double sec);
    void durationChanged(double sec);
    void pausedChanged(bool paused);
    void fileLoaded();
    void fpsChanged(double fps); // taxa de quadros do arquivo (0 se desconhecida)
    void volumeWheel(int delta); // roda do mouse sobre o vídeo: +5 / -5 por "clique"
    void firstFrame();

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;

private slots:
    void handleEvents();
    void onRenderUpdate();

private:
    void command(const QStringList &args);
    void endBurstVideo();
    void finishRestore(quint64 gen);
    static void wakeup(void *ctx);
    static void renderUpdate(void *ctx);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_ctx = nullptr;
    QImage m_frame;
    QImage m_scratch;                  // destino do mpv enquanto a imagem está congelada
    bool m_freeze = false;             // mostra m_frame parado (o vídeo anda por baixo, só o áudio importa)
    bool m_freezePending = false;      // congela logo depois de pintar o quadro do ponto pedido
    bool m_unfreezeOnRestart = false;  // descongela quando o seek de volta ao marcador terminar
    double m_pos = 0, m_dur = 0;
    bool m_paused = false;
    int m_volume = 100;
    bool m_awaitFirst = false, m_awaitPaint = false;
    QString m_graph;      // grafo de áudio atual (para não reconfigurar sem necessidade)
    bool m_muted = false; // aid=no
    // Trocar o filtro de áudio faz o mpv reposicionar a reprodução: guardamos a posição, pausamos e voltamos a ela.
    struct Restore { bool active = false; bool resume = false; int stage = 0; double pos = 0; } m_restore;
    quint64 m_restoreGen = 0;
    class QTimer *m_burstTimer = nullptr;
    bool m_burstPending = false; // trecho pedido, esperando o seek terminar para começar a contar
    int m_burstMs = 110;
    bool m_burstActive = false; // há um trecho tocando: ao acabar, o vídeo volta à posição pedida
    double m_lastSeek = 0;      // último seek pedido (a posição que o marcador mostra)
};

} // namespace shear

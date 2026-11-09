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
    void unload() { command({"stop"}); } // solta o arquivo (ex.: antes de apagá-lo)
    void seek(double sec);
    void seekBy(double sec);
    void setMinPosition(double sec) { m_minPos = sec; } // trecho anterior (corrompido) não é alcançável // relativo (negativo = para trás)
    void setPaused(bool p);
    void togglePause();
    void stepFrame(bool forward);
    // Áudio do preview: grafo lavfi-complex terminando em [ao]; vazio = sem áudio.
    void setAudioGraph(const QString &graph);
    void resetAudio(); // volta ao padrão do mpv (faixa 1), sem grafo
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
    void finishRestore(quint64 gen);
    static void wakeup(void *ctx);
    static void renderUpdate(void *ctx);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_ctx = nullptr;
    QImage m_frame;
    double m_pos = 0, m_dur = 0;
    double m_minPos = 0; // início utilizável (vídeo com começo corrompido)
    bool m_paused = false;
    int m_volume = 100;
    bool m_awaitFirst = false, m_awaitPaint = false;
    QString m_graph;      // grafo de áudio atual (para não reconfigurar sem necessidade)
    bool m_muted = false; // aid=no
    // Trocar o filtro de áudio faz o mpv reposicionar a reprodução: guardamos a posição, pausamos e voltamos a ela.
    struct Restore { bool active = false; bool resume = false; int stage = 0; double pos = 0; } m_restore;
    quint64 m_restoreGen = 0;
};

} // namespace shear

#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
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
    // Segura a imagem (tela preta) enquanto o app descobre se o começo do vídeo é utilizável: nada de quadro quebrado.
    void setHold(bool hold)
    {
        m_hold = hold;
        m_needRender = true;
        if (hold) { m_holdClock.restart(); QTimer::singleShot(600, this, [this] { if (m_hold) update(); }); }
        update();
    }
    void setMinPosition(double sec) { m_minPos = sec; } // trecho anterior (corrompido) não é alcançável

    // Animações rápidas sobre o vídeo (só desenham enquanto duram; sem animação o widget não faz nada extra).
    enum class Overlay { Play, Pause, Skip };
    void flash(Overlay kind, const QString &text = QString()); // Skip: text = "+5 s" / "−0,5 s"

    // Velocidade do preview (não afeta o arquivo exportado).
    void setSpeed(double speed);
    double speed() const { return m_speed; }

    // Zoom no vídeo (Ctrl + roda do mouse); com zoom, arrastar com o botão esquerdo move a imagem.
    void zoomBy(double stepsLog2); // positivo aproxima
    void resetZoom();
    double zoomLog2() const { return m_zoomLog; }
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
    void playbackRestarted(); // seek concluído
    void clicked();           // clique simples no vídeo (play/pause)

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private slots:
    void handleEvents();
    void onRenderUpdate();

private:
    void command(const QStringList &args);
    void applyView();                      // zoom e posição da imagem no mpv
    int margin() const;                    // margem do quadro desenhado (só com zoom)
    void pumpPan();                        // manda a posição desejada da imagem ao mpv quando ele não está ocupado
    void paintOverlay(QPainter &p, double t) const; // t = 0..1 do progresso da animação
    void finishRestore(quint64 gen);
    static void wakeup(void *ctx);
    static void renderUpdate(void *ctx);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_ctx = nullptr;
    QImage m_frame;
    double m_pos = 0, m_dur = 0;
    bool m_hold = false;
    QElapsedTimer m_holdClock;
    double m_minPos = 0; // início utilizável (vídeo com começo corrompido)
    bool m_paused = false;
    int m_volume = 100;
    double m_speed = 1.0;
    // zoom / posição da imagem
    double m_zoomLog = 0, m_panX = 0, m_panY = 0;
    QTimer m_viewTimer;
    // arrasto com zoom: o quadro já desenhado é só deslocado na tela; estas são as posições de cada coisa
    double m_appliedPanX = 0, m_appliedPanY = 0; // o que o mpv tem agora
    double m_framePanX = 0, m_framePanY = 0;     // com que posição o quadro guardado (m_frame) foi desenhado
    double m_panPxX = 1, m_panPxY = 1;           // px de tela por unidade de pan
    bool m_applyPending = false; // posição enviada ao mpv, esperando ele redesenhar (só com o vídeo parado)
    QElapsedTimer m_pumpClock;
    bool m_needRender = true;    // o mpv avisou que tem quadro novo para desenhar
    QPoint m_pressPos, m_lastPos;
    bool m_pressed = false, m_panning = false;
    // animação sobre o vídeo
    Overlay m_ovKind = Overlay::Play;
    bool m_ovActive = false;
    QString m_ovText;
    QElapsedTimer m_ovClock;
    QTimer m_ovTimer;
    bool m_awaitFirst = false, m_awaitPaint = false;
    QString m_graph;      // grafo de áudio atual (para não reconfigurar sem necessidade)
    bool m_muted = false; // aid=no
    // Trocar o filtro de áudio faz o mpv reposicionar a reprodução: guardamos a posição, pausamos e voltamos a ela.
    struct Restore { bool active = false; bool resume = false; int stage = 0; double pos = 0; } m_restore;
    quint64 m_restoreGen = 0;
};

} // namespace shear

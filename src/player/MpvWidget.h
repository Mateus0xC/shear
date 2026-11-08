#pragma once
#include <QOpenGLWidget>

struct mpv_handle;
struct mpv_render_context;

namespace shear {

class MpvWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    void loadFile(const QString &path);
    void seek(double sec);
    void setPaused(bool p);
    void togglePause();
    void stepFrame(bool forward);
    // Áudio do preview: grafo lavfi-complex terminando em [ao]; vazio = sem áudio.
    void setAudioGraph(const QString &graph);
    void resetAudio(); // volta ao padrão do mpv (faixa 1), sem grafo
    void setVolume(int percent); // só o preview, não afeta a exportação
    bool paused() const { return m_paused; }
    double position() const { return m_pos; }
    double duration() const { return m_dur; }

signals:
    void positionChanged(double sec);
    void durationChanged(double sec);
    void pausedChanged(bool paused);
    void fileLoaded();
    void firstFrame();

protected:
    void initializeGL() override;
    void paintGL() override;

private slots:
    void handleEvents();
    void onRenderUpdate() { update(); }

private:
    void command(const QStringList &args);
    static void wakeup(void *ctx);
    static void renderUpdate(void *ctx);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_ctx = nullptr;
    QString m_pending;
    double m_pos = 0, m_dur = 0;
    bool m_paused = false;
    int m_volume = 100;
    bool m_awaitFirst = false, m_awaitPaint = false;
};

} // namespace shear

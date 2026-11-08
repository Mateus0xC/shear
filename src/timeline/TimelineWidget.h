#pragma once
#include <QWidget>
#include <optional>
#include <vector>

namespace shear {

class TimelineLens;
class TimelineHint;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void reset(double duration = 0);
    void setDuration(double d);
    void setPosition(double p);
    void setStart(double s);
    void setEnd(double e);
    void addKeyframes(const QVector<double> &batch);
    void setScanDone(bool done) { m_scanDone = done; }
    // Modo Rápido: o marcador de início só existe em keyframes, então ele "gruda" neles ao arrastar.
    void setSnapStart(bool snap) { m_snapStart = snap; }
    double snapToKeyframe(double t) const; // keyframe mais próximo (ou t, se ainda não foi lido)
    void setGhostStart(std::optional<double> g);
    void setFps(double fps) { m_fps = fps; } // com a taxa de quadros, o zoom fino anda de frame em frame

    double duration() const { return m_dur; }
    double start() const { return m_start; }
    double end() const { return m_end; }
    bool scanDone() const { return m_scanDone; }
    // Keyframe <= t (ou vazio se ainda desconhecido)
    std::optional<double> keyframeAtOrBefore(double t) const;
    std::optional<double> nearestKeyframe(double t) const;

signals:
    void seekRequested(double sec);
    void startChanged(double sec);
    void endChanged(double sec);
    void dragStarted();  // botão do mouse pressionado na timeline (o vídeo deve pausar)
    void dragFinished(); // soltou o botão

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    QSize sizeHint() const override { return {600, 60}; }

private:
    enum class Drag { None, Seek, Start, End };
    void applyDrag(double t);
    void showLens();
    void showHint();
    void hideHint();
    void leaveZoom();
    double xToSec(double x) const;
    double secToX(double s) const;

    double m_dur = 0, m_pos = 0, m_start = 0, m_end = 0;
    std::vector<double> m_kf;
    std::optional<double> m_ghost;
    bool m_scanDone = false;
    bool m_snapStart = false;
    double m_fps = 0;
    Drag m_drag = Drag::None;
    double m_dragT = 0; // valor que está sendo arrastado

    // Zoom fino (arrastar puxando o mouse para baixo): régua ampliada com mapeamento ABSOLUTO sob o mouse.
    TimelineLens *m_lens = nullptr;
    TimelineHint *m_hint = nullptr; // dica "puxe para baixo", logo abaixo da timeline
    bool m_zoom = false;
    double m_zoomAmount = 0; // 0..1: quão fundo está o zoom (contínuo, escala logarítmica)
    double m_tCont = 0;      // valor arrastado SEM arredondar (frames/keyframes): acumula movimentos minúsculos
    double m_prevFrac = 0;   // coluna anterior do mouse (0..1), para o movimento relativo na escala atual
    double m_win0 = 0;    // início da janela ampliada (s)
    double m_span = 1;    // largura da janela ampliada (s)
    static constexpr int kMargin = 14;
};

} // namespace shear

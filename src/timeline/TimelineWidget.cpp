#include "TimelineWidget.h"
#include <QApplication>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <cmath>

namespace shear {

// Zoom fino CONTÍNUO: quanto mais o mouse desce, menor a janela, numa escala logarítmica suave (sem degraus).
static constexpr double kMaxSpan = 20.0, kMinSpan = 0.4; // janela ampliada (s)
static constexpr double kEnterBeyond = 8;                // px abaixo da timeline para abrir o zoom
static constexpr double kZoomRange = 220;                // px de descida entre o menor e o maior zoom

static QString fmtSpan(double s) { return QString::number(s, 'g', 3).replace('.', ',') + " s"; }

static QString fmtTime(double t, int dec)
{
    t = std::max(0.0, t);
    const int m = int(t) / 60;
    const double sec = t - m * 60;
    QString s = QString::number(sec, 'f', dec);
    if (sec < 10) s.prepend('0');
    return QString("%1:%2").arg(m).arg(s).replace('.', ',');
}

struct LensState {
    double win0 = 0, span = 1, dur = 0, start = 0, end = 0, cur = 0, fps = 0;
    double zoom = 0; // 0..1: posição no medidor de zoom
    const std::vector<double> *kf = nullptr;
};

// Régua ampliada que aparece logo abaixo da timeline enquanto você arrasta com o mouse puxado para baixo.
// O ponto sob o mouse na timeline fica na mesma coluna aqui (mapeamento absoluto): o que você vê é onde você está.
class TimelineLens : public QWidget {
public:
    explicit TimelineLens(QWidget *owner) : QWidget(owner, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setState(const LensState &s)
    {
        m_s = s;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const LensState &s = m_s;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = qApp->palette();
        const QColor text = pal.color(QPalette::WindowText), mid = pal.color(QPalette::Mid);
        const QColor accent = pal.color(QPalette::Highlight);
        QColor dim = text;
        dim.setAlpha(140);

        p.setBrush(pal.color(QPalette::Base));
        p.setPen(QPen(accent, 1.5));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 9, 9);

        const double m = 14, w = width() - 2 * m, t0 = s.win0;
        auto X = [&](double t) { return m + (t - t0) / s.span * w; };
        const double top = 48, base = height() - 10;

        // linha de dicas (esquerda) e medidor de zoom contínuo (direita)
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(dim);
        p.drawText(QRectF(m, 5, w * 0.45, 18), Qt::AlignLeft | Qt::AlignVCenter, "↓ mais zoom    ↑ menos zoom");
        {
            f.setBold(true);
            p.setFont(f);
            const QString win = QString("janela de %1").arg(fmtSpan(s.span));
            const int tw = QFontMetrics(f).horizontalAdvance(win);
            const QRectF pill(width() - m - tw - 16, 5, tw + 16, 18);
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawRoundedRect(pill, 9, 9);
            p.setPen(pal.color(QPalette::HighlightedText));
            p.drawText(pill, Qt::AlignCenter, win);
            f.setBold(false);
            p.setFont(f);
            // medidor: trilho com o preenchimento proporcional ao zoom (extremos = janela máxima e mínima)
            const double gw = 90, gx = pill.left() - 12 - gw - 40, gy = 14;
            p.setPen(dim);
            p.drawText(QRectF(gx - 40, 5, 36, 18), Qt::AlignRight | Qt::AlignVCenter, fmtSpan(kMaxSpan));
            p.drawText(QRectF(gx + gw + 4, 5, 40, 18), Qt::AlignLeft | Qt::AlignVCenter, fmtSpan(kMinSpan));
            p.setPen(Qt::NoPen);
            p.setBrush(mid);
            p.drawRoundedRect(QRectF(gx, gy - 2, gw, 4), 2, 2);
            p.setBrush(accent);
            p.drawRoundedRect(QRectF(gx, gy - 2, std::max(4.0, gw * s.zoom), 4), 2, 2);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(gx + gw * s.zoom, gy), 5, 5);
        }

        // fora do clipe (antes de 0 ou depois do fim): escurecido
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 110));
        if (t0 < 0) p.drawRect(QRectF(m, top, X(0) - m, base - top));
        if (t0 + s.span > s.dur) p.drawRect(QRectF(X(s.dur), top, m + w - X(s.dur), base - top));

        // seleção
        p.setBrush(QColor(60, 110, 200, 70));
        const double xs = std::clamp(X(s.start), m, m + w), xe = std::clamp(X(s.end), m, m + w);
        if (xe > xs) p.drawRect(QRectF(xs, top, xe - xs, base - top));

        // régua: passo "redondo" com no máximo ~8 marcas principais na janela
        static const double steps[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 30, 60};
        double major = 60;
        for (double st : steps)
            if (s.span / st <= 8) { major = st; break; }
        const double minor = major / 5;
        const int dec = major >= 1 ? 0 : (major >= 0.1 ? 1 : 2);
        f.setPixelSize(10);
        p.setFont(f);
        for (long long k = (long long)std::ceil(t0 / minor); k * minor <= t0 + s.span; ++k) {
            const double t = k * minor, x = X(t);
            const bool isMajor = std::abs(t - std::round(t / major) * major) < minor * 0.01;
            p.setPen(QPen(isMajor ? text : dim, 1));
            p.drawLine(QPointF(x, base), QPointF(x, base - (isMajor ? 22 : 10)));
            if (isMajor) {
                p.setPen(dim);
                p.drawText(QRectF(x - 34, base - 38, 68, 14), Qt::AlignHCenter, fmtTime(t, dec));
            }
        }

        // marcas de frame (quando há espaço): cada traço é um quadro
        if (s.fps > 0 && w / s.span / s.fps >= 3.0) {
            QColor fc = text;
            fc.setAlpha(70);
            p.setPen(QPen(fc, 1));
            for (long long k = (long long)std::ceil(t0 * s.fps); k / s.fps <= t0 + s.span; ++k)
                p.drawLine(QPointF(X(k / s.fps), base), QPointF(X(k / s.fps), base - 5));
        }

        // keyframes (laranja) na janela
        if (s.kf) {
            p.setPen(QPen(QColor(255, 160, 40), 2));
            auto it = std::lower_bound(s.kf->begin(), s.kf->end(), t0);
            for (; it != s.kf->end() && *it <= t0 + s.span; ++it)
                p.drawLine(QPointF(X(*it), base), QPointF(X(*it), base - 8));
        }

        // marcadores de início/fim
        p.setPen(QPen(QColor(90, 170, 255), 3));
        if (s.start >= t0 && s.start <= t0 + s.span) p.drawLine(QPointF(X(s.start), top), QPointF(X(s.start), base));
        p.setPen(QPen(QColor(255, 110, 110), 3));
        if (s.end >= t0 && s.end <= t0 + s.span) p.drawLine(QPointF(X(s.end), top), QPointF(X(s.end), base));

        // posição atual: linha branca exatamente embaixo do mouse, com o tempo preciso numa etiqueta
        const double cx = X(s.cur);
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(QPointF(cx, top - 4), QPointF(cx, base));
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        const QString label = fmtTime(s.cur, 3);
        const double bw = QFontMetrics(f).horizontalAdvance(label) + 18;
        const double bx = std::clamp(cx - bw / 2, m, m + w - bw);
        const QRectF bubble(bx, 26, bw, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(bubble, 9, 9);
        p.setPen(pal.color(QPalette::HighlightedText));
        p.drawText(bubble, Qt::AlignCenter, label);
    }

private:
    LensState m_s;
};

// Etiqueta flutuante logo abaixo da timeline: ensina o gesto de zoom enquanto você arrasta.
class TimelineHint : public QWidget {
public:
    explicit TimelineHint(QWidget *owner) : QWidget(owner, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont f = font();
        f.setPixelSize(12);
        setFont(f);
        resize(QFontMetrics(f).horizontalAdvance(text()) + 28, 26);
    }
    static QString text() { return QStringLiteral("↓  Puxe o mouse para baixo para ampliar e ajustar com precisão"); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = qApp->palette();
        const QColor accent = pal.color(QPalette::Highlight);
        p.setBrush(pal.color(QPalette::Base));
        p.setPen(QPen(accent, 1.5));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 13, 13);
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(rect(), Qt::AlignCenter, text());
    }
};

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(60);
    setFocusPolicy(Qt::NoFocus);
}

void TimelineWidget::reset(double duration)
{
    m_dur = duration;
    m_pos = 0;
    m_start = 0;
    m_end = duration;
    m_kf.clear();
    m_ghost.reset();
    m_scanDone = false;
    m_fps = 0;
    update();
}

void TimelineWidget::setDuration(double d)
{
    if (d <= 0) return;
    const bool endWasFull = m_end <= 0 || std::abs(m_end - m_dur) < 1e-6;
    m_dur = d;
    if (endWasFull) m_end = d;
    update();
}

void TimelineWidget::setPosition(double p) { m_pos = p; update(); }

void TimelineWidget::setStart(double s)
{
    m_start = std::clamp(s, 0.0, std::max(0.0, m_end - 0.05));
    update();
}

void TimelineWidget::setEnd(double e)
{
    m_end = std::clamp(e, std::min(m_dur, m_start + 0.05), m_dur);
    update();
}

void TimelineWidget::addKeyframes(const QVector<double> &batch)
{
    m_kf.insert(m_kf.end(), batch.begin(), batch.end());
    std::sort(m_kf.begin(), m_kf.end());
    update();
}

void TimelineWidget::setGhostStart(std::optional<double> g)
{
    if (g != m_ghost) { m_ghost = g; update(); }
}

std::optional<double> TimelineWidget::keyframeAtOrBefore(double t) const
{
    auto it = std::upper_bound(m_kf.begin(), m_kf.end(), t + 1e-3);
    if (it == m_kf.begin()) return std::nullopt;
    return *std::prev(it);
}

std::optional<double> TimelineWidget::nearestKeyframe(double t) const
{
    if (m_kf.empty()) return std::nullopt;
    auto it = std::lower_bound(m_kf.begin(), m_kf.end(), t);
    if (it == m_kf.end()) return m_kf.back();
    double best = *it;
    if (it != m_kf.begin() && std::abs(*std::prev(it) - t) <= std::abs(best - t))
        best = *std::prev(it);
    return best;
}

double TimelineWidget::snapToKeyframe(double t) const
{
    if (m_kf.empty())
        return t;
    if (!m_scanDone && t > m_kf.back())
        return t; // região ainda não lida: não puxa de volta para o último keyframe conhecido
    return nearestKeyframe(t).value_or(t);
}

double TimelineWidget::secToX(double s) const
{
    const double w = width() - 2.0 * kMargin;
    return kMargin + (m_dur > 0 ? s / m_dur * w : 0);
}

double TimelineWidget::xToSec(double x) const
{
    const double w = width() - 2.0 * kMargin;
    return m_dur > 0 ? std::clamp((x - kMargin) / w, 0.0, 1.0) * m_dur : 0;
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int top = 14, bottom = height() - 14;
    p.fillRect(rect(), palette().window());
    p.fillRect(QRectF(kMargin, top, width() - 2 * kMargin, bottom - top), QColor(38, 38, 42));
    if (m_dur <= 0) return;

    const double xs = secToX(m_start), xe = secToX(m_end);
    p.fillRect(QRectF(xs, top, xe - xs, bottom - top), QColor(60, 110, 200, 90));

    p.setPen(QPen(QColor(150, 150, 160), 1));
    for (double k : m_kf) {
        const double x = std::round(secToX(k)) + 0.5;
        p.drawLine(QPointF(x, bottom - 12), QPointF(x, bottom));
    }

    if (m_ghost) {
        p.setPen(QPen(QColor(255, 160, 40), 2, Qt::DashLine));
        const double gx = secToX(*m_ghost);
        p.drawLine(QPointF(gx, top - 6), QPointF(gx, bottom + 6));
    }

    p.setPen(QPen(QColor(90, 170, 255), 3));
    p.drawLine(QPointF(xs, top - 8), QPointF(xs, bottom + 8));
    p.drawLine(QPointF(xs, top - 8), QPointF(xs + 8, top - 8));
    p.setPen(QPen(QColor(255, 110, 110), 3));
    p.drawLine(QPointF(xe, top - 8), QPointF(xe, bottom + 8));
    p.drawLine(QPointF(xe, top - 8), QPointF(xe - 8, top - 8));

    p.setPen(QPen(Qt::white, 2));
    const double px = secToX(m_pos);
    p.drawLine(QPointF(px, top), QPointF(px, bottom));

    const QColor accent = palette().color(QPalette::Highlight);
    if (m_zoom) {
        // moldura mostrando QUAL trecho da timeline está ampliado na régua de baixo
        const double a = std::clamp(secToX(m_win0), double(kMargin), double(width() - kMargin));
        const double b = std::clamp(secToX(m_win0 + m_span), double(kMargin), double(width() - kMargin));
        QColor fill = accent;
        fill.setAlpha(60);
        p.setPen(QPen(accent, 2));
        p.setBrush(fill);
        const double bw = std::max(14.0, b - a); // largura mínima visível, mesmo nos níveis de zoom mais finos
        p.drawRoundedRect(QRectF((a + b) / 2 - bw / 2, top - 3, bw, bottom - top + 6), 3, 3);
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    if (m_dur <= 0) return;
    const double x = e->position().x();
    const double ds = std::abs(x - secToX(m_start)), de = std::abs(x - secToX(m_end));
    if (std::min(ds, de) <= 9) {
        m_drag = ds <= de ? Drag::Start : Drag::End;
        m_dragT = m_drag == Drag::Start ? m_start : m_end;
    } else {
        m_drag = Drag::Seek;
        m_dragT = xToSec(x);
        emit seekRequested(m_dragT);
    }
    m_zoom = false;
    m_tCont = m_dragT;
    update();
    emit dragStarted();
}

void TimelineWidget::applyDrag(double t)
{
    switch (m_drag) {
    case Drag::Seek:  m_dragT = t; emit seekRequested(t); break;
    // Os marcadores também levam o vídeo junto enquanto são arrastados (scrubbing em tempo real).
    case Drag::Start: setStart(m_snapStart ? snapToKeyframe(t) : t); m_dragT = m_start; emit startChanged(m_start); emit seekRequested(m_start); break;
    case Drag::End:   setEnd(t); m_dragT = m_end; emit endChanged(m_end); emit seekRequested(m_end); break;
    default: break;
    }
}

void TimelineWidget::showLens()
{
    if (!m_lens) m_lens = new TimelineLens(this);
    m_lens->resize(width(), 108);
    m_lens->move(mapToGlobal(QPoint(0, height() + 6)));
    LensState s;
    s.win0 = m_win0; s.span = m_span; s.dur = m_dur; s.start = m_start; s.end = m_end;
    s.cur = m_dragT; s.fps = m_fps; s.zoom = m_zoomAmount; s.kf = &m_kf;
    m_lens->setState(s);
    if (!m_lens->isVisible()) m_lens->show();
}

void TimelineWidget::showHint()
{
    if (!m_hint) m_hint = new TimelineHint(this);
    m_hint->move(mapToGlobal(QPoint((width() - m_hint->width()) / 2, height() + 6)));
    if (!m_hint->isVisible()) m_hint->show();
}

void TimelineWidget::hideHint()
{
    if (m_hint) m_hint->hide();
}

void TimelineWidget::leaveZoom()
{
    m_zoom = false;
    if (m_lens) m_lens->hide();
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Drag::None) return;
    const QPointF pos = e->position();
    const double beyond = pos.y() - height(); // quanto o mouse foi puxado para baixo da timeline
    const double w = width() - 2.0 * kMargin;
    const double frac = std::clamp((pos.x() - kMargin) / w, 0.0, 1.0); // coluna do mouse dentro da timeline/régua

    // Entra no zoom ao passar de kEnterBeyond e só sai ao voltar para dentro da timeline (histerese).
    const bool wantZoom = m_zoom ? beyond > 0 : beyond > kEnterBeyond;
    if (!wantZoom) {
        if (m_zoom) leaveZoom();
        m_tCont = xToSec(pos.x());
        applyDrag(m_tCont);
        showHint();
        return;
    }
    hideHint();

    // Zoom CONTÍNUO: a janela encolhe suavemente (escala logarítmica) conforme o mouse desce.
    const double spanMax = std::min(kMaxSpan, std::max(kMinSpan, m_dur));
    const double spanMin = std::min(kMinSpan, spanMax);
    m_zoomAmount = std::clamp((beyond - kEnterBeyond) / kZoomRange, 0.0, 1.0);
    const double span = spanMax * std::pow(spanMin / spanMax, m_zoomAmount);

    // O movimento horizontal vale na escala ATUAL (relativo ao ponto anterior), então descer ou subir só muda a
    // sensibilidade e nunca faz o valor pular. A janela mostrada fica ancorada de modo que a coluna do mouse
    // corresponda exatamente ao valor.
    if (!m_zoom) {
        m_zoom = true;
        m_tCont = m_dragT;
        m_prevFrac = frac;
    }
    m_tCont = std::clamp(m_tCont + (frac - m_prevFrac) * span, 0.0, m_dur);
    m_prevFrac = frac;
    m_span = span;
    m_win0 = m_tCont - frac * span;

    double t = m_tCont;
    if (m_fps > 0 && span <= 2.0)
        t = std::clamp(std::round(t * m_fps) / m_fps, 0.0, m_dur); // zoom fino: anda de frame em frame
    applyDrag(t);
    showLens();
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
    const bool was = m_drag != Drag::None;
    if (m_drag == Drag::Start) emit seekRequested(m_start);
    hideHint();
    if (m_zoom) leaveZoom();
    m_drag = Drag::None;
    update();
    if (was) emit dragFinished();
}

} // namespace shear

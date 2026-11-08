#include "TimelineWidget.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <cmath>

namespace shear {

// Régua ampliada que aparece abaixo da timeline enquanto você arrasta com o mouse puxado para baixo.
class TimelineLens : public QWidget {
public:
    explicit TimelineLens(QWidget *owner) : QWidget(owner, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setState(double center, double span, double dur, const std::vector<double> *kf, double st, double en)
    {
        m_c = center; m_span = span; m_dur = dur; m_kf = kf; m_start = st; m_end = en;
        update();
    }

protected:
    static QString fmtT(double t, int dec)
    {
        t = std::max(0.0, t);
        const int m = int(t) / 60;
        const double sec = t - m * 60;
        QString s = QString::number(sec, 'f', dec);
        if (sec < 10) s.prepend('0');
        return QString("%1:%2").arg(m).arg(s).replace('.', ',');
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = qApp->palette();
        const QColor text = pal.color(QPalette::WindowText), mid = pal.color(QPalette::Mid);
        QColor dim = text;
        dim.setAlpha(140);

        p.setBrush(pal.color(QPalette::Base));
        p.setPen(QPen(mid, 1));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

        const double m = 14, w = width() - 2 * m, t0 = m_c - m_span / 2;
        auto X = [&](double t) { return m + (t - t0) / m_span * w; };
        const double top = 26, base = height() - 10;

        // fora do clipe (antes de 0 ou depois do fim): escurecido
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 110));
        if (t0 < 0) p.drawRect(QRectF(m, top, X(0) - m, base - top));
        if (t0 + m_span > m_dur) p.drawRect(QRectF(X(m_dur), top, m + w - X(m_dur), base - top));

        // seleção
        p.setBrush(QColor(60, 110, 200, 70));
        const double xs = std::clamp(X(m_start), m, m + w), xe = std::clamp(X(m_end), m, m + w);
        if (xe > xs) p.drawRect(QRectF(xs, top, xe - xs, base - top));

        // régua: passo "redondo" com no máximo ~8 marcas principais na janela
        static const double steps[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 30, 60};
        double major = 60;
        for (double st : steps)
            if (m_span / st <= 8) { major = st; break; }
        const double minor = major / 5;
        const int dec = major >= 1 ? 0 : (major >= 0.1 ? 1 : 2);
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        for (long long k = (long long)std::ceil(t0 / minor); k * minor <= t0 + m_span; ++k) {
            const double t = k * minor, x = X(t);
            const bool isMajor = std::abs(t - std::round(t / major) * major) < minor * 0.01;
            p.setPen(QPen(isMajor ? text : dim, 1));
            p.drawLine(QPointF(x, base), QPointF(x, base - (isMajor ? 22 : 10)));
            if (isMajor) {
                p.setPen(dim);
                p.drawText(QRectF(x - 34, base - 38, 68, 14), Qt::AlignHCenter, fmtT(t, dec));
            }
        }

        // keyframes (laranja) na janela
        if (m_kf) {
            p.setPen(QPen(QColor(255, 160, 40), 2));
            auto it = std::lower_bound(m_kf->begin(), m_kf->end(), t0);
            for (; it != m_kf->end() && *it <= t0 + m_span; ++it)
                p.drawLine(QPointF(X(*it), base), QPointF(X(*it), base - 6));
        }

        // marcadores de início/fim
        p.setPen(QPen(QColor(90, 170, 255), 3));
        if (m_start >= t0 && m_start <= t0 + m_span) p.drawLine(QPointF(X(m_start), top), QPointF(X(m_start), base));
        p.setPen(QPen(QColor(255, 110, 110), 3));
        if (m_end >= t0 && m_end <= t0 + m_span) p.drawLine(QPointF(X(m_end), top), QPointF(X(m_end), base));

        // posição atual (fixa no centro; a régua desliza por baixo)
        const double cx = X(m_c);
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(QPointF(cx, top - 4), QPointF(cx, base));
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        p.setPen(text);
        p.drawText(QRectF(cx - 60, 3, 120, 18), Qt::AlignCenter, fmtT(m_c, 3));
        f.setBold(false);
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(dim);
        p.drawText(QRectF(width() - 170, 4, 156, 16), Qt::AlignRight, QString("zoom: janela de %1 s").arg(QString::number(m_span, 'f', m_span < 1 ? 2 : 1).replace('.', ',')));
    }

private:
    double m_c = 0, m_span = 1, m_dur = 0, m_start = 0, m_end = 0;
    const std::vector<double> *m_kf = nullptr;
};


TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(72);
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
}

void TimelineWidget::applyDrag(double t)
{
    switch (m_drag) {
    case Drag::Seek:  emit seekRequested(t); break;
    case Drag::Start: setStart(t); m_dragT = m_start; emit startChanged(m_start); break;
    case Drag::End:   setEnd(t); m_dragT = m_end; emit endChanged(m_end); break;
    default: break;
    }
}

void TimelineWidget::showLens(double span)
{
    if (!m_lens) m_lens = new TimelineLens(this);
    m_lens->resize(width(), 92);
    m_lens->move(mapToGlobal(QPoint(0, height() + 8)));
    m_lens->setState(m_dragT, span, m_dur, &m_kf, m_start, m_end);
    if (!m_lens->isVisible()) m_lens->show();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Drag::None) return;
    const QPointF pos = e->position();
    const double beyond = pos.y() - height(); // quanto o mouse foi puxado para baixo da timeline
    if (beyond > 6) {
        // Quanto mais para baixo, mais zoom (12 s -> ~0,4 s). O movimento horizontal passa a ser relativo e fino.
        const double span = std::clamp(12.0 * std::exp(-(beyond - 6) / 70.0), 0.4, std::max(0.4, std::min(m_dur, 12.0)));
        if (!m_zoom) {
            m_zoom = true;
            m_lastX = pos.x();
        }
        const double w = width() - 2.0 * kMargin;
        m_dragT = std::clamp(m_dragT + (pos.x() - m_lastX) / w * span, 0.0, m_dur);
        m_lastX = pos.x();
        applyDrag(m_dragT);
        showLens(span);
    } else {
        if (m_zoom) {
            m_zoom = false;
            if (m_lens) m_lens->hide();
        }
        m_dragT = xToSec(pos.x());
        applyDrag(m_dragT);
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag == Drag::Start) emit seekRequested(m_start);
    if (m_lens) m_lens->hide();
    m_zoom = false;
    m_drag = Drag::None;
}

} // namespace shear

#include "TimelineWidget.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace shear {

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
    if (std::min(ds, de) <= 9)
        m_drag = ds <= de ? Drag::Start : Drag::End;
    else {
        m_drag = Drag::Seek;
        emit seekRequested(xToSec(x));
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Drag::None) return;
    const double t = xToSec(e->position().x());
    switch (m_drag) {
    case Drag::Seek:  emit seekRequested(t); break;
    case Drag::Start: setStart(t); emit startChanged(m_start); break;
    case Drag::End:   setEnd(t); emit endChanged(m_end); break;
    default: break;
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag == Drag::Start) emit seekRequested(m_start);
    m_drag = Drag::None;
}

} // namespace shear

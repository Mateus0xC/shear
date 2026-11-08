#include "SoundToggle.h"
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace shear {

SoundToggle::SoundToggle(QWidget *parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(42, 42);
}

void SoundToggle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const bool on = isChecked();
    const bool hover = underMouse();
    const QColor accent = pal.color(QPalette::Highlight);

    // círculo
    const QRectF circle = QRectF(rect()).adjusted(3, 3, -3, -3);
    QColor fill = on ? accent : QColor(0, 0, 0, 0);
    if (hover) fill = on ? accent.lighter(115) : pal.color(QPalette::Mid);
    QColor line = pal.color(QPalette::Mid);
    p.setBrush(fill);
    p.setPen(on ? Qt::NoPen : QPen(line, 1.5));
    p.drawEllipse(circle);

    // ícone (caixa de 20x20 centralizada)
    QColor ink = on ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::WindowText);
    if (!on) ink.setAlpha(hover ? 230 : 170);
    p.translate(rect().center() - QPointF(10, 10));
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    QPainterPath speaker;
    speaker.moveTo(2, 7.5);
    speaker.lineTo(6, 7.5);
    speaker.lineTo(11, 3.5);
    speaker.lineTo(11, 16.5);
    speaker.lineTo(6, 12.5);
    speaker.lineTo(2, 12.5);
    speaker.closeSubpath();
    p.drawPath(speaker);

    QPen pen(ink, 1.7, Qt::SolidLine, Qt::RoundCap);
    p.setBrush(Qt::NoBrush);
    p.setPen(pen);
    if (on) { // ondas sonoras
        p.drawArc(QRectF(7.5, 5.5, 9, 9), -45 * 16, 90 * 16);
        p.drawArc(QRectF(4.5, 2, 15, 16), -45 * 16, 90 * 16);
    } else {  // risco: som desligado
        p.setPen(QPen(ink, 1.9, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(3.5, 3), QPointF(18.5, 17.5));
    }
}

} // namespace shear

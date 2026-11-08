#include "VerticalFader.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>

namespace shear {

static constexpr double kPadTop = 10, kPadBottom = 10;

VerticalFader::VerticalFader(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Volume do microfone (dB). Duplo clique ou o botão abaixo zera; a roda do mouse ajusta de 1 em 1.");
}

void VerticalFader::setValue(int v)
{
    v = std::clamp(v, kMin, kMax);
    if (v == m_val) return;
    m_val = v;
    update();
    emit valueChanged(m_val);
}

double VerticalFader::yFor(int v) const
{
    const double h = height() - kPadTop - kPadBottom;
    return kPadTop + (kMax - v) / double(kMax - kMin) * h;
}

int VerticalFader::valueAt(double y) const
{
    const double h = height() - kPadTop - kPadBottom;
    const double f = std::clamp((y - kPadTop) / h, 0.0, 1.0);
    return int(std::lround(kMax - f * (kMax - kMin)));
}

void VerticalFader::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const bool on = isEnabled();
    const QColor text = pal.color(QPalette::WindowText);
    QColor dim = text;
    dim.setAlpha(on ? 150 : 80);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor accent = on ? pal.color(QPalette::Highlight) : mid;

    const double cx = width() - 24; // alça de 26 px encosta quase na borda direita: sem faixa vazia

    // escala em dB (esquerda)
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    for (int v = kMax; v >= kMin; v -= 10) {
        const double y = yFor(v);
        const bool zero = v == 0;
        p.setPen(QPen(zero ? text : dim, zero ? 1.5 : 1));
        p.drawLine(QPointF(cx - 13, y), QPointF(cx - 6, y));
        p.setPen(zero ? text : dim);
        const QString label = v > 0 ? QString("+%1").arg(v) : QString::number(v);
        p.drawText(QRectF(0, y - 8, cx - 17, 16), Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // trilho + preenchimento a partir do 0 dB até a alça
    p.setPen(Qt::NoPen);
    p.setBrush(mid);
    p.drawRoundedRect(QRectF(cx - 2.5, yFor(kMax), 5, yFor(kMin) - yFor(kMax)), 2.5, 2.5);
    const double y0 = yFor(0), yv = yFor(m_val);
    p.setBrush(accent);
    p.drawRect(QRectF(cx - 2.5, std::min(y0, yv), 5, std::abs(yv - y0)));

    // alça
    const QRectF h(cx - 13, yv - 7, 26, 14);
    p.setBrush(on ? text : pal.color(QPalette::Midlight));
    p.setPen(QPen(on ? accent : dim, 1.5));
    p.drawRoundedRect(h, 5, 5);
}

void VerticalFader::mousePressEvent(QMouseEvent *e) { setValue(valueAt(e->position().y())); }
void VerticalFader::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton)
        setValue(valueAt(e->position().y()));
}
void VerticalFader::mouseDoubleClickEvent(QMouseEvent *) { setValue(0); }

void VerticalFader::wheelEvent(QWheelEvent *e)
{
    const int notches = e->angleDelta().y() / 120;
    if (notches != 0) setValue(m_val + notches);
    e->accept();
}

} // namespace shear

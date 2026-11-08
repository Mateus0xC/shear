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
    setToolTip("Volume do microfone. Duplo clique ou o botão abaixo zera; a roda do mouse ajusta de pouco em pouco.");
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

    const double cx = width() - 21; // alça de 30 px encosta quase na borda direita: sem faixa vazia

    // escala em dB (esquerda): marca a cada 5 dB, número a cada 10
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    for (int v = kMax; v >= kMin; v -= 5) {
        const double y = yFor(v);
        const bool labeled = v % 10 == 0;
        const bool zero = v == 0;
        p.setPen(QPen(zero ? text : dim, zero ? 1.6 : 1));
        p.drawLine(QPointF(cx - (labeled ? 15 : 10), y), QPointF(cx - 6, y));
        if (labeled) {
            p.setPen(zero ? text : dim);
            const QString label = v > 0 ? QString("+%1").arg(v) : QString::number(v);
            p.drawText(QRectF(0, y - 8, cx - 19, 16), Qt::AlignRight | Qt::AlignVCenter, label);
        }
    }

    // trilho + preenchimento a partir do 0 dB até a alça
    p.setPen(Qt::NoPen);
    p.setBrush(mid);
    p.drawRoundedRect(QRectF(cx - 3, yFor(kMax), 6, yFor(kMin) - yFor(kMax)), 3, 3);
    const double y0 = yFor(0), yv = yFor(m_val);
    p.setBrush(accent);
    p.drawRect(QRectF(cx - 3, std::min(y0, yv), 6, std::abs(yv - y0)));

    // alça
    const QRectF h(cx - 15, yv - 8, 30, 16);
    p.setBrush(on ? text : pal.color(QPalette::Midlight));
    p.setPen(QPen(on ? accent : dim, 1.5));
    p.drawRoundedRect(h, 6, 6);
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

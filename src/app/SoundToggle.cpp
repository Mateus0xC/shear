#include "SoundToggle.h"
#include <QFontDatabase>
#include <QPainter>
#include <QPalette>

namespace shear {

// Ícones do próprio Windows (Segoe Fluent Icons no Windows 11, Segoe MDL2 Assets no 10): o alto-falante nativo.
// Sem nenhuma das duas fontes, usa o emoji 🔊 / 🔇.
static QString iconFontFamily()
{
    static const QString family = [] {
        const QStringList all = QFontDatabase::families();
        for (const char *name : {"Segoe Fluent Icons", "Segoe MDL2 Assets"})
            if (all.contains(QString::fromLatin1(name)))
                return QString::fromLatin1(name);
        return QString();
    }();
    return family;
}

SoundToggle::SoundToggle(QWidget *parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SoundToggle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const bool on = isChecked();
    const bool hover = underMouse();
    const QColor accent = pal.color(QPalette::Highlight);
    const QColor mid = pal.color(QPalette::Mid);

    // pílula (mesmo desenho das pílulas Desktop/Mic: borda de 1 px, cantos totalmente arredondados)
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const double r = box.height() / 2;
    QColor fill = on ? accent : QColor(0, 0, 0, 0);
    QColor border = on ? accent : mid;
    if (hover) {
        if (on) fill = fill.lighter(112);
        else    fill = QColor(255, 255, 255, 14), border = border.lighter(130);
    }
    p.setBrush(fill);
    p.setPen(QPen(border, 1));
    p.drawRoundedRect(box, r, r);

    QColor ink = on ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::WindowText);
    if (!on) ink.setAlpha(hover ? 230 : 160);

    if (const QString family = iconFontFamily(); !family.isEmpty()) {
        QFont f(family);
        f.setPixelSize(16);
        p.setFont(f);
        p.setPen(ink);
        p.drawText(box, Qt::AlignCenter, QString(QChar(on ? 0xE767 : 0xE74F))); // E767 = volume, E74F = mudo
    } else {
        QFont f = font();
        f.setPixelSize(14);
        p.setFont(f);
        p.setOpacity(on || hover ? 1.0 : 0.65);
        p.drawText(box, Qt::AlignCenter, QString::fromUcs4(on ? U"\U0001F50A" : U"\U0001F507")); // 🔊 / 🔇
    }
}

} // namespace shear

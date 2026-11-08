#pragma once
#include <QAbstractButton>

namespace shear {

// Pílula de ligar/desligar o som ao arrastar, da mesma família das pílulas Desktop e Mic do cartão de áudio:
// ligada = cor de destaque com o alto-falante e ondas; desligada = contorno discreto com o alto-falante riscado.
class SoundToggle : public QAbstractButton {
    Q_OBJECT
public:
    explicit SoundToggle(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {80, 28}; }
    QSize minimumSizeHint() const override { return {40, 28}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

} // namespace shear

#pragma once
#include <QAbstractButton>

namespace shear {

// Botão redondo com ícone de alto-falante desenhado por código: ligado = círculo colorido com ondas sonoras;
// desligado = contorno discreto com o alto-falante riscado.
class SoundToggle : public QAbstractButton {
    Q_OBJECT
public:
    explicit SoundToggle(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {42, 42}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

} // namespace shear

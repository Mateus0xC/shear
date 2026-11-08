#pragma once
#include <QWidget>

namespace shear {

// Fader vertical com escala em dB. Arrastar/clicar muda o valor; roda do mouse = ±1 dB; duplo clique zera.
class VerticalFader : public QWidget {
    Q_OBJECT
public:
    explicit VerticalFader(QWidget *parent = nullptr);
    int value() const { return m_val; }
    void setValue(int v);

signals:
    void valueChanged(int v);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    QSize sizeHint() const override { return {80, 260}; }
    QSize minimumSizeHint() const override { return {76, 140}; }

private:
    double yFor(int v) const;
    int valueAt(double y) const;

    static constexpr int kMin = -30, kMax = 30;
    int m_val = 0;
};

} // namespace shear

#pragma once
#include <QDialog>

namespace shear {

// "Atalhos e controles": abas com todas as teclas e gestos do mouse. Montada só quando é aberta (não pesa na abertura).
class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr);
};

} // namespace shear

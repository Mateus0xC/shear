#pragma once
#include <QDialog>
#include <QPair>
#include <QVector>

namespace shear {

// "Atalhos e controles": abas com todas as teclas e gestos do mouse. Montada só quando é aberta (não pesa na abertura).
class HelpDialog : public QDialog {
    Q_OBJECT
public:
    // `pluginRows`: (teclas, o que faz) dos atalhos que os plugins instalados registraram
    explicit HelpDialog(const QVector<QPair<QString, QString>> &pluginRows = {}, QWidget *parent = nullptr);
};

} // namespace shear

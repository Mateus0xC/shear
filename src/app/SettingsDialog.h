#pragma once
#include "util/Settings.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace shear {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const Config &cfg, QWidget *parent = nullptr);
    Config config() const;
    bool wantsHelp() const { return m_wantsHelp; } // fechou para abrir "Atalhos e controles"
    void accept() override; // aplica o menu de contexto do Windows ao salvar

private:
    QComboBox *m_mode;
    QCheckBox *m_gpu, *m_scrub, *m_fade, *m_ctx;
    QLineEdit *m_out, *m_sp;
    bool m_wantsHelp = false;
};

} // namespace shear

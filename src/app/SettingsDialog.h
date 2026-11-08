#pragma once
#include "util/Settings.h"
#include <QDialog>

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

private:
    void refreshContextState();
    QComboBox *m_mode, *m_preset, *m_tracks;
    QDoubleSpinBox *m_threshold;
    QLineEdit *m_out, *m_sp;
    QLabel *m_ctxState;
};

} // namespace shear

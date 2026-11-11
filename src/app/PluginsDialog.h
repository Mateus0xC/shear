#pragma once
#include <QDialog>

class QLabel;
class QVBoxLayout;

namespace shear {

class PluginInstaller;
class PluginManager;

// Configurações → Plugins…: instalar, atualizar e remover plugins (só faz rede quando você clica).
class PluginsDialog : public QDialog {
    Q_OBJECT
public:
    PluginsDialog(PluginManager *mgr, QWidget *parent = nullptr);
    bool wantsCheck() const { return m_wantsCheck; } // clicou em "Verificar atualizações agora"

private:
    void rebuild();
    void install(const QString &id, const QString &installedVersion);
    void remove(const QString &id);

    PluginManager *m_mgr;
    PluginInstaller *m_installer;
    QVBoxLayout *m_list = nullptr;
    QLabel *m_status = nullptr;
    bool m_wantsCheck = false;
};

} // namespace shear

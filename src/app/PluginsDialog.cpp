#include "PluginsDialog.h"
#include "PluginInstaller.h"
#include "PluginManager.h"
#include "Updater.h"
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace shear {

PluginsDialog::PluginsDialog(PluginManager *mgr, QWidget *parent) : QDialog(parent), m_mgr(mgr), m_installer(new PluginInstaller(this))
{
    setWindowTitle("Plugins e atualizações");
    setMinimumWidth(560);
    auto *root = new QVBoxLayout(this);
    auto *intro = new QLabel("Plugins acrescentam recursos ao shear. Eles não vêm instalados: instalar baixa o plugin e confere o "
                             "fingerprint (SHA-256) de cada arquivo antes de ligar.");
    intro->setWordWrap(true);
    root->addWidget(intro);
    m_list = new QVBoxLayout;
    root->addLayout(m_list);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    // Atualizações: o shear consulta sozinho uma vez por dia (só avisa; nada é instalado sem você clicar)
    auto *ver = new QLabel(QString("shear %1").arg(Updater::currentVersion()));
    ver->setForegroundRole(QPalette::PlaceholderText);
    auto *check = bb->addButton("Verificar atualizações agora", QDialogButtonBox::ActionRole);
    connect(check, &QPushButton::clicked, this, [this] {
        m_wantsCheck = true;
        reject();
    });
    root->addWidget(ver);
    bb->button(QDialogButtonBox::Close)->setText("Fechar");
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);

    connect(m_installer, &PluginInstaller::status, m_status, &QLabel::setText);
    connect(m_installer, &PluginInstaller::finished, this, [this](const QString &id, bool ok, const QString &error) {
        if (!ok) {
            m_status->setText("Não foi possível instalar: " + error);
        } else if (m_installer->upToDate()) {
            m_status->setText("Este plugin já está na versão mais nova.");
        } else {
            QString err;
            switch (m_mgr->commitStaged(id, &err)) {
            case PluginManager::Commit::Loaded: m_status->setText("Plugin instalado e ativo."); break;
            case PluginManager::Commit::NeedsRestart:
                m_status->setText("Atualização pronta: vale na próxima vez que você abrir o shear.");
                break;
            case PluginManager::Commit::Failed: m_status->setText("Não foi possível instalar: " + err); break;
            }
        }
        rebuild();
    });
    rebuild();
}

void PluginsDialog::rebuild()
{
    while (QLayoutItem *it = m_list->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
    // os plugins que o shear conhece + os que estão instalados (mesmo os de fora do catálogo)
    QVector<PluginInfo> rows = PluginManager::builtinCatalog();
    QSet<QString> seen;
    for (const PluginInfo &c : std::as_const(rows)) seen.insert(c.id);
    for (const PluginInfo &p : m_mgr->plugins())
        if (!seen.contains(p.id)) rows.push_back(p);

    for (const PluginInfo &known : std::as_const(rows)) {
        const PluginInfo *inst = m_mgr->info(known.id);
        auto *card = new QFrame;
        card->setFrameShape(QFrame::StyledPanel);
        auto *cl = new QHBoxLayout(card);
        auto *text = new QVBoxLayout;
        auto *name = new QLabel(QString("<b>%1</b>%2").arg(known.name.toHtmlEscaped(),
                                inst && !inst->version.isEmpty() ? "  " + inst->version.toHtmlEscaped() : QString()));
        auto *desc = new QLabel((inst && !inst->description.isEmpty() ? inst->description : known.description));
        desc->setWordWrap(true);
        desc->setForegroundRole(QPalette::PlaceholderText);
        QString state;
        if (!inst) state = "Não instalado";
        else if (inst->loaded) state = inst->userInstalled ? "Ativo" : "Ativo (pasta do shear, modo de desenvolvimento)";
        else state = "Não carregou: " + inst->error;
        auto *st = new QLabel(state);
        text->addWidget(name);
        text->addWidget(desc);
        text->addWidget(st);
        cl->addLayout(text, 1);
        const QString id = known.id;
        auto *more = new QPushButton("Saiba mais");
        more->setFlat(true);
        more->setCursor(Qt::PointingHandCursor);
        more->setFocusPolicy(Qt::NoFocus);
        connect(more, &QPushButton::clicked, this, [this, id] { showDetails(id); });
        text->addWidget(more, 0, Qt::AlignLeft);
        if (!inst) {
            auto *b = new QPushButton("Instalar");
            connect(b, &QPushButton::clicked, this, [this, id] { install(id, QString()); });
            cl->addWidget(b);
        } else if (inst->userInstalled) {
            auto *up = new QPushButton("Atualizar");
            connect(up, &QPushButton::clicked, this, [this, id, v = inst->version] { install(id, v); });
            auto *rm = new QPushButton("Remover");
            connect(rm, &QPushButton::clicked, this, [this, id] { remove(id); });
            cl->addWidget(up);
            cl->addWidget(rm);
        }
        m_list->addWidget(card);
    }
}

// "Saiba mais": do próprio plugin (plugin.json) se está instalado; senão, do catálogo (uma consulta rápida, só quando você pede).
void PluginsDialog::showDetails(const QString &id)
{
    if (const PluginInfo *p = m_mgr->info(id); p && !p->details.isEmpty()) {
        QMessageBox::information(this, p->name, p->details);
        return;
    }
    m_status->setText("Buscando os detalhes…");
    auto *c = new PluginInstaller(this);
    connect(c, &PluginInstaller::metaReady, this, [this, c](const QString &pid, const QString &name, const QString &, const QString &details, const QString &err) {
        c->deleteLater();
        m_status->clear();
        QString shown = details;
        if (shown.isEmpty()) {
            for (const PluginInfo &k : PluginManager::builtinCatalog())
                if (k.id == pid) shown = k.description;
            shown += err.isEmpty() ? QString() : "\n\n(Não consegui ler os detalhes agora: " + err + ")";
        }
        QMessageBox::information(this, name.isEmpty() ? pid : name, shown);
    });
    c->fetchMeta(id);
}

void PluginsDialog::install(const QString &id, const QString &installedVersion)
{
    if (m_installer->busy()) return;
    m_installer->install(id, installedVersion);
}

void PluginsDialog::remove(const QString &id)
{
    const PluginInfo *p = m_mgr->info(id);
    if (!p) return;
    if (QMessageBox::question(this, "Remover plugin", QString("Remover o plugin %1?").arg(p->name), QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    if (m_mgr->requestRemoval(id))
        m_status->setText("Plugin removido.");
    else
        m_status->setText("O plugin está em uso: ele some quando você fechar o shear.");
    PluginManager::setSuggestionDismissed(id, false);
    rebuild();
}

} // namespace shear

#include "SettingsDialog.h"
#include "platform/Windows.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace shear {

static QWidget *folderRow(QLineEdit *edit, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *b = new QPushButton("Procurar…", w);
    l->addWidget(edit, 1);
    l->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, w, [edit, w] {
        const QString d = QFileDialog::getExistingDirectory(w, "Escolher pasta", edit->text());
        if (!d.isEmpty()) edit->setText(d);
    });
    return w;
}

SettingsDialog::SettingsDialog(const Config &c, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Configurações");
    setMinimumWidth(520);
    m_mode = new QComboBox;
    m_mode->addItems({"Rápido (instantâneo; o início encaixa nas marcas da timeline)", "Exato (no frame escolhido; demora mais)"});
    m_mode->setCurrentIndex(c.mode == CutMode::Exact ? 1 : 0);
    m_out = new QLineEdit(c.outputDir);
    m_out->setPlaceholderText("Mesma pasta do vídeo original");
    m_sp = new QLineEdit(c.shadowPlayDir);
    m_sp->setPlaceholderText("Pasta Vídeos do usuário");

    auto *form = new QFormLayout;
    form->addRow("Modo de corte padrão:", m_mode);
    form->addRow("Pasta dos clipes mantidos:", folderRow(m_out, this));
    form->addRow("Pasta do ShadowPlay:", folderRow(m_sp, this));
    m_gpu = new QCheckBox("Usar a placa de vídeo no preview");
    m_gpu->setChecked(c.gpuDecode);
    m_gpu->setToolTip("Deixa o preview mais leve para o processador. Vale ao reabrir o Shear.");
    form->addRow("Preview:", m_gpu);

    m_ctxState = new QLabel;
    auto *add = new QPushButton("Adicionar “Cortar com Shear” ao menu de contexto");
    auto *rem = new QPushButton("Remover");
    connect(add, &QPushButton::clicked, this, [this] {
        installContextMenu(QCoreApplication::applicationFilePath());
        refreshContextState();
    });
    connect(rem, &QPushButton::clicked, this, [this] {
        removeContextMenu();
        refreshContextState();
    });
    auto *ctx = new QHBoxLayout;
    ctx->addWidget(add);
    ctx->addWidget(rem);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Salvar");
    bb->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addSpacing(8);
    root->addWidget(new QLabel("Integração com o Windows (sem precisar de administrador):"));
    root->addLayout(ctx);
    root->addWidget(m_ctxState);
    root->addWidget(bb);
    refreshContextState();
}

void SettingsDialog::refreshContextState()
{
    m_ctxState->setText(contextMenuInstalled() ? "Menu de contexto: instalado"
                                                : "Menu de contexto: não instalado");
}

Config SettingsDialog::config() const
{
    Config c;
    c.mode = m_mode->currentIndex() == 1 ? CutMode::Exact : CutMode::Fast;
    c.gpuDecode = m_gpu->isChecked();
    c.outputDir = m_out->text().trimmed();
    c.shadowPlayDir = m_sp->text().trimmed();
    return c;
}

} // namespace shear

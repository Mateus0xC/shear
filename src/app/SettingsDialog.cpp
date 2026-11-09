#include "SettingsDialog.h"
#include "HelpDialog.h"
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
    form->addRow("Pasta dos vídeos:", folderRow(m_sp, this));
    m_gpu = new QCheckBox("Usar a placa de vídeo no preview");
    m_gpu->setChecked(c.gpuDecode);
    m_gpu->setToolTip("Deixa o preview mais leve para o processador. Vale ao reabrir o shear.");
    form->addRow("Preview:", m_gpu);
    m_scrub = new QCheckBox("Ouvir o som enquanto arrasto na timeline");
    m_scrub->setChecked(c.scrubAudio);
    m_scrub->setToolTip("Toca um trecho do áudio a cada posição, com o vídeo parado. Quanto maior o zoom, mais curto o trecho.");
    form->addRow("Som ao arrastar:", m_scrub);

    m_fade = new QCheckBox("Suavizar as emendas ao apagar trechos do meio");
    m_fade->setChecked(c.fadeJoins);
    m_fade->setToolTip("Nas emendas o vídeo esmaece rapidinho (0,2 s) e o áudio faz crossfade. "
                       "O começo e o fim do vídeo ficam como estão.");
    form->addRow("Emendas:", m_fade);

    m_ctx = new QCheckBox("Mostrar “Cortar com shear” ao clicar com o botão direito em um vídeo");
    m_ctx->setChecked(contextMenuInstalled());
    m_ctx->setToolTip("Adiciona a opção ao menu do Windows (não precisa de administrador). No Windows 11 ela fica em “Mostrar mais opções”.");
    form->addRow("Menu do Windows:", m_ctx);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    // Ajuda com todos os atalhos: fica aqui nas Configurações (e abre também com F1 em qualquer tela)
    auto *help = bb->addButton("Atalhos e controles…", QDialogButtonBox::HelpRole);
    help->setToolTip("Todas as teclas e gestos do mouse (F1)");
    connect(help, &QPushButton::clicked, this, [this] { HelpDialog(this).exec(); });
    bb->button(QDialogButtonBox::Ok)->setText("Salvar");
    bb->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addSpacing(8);
    root->addWidget(bb);
}

void SettingsDialog::accept()
{
    if (m_ctx->isChecked() != contextMenuInstalled()) {
        if (m_ctx->isChecked())
            installContextMenu(QCoreApplication::applicationFilePath());
        else
            removeContextMenu();
    }
    QDialog::accept();
}

Config SettingsDialog::config() const
{
    Config c;
    c.mode = m_mode->currentIndex() == 1 ? CutMode::Exact : CutMode::Fast;
    c.gpuDecode = m_gpu->isChecked();
    c.scrubAudio = m_scrub->isChecked();
    c.fadeJoins = m_fade->isChecked();
    c.outputDir = m_out->text().trimmed();
    c.shadowPlayDir = m_sp->text().trimmed();
    return c;
}

} // namespace shear

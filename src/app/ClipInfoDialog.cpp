#include "ClipInfoDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace shear {

static QComboBox *makeCombo(const QStringList &names, const QString &current, const QString &hint)
{
    auto *c = new QComboBox;
    c->setEditable(true); // dá para digitar um nome que não está na lista
    c->setInsertPolicy(QComboBox::NoInsert);
    c->addItem(QString()); // vazio = não mexe
    c->addItems(names);
    c->setCurrentText(current);
    c->lineEdit()->setPlaceholderText(hint.isEmpty() ? QStringLiteral("não informado") : "do jogo: " + hint);
    return c;
}

ClipInfoDialog::ClipInfoDialog(const QStringList &maps, const QStringList &agents, const QJsonObject &current,
                               const QString &apiMap, const QString &apiAgent, int autoKills, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Dados do clipe");
    auto *form = new QFormLayout;
    m_map = makeCombo(maps, current.value("map").toString(), apiMap);
    m_agent = makeCombo(agents, current.value("agent").toString(), apiAgent);
    m_kills = new QSpinBox;
    m_kills->setRange(0, 30);
    m_kills->setSpecialValueText(autoKills > 0 ? QString("automático (%1)").arg(autoKills) : QStringLiteral("automático"));
    m_kills->setValue(current.value("kills").toInt(0));
    m_kills->setToolTip("Maior número de kills num mesmo round, no clipe. 2 ou mais vira a tag 2k, 3k, 4k...");
    m_comment = new QLineEdit(current.value("comment").toString());
    m_comment->setPlaceholderText("opcional");
    m_comment->setMaxLength(300);
    form->addRow("Mapa", m_map);
    form->addRow("Agente", m_agent);
    form->addRow("Kills", m_kills);
    form->addRow("Comentário", m_comment);

    auto *hint = new QLabel("O que você digitar vale no lugar do que vem do jogo. Campos vazios (ou \"automático\") não mudam nada.");
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *clear = buttons->addButton("Limpar tudo", QDialogButtonBox::ResetRole);
    buttons->button(QDialogButtonBox::Ok)->setText("Salvar");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_map->setCurrentText(QString());
        m_agent->setCurrentText(QString());
        m_kills->setValue(0);
        m_comment->clear();
    });

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(hint);
    lay->addWidget(buttons);
    setMinimumWidth(420);
}

QJsonObject ClipInfoDialog::result() const
{
    QJsonObject o;
    if (const QString s = m_map->currentText().trimmed(); !s.isEmpty()) o.insert("map", s);
    if (const QString s = m_agent->currentText().trimmed(); !s.isEmpty()) o.insert("agent", s);
    if (m_kills->value() > 0) o.insert("kills", m_kills->value());
    if (const QString s = m_comment->text().trimmed(); !s.isEmpty()) o.insert("comment", s);
    return o;
}

} // namespace shear

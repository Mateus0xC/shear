#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QSpinBox;

namespace shear {

// "Dados do clipe": mapa, agente, kills e comentário digitados à mão. Campo vazio = não mexe (vale o que veio do jogo).
class ClipInfoDialog : public QDialog {
    Q_OBJECT
public:
    // `current`: o que já foi digitado antes ({map, agent, kills, comment}); `apiMap`/`apiAgent`: o que o jogo informou
    // (só para mostrar como dica); `autoKills`: kills achadas sozinhas no clipe (0 se nenhuma).
    ClipInfoDialog(const QStringList &maps, const QStringList &agents, const QJsonObject &current, const QString &apiMap,
                   const QString &apiAgent, int autoKills, QWidget *parent = nullptr);
    QJsonObject result() const; // só os campos preenchidos; objeto vazio = limpar tudo

private:
    QComboBox *m_map, *m_agent;
    QSpinBox *m_kills;
    QLineEdit *m_comment;
};

} // namespace shear

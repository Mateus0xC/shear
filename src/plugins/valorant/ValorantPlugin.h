#pragma once
#include "../../plugin_api/ShearPlugin.h"
#include "MatchEvents.h"
#include "ValorantService.h"
#include <QJsonObject>
#include <QObject>
#include <limits>

namespace shear::valorant {

// Plugin do Valorant: mapa, agente, kills e mortes do clipe (pela API local do cliente), marcas na timeline, I/O no último
// round, nome do arquivo e dados que ficam guardados no clipe mantido. Fala com o shear só pela interface de plugin.
class ValorantPlugin : public QObject, public plugin::IShearPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "br.shear.Plugin/1")
    Q_INTERFACES(shear::plugin::IShearPlugin)
public:
    void init(plugin::IShearHost *host) override;
    QVector<plugin::Action> actions() const override;
    void triggerAction(const QString &id) override;
    void clipClosed() override;
    void clipOpened(const plugin::ClipContext &clip) override;
    void durationChanged(double seconds) override;
    plugin::ChoiceMenu chipMenu(const QString &chipId) override;
    void chipChosen(const QString &chipId, const QString &value) override;
    QVector<plugin::RowInfo> rowInfos(const QVector<plugin::RowQuery> &rows) const override;
    QString exportStem(const plugin::ExportContext &ctx) override;
    void exportKept(const QString &finalPath, qint64 size) override;
    QVariant saveState() const override;
    void restoreState(const QVariant &state) override;

private:
    ValorantService *svc();
    void resetState();
    void apply();              // alinha os eventos ao clipe e publica marcas, I/O e chips
    void publishChips();
    void calibrate();          // K
    void resetCalibration();   // Shift+K
    void commitManual();
    double currentOffset() const;

    plugin::IShearHost *m_host = nullptr;
    ValorantService *m_svc = nullptr;

    // clipe aberto
    QString m_file;
    qint64 m_clipEndMs = 0, m_clipSize = 0; // data de criação (fim do clipe) e tamanho
    double m_duration = 0;
    QString m_apiMap, m_apiAgent;           // o que o jogo informou
    QJsonObject m_manual;                   // o que foi escolhido à mão: {map, agent, kills}
    QVector<MatchEvent> m_vEvents;          // meus kills/mortes da partida (horário absoluto)
    QVector<qint64> m_roundStarts;
    qint64 m_matchEndMs = 0;
    QString m_matchId;
    double m_matchOffset = std::numeric_limits<double>::quiet_NaN(); // calibrado com K (NaN = não)
    double m_autoOffset = std::numeric_limits<double>::quiet_NaN();  // achado no log do jogo (NaN = não)
    QVector<ClipEvent> m_keptEvents;        // clipe MANTIDO reaberto: eventos já no tempo dele
    QStringList m_keptTags, m_autoTags, m_vTags;

    // dados do clipe que está sendo exportado
    struct ExportMeta {
        QString map, agent, tag;
        QVector<ClipEvent> events; // já no tempo do arquivo exportado
    } m_exportMeta;
    bool m_listRefreshQueued = false;
};

} // namespace shear::valorant

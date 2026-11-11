#pragma once
#include "../plugin_api/ShearPlugin.h"
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>
#include <memory>

class QPluginLoader;

namespace shear {

// O que a janela principal implementa para os plugins. É a mesma API de plugin::IShearHost, mas sabendo de QUAL plugin veio
// a chamada (vários plugins podem pôr chips, marcas e fundo ao mesmo tempo).
class IHostBackend {
public:
    virtual ~IHostBackend() = default;
    virtual double position() const = 0;
    virtual double duration() const = 0;
    virtual QString dataDir() const = 0;
    virtual void log(const QString &line) = 0;
    virtual void showStatus(const QString &text) = 0;
    virtual void pushUndo() = 0;
    virtual void setInfoChips(const QString &pluginId, const QVector<plugin::InfoChip> &chips) = 0;
    virtual void setBackdrop(const QString &pluginId, const QString &imagePath) = 0;
    virtual void setMarkers(const QString &pluginId, const QVector<plugin::Marker> &markers) = 0;
    virtual void suggestRange(const QString &pluginId, double start, double end) = 0;
    virtual void refreshList() = 0;
};

struct PluginInfo {
    QString id, name, version, description;
    QString dir, library;
    int apiMajor = 0, apiMinor = 0, minHostApiMinor = 0;
    QStringList appliesTo; // padrões do nome/caminho do clipe para os quais vale sugerir este plugin
    bool userInstalled = false; // em %LOCALAPPDATA%/Shear/plugins (pode ser removido pelo app)
    bool loaded = false;
    QString error;         // por que não carregou ("" = ok)
};

// Descobre, confere e carrega os plugins; repassa os eventos do shear a eles.
//  - Procura em <pasta do shear>/plugins/<id>/ (desenvolvimento) e em %LOCALAPPDATA%/Shear/plugins/<id>/ (instalados).
//  - Compatível = mesma API MAIOR e o shear ter a API MENOR que o plugin exige (plugin.json: apiMajor, minHostApiMinor).
//    Não precisa ser a mesma versão do shear.
//  - Plugin instalado pelo app tem um installed.json com o SHA-256 de cada arquivo: é conferido ANTES de carregar a DLL.
class PluginManager : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(IHostBackend *backend, QObject *parent = nullptr);
    ~PluginManager() override;

    void loadAll();
    const QVector<PluginInfo> &plugins() const { return m_infos; }
    const PluginInfo *info(const QString &id) const;
    // Plugins oficiais que o shear conhece (nome, descrição, quando sugerir), instalados ou não.
    static QVector<PluginInfo> builtinCatalog();
    // Ao abrir: aplica atualizações e remoções que ficaram pendentes (uma DLL carregada não pode ser trocada/apagada).
    static void applyPending();
    enum class Commit { Loaded, NeedsRestart, Failed };
    // Instala o que o PluginInstaller preparou em .stage-<id> e, se der, carrega na hora (sem reiniciar).
    Commit commitStaged(const QString &id, QString *error = nullptr);
    // Remove um plugin instalado pelo app. true = removido agora; false = ficou marcado e some na próxima abertura.
    bool requestRemoval(const QString &id);
    // Id do plugin oficial (não instalado, não dispensado) que combina com esse clipe ("" = nenhum). Ex.: "valorant".
    QString suggestionFor(const QString &clipPath) const;
    static bool suggestionDismissed(const QString &id);
    static void setSuggestionDismissed(const QString &id, bool dismissed);
    bool isLoaded(const QString &id) const;
    static QString userPluginsDir(); // %LOCALAPPDATA%/Shear/plugins

    // "" = ok; senão o motivo (em português, para mostrar ao usuário)
    static QString checkCompatible(int apiMajor, int minHostApiMinor);
    static QString verifyInstalled(const QString &dir);
    static QString sha256File(const QString &path);

    // --- despacho para os plugins carregados ---
    void clipClosed();
    void clipOpened(const plugin::ClipContext &clip);
    void durationChanged(double seconds);
    void clipOpenedFor(const QString &pluginId, const plugin::ClipContext &clip, double duration); // plugin recém-carregado
    struct PluginAction {
        QString pluginId;
        plugin::Action action;
    };
    QVector<PluginAction> actions() const;
    void triggerAction(const QString &pluginId, const QString &actionId);
    plugin::ChoiceMenu chipMenu(const QString &pluginId, const QString &chipId);
    void chipChosen(const QString &pluginId, const QString &chipId, const QString &value);
    QVector<plugin::RowInfo> rowInfos(const QVector<plugin::RowQuery> &rows) const; // thread-safe
    QString exportStem(const plugin::ExportContext &ctx); // o primeiro nome não vazio
    void exportKept(const QString &finalPath, qint64 size);
    QVariantMap saveState() const;
    void restoreState(const QVariantMap &state);

signals:
    void pluginsChanged();
    void pluginLoaded(const QString &pluginId); // carregado agora (instalação sem reiniciar)

private:
    struct Loaded;
    void scanDir(const QString &root, bool user, const QString &onlyId = QString());
    void loadOne(PluginInfo &info);
    plugin::IShearPlugin *find(const QString &id) const;

    IHostBackend *m_backend;
    QVector<PluginInfo> m_infos;
    std::vector<std::unique_ptr<Loaded>> m_loaded;
};

} // namespace shear

#include "PluginManager.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPluginLoader>
#include <QSettings>
#include <QStandardPaths>

namespace shear {

namespace {

// O que o plugin enxerga: a API de plugin::IShearHost, repassada ao backend (a janela principal) com o id do plugin.
class HostProxy : public plugin::IShearHost {
public:
    HostProxy(QString id, IHostBackend *b) : m_id(std::move(id)), m_b(b) {}
    int apiMinor() const override { return plugin::kApiMinor; }
    double position() const override { return m_b->position(); }
    double duration() const override { return m_b->duration(); }
    QString dataDir() const override { return m_b->dataDir(); }
    void log(const QString &line) override { m_b->log(line); }
    void showStatus(const QString &text) override { m_b->showStatus(text); }
    void pushUndo() override { m_b->pushUndo(); }
    void setInfoChips(const QVector<plugin::InfoChip> &chips) override { m_b->setInfoChips(m_id, chips); }
    void setBackdrop(const QString &imagePath) override { m_b->setBackdrop(m_id, imagePath); }
    void setMarkers(const QVector<plugin::Marker> &markers) override { m_b->setMarkers(m_id, markers); }
    void suggestRange(double start, double end) override { m_b->suggestRange(m_id, start, end); }
    void refreshList() override { m_b->refreshList(); }

private:
    QString m_id;
    IHostBackend *m_b;
};

} // namespace

struct PluginManager::Loaded {
    QString id;
    std::unique_ptr<QPluginLoader> loader;
    std::unique_ptr<HostProxy> host;
    plugin::IShearPlugin *iface = nullptr;
};

PluginManager::PluginManager(IHostBackend *backend, QObject *parent) : QObject(parent), m_backend(backend) {}

PluginManager::~PluginManager()
{
    // não descarrega as DLLs: o processo está acabando e os plugins podem ter trabalho em andamento
    for (auto &l : m_loaded)
        if (l->loader)
            l->loader.release();
}

QString PluginManager::userPluginsDir()
{
    if (const QString env = qEnvironmentVariable("SHEAR_PLUGINS_DIR"); !env.isEmpty())
        return QDir::cleanPath(env);
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/../plugins"); // %LOCALAPPDATA%/Shear/plugins
}

QString PluginManager::checkCompatible(int apiMajor, int minHostApiMinor)
{
    if (apiMajor != plugin::kApiMajor)
        return QString("feito para outra geração dos plugins do shear (API %1; este shear usa a %2)")
            .arg(apiMajor).arg(plugin::kApiMajor);
    if (minHostApiMinor > plugin::kApiMinor)
        return QString("pede um shear mais novo (API %1.%2; este tem a %3.%4)")
            .arg(apiMajor).arg(minHostApiMinor).arg(plugin::kApiMajor).arg(plugin::kApiMinor);
    return {};
}

QString PluginManager::sha256File(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f))
        return {};
    return QString::fromLatin1(h.result().toHex());
}

// installed.json: {"files": {"valorant.dll": "<sha256>", "tls/qschannelbackend.dll": "<sha256>", ...}}
QString PluginManager::verifyInstalled(const QString &dir)
{
    QFile f(dir + "/installed.json");
    if (!f.open(QIODevice::ReadOnly))
        return "faltou o installed.json (reinstale o plugin)";
    const QJsonObject files = QJsonDocument::fromJson(f.readAll()).object().value("files").toObject();
    if (files.isEmpty())
        return "installed.json vazio (reinstale o plugin)";
    const QString root = QDir(dir).canonicalPath();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString path = QDir(dir).filePath(it.key());
        if (!QDir(root).absoluteFilePath(it.key()).startsWith(root) || it.key().contains(".."))
            return "caminho inválido no installed.json";
        const QString got = sha256File(path);
        if (got.isEmpty())
            return QString("arquivo ausente: %1").arg(it.key());
        if (got.compare(it.value().toString(), Qt::CaseInsensitive) != 0)
            return QString("arquivo alterado (SHA-256 diferente): %1").arg(it.key());
    }
    return {};
}

void PluginManager::scanDir(const QString &rootPath, bool user, const QString &onlyId)
{
    const QDir root(rootPath);
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!onlyId.isEmpty() && name != onlyId)
            continue;
        const QString dir = root.filePath(name);
        QFile f(dir + "/plugin.json");
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        PluginInfo info;
        info.id = o.value("id").toString();
        if (info.id.isEmpty() || info.id != name)
            continue; // o nome da pasta tem de ser o id
        info.name = o.value("name").toString(info.id);
        info.version = o.value("version").toString();
        info.description = o.value("description").toString();
        info.library = o.value("library").toString();
        info.apiMajor = o.value("apiMajor").toInt();
        info.apiMinor = o.value("apiMinor").toInt();
        info.minHostApiMinor = o.value("minHostApiMinor").toInt();
        for (const auto &v : o.value("appliesTo").toArray())
            info.appliesTo << v.toString();
        info.dir = dir;
        info.userInstalled = user;
        bool dup = false;
        for (const PluginInfo &p : std::as_const(m_infos))
            dup = dup || p.id == info.id;
        if (!dup)
            m_infos.push_back(info);
    }
}

const PluginInfo *PluginManager::info(const QString &id) const
{
    for (const PluginInfo &p : m_infos)
        if (p.id == id)
            return &p;
    return nullptr;
}

QVector<PluginInfo> PluginManager::builtinCatalog()
{
    PluginInfo v;
    v.id = "valorant";
    v.name = "Valorant";
    v.description = "Mostra mapa, agente, kills e mortes dos clipes do Valorant (pela API local do cliente), marca as kills na "
                    "timeline, ajusta I/O ao último round e dá nome ao clipe.";
    v.appliesTo = {"valorant"};
    return {v};
}

static void removeDir(const QString &path) { QDir(path).removeRecursively(); }

void PluginManager::applyPending()
{
    const QDir root(userPluginsDir());
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString dir = root.filePath(name);
        if (name.startsWith(".stage-")) { // preparo que ficou pela metade
            removeDir(dir);
        } else if (name.endsWith(".new")) { // atualização que esperava o plugin ser descarregado
            const QString id = name.chopped(4);
            removeDir(root.filePath(id));
            QDir().rename(dir, root.filePath(id));
        } else if (QFile::exists(dir + "/.remove")) { // remoção pedida na sessão anterior
            removeDir(dir);
        }
    }
}

PluginManager::Commit PluginManager::commitStaged(const QString &id, QString *error)
{
    const QString stage = userPluginsDir() + "/.stage-" + id, target = userPluginsDir() + "/" + id;
    auto fail = [&](const QString &why) {
        if (error) *error = why;
        removeDir(stage);
        return Commit::Failed;
    };
    if (!QFile::exists(stage + "/installed.json"))
        return fail("Preparo do plugin incompleto.");
    const PluginInfo *cur = info(id);
    if (cur && cur->loaded) { // a DLL em uso não pode ser trocada: a atualização entra na próxima abertura
        removeDir(target + ".new");
        return QDir().rename(stage, target + ".new") ? Commit::NeedsRestart : fail("Não consegui preparar a atualização.");
    }
    removeDir(target);
    if (!QDir().rename(stage, target))
        return fail("Não consegui instalar na pasta de plugins.");
    for (int i = 0; i < m_infos.size(); ++i) // tira o registro antigo (ex.: não carregou por erro)
        if (m_infos[i].id == id) { m_infos.remove(i); break; }
    scanDir(userPluginsDir(), true, id);
    PluginInfo *pi = nullptr;
    for (PluginInfo &p : m_infos)
        if (p.id == id) pi = &p;
    if (!pi) {
        if (error) *error = "O plugin instalado não foi reconhecido.";
        return Commit::Failed;
    }
    loadOne(*pi);
    emit pluginsChanged();
    if (!pi->loaded) {
        if (error) *error = pi->error;
        return Commit::Failed;
    }
    emit pluginLoaded(id);
    return Commit::Loaded;
}

bool PluginManager::requestRemoval(const QString &id)
{
    const PluginInfo *p = info(id);
    if (!p || !p->userInstalled)
        return false;
    const QString dir = p->dir;
    if (p->loaded) { // não dá para apagar uma DLL carregada: marca e apaga na próxima abertura
        QFile f(dir + "/.remove");
        if (f.open(QIODevice::WriteOnly)) f.close();
        return false;
    }
    removeDir(dir);
    for (int i = 0; i < m_infos.size(); ++i)
        if (m_infos[i].id == id) { m_infos.remove(i); break; }
    emit pluginsChanged();
    return true;
}

bool PluginManager::suggestionDismissed(const QString &id) { return QSettings().value("plugins/dismissed/" + id, false).toBool(); }

void PluginManager::setSuggestionDismissed(const QString &id, bool dismissed)
{
    QSettings().setValue("plugins/dismissed/" + id, dismissed);
}

QString PluginManager::suggestionFor(const QString &clipPath) const
{
    for (const PluginInfo &c : builtinCatalog()) {
        if (info(c.id) || suggestionDismissed(c.id)) // já instalado (mesmo que com erro) ou "não perguntar de novo"
            continue;
        for (const QString &pattern : c.appliesTo)
            if (clipPath.contains(pattern, Qt::CaseInsensitive))
                return c.id;
    }
    return {};
}

void PluginManager::loadAll()
{
    applyPending();
    m_infos.clear();
    m_loaded.clear();
    scanDir(userPluginsDir(), true);
    scanDir(QCoreApplication::applicationDirPath() + "/plugins", false);
    for (PluginInfo &info : m_infos)
        loadOne(info);
    emit pluginsChanged();
}

void PluginManager::loadOne(PluginInfo &info)
{
    if (info.library.isEmpty() || info.library.contains("..") || info.library.contains('/') || info.library.contains('\\')) {
        info.error = "plugin.json sem uma biblioteca válida";
        return;
    }
    if (const QString why = checkCompatible(info.apiMajor, info.minHostApiMinor); !why.isEmpty()) {
        info.error = why;
        return;
    }
    if (info.userInstalled) { // fingerprint: o que está na pasta tem de ser o que foi baixado e conferido na instalação
        if (const QString why = verifyInstalled(info.dir); !why.isEmpty()) {
            info.error = why;
            return;
        }
    }
    QCoreApplication::addLibraryPath(info.dir); // para o plugin trazer os próprios módulos do Qt (ex.: tls/)
    auto l = std::make_unique<Loaded>();
    l->id = info.id;
    l->loader = std::make_unique<QPluginLoader>(info.dir + "/" + info.library);
    QObject *obj = l->loader->instance();
    if (!obj) {
        info.error = l->loader->errorString();
        return;
    }
    l->iface = qobject_cast<plugin::IShearPlugin *>(obj);
    if (!l->iface) {
        info.error = "a biblioteca não é um plugin do shear";
        return;
    }
    l->host = std::make_unique<HostProxy>(info.id, m_backend);
    l->iface->init(l->host.get());
    info.loaded = true;
    m_loaded.push_back(std::move(l));
}

bool PluginManager::isLoaded(const QString &id) const { return find(id) != nullptr; }

plugin::IShearPlugin *PluginManager::find(const QString &id) const
{
    for (const auto &l : m_loaded)
        if (l->id == id)
            return l->iface;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------

void PluginManager::clipClosed()
{
    for (const auto &l : m_loaded) l->iface->clipClosed();
}

void PluginManager::clipOpened(const plugin::ClipContext &clip)
{
    for (const auto &l : m_loaded) l->iface->clipOpened(clip);
}

void PluginManager::clipOpenedFor(const QString &pluginId, const plugin::ClipContext &clip, double duration)
{
    if (auto *p = find(pluginId)) {
        p->clipOpened(clip);
        p->durationChanged(duration);
    }
}

void PluginManager::durationChanged(double seconds)
{
    for (const auto &l : m_loaded) l->iface->durationChanged(seconds);
}

QVector<PluginManager::PluginAction> PluginManager::actions() const
{
    QVector<PluginAction> out;
    for (const auto &l : m_loaded)
        for (const plugin::Action &a : l->iface->actions())
            out.push_back({l->id, a});
    return out;
}

void PluginManager::triggerAction(const QString &pluginId, const QString &actionId)
{
    if (auto *p = find(pluginId)) p->triggerAction(actionId);
}

plugin::ChoiceMenu PluginManager::chipMenu(const QString &pluginId, const QString &chipId)
{
    auto *p = find(pluginId);
    return p ? p->chipMenu(chipId) : plugin::ChoiceMenu();
}

void PluginManager::chipChosen(const QString &pluginId, const QString &chipId, const QString &value)
{
    if (auto *p = find(pluginId)) p->chipChosen(chipId, value);
}

QVector<plugin::RowInfo> PluginManager::rowInfos(const QVector<plugin::RowQuery> &rows) const
{
    QVector<plugin::RowInfo> out(rows.size());
    for (const auto &l : m_loaded) {
        const QVector<plugin::RowInfo> r = l->iface->rowInfos(rows);
        for (int i = 0; i < rows.size() && i < r.size(); ++i) { // o primeiro plugin que sabe de uma linha vale
            plugin::RowInfo &o = out[i];
            if (o.iconPath.isEmpty()) o.iconPath = r[i].iconPath;
            if (o.bannerPath.isEmpty()) o.bannerPath = r[i].bannerPath;
            if (o.bannerText.isEmpty()) o.bannerText = r[i].bannerText;
            if (o.tag.isEmpty()) { o.tag = r[i].tag; o.tagHot = r[i].tagHot; }
        }
    }
    return out;
}

QString PluginManager::exportStem(const plugin::ExportContext &ctx)
{
    for (const auto &l : m_loaded)
        if (const QString s = l->iface->exportStem(ctx); !s.isEmpty())
            return s;
    return {};
}

void PluginManager::exportKept(const QString &finalPath, qint64 size)
{
    for (const auto &l : m_loaded) l->iface->exportKept(finalPath, size);
}

QVariantMap PluginManager::saveState() const
{
    QVariantMap m;
    for (const auto &l : m_loaded)
        if (const QVariant v = l->iface->saveState(); v.isValid())
            m.insert(l->id, v);
    return m;
}

void PluginManager::restoreState(const QVariantMap &state)
{
    for (const auto &l : m_loaded)
        l->iface->restoreState(state.value(l->id)); // invalid = "nenhum estado"
}

} // namespace shear

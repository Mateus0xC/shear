#include "PluginInstaller.h"
#include "PluginManager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
#include <functional>

namespace shear {

// O catálogo mora no repositório (sempre acessível, independente das releases); os arquivos dos plugins ficam em releases.
static const char *kDefaultCatalogUrl = "https://raw.githubusercontent.com/Mateus0xC/shear/main/plugins/catalog.json";
static constexpr qint64 kMaxCatalogBytes = 1 << 20;      // 1 MB
static constexpr qint64 kMaxFileBytes = 64LL << 20;      // 64 MB por arquivo

PluginInstaller::PluginInstaller(QObject *parent) : QObject(parent) {}

QString PluginInstaller::catalogUrl()
{
    const QString env = qEnvironmentVariable("SHEAR_PLUGIN_CATALOG");
    return env.isEmpty() ? QString::fromLatin1(kDefaultCatalogUrl) : env;
}

int PluginInstaller::compareVersions(const QString &a, const QString &b)
{
    const QStringList pa = a.split('.'), pb = b.split('.');
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i].toInt() : 0, y = i < pb.size() ? pb[i].toInt() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

bool PluginInstaller::safeRelPath(const QString &path)
{
    if (path.isEmpty() || path.startsWith('/') || path.startsWith('\\') || path.contains(':') || path.contains(".."))
        return false;
    return true;
}

std::optional<PluginInstaller::Version> PluginInstaller::pickVersion(const QJsonObject &catalog, const QString &pluginId)
{
    std::optional<Version> best;
    for (const auto &pv : catalog.value("plugins").toArray()) {
        const QJsonObject p = pv.toObject();
        if (p.value("id").toString() != pluginId)
            continue;
        for (const auto &vv : p.value("versions").toArray()) {
            const QJsonObject o = vv.toObject();
            Version v;
            v.version = o.value("version").toString();
            v.apiMajor = o.value("apiMajor").toInt();
            v.apiMinor = o.value("apiMinor").toInt();
            v.minHostApiMinor = o.value("minHostApiMinor").toInt();
            if (!PluginManager::checkCompatible(v.apiMajor, v.minHostApiMinor).isEmpty())
                continue; // este shear não consegue usar essa versão
            bool ok = !v.version.isEmpty();
            for (const auto &fv : o.value("files").toArray()) {
                const QJsonObject f = fv.toObject();
                FileEntry e{f.value("path").toString(), f.value("url").toString(), f.value("sha256").toString().toLower()};
                ok = ok && safeRelPath(e.path) && !e.url.isEmpty() && e.sha256.size() == 64;
                v.files.push_back(e);
            }
            if (!ok || v.files.isEmpty())
                continue;
            if (!best || compareVersions(v.version, best->version) > 0)
                best = v;
        }
    }
    return best;
}

void PluginInstaller::fetch(const QUrl &url, qint64 maxBytes, std::function<void(const QByteArray &, const QString &)> done)
{
    QNetworkRequest rq(url);
    rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy); // releases do GitHub redirecionam
    rq.setTransferTimeout(30000);
    QNetworkReply *r = m_nam.get(rq);
    connect(r, &QNetworkReply::downloadProgress, r, [r, maxBytes](qint64 got, qint64) {
        if (got > maxBytes) r->abort(); // arquivo grande demais: não é um plugin
    });
    connect(r, &QNetworkReply::finished, this, [r, done = std::move(done), maxBytes] {
        const QByteArray body = r->readAll();
        const QString err = r->error() == QNetworkReply::NoError ? (body.size() > maxBytes ? "arquivo grande demais" : QString())
                                                                  : r->errorString();
        r->deleteLater();
        done(body, err);
    });
}

void PluginInstaller::fail(const QString &why)
{
    QDir(m_stage).removeRecursively();
    m_busy = false;
    emit finished(m_id, false, why);
}

void PluginInstaller::install(const QString &pluginId, const QString &installedVersion)
{
    if (m_busy)
        return;
    m_busy = true;
    m_upToDate = false;
    m_id = pluginId;
    m_version.clear();
    m_files.clear();
    m_stage = PluginManager::userPluginsDir() + "/.stage-" + pluginId;
    emit status("Consultando o catálogo de plugins…");
    fetch(QUrl(catalogUrl()), kMaxCatalogBytes, [this, installedVersion](const QByteArray &data, const QString &err) {
        if (!err.isEmpty())
            return fail("Não consegui ler o catálogo de plugins: " + err);
        const auto ver = pickVersion(QJsonDocument::fromJson(data).object(), m_id);
        if (!ver)
            return fail("O catálogo não tem uma versão deste plugin que funcione com este shear.");
        m_version = ver->version;
        if (!installedVersion.isEmpty() && compareVersions(ver->version, installedVersion) <= 0) {
            m_busy = false;
            m_upToDate = true;
            emit finished(m_id, true, QString());
            return;
        }
        m_files = ver->files;
        QDir(m_stage).removeRecursively();
        if (!QDir().mkpath(m_stage))
            return fail("Não consegui criar a pasta de preparo do plugin.");
        downloadNext(0);
    });
}

PluginInstaller::Meta PluginInstaller::catalogMeta(const QJsonObject &catalog, const QString &pluginId)
{
    for (const auto &pv : catalog.value("plugins").toArray()) {
        const QJsonObject p = pv.toObject();
        if (p.value("id").toString() == pluginId)
            return {p.value("name").toString(), p.value("description").toString(), p.value("details").toString()};
    }
    return {};
}

void PluginInstaller::fetchMeta(const QString &pluginId)
{
    fetch(QUrl(catalogUrl()), kMaxCatalogBytes, [this, pluginId](const QByteArray &data, const QString &err) {
        if (!err.isEmpty())
            return emit metaReady(pluginId, {}, {}, {}, err);
        const Meta m = catalogMeta(QJsonDocument::fromJson(data).object(), pluginId);
        emit metaReady(pluginId, m.name, m.description, m.details, QString());
    });
}

void PluginInstaller::checkOnly(const QString &pluginId, const QString &installedVersion)
{
    fetch(QUrl(catalogUrl()), kMaxCatalogBytes, [this, pluginId, installedVersion](const QByteArray &data, const QString &err) {
        if (!err.isEmpty())
            return emit checked(pluginId, QString(), err);
        const auto ver = pickVersion(QJsonDocument::fromJson(data).object(), pluginId);
        emit checked(pluginId, ver && compareVersions(ver->version, installedVersion) > 0 ? ver->version : QString(), QString());
    });
}

void PluginInstaller::downloadNext(int index)
{
    if (index >= m_files.size())
        return finishStaged();
    const FileEntry f = m_files[index];
    emit status(QString("Baixando %1 (%2 de %3)…").arg(f.path).arg(index + 1).arg(m_files.size()));
    fetch(QUrl(f.url), kMaxFileBytes, [this, index, f](const QByteArray &data, const QString &err) {
        if (!err.isEmpty())
            return fail(QString("Falha ao baixar %1: %2").arg(f.path, err));
        // fingerprint: o que chegou tem de ter exatamente o SHA-256 do catálogo
        const QString got = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        if (got != f.sha256)
            return fail(QString("O arquivo %1 não confere com o fingerprint (SHA-256) do catálogo. Instalação cancelada.").arg(f.path));
        const QString dest = m_stage + "/" + f.path;
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QSaveFile out(dest);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
            return fail("Não consegui gravar " + f.path);
        downloadNext(index + 1);
    });
}

// Tudo baixado e conferido: confere o plugin.json e escreve o installed.json (hashes que serão reconferidos a cada abertura).
void PluginInstaller::finishStaged()
{
    QFile meta(m_stage + "/plugin.json");
    if (!meta.open(QIODevice::ReadOnly))
        return fail("O pacote do plugin não tem plugin.json.");
    const QJsonObject m = QJsonDocument::fromJson(meta.readAll()).object();
    meta.close();
    if (m.value("id").toString() != m_id)
        return fail("O plugin.json do pacote é de outro plugin.");
    if (const QString why = PluginManager::checkCompatible(m.value("apiMajor").toInt(), m.value("minHostApiMinor").toInt());
        !why.isEmpty())
        return fail("Plugin incompatível: " + why);
    QJsonObject files;
    for (const FileEntry &f : std::as_const(m_files))
        files.insert(f.path, PluginManager::sha256File(m_stage + "/" + f.path));
    QSaveFile inst(m_stage + "/installed.json");
    if (!inst.open(QIODevice::WriteOnly) ||
        inst.write(QJsonDocument(QJsonObject{{"version", m_version}, {"files", files}}).toJson()) < 0 || !inst.commit())
        return fail("Não consegui gravar o installed.json.");
    m_busy = false;
    emit finished(m_id, true, QString());
}

} // namespace shear

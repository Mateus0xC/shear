#include "Updater.h"
#include "PluginInstaller.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QThread>
#include <QUrl>

#ifndef SHEAR_VERSION
#define SHEAR_VERSION "0.0.0"
#endif

namespace shear {

static const char *kDefaultManifestUrl = "https://raw.githubusercontent.com/Mateus0xC/shear/main/updates/manifest.json";
static constexpr qint64 kMaxManifestBytes = 2 << 20;      // 2 MB
static constexpr qint64 kMaxFileBytes = 300LL << 20;      // 300 MB por arquivo (libmpv tem ~100 MB)

Updater::Updater(QObject *parent) : QObject(parent), m_appDir(QCoreApplication::applicationDirPath()) {}

QString Updater::currentVersion() { return QString::fromLatin1(SHEAR_VERSION); }

QString Updater::manifestUrl()
{
    const QString env = qEnvironmentVariable("SHEAR_UPDATE_MANIFEST");
    return env.isEmpty() ? QString::fromLatin1(kDefaultManifestUrl) : env;
}

bool Updater::isProtectedPath(const QString &path)
{
    if (!PluginInstaller::safeRelPath(path))
        return true;
    const QString p = path.toLower();
    if (p.startsWith("plugins/") || p.startsWith(".") || p.contains("/.") || p.endsWith(".old"))
        return true;
    return false;
}

UpdateManifest Updater::parseManifest(const QByteArray &json)
{
    UpdateManifest m;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    m.version = o.value("version").toString();
    m.notes = o.value("notes").toString();
    for (const auto &v : o.value("files").toArray()) {
        const QJsonObject f = v.toObject();
        UpdateFile e{f.value("path").toString(), f.value("url").toString(), f.value("sha256").toString().toLower(),
                     qint64(f.value("size").toDouble())};
        if (isProtectedPath(e.path) || e.url.isEmpty() || e.sha256.size() != 64 || e.size < 0)
            return {}; // um arquivo suspeito invalida o manifesto inteiro
        m.files.push_back(e);
    }
    m.valid = !m.version.isEmpty() && !m.files.isEmpty();
    return m;
}

QVector<UpdateFile> Updater::changedFiles(const UpdateManifest &m, const QString &appDir,
                                          const std::function<QString(const QString &)> &sha256Of)
{
    QVector<UpdateFile> out;
    for (const UpdateFile &f : m.files) {
        const QString path = QDir(appDir).filePath(f.path);
        const QFileInfo fi(path);
        if (!fi.exists() || (f.size > 0 && fi.size() != f.size) || sha256Of(path).compare(f.sha256, Qt::CaseInsensitive) != 0)
            out.push_back(f);
    }
    return out;
}

QString Updater::applyStaged(const QString &stage, const QVector<UpdateFile> &files, const QString &appDir)
{
    struct Done {
        QString dest, old;
        bool hadOld;
    };
    QVector<Done> done;
    auto rollback = [&] {
        for (int i = done.size() - 1; i >= 0; --i) {
            QFile::remove(done[i].dest);
            if (done[i].hadOld)
                QFile::rename(done[i].old, done[i].dest);
        }
    };
    for (const UpdateFile &f : files) {
        if (isProtectedPath(f.path)) {
            rollback();
            return "caminho inválido: " + f.path;
        }
        const QString src = QDir(stage).filePath(f.path), dest = QDir(appDir).filePath(f.path), old = dest + ".old";
        if (!QFile::exists(src)) {
            rollback();
            return "arquivo preparado ausente: " + f.path;
        }
        QDir().mkpath(QFileInfo(dest).absolutePath());
        const bool hadOld = QFile::exists(dest);
        if (hadOld) {
            QFile::remove(old);
            if (!QFile::rename(dest, old)) { // renomear funciona mesmo com o arquivo em uso
                rollback();
                return "não consegui preparar a troca de " + f.path;
            }
        }
        if (!QFile::rename(src, dest)) {
            if (hadOld) QFile::rename(old, dest);
            rollback();
            return "não consegui colocar " + f.path;
        }
        done.push_back({dest, old, hadOld});
    }
    QDir(stage).removeRecursively();
    return {};
}

void Updater::cleanupOld(const QString &appDir)
{
    QDirIterator it(appDir, {"*.old"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (p.contains("/plugins/", Qt::CaseInsensitive)) continue;
        QFile::remove(p); // se ainda estiver em uso, fica para a próxima abertura
    }
    QDir(appDir + "/.update").removeRecursively();
}

bool Updater::dirWritable(const QString &dir)
{
    QTemporaryFile t(QDir(dir).filePath(".write-test-XXXXXX"));
    return t.open();
}

bool Updater::startNewInstance()
{
    // o novo processo espera este terminar (senão a instância única mandaria o pedido para cá e sairia)
    return QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                   {QString("--after-restart=%1").arg(QCoreApplication::applicationPid())});
}

// ---------------------------------------------------------------------------------------------------------------------

void Updater::fetch(const QUrl &url, qint64 maxBytes, std::function<void(const QByteArray &, const QString &)> done)
{
    QNetworkRequest rq(url);
    rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    rq.setTransferTimeout(30000);
    QNetworkReply *r = m_nam.get(rq);
    connect(r, &QNetworkReply::downloadProgress, r, [r, maxBytes](qint64 got, qint64) {
        if (got > maxBytes) r->abort();
    });
    connect(r, &QNetworkReply::finished, this, [r, done = std::move(done), maxBytes] {
        const QByteArray body = r->readAll();
        const QString err = r->error() == QNetworkReply::NoError ? (body.size() > maxBytes ? "arquivo grande demais" : QString())
                                                                  : r->errorString();
        r->deleteLater();
        done(body, err);
    });
}

void Updater::check()
{
    if (m_busy)
        return;
    m_busy = true;
    fetch(QUrl(manifestUrl()), kMaxManifestBytes, [this](const QByteArray &data, const QString &err) {
        m_busy = false;
        if (!err.isEmpty())
            return emit checked(false, {}, {}, err);
        m_manifest = parseManifest(data);
        if (!m_manifest.valid)
            return emit checked(false, {}, {}, "manifesto de atualização inválido");
        const bool newer = PluginInstaller::compareVersions(m_manifest.version, currentVersion()) > 0;
        emit checked(newer, m_manifest.version, m_manifest.notes, QString());
    });
}

void Updater::fail(const QString &why)
{
    QDir(m_stage).removeRecursively();
    m_busy = false;
    emit applied(false, why);
}

void Updater::downloadAndApply()
{
    if (m_busy || !m_manifest.valid)
        return;
    if (!dirWritable(m_appDir)) {
        emit applied(false, "A pasta do shear não permite gravar (instalado para todos os usuários?). Atualize pelo instalador.");
        return;
    }
    m_busy = true;
    m_stage = m_appDir + "/.update";
    QDir(m_stage).removeRecursively();
    emit status("Comparando os arquivos…");
    // conferir o SHA-256 de ~300 MB leva uns segundos: em thread de fundo
    auto *t = QThread::create([this, manifest = m_manifest, appDir = m_appDir] {
        const auto todo = changedFiles(manifest, appDir, [](const QString &p) {
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) return QString();
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            return QString::fromLatin1(h.result().toHex());
        });
        QMetaObject::invokeMethod(this, [this, todo] {
            m_todo = todo;
            if (m_todo.isEmpty()) { // nada a baixar (já igual): só falta a versão, que vem do próprio shear.exe
                m_busy = false;
                emit applied(true, QString());
                return;
            }
            QDir().mkpath(m_stage);
            downloadNext(0);
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Updater::downloadNext(int index)
{
    if (index >= m_todo.size()) {
        emit status("Instalando…");
        const QString err = applyStaged(m_stage, m_todo, m_appDir);
        m_busy = false;
        if (!err.isEmpty()) {
            QDir(m_stage).removeRecursively();
            return emit applied(false, err);
        }
        return emit applied(true, QString());
    }
    const UpdateFile f = m_todo[index];
    emit status(QString("Baixando %1 (%2 de %3)…").arg(QFileInfo(f.path).fileName()).arg(index + 1).arg(m_todo.size()));
    fetch(QUrl(f.url), std::min(kMaxFileBytes, f.size > 0 ? f.size + 1024 : kMaxFileBytes),
          [this, index, f](const QByteArray &data, const QString &err) {
              if (!err.isEmpty())
                  return fail(QString("Falha ao baixar %1: %2").arg(f.path, err));
              if (f.size > 0 && data.size() != f.size)
                  return fail(QString("%1 veio com tamanho diferente do esperado.").arg(f.path));
              // fingerprint: o que chegou tem de ter exatamente o SHA-256 do manifesto
              if (QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) != f.sha256)
                  return fail(QString("O arquivo %1 não confere com o fingerprint (SHA-256) do manifesto. Atualização cancelada.").arg(f.path));
              const QString dest = m_stage + "/" + f.path;
              QDir().mkpath(QFileInfo(dest).absolutePath());
              QSaveFile out(dest);
              if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
                  return fail("Não consegui gravar " + f.path);
              downloadNext(index + 1);
          });
}

} // namespace shear

#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVector>
#include <optional>

namespace shear {

// Baixa um plugin do catálogo, confere o SHA-256 de CADA arquivo e deixa tudo num diretório de preparo
// (%LOCALAPPDATA%/Shear/plugins/.stage-<id>). Só depois disso o PluginManager::commitStaged instala de verdade.
//
// Catálogo (JSON, hospedado no GitHub; o endereço é catalogUrl()):
//   {"plugins":[{"id":"valorant","versions":[{"version":"1.0.0","apiMajor":1,"apiMinor":0,"minHostApiMinor":0,
//      "files":[{"path":"valorant.dll","url":"https://...","sha256":"..."},{"path":"plugin.json","url":"...","sha256":"..."}]}]}]}
// O shear escolhe a versão MAIS NOVA que ele consegue usar (mesma API maior e API menor suficiente): não precisa ser a mesma
// versão do shear.
class PluginInstaller : public QObject {
    Q_OBJECT
public:
    struct FileEntry {
        QString path, url, sha256;
    };
    struct Version {
        QString version;
        int apiMajor = 0, apiMinor = 0, minHostApiMinor = 0;
        QVector<FileEntry> files;
    };

    explicit PluginInstaller(QObject *parent = nullptr);

    // Endereço do catálogo: variável SHEAR_PLUGIN_CATALOG (para testes, aceita file:///...) ou o oficial no GitHub.
    static QString catalogUrl();
    // A versão mais nova do plugin que este shear consegue usar (nullopt se não há nenhuma).
    static std::optional<Version> pickVersion(const QJsonObject &catalog, const QString &pluginId);
    static int compareVersions(const QString &a, const QString &b); // <0, 0, >0
    static bool safeRelPath(const QString &path);                   // sem "..", sem caminho absoluto

    // `installedVersion` ("" se não está instalado): se o catálogo não tem nada mais novo, termina com ok e `upToDate`.
    void install(const QString &pluginId, const QString &installedVersion = QString());
    // Só consulta o catálogo: há versão mais nova (compatível) do plugin instalado? -> checked(id, versão nova ou "", erro)
    void checkOnly(const QString &pluginId, const QString &installedVersion);
    bool busy() const { return m_busy; }
    bool upToDate() const { return m_upToDate; }
    QString newVersion() const { return m_version; }

signals:
    void status(const QString &text);
    void finished(const QString &pluginId, bool ok, const QString &error); // ok: preparado em stagedDir (ou já atualizado)
    void checked(const QString &pluginId, const QString &newVersion, const QString &error);

private:
    void fetch(const QUrl &url, qint64 maxBytes, std::function<void(const QByteArray &, const QString &)> done);
    void downloadNext(int index);
    void finishStaged();
    void fail(const QString &why);

    QNetworkAccessManager m_nam;
    bool m_busy = false, m_upToDate = false;
    QString m_id, m_version, m_stage;
    QVector<FileEntry> m_files;
};

} // namespace shear

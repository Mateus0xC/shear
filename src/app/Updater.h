#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

namespace shear {

struct UpdateFile {
    QString path;   // relativo à pasta do shear, com "/"
    QString url;
    QString sha256; // minúsculas, 64 caracteres
    qint64 size = 0;
};

struct UpdateManifest {
    QString version, notes;
    QVector<UpdateFile> files;
    bool valid = false;
};

// Atualização do shear SEM reinstalar: o manifesto lista todos os arquivos da versão com SHA-256; o shear baixa SÓ o que mudou
// (tipicamente o shear.exe, ~1 MB; as DLLs grandes do player quase nunca mudam), confere cada hash e troca os arquivos.
// O Windows não deixa sobrescrever um .exe/.dll em uso, mas deixa RENOMEAR: o arquivo atual vira "<nome>.old" e o novo entra no
// lugar; depois de reiniciar, os .old são apagados. Se algo falhar no meio, tudo volta ao que era.
// Nada é baixado sem você clicar em "Atualizar": a consulta ao manifesto (poucos KB) é só uma vez por dia.
class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject *parent = nullptr);

    static QString currentVersion();  // versão deste shear (CMake)
    static QString manifestUrl();     // SHEAR_UPDATE_MANIFEST (aceita file:///...) ou o oficial no GitHub
    static UpdateManifest parseManifest(const QByteArray &json);
    static bool isProtectedPath(const QString &path); // nunca mexe em plugins/, em arquivos ocultos nem em .old
    // Arquivos do manifesto que NÃO estão iguais na pasta (ausentes, de outro tamanho ou com outro SHA-256).
    static QVector<UpdateFile> changedFiles(const UpdateManifest &m, const QString &appDir,
                                            const std::function<QString(const QString &)> &sha256Of);
    // Troca os arquivos preparados em `stage` pelos da pasta (renomeando os atuais para .old). "" = ok; senão o erro (e já desfez).
    static QString applyStaged(const QString &stage, const QVector<UpdateFile> &files, const QString &appDir);
    static void cleanupOld(const QString &appDir); // apaga os .old e o preparo de uma atualização anterior
    static bool dirWritable(const QString &dir);
    static bool startNewInstance();                 // abre o shear de novo; o novo espera este fechar

    void setAppDir(const QString &dir) { m_appDir = dir; }
    QString appDir() const { return m_appDir; }
    bool busy() const { return m_busy; }
    const UpdateManifest &manifest() const { return m_manifest; }

    void check();             // consulta o manifesto -> checked(...)
    void downloadAndApply();  // baixa o que mudou, confere e troca -> applied(...)

signals:
    void checked(bool available, const QString &version, const QString &notes, const QString &error);
    void status(const QString &text);
    void applied(bool ok, const QString &error); // ok: arquivos trocados; falta reiniciar

private:
    void fetch(const QUrl &url, qint64 maxBytes, std::function<void(const QByteArray &, const QString &)> done);
    void downloadNext(int index);
    void fail(const QString &why);

    QNetworkAccessManager m_nam;
    QString m_appDir;
    UpdateManifest m_manifest;
    bool m_busy = false;
    QString m_stage;
    QVector<UpdateFile> m_todo;
};

} // namespace shear

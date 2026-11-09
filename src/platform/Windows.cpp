#include "Windows.h"
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QSettings>
#include <QUrl>
#include <windows.h>
#include <shlobj.h>

namespace shear {

void copyFileToClipboard(const QString &path)
{
    // O Qt converte text/uri-list de arquivos locais em CF_HDROP no Windows.
    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QGuiApplication::clipboard()->setMimeData(mime);
}

void revealInExplorer(const QString &path)
{
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
}

static const QStringList kVideoExts = {".mp4", ".mkv", ".mov", ".webm", ".avi"};

static QString extKey(const QString &ext)
{
    return R"(HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\)" + ext;
}

// Local usado pelas versões antigas (por tipo "video"): o Explorer não mostrava o item ali.
static const char *kLegacyKey = R"(HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\video)";

bool contextMenuInstalled()
{
    QSettings s(extKey(kVideoExts.first()), QSettings::NativeFormat);
    return s.contains("shell/Shear/command/Default");
}

bool installContextMenu(const QString &exePath)
{
    const QString exe = QDir::toNativeSeparators(exePath);
    bool ok = true;
    for (const QString &ext : kVideoExts) {
        QSettings s(extKey(ext), QSettings::NativeFormat);
        s.setValue("shell/Shear/Default", QStringLiteral("Cortar com Shear"));
        s.setValue("shell/Shear/Icon", QString("\"%1\",0").arg(exe));
        s.setValue("shell/Shear/command/Default", QString("\"%1\" \"%2\"").arg(exe, "%1"));
        s.sync();
        ok = ok && s.status() == QSettings::NoError;
    }
    QSettings legacy(kLegacyKey, QSettings::NativeFormat); // tira o registro antigo, que não funcionava
    legacy.remove("shell/Shear");
    legacy.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); // o Explorer recarrega ícones e menus
    return ok;
}

bool removeContextMenu()
{
    bool ok = true;
    for (const QString &ext : kVideoExts) {
        QSettings s(extKey(ext), QSettings::NativeFormat);
        s.remove("shell/Shear");
        s.sync();
        ok = ok && s.status() == QSettings::NoError;
    }
    QSettings legacy(kLegacyKey, QSettings::NativeFormat);
    legacy.remove("shell/Shear");
    legacy.sync();
    return ok;
}

void migrateLegacyContextMenu(const QString &exePath)
{
    QSettings legacy(kLegacyKey, QSettings::NativeFormat);
    if (legacy.contains("shell/Shear/command/Default")) {
        installContextMenu(exePath); // instala nos locais novos e remove o antigo
        return;
    }
    // Menu já instalado, mas apontando para outro exe ou sem o ícone atual (ex.: o exe mudou de pasta): atualiza.
    if (contextMenuInstalled()) {
        QSettings s(extKey(kVideoExts.first()), QSettings::NativeFormat);
        const QString exe = QDir::toNativeSeparators(exePath);
        if (s.value("shell/Shear/Icon").toString() != QString("\"%1\",0").arg(exe))
            installContextMenu(exePath);
    }
}

} // namespace shear

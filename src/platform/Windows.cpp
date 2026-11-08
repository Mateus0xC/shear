#include "Windows.h"
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QSettings>
#include <QUrl>

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

static const char *kBase = R"(HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\video)";

bool contextMenuInstalled()
{
    QSettings s(kBase, QSettings::NativeFormat);
    return s.contains("shell/Shear/command/Default");
}

bool installContextMenu(const QString &exePath)
{
    const QString exe = QDir::toNativeSeparators(exePath);
    QSettings s(kBase, QSettings::NativeFormat);
    s.setValue("shell/Shear/Default", QStringLiteral("Cortar com Shear"));
    s.setValue("shell/Shear/Icon", QString("\"%1\",0").arg(exe));
    s.setValue("shell/Shear/command/Default", QString("\"%1\" \"%2\"").arg(exe, "%1"));
    s.sync();
    return s.status() == QSettings::NoError;
}

bool removeContextMenu()
{
    QSettings s(kBase, QSettings::NativeFormat);
    s.remove("shell/Shear");
    s.sync();
    return s.status() == QSettings::NoError;
}

} // namespace shear

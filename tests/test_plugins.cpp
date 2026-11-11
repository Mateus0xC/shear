#include <QtTest>
#include "app/PluginManager.h"

using namespace shear;

class PluginTest : public QObject {
    Q_OBJECT
private slots:
    void compatibility_needs_same_major_and_enough_minor()
    {
        QVERIFY(PluginManager::checkCompatible(plugin::kApiMajor, 0).isEmpty());
        QVERIFY(PluginManager::checkCompatible(plugin::kApiMajor, plugin::kApiMinor).isEmpty());
        QVERIFY(!PluginManager::checkCompatible(plugin::kApiMajor + 1, 0).isEmpty());          // outra geração
        QVERIFY(!PluginManager::checkCompatible(plugin::kApiMajor, plugin::kApiMinor + 1).isEmpty()); // pede shear mais novo
    }
    void fingerprint_detects_changed_and_missing_files()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&](const QString &name, const QByteArray &data) {
            QDir().mkpath(QFileInfo(dir.filePath(name)).absolutePath());
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write("a.dll", "conteudo");
        write("tls/b.dll", "outro");
        const QString ha = PluginManager::sha256File(dir.filePath("a.dll"));
        const QString hb = PluginManager::sha256File(dir.filePath("tls/b.dll"));
        QCOMPARE(ha.size(), 64);
        write("installed.json", QString(R"({"files":{"a.dll":"%1","tls/b.dll":"%2"}})").arg(ha, hb).toUtf8());
        QVERIFY2(PluginManager::verifyInstalled(dir.path()).isEmpty(), qPrintable(PluginManager::verifyInstalled(dir.path())));
        write("a.dll", "adulterado"); // alguém trocou a DLL
        QVERIFY(PluginManager::verifyInstalled(dir.path()).contains("a.dll"));
        write("a.dll", "conteudo");
        QFile::remove(dir.filePath("tls/b.dll"));
        QVERIFY(PluginManager::verifyInstalled(dir.path()).contains("ausente"));
        QFile::remove(dir.filePath("installed.json"));
        QVERIFY(!PluginManager::verifyInstalled(dir.path()).isEmpty()); // sem installed.json não vale
    }
    void path_traversal_in_installed_json_is_refused()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath("installed.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"files":{"../fora.dll":"00"}})");
        f.close();
        QVERIFY(!PluginManager::verifyInstalled(dir.path()).isEmpty());
    }
};

QTEST_APPLESS_MAIN(PluginTest)
#include "test_plugins.moc"

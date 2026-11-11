#include <QtTest>
#include <QCryptographicHash>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include "app/PluginInstaller.h"
#include "app/PluginManager.h"

using namespace shear;

// Backend de mentira: só guarda o que os plugins mandam.
class FakeBackend : public IHostBackend {
public:
    double position() const override { return 0; }
    double duration() const override { return 0; }
    QString dataDir() const override { return QDir::tempPath(); }
    void log(const QString &) override {}
    void showStatus(const QString &) override {}
    void pushUndo() override {}
    void setInfoChips(const QString &, const QVector<plugin::InfoChip> &) override {}
    void setBackdrop(const QString &, const QString &) override {}
    void setMarkers(const QString &, const QVector<plugin::Marker> &) override {}
    void suggestRange(const QString &, double, double) override {}
    void refreshList() override {}
};

static QString sha(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
}

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
    void catalog_picks_newest_compatible_version()
    {
        const QByteArray json = R"({"plugins":[{"id":"x","versions":[
          {"version":"1.0.0","apiMajor":1,"apiMinor":0,"minHostApiMinor":0,"files":[{"path":"x.dll","url":"u","sha256":"%1"}]},
          {"version":"1.2.0","apiMajor":1,"apiMinor":0,"minHostApiMinor":0,"files":[{"path":"x.dll","url":"u","sha256":"%1"}]},
          {"version":"1.10.0","apiMajor":1,"apiMinor":0,"minHostApiMinor":0,"files":[{"path":"x.dll","url":"u","sha256":"%1"}]},
          {"version":"2.0.0","apiMajor":2,"apiMinor":0,"minHostApiMinor":0,"files":[{"path":"x.dll","url":"u","sha256":"%1"}]},
          {"version":"1.9.0","apiMajor":1,"apiMinor":5,"minHostApiMinor":9,"files":[{"path":"x.dll","url":"u","sha256":"%1"}]}]}]})";
        const QString h(64, 'a');
        const auto doc = QJsonDocument::fromJson(QString(json).arg(h).toUtf8()).object();
        const auto v = PluginInstaller::pickVersion(doc, "x");
        QVERIFY(v.has_value());
        QCOMPARE(v->version, QString("1.10.0")); // 2.0.0 é de outra geração; 1.9.0 exige um shear mais novo
        QVERIFY(!PluginInstaller::pickVersion(doc, "outro").has_value());
        // hash que não tem 64 caracteres ou caminho perigoso: a versão é ignorada
        const QByteArray bad = R"({"plugins":[{"id":"x","versions":[{"version":"1.0.0","apiMajor":1,"apiMinor":0,"minHostApiMinor":0,"files":[{"path":"../x.dll","url":"u","sha256":"abc"}]}]}]})";
        QVERIFY(!PluginInstaller::pickVersion(QJsonDocument::fromJson(bad).object(), "x").has_value());
    }
    void catalog_meta_comes_from_the_plugin()
    {
        const QByteArray json = R"({"plugins":[{"id":"x","name":"Nome","description":"curta","details":"longa\nem linhas","versions":[]}]})";
        const auto m = PluginInstaller::catalogMeta(QJsonDocument::fromJson(json).object(), "x");
        QCOMPARE(m.name, QString("Nome"));
        QCOMPARE(m.description, QString("curta"));
        QCOMPARE(m.details, QString("longa\nem linhas"));
        QVERIFY(PluginInstaller::catalogMeta(QJsonDocument::fromJson(json).object(), "y").details.isEmpty());
    }
    void versions_compare_numerically()
    {
        QVERIFY(PluginInstaller::compareVersions("1.10.0", "1.9.0") > 0);
        QVERIFY(PluginInstaller::compareVersions("1.0", "1.0.0") == 0);
        QVERIFY(PluginInstaller::compareVersions("0.9.9", "1.0.0") < 0);
        QVERIFY(PluginInstaller::safeRelPath("tls/a.dll"));
        QVERIFY(!PluginInstaller::safeRelPath("../a.dll"));
        QVERIFY(!PluginInstaller::safeRelPath("C:/a.dll"));
        QVERIFY(!PluginInstaller::safeRelPath("/a.dll"));
    }
    // Instala de verdade a valorant.dll compilada: catálogo local (file://), download, SHA-256, carga a quente e remoção.
    void install_flow_end_to_end()
    {
        const QString built = QStringLiteral(VALORANT_PLUGIN_DIR); // pasta com valorant.dll + plugin.json recém-compilados
        QVERIFY2(QFile::exists(built + "/valorant.dll"), "compile o alvo 'valorant' antes");
        QTemporaryDir work;
        QVERIFY(work.isValid());
        qputenv("SHEAR_PLUGINS_DIR", (work.path() + "/plugins").toUtf8());
        // "release" local: os mesmos arquivos que iriam para o GitHub
        QDir().mkpath(work.path() + "/release");
        QFile::copy(built + "/valorant.dll", work.path() + "/release/valorant.dll");
        QFile::copy(built + "/plugin.json", work.path() + "/release/plugin.json");
        auto writeCatalog = [&](const QString &dllHash) {
            const QString base = QUrl::fromLocalFile(work.path() + "/release").toString();
            const QByteArray json = QString(R"({"plugins":[{"id":"valorant","versions":[{"version":"1.0.0","apiMajor":%1,"apiMinor":0,
              "minHostApiMinor":0,"files":[{"path":"valorant.dll","url":"%2/valorant.dll","sha256":"%3"},
              {"path":"plugin.json","url":"%2/plugin.json","sha256":"%4"}]}]}]})")
                .arg(plugin::kApiMajor).arg(base, dllHash, sha(work.path() + "/release/plugin.json")).toUtf8();
            QFile f(work.path() + "/catalog.json");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(json);
            qputenv("SHEAR_PLUGIN_CATALOG", QUrl::fromLocalFile(work.path() + "/catalog.json").toString().toUtf8());
        };

        // 1) fingerprint errado no catálogo: a instalação TEM de ser recusada e não deixar nada para trás
        writeCatalog(QString(64, '0'));
        {
            PluginInstaller bad;
            QSignalSpy spy(&bad, &PluginInstaller::finished);
            bad.install("valorant");
            QVERIFY(spy.wait(20000));
            QVERIFY(!spy.first().at(1).toBool());
            QVERIFY2(spy.first().at(2).toString().contains("fingerprint"), qPrintable(spy.first().at(2).toString()));
            QVERIFY(!QFile::exists(PluginManager::userPluginsDir() + "/.stage-valorant/valorant.dll"));
            QVERIFY(!QFile::exists(PluginManager::userPluginsDir() + "/valorant"));
        }

        // 2) catálogo certo: baixa, confere, instala e carrega sem reiniciar
        writeCatalog(sha(work.path() + "/release/valorant.dll"));
        FakeBackend backend;
        PluginManager mgr(&backend);
        mgr.loadAll();
        QVERIFY(!mgr.isLoaded("valorant"));
        QCOMPARE(mgr.suggestionFor("D:/Videos/Valorant 2026.11.08.mp4"), QString("valorant")); // sugere
        QVERIFY(mgr.suggestionFor("D:/Videos/Outro jogo.mp4").isEmpty());
        PluginInstaller good;
        QSignalSpy spy(&good, &PluginInstaller::finished);
        good.install("valorant");
        QVERIFY(spy.wait(20000));
        QVERIFY2(spy.first().at(1).toBool(), qPrintable(spy.first().at(2).toString()));
        QString err;
        QCOMPARE(int(mgr.commitStaged("valorant", &err)), int(PluginManager::Commit::Loaded));
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(mgr.isLoaded("valorant"));
        QVERIFY(mgr.suggestionFor("D:/Videos/Valorant 2026.11.08.mp4").isEmpty()); // instalado: não sugere mais
        QVERIFY(!mgr.actions().isEmpty()); // o plugin registrou K e Shift+K
        // o que ficou na pasta bate com o installed.json (fingerprint reconferido a cada abertura)
        QVERIFY(PluginManager::verifyInstalled(PluginManager::userPluginsDir() + "/valorant").isEmpty());

        // 3) já está na versão mais nova: não baixa de novo
        PluginInstaller again;
        QSignalSpy spy2(&again, &PluginInstaller::finished);
        again.install("valorant", "1.0.0");
        QVERIFY(spy2.wait(20000));
        QVERIFY(spy2.first().at(1).toBool());
        QVERIFY(again.upToDate());

        // 4) DLL adulterada depois de instalada: o fingerprint reconferido na abertura recusa carregar
        //    (trabalha numa cópia, porque a DLL instalada está carregada neste processo)
        const QString inst = PluginManager::userPluginsDir() + "/valorant", copy = work.path() + "/copia";
        QDir().mkpath(copy);
        for (const char *name : {"valorant.dll", "plugin.json", "installed.json"})
            QVERIFY(QFile::copy(inst + "/" + name, copy + "/" + name));
        QVERIFY(PluginManager::verifyInstalled(copy).isEmpty());
        QFile tampered(copy + "/valorant.dll");
        QVERIFY(tampered.open(QIODevice::ReadWrite | QIODevice::Append));
        tampered.write("lixo");
        tampered.close();
        QVERIFY(PluginManager::verifyInstalled(copy).contains("alterado"));
        // 5) remoção: DLL carregada não se apaga na hora (o Windows a trava): fica marcada e some na próxima abertura
        QVERIFY(!mgr.requestRemoval("valorant"));
        QVERIFY(QFile::exists(PluginManager::userPluginsDir() + "/valorant/.remove"));
        // (na próxima abertura, applyPending roda ANTES de carregar; aqui simulamos com uma pasta que não está carregada)
        QDir().mkpath(PluginManager::userPluginsDir() + "/antigo");
        QFile(PluginManager::userPluginsDir() + "/antigo/.remove").open(QIODevice::WriteOnly);
        QDir().mkpath(PluginManager::userPluginsDir() + "/.stage-sobra");
        PluginManager::applyPending();
        QVERIFY(!QFile::exists(PluginManager::userPluginsDir() + "/antigo"));        // remoção pendente aplicada
        QVERIFY(!QFile::exists(PluginManager::userPluginsDir() + "/.stage-sobra"));  // preparo pela metade limpo

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

QTEST_GUILESS_MAIN(PluginTest)
#include "test_plugins.moc"

#include <QtTest>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include "app/Updater.h"

using namespace shear;

static QString sha(const QByteArray &d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }
static void put(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
static QByteArray get(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
static QString fileSha(const QString &p) { return sha(get(p)); }

class UpdaterTest : public QObject {
    Q_OBJECT
private slots:
    void manifest_parsing_and_protected_paths()
    {
        const QString h(64, 'a');
        auto mk = [&](const QString &path) {
            return QString(R"({"version":"0.2.0","notes":"n","files":[{"path":"%1","url":"u","sha256":"%2","size":5}]})").arg(path, h).toUtf8();
        };
        QVERIFY(Updater::parseManifest(mk("shear.exe")).valid);
        QVERIFY(Updater::parseManifest(mk("platforms/qwindows.dll")).valid);
        QVERIFY(!Updater::parseManifest(mk("../fora.dll")).valid);       // fora da pasta
        QVERIFY(!Updater::parseManifest(mk("C:/x.dll")).valid);
        QVERIFY(!Updater::parseManifest(mk("plugins/valorant/a.dll")).valid); // plugins têm o próprio fluxo
        QVERIFY(!Updater::parseManifest(mk("a.dll.old")).valid);
        QVERIFY(!Updater::parseManifest(QByteArray(R"({"version":"1","files":[{"path":"a","url":"u","sha256":"curto","size":1}]})")).valid);
        QVERIFY(!Updater::parseManifest("lixo").valid);
    }
    void only_changed_files_are_listed()
    {
        QTemporaryDir app;
        put(app.filePath("igual.dll"), "AAAA");
        put(app.filePath("mesmo-tamanho.dll"), "BBBB");   // mesmo tamanho, conteúdo diferente
        put(app.filePath("tamanho.dll"), "CC");           // tamanho diferente
        UpdateManifest m;
        m.valid = true;
        m.version = "0.2.0";
        m.files = {{"igual.dll", "u", sha("AAAA"), 4},
                   {"mesmo-tamanho.dll", "u", sha("XXXX"), 4},
                   {"tamanho.dll", "u", sha("CCCC"), 4},
                   {"novo.dll", "u", sha("NNNN"), 4}};
        int hashed = 0;
        const auto todo = Updater::changedFiles(m, app.path(), [&](const QString &p) { ++hashed; return fileSha(p); });
        QStringList names;
        for (const auto &f : todo) names << f.path;
        QCOMPARE(names, QStringList({"mesmo-tamanho.dll", "tamanho.dll", "novo.dll"}));
        QCOMPARE(hashed, 2); // só confere o hash de quem tem o mesmo tamanho (igual e mesmo-tamanho)
    }
    void apply_swaps_with_old_copies_and_cleanup()
    {
        QTemporaryDir app, stage;
        put(app.filePath("shear.exe"), "v1");
        put(app.filePath("lib/a.dll"), "a1");
        put(stage.filePath("shear.exe"), "v2");
        put(stage.filePath("lib/a.dll"), "a2");
        put(stage.filePath("novo.dll"), "n");
        const QVector<UpdateFile> files = {{"shear.exe", "", "", 2}, {"lib/a.dll", "", "", 2}, {"novo.dll", "", "", 1}};
        QVERIFY(Updater::applyStaged(stage.path(), files, app.path()).isEmpty());
        QCOMPARE(get(app.filePath("shear.exe")), QByteArray("v2"));
        QCOMPARE(get(app.filePath("lib/a.dll")), QByteArray("a2"));
        QCOMPARE(get(app.filePath("novo.dll")), QByteArray("n"));
        QCOMPARE(get(app.filePath("shear.exe.old")), QByteArray("v1")); // o antigo fica para trás (o exe em uso foi renomeado)
        Updater::cleanupOld(app.path());
        QVERIFY(!QFile::exists(app.filePath("shear.exe.old")));
        QVERIFY(!QFile::exists(app.filePath("lib/a.dll.old")));
    }
    void failure_in_the_middle_rolls_everything_back()
    {
        QTemporaryDir app, stage;
        put(app.filePath("a.dll"), "a1");
        put(app.filePath("b.dll"), "b1");
        put(stage.filePath("a.dll"), "a2");
        // b.dll não foi preparado: a segunda troca falha
        const QVector<UpdateFile> files = {{"a.dll", "", "", 2}, {"b.dll", "", "", 2}};
        QVERIFY(!Updater::applyStaged(stage.path(), files, app.path()).isEmpty());
        QCOMPARE(get(app.filePath("a.dll")), QByteArray("a1")); // voltou ao que era
        QCOMPARE(get(app.filePath("b.dll")), QByteArray("b1"));
        QVERIFY(!QFile::exists(app.filePath("a.dll.old")));
    }
    void dir_writable_check()
    {
        QTemporaryDir d;
        QVERIFY(Updater::dirWritable(d.path()));
        QVERIFY(!Updater::dirWritable(d.filePath("nao-existe")));
    }
    // Atualiza de verdade, via file://: baixa só o que mudou, confere o SHA-256 e troca; hash errado é recusado.
    void download_flow_end_to_end()
    {
        QTemporaryDir work;
        const QString rel = work.filePath("release"), app = work.filePath("app");
        put(app + "/shear.exe", "exe-v1");
        put(app + "/libgrande.dll", "grande-igual");     // não muda: não pode ser baixada
        put(rel + "/shear.exe", "exe-v2");
        put(rel + "/platforms-q.dll", "plat");
        auto writeManifest = [&](const QString &exeHash) {
            const QString base = QUrl::fromLocalFile(rel).toString();
            const QByteArray json = QString(R"({"version":"0.2.0","notes":"melhorias","files":[
              {"path":"shear.exe","url":"%1/shear.exe","sha256":"%2","size":6},
              {"path":"libgrande.dll","url":"%1/NAO-EXISTE.dll","sha256":"%3","size":12},
              {"path":"platforms/q.dll","url":"%1/platforms-q.dll","sha256":"%4","size":4}]})")
                .arg(base, exeHash, sha("grande-igual"), sha("plat")).toUtf8();
            put(work.filePath("manifest.json"), json);
            qputenv("SHEAR_UPDATE_MANIFEST", QUrl::fromLocalFile(work.filePath("manifest.json")).toString().toUtf8());
        };
        // 1) fingerprint errado: recusa e não mexe em nada
        writeManifest(QString(64, '0'));
        {
            Updater u;
            u.setAppDir(app);
            QSignalSpy c(&u, &Updater::checked);
            u.check();
            QVERIFY(c.wait(20000));
            QVERIFY(c.first().at(0).toBool()); // 0.2.0 > 0.1.0
            QSignalSpy a(&u, &Updater::applied);
            u.downloadAndApply();
            QVERIFY(a.wait(30000));
            QVERIFY(!a.first().at(0).toBool());
            QVERIFY2(a.first().at(1).toString().contains("fingerprint"), qPrintable(a.first().at(1).toString()));
            QCOMPARE(get(app + "/shear.exe"), QByteArray("exe-v1"));
            QVERIFY(!QFile::exists(app + "/.update"));
        }
        // 2) certo: só shear.exe e platforms/q.dll são baixados (libgrande.dll já está igual: a URL dela nem existe)
        writeManifest(sha("exe-v2"));
        {
            Updater u;
            u.setAppDir(app);
            QSignalSpy c(&u, &Updater::checked);
            u.check();
            QVERIFY(c.wait(20000));
            QSignalSpy a(&u, &Updater::applied);
            u.downloadAndApply();
            QVERIFY(a.wait(30000));
            QVERIFY2(a.first().at(0).toBool(), qPrintable(a.first().at(1).toString()));
            QCOMPARE(get(app + "/shear.exe"), QByteArray("exe-v2"));
            QCOMPARE(get(app + "/platforms/q.dll"), QByteArray("plat"));
            QCOMPARE(get(app + "/libgrande.dll"), QByteArray("grande-igual"));
            QCOMPARE(get(app + "/shear.exe.old"), QByteArray("exe-v1"));
        }
    }
    void same_or_older_version_is_not_an_update()
    {
        QTemporaryDir work;
        put(work.filePath("m.json"), R"({"version":"0.1.0","files":[{"path":"a","url":"u","sha256":")" + QByteArray(64, 'a') + R"(","size":1}]})");
        qputenv("SHEAR_UPDATE_MANIFEST", QUrl::fromLocalFile(work.filePath("m.json")).toString().toUtf8());
        Updater u;
        QSignalSpy c(&u, &Updater::checked);
        u.check();
        QVERIFY(c.wait(20000));
        QVERIFY(!c.first().at(0).toBool());
        QVERIFY(c.first().at(3).toString().isEmpty());
    }
};

QTEST_GUILESS_MAIN(UpdaterTest)
#include "test_updater.moc"

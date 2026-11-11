#include "ClipMeta.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace shear::clipmeta {

static QString baseDir() { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation); }
static QString filePath() { return baseDir() + "/clipmeta.json"; }

static bool writeAll(const QJsonObject &all)
{
    QDir().mkpath(baseDir());
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(all).toJson(QJsonDocument::Indented));
    return f.commit();
}

QString key(const QString &fileName, qint64 size) { return fileName + "|" + QString::number(size); }

QJsonObject loadAll()
{
    QFile f(filePath());
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

QString comment(const QString &key) { return loadAll().value(key).toObject().value("comment").toString(); }

bool setComment(const QString &key, const QString &comment)
{
    QJsonObject all = loadAll();
    QJsonObject e = all.value(key).toObject();
    if (comment.isEmpty())
        e.remove("comment");
    else
        e.insert("comment", comment);
    if (e.isEmpty())
        all.remove(key);
    else
        all.insert(key, e);
    return writeAll(all);
}

void copyComment(const QString &fromKey, const QString &toKey)
{
    if (const QString c = comment(fromKey); !c.isEmpty())
        setComment(toKey, c);
}

void migrateFromValorant()
{
    if (QFile::exists(filePath()))
        return; // já feito
    QJsonObject out;
    QFile f(baseDir() + "/valorant/clips.json");
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject clips = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = clips.begin(); it != clips.end(); ++it)
            if (const QString c = it.value().toObject().value("manual").toObject().value("comment").toString(); !c.isEmpty())
                out.insert(it.key(), QJsonObject{{"comment", c}});
    }
    writeAll(out); // mesmo vazio: marca a migração como feita
}

} // namespace shear::clipmeta

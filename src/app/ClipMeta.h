#pragma once
#include <QJsonObject>
#include <QString>

namespace shear {

// Dados de clipe que são do shear base (não de plugin): hoje, o comentário. Fica em %LOCALAPPDATA%/Shear/Shear/clipmeta.json,
// chave "nome|tamanho" (sobrevive a mover o arquivo de pasta).
namespace clipmeta {

QString key(const QString &fileName, qint64 size);
QJsonObject loadAll();                                       // thread-safe (só lê)
QString comment(const QString &key);
bool setComment(const QString &key, const QString &comment); // vazio apaga
void copyComment(const QString &fromKey, const QString &toKey);
// Uma vez: traz os comentários que a versão anterior guardava no clips.json do Valorant ("manual.comment").
void migrateFromValorant();

} // namespace clipmeta
} // namespace shear

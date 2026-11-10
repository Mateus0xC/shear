#pragma once
#include <QString>
#include <QDateTime>
#include <functional>

namespace shear {

// Base do nome do clipe: "Mapa_Agente_Nk" quando há dados do Valorant (partes vazias somem, caracteres proibidos em nome de
// arquivo saem); sem nenhum dado, o nome do vídeo original. Ex.: "Ascent_Jett_3k".
QString clipBaseName(const QString &original, const QString &map, const QString &agent, const QString &tag);

// <nome>_<aaaa-mm-dd>_<hh-mm>.mp4 ; se já existir um com esse nome, acrescenta _2, _3...
// `exists` é injetável para testes.
QString uniqueOutputPath(const QString &dir, const QString &baseName, const QDateTime &now,
                         const std::function<bool(const QString &)> &exists);

} // namespace shear

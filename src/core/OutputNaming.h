#pragma once
#include <QDate>
#include <QDateTime>
#include <QString>
#include <functional>

namespace shear {

// Nome (sem extensão) de um clipe do Valorant: "Valorant aaaa.mm.dd - Mapa_Agente". Partes vazias somem e caracteres proibidos
// em nome de arquivo saem. Sem mapa e sem agente devolve "" (aí vale `fallbackStem`).
QString valorantStem(const QDate &date, const QString &map, const QString &agent);

// Nome (sem extensão) quando não há dados do Valorant (outro jogo): "<original>_shear".
QString fallbackStem(const QString &original);

// <dir>/<stem>.mp4; se já existir um com esse nome, acrescenta _2, _3, _4...
// `exists` é injetável para testes.
QString uniquePath(const QString &dir, const QString &stem, const std::function<bool(const QString &)> &exists);

} // namespace shear

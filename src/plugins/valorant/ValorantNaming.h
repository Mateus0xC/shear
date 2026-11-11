#pragma once
#include <QDate>
#include <QString>

namespace shear::valorant {

// Nome (sem extensão) de um clipe do Valorant: "Valorant aaaa.mm.dd - Mapa_Agente_Nk" (o Nk, logo depois do agente, só quando
// há tag de kills). Partes vazias somem e caracteres proibidos em nome de arquivo saem. Sem mapa e sem agente devolve "".
QString valorantStem(const QDate &date, const QString &map, const QString &agent, const QString &tag = QString());

} // namespace shear::valorant

#pragma once
#include <QString>
#include <functional>

namespace shear {

// Nome (sem extensão) de um clipe exportado quando nenhum plugin dá outro: "<original>_shear".
QString fallbackStem(const QString &original);

// <dir>/<stem>.mp4; se já existir um com esse nome, acrescenta _2, _3, _4...
// `exists` é injetável para testes.
QString uniquePath(const QString &dir, const QString &stem, const std::function<bool(const QString &)> &exists);

} // namespace shear

#pragma once
#include <QString>
#include <QTime>
#include <functional>

namespace shear {

// <nome>_shear_<hh-mm-ss>.mp4 ; se existir, acrescenta _2, _3...
// `exists` é injetável para testes.
QString uniqueOutputPath(const QString &dir, const QString &baseName, const QTime &now,
                         const std::function<bool(const QString &)> &exists);

} // namespace shear

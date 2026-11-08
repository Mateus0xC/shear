#include "Metrics.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <cstdio>
#include <windows.h>

namespace shear::metrics {

double processAgeMs()
{
    FILETIME c, e, k, u, now;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
        return -1;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a, b;
    a.LowPart = c.dwLowDateTime;   a.HighPart = c.dwHighDateTime;
    b.LowPart = now.dwLowDateTime; b.HighPart = now.dwHighDateTime;
    return double(b.QuadPart - a.QuadPart) / 10000.0; // 100 ns -> ms
}

void log(const QString &line)
{
    fprintf(stderr, "[shear] %s\n", qPrintable(line));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    QFile f(dir + "/metrics.log");
    if (f.open(QIODevice::Append | QIODevice::Text))
        QTextStream(&f) << QDateTime::currentDateTime().toString(Qt::ISODate) << "  " << line << "\n";
}

} // namespace shear::metrics

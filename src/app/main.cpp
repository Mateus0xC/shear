#include "MainWindow.h"
#include "SingleInstance.h"
#include "util/Metrics.h"
#include <QApplication>
#include <QFileInfo>
#include <QTimer>

int main(int argc, char **argv)
{
    using shear::metrics::processAgeMs;
    const double tMain = processAgeMs();
    QApplication app(argc, argv);
    const double tApp = processAgeMs();
    QApplication::setOrganizationName("Shear");
    QApplication::setApplicationName("Shear");

    QString path;
    for (const QString &a : app.arguments().mid(1))
        if (!a.startsWith('-')) { path = QFileInfo(a).absoluteFilePath(); break; }

    if (shear::SingleInstance::sendToRunning(path))
        return 0;

    shear::SingleInstance server;
    server.listen();

    const double tSrv = processAgeMs();
    shear::MainWindow w;
    const double tWin = processAgeMs();
    shear::metrics::log(QString("etapas (ms): main=%1 QApplication=%2 instância-única=%3 MainWindow=%4")
                            .arg(tMain, 0, 'f', 0).arg(tApp, 0, 'f', 0).arg(tSrv, 0, 'f', 0).arg(tWin, 0, 'f', 0));
    QObject::connect(&server, &shear::SingleInstance::openRequested, &w, [&w](const QString &p) {
        w.openFile(p);
        w.showNormal();
        w.raise();
        w.activateWindow();
    });
    w.show();
    if (!path.isEmpty())
        QTimer::singleShot(0, &w, [&w, path] { w.openFile(path); });
    return app.exec();
}

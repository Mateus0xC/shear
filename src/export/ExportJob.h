#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QStringList>

namespace shear {

class ExportJob : public QObject {
    Q_OBJECT
public:
    ExportJob(QString ffmpeg, QStringList args, double durationSec, QString output, QObject *parent = nullptr);
    void start();
    void cancel();

signals:
    void progress(double ratio); // 0..1
    void finished(bool ok, const QString &outputPath, const QString &error, double elapsedSec);

private:
    void onReadyRead();
    void onFinished(int code, QProcess::ExitStatus st);

    QString m_ffmpeg, m_output;
    QStringList m_args;
    double m_dur;
    QProcess m_proc;
    QByteArray m_buf, m_err;
    QElapsedTimer m_clock;
    bool m_cancelled = false;
};

} // namespace shear

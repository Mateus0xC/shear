#include "ExportJob.h"
#include <QFile>
#include <algorithm>
#include <windows.h>

namespace shear {

ExportJob::ExportJob(QString ffmpeg, QStringList args, double durationSec, QString output, QObject *parent)
    : QObject(parent), m_ffmpeg(std::move(ffmpeg)), m_output(std::move(output)),
      m_args(std::move(args)), m_dur(durationSec)
{
    // Prioridade baixa para não derrubar o FPS do jogo aberto.
    m_proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &ExportJob::onReadyRead);
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_err += m_proc.readAllStandardError();
        if (m_err.size() > 16384) m_err = m_err.right(8192);
    });
    connect(&m_proc, &QProcess::finished, this, &ExportJob::onFinished);
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit finished(false, m_output, "Não foi possível iniciar o ffmpeg: " + m_ffmpeg, 0);
    });
}

void ExportJob::start()
{
    m_clock.start();
    m_proc.start(m_ffmpeg, m_args);
}

void ExportJob::cancel()
{
    m_cancelled = true;
    m_proc.kill();
}

void ExportJob::onReadyRead()
{
    m_buf += m_proc.readAllStandardOutput();
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl).trimmed();
        m_buf.remove(0, nl + 1);
        if (line.startsWith("out_time_us=") && m_dur > 0) {
            const double sec = line.mid(12).toDouble() / 1e6;
            emit progress(std::clamp(sec / m_dur, 0.0, 1.0));
        }
    }
}

void ExportJob::onFinished(int code, QProcess::ExitStatus st)
{
    const double el = m_clock.elapsed() / 1000.0;
    if (m_cancelled) {
        QFile::remove(m_output);
        emit finished(false, m_output, "Cancelado", el);
    } else if (st != QProcess::NormalExit || code != 0) {
        QFile::remove(m_output);
        emit finished(false, m_output, QString::fromLocal8Bit(m_err.right(600)).trimmed(), el);
    } else {
        emit progress(1.0);
        emit finished(true, m_output, {}, el);
    }
}

} // namespace shear

#include "AudioScrubber.h"
#include <QList>
#include <QStringList>
#include <mpv/client.h>

namespace shear {

static constexpr int kMinSeekGapMs = 30; // no máximo ~33 reposicionamentos por segundo
static constexpr int kIdleMs = 140;      // sem movimento por tanto tempo: o som para

AudioScrubber::AudioScrubber(QObject *parent) : QObject(parent)
{
    m_mpv = mpv_create();
    mpv_set_option_string(m_mpv, "config", "no");
    mpv_set_option_string(m_mpv, "load-scripts", "no");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "vid", "no");          // sem vídeo: seeks quase instantâneos
    mpv_set_option_string(m_mpv, "vo", "null");
    mpv_set_option_string(m_mpv, "audio-display", "no");
    mpv_set_option_string(m_mpv, "pause", "yes");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "hr-seek", "yes");
    mpv_set_option_string(m_mpv, "audio-buffer", "0.04"); // pouca latência: o som começa logo depois do movimento
    mpv_set_option_string(m_mpv, "volume", QByteArray::number(m_volume).constData());
    if (const QByteArray lg = qgetenv("SHEAR_SCRUB_LOG"); !lg.isEmpty()) { // diagnóstico opcional
        mpv_set_option_string(m_mpv, "log-file", lg.constData());
        mpv_set_option_string(m_mpv, "msg-level", "all=v");
    }
    if (mpv_initialize(m_mpv) < 0) {
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }
    mpv_set_wakeup_callback(m_mpv, &AudioScrubber::wakeup, this);

    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleMs);
    connect(&m_idle, &QTimer::timeout, this, [this] { stop(); });
}

AudioScrubber::~AudioScrubber()
{
    if (m_mpv)
        mpv_terminate_destroy(m_mpv);
}

void AudioScrubber::command(const QStringList &args)
{
    if (!m_mpv)
        return;
    QList<QByteArray> bytes;
    for (const auto &a : args)
        bytes << a.toUtf8();
    QList<const char *> argv;
    for (const auto &b : bytes)
        argv << b.constData();
    argv << nullptr;
    mpv_command_async(m_mpv, 0, argv.data());
}

void AudioScrubber::loadFile(const QString &path)
{
    m_playing = false;
    m_idle.stop();
    command({"set", "pause", "yes"});
    command({"loadfile", path});
}

void AudioScrubber::resetAudio()
{
    m_graph.clear();
    m_muted = false;
    command({"set", "lavfi-complex", ""});
    command({"set", "aid", "auto"});
}

void AudioScrubber::setAudioGraph(const QString &graph)
{
    if (graph == m_graph)
        return;
    m_graph = graph;
    if (graph.isEmpty()) {
        command({"set", "lavfi-complex", ""});
        command({"set", "aid", "no"});
        m_muted = true;
    } else {
        if (m_muted) {
            command({"set", "aid", "auto"});
            m_muted = false;
        }
        command({"set", "lavfi-complex", graph});
    }
}

void AudioScrubber::setVolume(int percent)
{
    m_volume = percent;
    command({"set", "volume", QString::number(percent)});
}

void AudioScrubber::scrub(double sec)
{
    if (!m_mpv)
        return;
    m_idle.start(); // enquanto houver movimento, o som continua
    if (m_last.isValid() && m_last.elapsed() < kMinSeekGapMs)
        return;
    m_last.restart();
    command({"seek", QString::number(sec, 'f', 3), "absolute+exact"});
    if (!m_playing) {
        m_playing = true;
        command({"set", "pause", "no"});
    }
}

void AudioScrubber::stop()
{
    m_idle.stop();
    if (m_playing) {
        m_playing = false;
        command({"set", "pause", "yes"});
    }
}

void AudioScrubber::wakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<AudioScrubber *>(ctx), "handleEvents", Qt::QueuedConnection);
}

void AudioScrubber::handleEvents()
{
    while (m_mpv) {
        mpv_event *e = mpv_wait_event(m_mpv, 0);
        if (e->event_id == MPV_EVENT_NONE)
            break;
        // O grafo pode ter sido pedido antes de o arquivo carregar: reaplica quando as faixas existirem.
        if (e->event_id == MPV_EVENT_FILE_LOADED && !m_graph.isEmpty())
            command({"set", "lavfi-complex", m_graph});
    }
}

} // namespace shear

#include "MpvWidget.h"
#include <QOpenGLContext>
#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace shear {

static void *getProcAddress(void *, const char *name)
{
    auto *c = QOpenGLContext::currentContext();
    return c ? reinterpret_cast<void *>(c->getProcAddress(name)) : nullptr;
}

MpvWidget::MpvWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
}

MpvWidget::~MpvWidget()
{
    makeCurrent();
    if (m_ctx)
        mpv_render_context_free(m_ctx);
    doneCurrent();
    if (m_mpv)
        mpv_terminate_destroy(m_mpv);
}

void MpvWidget::initializeGL()
{
    m_mpv = mpv_create();
    mpv_set_option_string(m_mpv, "config", "no");
    mpv_set_option_string(m_mpv, "load-scripts", "no");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "hwdec", "auto");
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "terminal", "no");
    if (const QByteArray lg = qgetenv("SHEAR_MPV_LOG"); !lg.isEmpty()) { // diagnóstico opcional
        mpv_set_option_string(m_mpv, "log-file", lg.constData());
        mpv_set_option_string(m_mpv, "msg-level", "all=v");
    }
    mpv_set_option_string(m_mpv, "volume", QByteArray::number(m_volume).constData());
    if (mpv_initialize(m_mpv) < 0)
        return;

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_set_wakeup_callback(m_mpv, &MpvWidget::wakeup, this);

    mpv_opengl_init_params gl{getProcAddress, nullptr};
    int advanced = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&m_ctx, m_mpv, params) < 0)
        return;
    mpv_render_context_set_update_callback(m_ctx, &MpvWidget::renderUpdate, this);

    if (!m_pending.isEmpty()) {
        const QString p = m_pending;
        m_pending.clear();
        loadFile(p);
    }
}

void MpvWidget::paintGL()
{
    if (!m_ctx)
        return;
    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{static_cast<int>(defaultFramebufferObject()), int(width() * dpr),
                       int(height() * dpr), 0};
    int flipY = 1;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                                 {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(m_ctx, params);
    if (m_awaitPaint) {
        m_awaitPaint = false;
        emit firstFrame();
    }
}

void MpvWidget::command(const QStringList &args)
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

void MpvWidget::loadFile(const QString &path)
{
    if (!m_mpv) { // GL ainda não inicializou: carrega assim que estiver pronto
        m_pending = path;
        return;
    }
    m_awaitFirst = true;
    m_pos = m_dur = 0;
    command({"loadfile", path});
}

void MpvWidget::seek(double sec) { command({"seek", QString::number(sec, 'f', 3), "absolute+exact"}); }

void MpvWidget::setPaused(bool p)
{
    if (m_mpv) {
        int f = p;
        mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &f);
    }
}

void MpvWidget::setAudioGraph(const QString &graph)
{
    command({"set", "lavfi-complex", graph}); // "" desliga o grafo
    command({"set", "aid", graph.isEmpty() ? "no" : "auto"});
}

void MpvWidget::resetAudio()
{
    command({"set", "lavfi-complex", ""});
    command({"set", "aid", "auto"});
}

void MpvWidget::setVolume(int percent)
{
    m_volume = percent;
    command({"set", "volume", QString::number(percent)});
}

void MpvWidget::togglePause() { setPaused(!m_paused); }

void MpvWidget::stepFrame(bool forward) { command({forward ? "frame-step" : "frame-back-step"}); }

void MpvWidget::wakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "handleEvents", Qt::QueuedConnection);
}

void MpvWidget::renderUpdate(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "onRenderUpdate", Qt::QueuedConnection);
}

void MpvWidget::handleEvents()
{
    while (m_mpv) {
        mpv_event *e = mpv_wait_event(m_mpv, 0);
        if (e->event_id == MPV_EVENT_NONE)
            break;
        if (e->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto *p = static_cast<mpv_event_property *>(e->data);
            const QByteArray n = p->name;
            if (p->format == MPV_FORMAT_DOUBLE && n == "time-pos") {
                m_pos = *static_cast<double *>(p->data);
                emit positionChanged(m_pos);
            } else if (p->format == MPV_FORMAT_DOUBLE && n == "duration") {
                m_dur = *static_cast<double *>(p->data);
                emit durationChanged(m_dur);
            } else if (p->format == MPV_FORMAT_FLAG && n == "pause") {
                m_paused = *static_cast<int *>(p->data) != 0;
                emit pausedChanged(m_paused);
            }
        } else if (e->event_id == MPV_EVENT_FILE_LOADED) {
            emit fileLoaded();
        } else if (e->event_id == MPV_EVENT_PLAYBACK_RESTART && m_awaitFirst) {
            m_awaitFirst = false;
            m_awaitPaint = true;
            update();
        }
    }
}

} // namespace shear

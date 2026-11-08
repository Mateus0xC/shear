#include "MpvWidget.h"
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QTimer>
#include <cmath>
#include <QWheelEvent>
#include <mpv/client.h>
#include <mpv/render.h>

namespace shear {

// Diagnóstico opcional: SHEAR_TRACE=arquivo grava "ms_de_relógio posição" a cada atualização de time-pos.
static void trace(const QString &line)
{
    static QFile *f = [] {
        const QByteArray p = qgetenv("SHEAR_TRACE");
        if (p.isEmpty()) return static_cast<QFile *>(nullptr);
        auto *file = new QFile(QString::fromLocal8Bit(p));
        file->open(QIODevice::Append | QIODevice::Text);
        return file;
    }();
    static QElapsedTimer clock = [] { QElapsedTimer t; t.start(); return t; }();
    if (f) {
        f->write((QString::number(clock.elapsed()) + ' ' + line + '\n').toUtf8());
        f->flush();
    }
}

MpvWidget::MpvWidget(bool gpuDecode, QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_mpv = mpv_create();
    mpv_set_option_string(m_mpv, "config", "no");
    mpv_set_option_string(m_mpv, "load-scripts", "no");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    // GPU via NVDEC ("nvdec-copy": decodifica na GPU e copia o frame para a CPU; se o codec não for suportado,
    // cai para software). Medido aqui: nvdec-copy NÃO aciona o overlay da NVIDIA, mas d3d11va/dxva2 acionam.
    // SHEAR_HWDEC=... sobrepõe (diagnóstico).
    const QByteArray hw = qEnvironmentVariableIsSet("SHEAR_HWDEC") ? qgetenv("SHEAR_HWDEC")
                          : QByteArray(gpuDecode ? "nvdec-copy,no" : "no");
    mpv_set_option_string(m_mpv, "hwdec", hw.constData());
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

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&m_ctx, m_mpv, params) < 0)
        return;
    mpv_render_context_set_update_callback(m_ctx, &MpvWidget::renderUpdate, this);
}

MpvWidget::~MpvWidget()
{
    if (m_ctx)
        mpv_render_context_free(m_ctx);
    if (m_mpv)
        mpv_terminate_destroy(m_mpv);
}

void MpvWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const qreal dpr = devicePixelRatioF();
    const QSize px(int(width() * dpr), int(height() * dpr));
    if (px.isEmpty())
        return;
    if (m_frame.size() != px) {
        m_frame = QImage(px, QImage::Format_RGB32);
        m_frame.fill(Qt::black);
    }
    if (m_ctx) {
        int size[2] = {px.width(), px.height()};
        size_t stride = size_t(m_frame.bytesPerLine());
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_SW_SIZE, size},
            {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char *>("bgr0")}, // = QImage::Format_RGB32 (little-endian)
            {MPV_RENDER_PARAM_SW_STRIDE, &stride},
            {MPV_RENDER_PARAM_SW_POINTER, m_frame.bits()},
            {MPV_RENDER_PARAM_INVALID, nullptr}};
        mpv_render_context_render(m_ctx, params);
    }
    m_frame.setDevicePixelRatio(dpr);
    p.drawImage(0, 0, m_frame);
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
    trace("MARK setAudioGraph");
    if (graph == m_graph)
        return; // nada mudou: não reconfigura (e não reposiciona) à toa
    m_graph = graph;

    // Trocar o filtro faz o mpv reposicionar a reprodução e o vídeo "pula". Então: guarda a posição, pausa, troca,
    // e depois volta exatamente para onde estava e retoma.
    if (m_mpv && m_dur > 0 && !m_awaitFirst) {
        double pos = m_pos;
        mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
        m_restore = {true, !m_paused, 0, pos};
        if (!m_paused)
            command({"set", "pause", "yes"});
        const quint64 gen = ++m_restoreGen;
        QTimer::singleShot(900, this, [this, gen] { finishRestore(gen); }); // rede de segurança: nunca fica pausado
    }
    if (graph.isEmpty()) {
        command({"set", "lavfi-complex", ""}); // "" desliga o grafo
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

void MpvWidget::finishRestore(quint64 gen)
{
    if (!m_restore.active || gen != m_restoreGen)
        return;
    m_restore.active = false;
    if (m_restore.resume)
        command({"set", "pause", "no"});
}

void MpvWidget::resetAudio()
{
    m_restore.active = false;
    ++m_restoreGen;
    m_graph.clear();
    m_muted = false;
    command({"set", "lavfi-complex", ""});
    command({"set", "aid", "auto"});
}

void MpvWidget::setVolume(int percent)
{
    m_volume = percent;
    command({"set", "volume", QString::number(percent)});
}

void MpvWidget::showText(const QString &text, int ms)
{
    command({"show-text", text, QString::number(ms)});
}

void MpvWidget::wheelEvent(QWheelEvent *e)
{
    const int notches = e->angleDelta().y() / 120;
    if (notches != 0)
        emit volumeWheel(notches * 5);
    e->accept();
}

void MpvWidget::togglePause() { setPaused(!m_paused); }

void MpvWidget::stepFrame(bool forward) { command({forward ? "frame-step" : "frame-back-step"}); }

void MpvWidget::onRenderUpdate()
{
    if (m_ctx)
        mpv_render_context_update(m_ctx);
    update();
}

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
                trace(QString::number(m_pos, 'f', 3));
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
        } else if (e->event_id == MPV_EVENT_PLAYBACK_RESTART && m_restore.active) {
            // o filtro novo está no ar: volta exatamente ao ponto de antes e retoma
            double now = m_pos; // m_pos pode estar defasado: lê a posição atual direto do mpv
            mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &now);
            trace(QString("MARK restart now=%1 saved=%2").arg(now, 0, 'f', 3).arg(m_restore.pos, 0, 'f', 3));
            if (m_restore.stage == 0 && std::abs(now - m_restore.pos) > 0.02) {
                m_restore.stage = 1;
                command({"seek", QString::number(m_restore.pos, 'f', 3), "absolute+exact"});
            } else {
                finishRestore(m_restoreGen);
            }
        } else if (e->event_id == MPV_EVENT_PLAYBACK_RESTART && m_awaitFirst) {
            m_awaitFirst = false;
            m_awaitPaint = true;
            update();
        }
    }
}

} // namespace shear

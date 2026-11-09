#include "MpvWidget.h"
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QTimer>
#include <algorithm>
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
    m_ovTimer.setInterval(16); // ~60 quadros/s, só enquanto uma animação está na tela
    connect(&m_ovTimer, &QTimer::timeout, this, [this] {
        if (m_ovActive) update(); else m_ovTimer.stop();
    });

    connect(&m_viewTimer, &QTimer::timeout, this, [this] { if (m_panning) applyPan(); else m_viewTimer.stop(); });

    m_mpv = mpv_create();
    mpv_set_option_string(m_mpv, "config", "no");
    // Render por software: o redimensionamento da imagem é feito na CPU. "bilinear" é bem mais leve que o padrão
    // (bicúbico) e na prática não se nota; com zoom isso pesa a cada quadro.
    mpv_set_option_string(m_mpv, "sws-scaler", "bilinear");
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
    mpv_observe_property(m_mpv, 0, "container-fps", MPV_FORMAT_DOUBLE);
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
    if (m_hold) {
        p.fillRect(rect(), Qt::black);
        if (m_holdClock.isValid() && m_holdClock.elapsed() > 500) { // só se demorar: vídeos normais nem chegam a ver
            p.setPen(QColor(170, 170, 170));
            p.drawText(rect(), Qt::AlignCenter, "Verificando o início do vídeo…");
        }
        return;
    }
    if (m_panning && !m_useRender && !m_frame.isNull()) { // arrastando: só desloca o quadro que já está pronto
        p.fillRect(rect(), Qt::black);
        p.drawImage(QPointF((m_panX - m_framePanX) * m_panPxX, (m_panY - m_framePanY) * m_panPxY), m_frame);
        return;
    }
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
        m_framePanX = m_appliedPanX; // o quadro pronto foi desenhado com esta posição
        m_framePanY = m_appliedPanY;
        m_useRender = false;
    }
    m_frame.setDevicePixelRatio(dpr);
    p.drawImage(0, 0, m_frame);
    if (m_ovActive) {
        const double dur = m_ovKind == Overlay::Skip ? 700.0 : 480.0;
        const double t = double(m_ovClock.elapsed()) / dur;
        if (t >= 1.0) m_ovActive = false; // acabou: o timer se desliga sozinho
        else paintOverlay(p, t);
    }
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
    if (m_zoomLog != 0 || m_panX != 0 || m_panY != 0) { // vídeo novo começa sem zoom
        m_zoomLog = m_panX = m_panY = 0;
        applyView();
    }
    command({"loadfile", path});
}

void MpvWidget::seek(double sec)
{
    sec = std::max(sec, m_minPos);
    command({"seek", QString::number(sec, 'f', 3), "absolute+exact"});
}

void MpvWidget::seekBy(double sec)
{
    if (m_minPos > 0 && m_pos + sec < m_minPos) {
        seek(m_minPos);
        return;
    }
    command({"seek", QString::number(sec, 'f', 3), "relative+exact"});
}

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
    if (notches != 0) {
        if (e->modifiers() & Qt::ControlModifier)
            zoomBy(notches * 0.25); // Ctrl + roda = zoom no vídeo
        else
            emit volumeWheel(notches * 5);
    }
    e->accept();
}

void MpvWidget::flash(Overlay kind, const QString &text)
{
    if (m_hold) return;
    m_ovKind = kind;
    m_ovText = text;
    m_ovActive = true;
    m_ovClock.restart();
    if (!m_ovTimer.isActive()) m_ovTimer.start();
    update();
}

// t: 0 (começou) .. 1 (sumiu). O ícone cresce um pouco e some; o texto do pulo desliza e some.
void MpvWidget::paintOverlay(QPainter &p, double t) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double fade = t < 0.35 ? 1.0 : 1.0 - (t - 0.35) / 0.65; // fica cheio e depois some
    const double alpha = std::clamp(fade, 0.0, 1.0);
    const QPointF c(width() / 2.0, height() / 2.0);
    if (m_ovKind == Overlay::Skip) {
        const bool fwd = !m_ovText.startsWith(QChar(0x2212)) && !m_ovText.startsWith('-'); // "−" ou "-" = para trás
        const double r = std::min(width(), height()) * 0.075;
        const QPointF ctr(width() * (fwd ? 0.80 : 0.20), height() / 2.0);
        const QRectF pill(ctr.x() - r * 1.9, ctr.y() - r, r * 3.8, r * 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, int(150 * alpha)));
        p.drawRoundedRect(pill, r, r);
        p.setBrush(QColor(255, 255, 255, int(235 * alpha)));
        // dois triângulos (▶▶ ou ◀◀)
        const double s = r * 0.34, dir = fwd ? 1.0 : -1.0;
        for (int i = 0; i < 2; ++i) {
            const double x0 = ctr.x() - dir * r * 1.15 + dir * i * s * 1.1;
            QPolygonF tri;
            tri << QPointF(x0, ctr.y() - s) << QPointF(x0, ctr.y() + s) << QPointF(x0 + dir * s * 1.4, ctr.y());
            p.drawPolygon(tri);
        }
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(std::max(12, int(r * 0.62)));
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, int(240 * alpha)));
        p.drawText(QRectF(ctr.x() - r * 0.4, ctr.y() - r, r * 2.2, r * 2), Qt::AlignCenter, m_ovText);
    } else {
        const double grow = 1.0 + 0.35 * t;
        const double r = std::min(width(), height()) * 0.085 * grow;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, int(150 * alpha)));
        p.drawEllipse(c, r, r);
        p.setBrush(QColor(255, 255, 255, int(240 * alpha)));
        if (m_ovKind == Overlay::Play) {
            const double s = r * 0.46;
            QPolygonF tri;
            tri << QPointF(c.x() - s * 0.7, c.y() - s) << QPointF(c.x() - s * 0.7, c.y() + s) << QPointF(c.x() + s * 1.1, c.y());
            p.drawPolygon(tri);
        } else {
            const double w = r * 0.20, h = r * 0.9, gap = r * 0.16;
            p.drawRoundedRect(QRectF(c.x() - gap - w, c.y() - h / 2, w, h), w / 3, w / 3);
            p.drawRoundedRect(QRectF(c.x() + gap, c.y() - h / 2, w, h), w / 3, w / 3);
        }
    }
    p.restore();
}

void MpvWidget::setSpeed(double speed)
{
    m_speed = std::clamp(speed, 0.1, 4.0);
    command({"set", "speed", QString::number(m_speed, 'f', 3)});
}

// --- zoom e posição da imagem ---------------------------------------------------------------------------------------
void MpvWidget::zoomBy(double stepsLog2)
{
    m_zoomLog = std::clamp(m_zoomLog + stepsLog2, 0.0, 3.0); // até 8x
    if (m_zoomLog <= 1e-9) m_panX = m_panY = 0;
    applyView();
    showText(m_zoomLog <= 1e-9 ? QStringLiteral("Zoom 1×")
                               : QString("Zoom %1×").arg(std::pow(2.0, m_zoomLog), 0, 'f', 1).replace('.', ','),
             700);
}

void MpvWidget::resetZoom()
{
    if (m_zoomLog <= 1e-9 && m_panX == 0 && m_panY == 0) return;
    m_zoomLog = m_panX = m_panY = 0;
    applyView();
    showText(QStringLiteral("Zoom 1×"), 700);
}

void MpvWidget::applyView()
{
    // O pan do mpv é em frações do vídeo JÁ ampliado: para ver as bordas basta (z-1)/(2z).
    const double z = std::pow(2.0, m_zoomLog), maxPan = (z - 1.0) / (2.0 * z);
    m_panX = std::clamp(m_panX, -maxPan, maxPan);
    m_panY = std::clamp(m_panY, -maxPan, maxPan);
    command({"set", "video-zoom", QString::number(m_zoomLog, 'f', 4)});
    command({"set", "video-pan-x", QString::number(m_panX, 'f', 5)});
    command({"set", "video-pan-y", QString::number(m_panY, 'f', 5)});
    m_appliedPanX = m_panX;
    m_appliedPanY = m_panY;
    setCursor(m_zoomLog > 1e-9 ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void MpvWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_pressed = true;
    m_panning = false;
    m_pressPos = m_lastPos = e->pos();
}

void MpvWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_pressed || m_zoomLog <= 1e-9) return;
    if (!m_panning && (e->pos() - m_pressPos).manhattanLength() < 4) return; // ainda é um clique
    m_panning = true;
    setCursor(Qt::ClosedHandCursor);
    double aspect = 16.0 / 9.0;
    if (m_mpv) mpv_get_property(m_mpv, "video-params/aspect", MPV_FORMAT_DOUBLE, &aspect);
    if (aspect <= 0.1) aspect = 16.0 / 9.0;
    const double z = std::pow(2.0, m_zoomLog);
    const double dispW = std::min<double>(width(), height() * aspect) * z; // tamanho do vídeo ampliado, em px
    const double dispH = std::min<double>(height(), width() / aspect) * z;
    const QPoint d = e->pos() - m_lastPos;
    m_lastPos = e->pos();
    const double maxPan = (z - 1.0) / (2.0 * z);
    m_panX = std::clamp(m_panX + d.x() / dispW, -maxPan, maxPan);
    m_panY = std::clamp(m_panY + d.y() / dispH, -maxPan, maxPan);
    m_panPxX = dispW;
    m_panPxY = dispH;
    // Enquanto arrasta, a imagem só é DESLOCADA na tela (copiar o quadro já pronto custa quase nada, então segue o
    // mouse sem atraso). O mpv só recebe a posição de tempos em tempos e redesenha; ao soltar, a posição exata.
    if (!m_viewTimer.isActive())
        m_viewTimer.start(120);
    update();
}

void MpvWidget::applyPan()
{
    if (!m_mpv) return;
    m_appliedPanX = m_panX;
    m_appliedPanY = m_panY;
    mpv_set_property(m_mpv, "video-pan-x", MPV_FORMAT_DOUBLE, &m_panX); // síncrono: o próximo quadro já vem com ela
    mpv_set_property(m_mpv, "video-pan-y", MPV_FORMAT_DOUBLE, &m_panY);
    m_applyPending = true; // o próximo "quadro novo" do mpv já reflete essa posição
}

void MpvWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_pressed) return;
    m_pressed = false;
    if (m_panning) {
        m_panning = false;
        m_viewTimer.stop();
        applyPan();               // posição final exata
        update();
        setCursor(m_zoomLog > 1e-9 ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return; // era um arrasto, não um clique
    }
    emit clicked();
}

void MpvWidget::togglePause() { setPaused(!m_paused); }

void MpvWidget::stepFrame(bool forward)
{
    if (!forward && m_minPos > 0 && m_pos - 1.5 / 60 < m_minPos)
        return; // não volta para dentro do trecho corrompido
    command({forward ? "frame-step" : "frame-back-step"});
}

void MpvWidget::onRenderUpdate()
{
    if (m_ctx)
        mpv_render_context_update(m_ctx);
    if (m_applyPending) { // chegou o quadro que já tem a posição nova: pode desenhar de verdade
        m_applyPending = false;
        m_useRender = true;
    }
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
        if (e->event_id == MPV_EVENT_PLAYBACK_RESTART)
            emit playbackRestarted(); // um seek (ou o carregamento) terminou: o quadro novo já está pronto
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
            } else if (n == "container-fps") {
                emit fpsChanged(p->format == MPV_FORMAT_DOUBLE ? *static_cast<double *>(p->data) : 0.0);
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

#include "TimelineWidget.h"
#include <QApplication>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace shear {

// Janelas do zoom fino. Quanto mais o mouse desce, menor a janela (mais zoom).
static const double kSpans[] = {20.0, 5.0, 1.5, 0.4};
static constexpr int kLevels = 4;
static constexpr double kEnterBeyond = 8;   // px abaixo da timeline para abrir o zoom
static constexpr double kLevelHeight = 55;  // px de descida por nível de zoom
static constexpr double kHysteresis = 10;   // px de folga para o nível não ficar piscando

static QString fmtSpan(double s) { return QString::number(s, 'g', 3).replace('.', ',') + " s"; }

static QString fmtTime(double t, int dec)
{
    t = std::max(0.0, t);
    const int m = int(t) / 60;
    const double sec = t - m * 60;
    QString s = QString::number(sec, 'f', dec);
    if (sec < 10) s.prepend('0');
    return QString("%1:%2").arg(m).arg(s).replace('.', ',');
}

struct LensState {
    double win0 = 0, span = 1, dur = 0, start = 0, end = 0, cur = 0, fps = 0;
    int level = 0;
    const std::vector<double> *kf = nullptr;
};

// Régua ampliada que aparece logo abaixo da timeline enquanto você arrasta com o mouse puxado para baixo.
// O ponto sob o mouse na timeline fica na mesma coluna aqui (mapeamento absoluto): o que você vê é onde você está.
class TimelineLens : public QWidget {
public:
    explicit TimelineLens(QWidget *owner) : QWidget(owner, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setState(const LensState &s)
    {
        m_s = s;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const LensState &s = m_s;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = qApp->palette();
        const QColor text = pal.color(QPalette::WindowText), mid = pal.color(QPalette::Mid);
        const QColor accent = pal.color(QPalette::Highlight);
        QColor dim = text;
        dim.setAlpha(140);

        p.setBrush(pal.color(QPalette::Base));
        p.setPen(QPen(accent, 1.5));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 9, 9);

        const double m = 14, w = width() - 2 * m, t0 = s.win0;
        auto X = [&](double t) { return m + (t - t0) / s.span * w; };
        const double top = 48, base = height() - 10;

        // linha de dicas (esquerda) e níveis de zoom (direita, o atual destacado)
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(dim);
        p.drawText(QRectF(m, 5, w * 0.5, 18), Qt::AlignLeft | Qt::AlignVCenter, "↓ mais zoom    ↑ menos zoom");
        double rx = width() - m;
        for (int i = kLevels - 1; i >= 0; --i) {
            const bool active = i == s.level;
            f.setBold(active);
            p.setFont(f);
            const QString lab = fmtSpan(kSpans[i]);
            const int tw = QFontMetrics(f).horizontalAdvance(lab);
            const QRectF box(rx - tw - 14, 5, tw + 14, 18);
            if (active) {
                QColor bg = accent;
                bg.setAlpha(255);
                p.setPen(Qt::NoPen);
                p.setBrush(bg);
                p.drawRoundedRect(box, 9, 9);
                p.setPen(pal.color(QPalette::HighlightedText));
            } else {
                p.setPen(dim);
            }
            p.drawText(box, Qt::AlignCenter, lab);
            rx -= tw + 18;
        }
        f.setBold(false);

        // fora do clipe (antes de 0 ou depois do fim): escurecido
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 110));
        if (t0 < 0) p.drawRect(QRectF(m, top, X(0) - m, base - top));
        if (t0 + s.span > s.dur) p.drawRect(QRectF(X(s.dur), top, m + w - X(s.dur), base - top));

        // seleção
        p.setBrush(QColor(60, 110, 200, 70));
        const double xs = std::clamp(X(s.start), m, m + w), xe = std::clamp(X(s.end), m, m + w);
        if (xe > xs) p.drawRect(QRectF(xs, top, xe - xs, base - top));

        // régua: passo "redondo" com no máximo ~8 marcas principais na janela
        static const double steps[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 30, 60};
        double major = 60;
        for (double st : steps)
            if (s.span / st <= 8) { major = st; break; }
        const double minor = major / 5;
        const int dec = major >= 1 ? 0 : (major >= 0.1 ? 1 : 2);
        f.setPixelSize(10);
        p.setFont(f);
        for (long long k = (long long)std::ceil(t0 / minor); k * minor <= t0 + s.span; ++k) {
            const double t = k * minor, x = X(t);
            const bool isMajor = std::abs(t - std::round(t / major) * major) < minor * 0.01;
            p.setPen(QPen(isMajor ? text : dim, 1));
            p.drawLine(QPointF(x, base), QPointF(x, base - (isMajor ? 22 : 10)));
            if (isMajor) {
                p.setPen(dim);
                p.drawText(QRectF(x - 34, base - 38, 68, 14), Qt::AlignHCenter, fmtTime(t, dec));
            }
        }

        // marcas de frame (quando há espaço): cada traço é um quadro
        if (s.fps > 0 && w / s.span / s.fps >= 3.0) {
            QColor fc = text;
            fc.setAlpha(70);
            p.setPen(QPen(fc, 1));
            for (long long k = (long long)std::ceil(t0 * s.fps); k / s.fps <= t0 + s.span; ++k)
                p.drawLine(QPointF(X(k / s.fps), base), QPointF(X(k / s.fps), base - 5));
        }

        // keyframes (laranja) na janela
        if (s.kf) {
            p.setPen(QPen(QColor(255, 160, 40), 2));
            auto it = std::lower_bound(s.kf->begin(), s.kf->end(), t0);
            for (; it != s.kf->end() && *it <= t0 + s.span; ++it)
                p.drawLine(QPointF(X(*it), base), QPointF(X(*it), base - 8));
        }

        // marcadores de início/fim
        p.setPen(QPen(QColor(90, 170, 255), 3));
        if (s.start >= t0 && s.start <= t0 + s.span) p.drawLine(QPointF(X(s.start), top), QPointF(X(s.start), base));
        p.setPen(QPen(QColor(255, 110, 110), 3));
        if (s.end >= t0 && s.end <= t0 + s.span) p.drawLine(QPointF(X(s.end), top), QPointF(X(s.end), base));

        // posição atual: linha branca exatamente embaixo do mouse, com o tempo preciso numa etiqueta
        const double cx = X(s.cur);
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(QPointF(cx, top - 4), QPointF(cx, base));
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        const QString label = fmtTime(s.cur, 3);
        const double bw = QFontMetrics(f).horizontalAdvance(label) + 18;
        const double bx = std::clamp(cx - bw / 2, m, m + w - bw);
        const QRectF bubble(bx, 26, bw, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(bubble, 9, 9);
        p.setPen(pal.color(QPalette::HighlightedText));
        p.drawText(bubble, Qt::AlignCenter, label);
    }

private:
    LensState m_s;
};

// Etiqueta flutuante logo abaixo da timeline: ensina o gesto de zoom enquanto você arrasta.
class TimelineHint : public QWidget {
public:
    explicit TimelineHint(QWidget *owner) : QWidget(owner, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont f = font();
        f.setPixelSize(12);
        setFont(f);
        resize(QFontMetrics(f).horizontalAdvance(text()) + 28, 26);
    }
    static QString text() { return QStringLiteral("↓  Puxe o mouse para baixo para ampliar e ajustar com precisão"); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = qApp->palette();
        p.setBrush(pal.color(QPalette::Base)); // sem contorno azul: só um contorno discreto, neutro
        p.setPen(QPen(pal.color(QPalette::Mid), 1));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(rect(), Qt::AlignCenter, text());
    }
};

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(60);
    setFocusPolicy(Qt::NoFocus);
}

void TimelineWidget::reset(double duration)
{
    m_dur = duration;
    m_pos = 0;
    m_start = 0;
    m_end = duration;
    m_kf.clear();
    m_events.clear();
    m_floor = 0;
    m_gaps.clear();
    m_pendingGap.reset();
    m_ghost.reset();
    m_scanDone = false;
    m_fps = 0;
    update();
}

void TimelineWidget::setDuration(double d)
{
    if (d <= 0) return;
    const bool endWasFull = m_end <= 0 || std::abs(m_end - m_dur) < 1e-6;
    m_dur = d;
    if (endWasFull) m_end = d;
    update();
}

void TimelineWidget::setPosition(double p) { m_pos = p; update(); }

void TimelineWidget::setStart(double s)
{
    m_start = std::clamp(s, m_floor, std::max(m_floor, m_end - 0.05));
    update();
}

void TimelineWidget::setEnd(double e)
{
    m_end = std::clamp(e, std::min(m_dur, m_start + 0.05), m_dur);
    update();
}

void TimelineWidget::setRange(double s, double e)
{
    m_start = std::clamp(s, m_floor, m_dur);
    m_end = std::clamp(e, m_start, m_dur);
    update();
}

void TimelineWidget::addKeyframes(const QVector<double> &batch)
{
    for (double k : batch)
        if (k >= m_floor - 1e-6) m_kf.push_back(k); // keyframes do trecho corrompido não servem
    std::sort(m_kf.begin(), m_kf.end());
    update();
}

void TimelineWidget::setDamagedUntil(double t)
{
    m_floor = std::max(0.0, t);
    m_kf.erase(std::remove_if(m_kf.begin(), m_kf.end(), [this](double k) { return k < m_floor - 1e-6; }), m_kf.end());
    if (m_start < m_floor) m_start = std::min(m_floor, std::max(m_floor, m_end - 0.05));
    update();
}

void TimelineWidget::setGaps(std::vector<Gap> g)
{
    m_gaps = std::move(g);
    update();
}

void TimelineWidget::addGap(double a, double b)
{
    if (b < a) std::swap(a, b);
    std::vector<Gap> out;
    for (const Gap &g : m_gaps) {
        if (g.second < a || g.first > b) out.push_back(g); // não encosta: fica como está
        else { a = std::min(a, g.first); b = std::max(b, g.second); } // encosta: vira um só
    }
    out.push_back({a, b});
    std::sort(out.begin(), out.end());
    m_gaps = std::move(out);
    update();
}

bool TimelineWidget::removeGapAt(double t)
{
    for (auto it = m_gaps.begin(); it != m_gaps.end(); ++it)
        if (t >= it->first && t <= it->second) {
            m_gaps.erase(it);
            update();
            return true;
        }
    return false;
}

void TimelineWidget::setPendingGap(std::optional<double> p)
{
    if (p != m_pendingGap) { m_pendingGap = p; update(); }
}

void TimelineWidget::setGhostStart(std::optional<double> g)
{
    if (g != m_ghost) { m_ghost = g; update(); }
}

std::optional<double> TimelineWidget::keyframeAtOrBefore(double t) const
{
    auto it = std::upper_bound(m_kf.begin(), m_kf.end(), t + 1e-3);
    if (it == m_kf.begin()) return std::nullopt;
    return *std::prev(it);
}

std::optional<double> TimelineWidget::firstKeyframeAtOrAfter(double t) const
{
    auto it = std::lower_bound(m_kf.begin(), m_kf.end(), t - 1e-3);
    if (it == m_kf.end()) return std::nullopt;
    return *it;
}

std::optional<double> TimelineWidget::nearestKeyframe(double t) const
{
    if (m_kf.empty()) return std::nullopt;
    auto it = std::lower_bound(m_kf.begin(), m_kf.end(), t);
    if (it == m_kf.end()) return m_kf.back();
    double best = *it;
    if (it != m_kf.begin() && std::abs(*std::prev(it) - t) <= std::abs(best - t))
        best = *std::prev(it);
    return best;
}

double TimelineWidget::snapToKeyframe(double t) const
{
    if (m_kf.empty())
        return t;
    if (!m_scanDone && t > m_kf.back())
        return t; // região ainda não lida: não puxa de volta para o último keyframe conhecido
    return nearestKeyframe(t).value_or(t);
}

double TimelineWidget::secToX(double s) const
{
    const double w = width() - 2.0 * kMargin;
    return kMargin + (m_dur > 0 ? s / m_dur * w : 0);
}

double TimelineWidget::xToSec(double x) const
{
    const double w = width() - 2.0 * kMargin;
    return m_dur > 0 ? std::clamp((x - kMargin) / w, 0.0, 1.0) * m_dur : 0;
}

void TimelineWidget::setEvents(std::vector<Event> ev)
{
    std::sort(ev.begin(), ev.end(), [](const Event &a, const Event &b) { return a.t < b.t; });
    m_events = std::move(ev);
    update();
}

std::optional<double> TimelineWidget::prevEvent(double t) const
{
    for (auto it = m_events.rbegin(); it != m_events.rend(); ++it)
        if (it->t < t - 0.15)
            return it->t;
    return std::nullopt;
}

std::optional<double> TimelineWidget::nextEvent(double t) const
{
    for (const Event &e : m_events)
        if (e.t > t + 0.05)
            return e.t;
    return std::nullopt;
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int top = 14, bottom = height() - 14;
    p.fillRect(rect(), palette().window());
    p.fillRect(QRectF(kMargin, top, width() - 2 * kMargin, bottom - top), QColor(38, 38, 42));
    if (m_dur <= 0) return;

    const double xs = secToX(m_start), xe = secToX(m_end);
    p.fillRect(QRectF(xs, top, xe - xs, bottom - top), QColor(60, 110, 200, 90));
    if (m_floor > 0) { // trecho corrompido: hachurado em vermelho
        const QRectF bad(kMargin, top, secToX(m_floor) - kMargin, bottom - top);
        p.fillRect(bad, QBrush(QColor(200, 60, 60, 150), Qt::BDiagPattern));
        p.fillRect(bad, QColor(200, 60, 60, 35));
    }

    // Trechos a apagar: escuros e hachurados, com bordas laranja (só valem dentro do início..fim).
    for (const Gap &g : m_gaps) {
        const double a = std::clamp(g.first, m_start, m_end), b = std::clamp(g.second, m_start, m_end);
        if (b <= a) continue;
        const QRectF r(secToX(a), top, std::max(2.0, secToX(b) - secToX(a)), bottom - top);
        p.fillRect(r, QColor(20, 20, 24, 215));
        p.fillRect(r, QBrush(QColor(255, 150, 60, 150), Qt::BDiagPattern));
        p.setPen(QPen(QColor(255, 150, 60), 2));
        p.drawLine(QPointF(r.left(), top), QPointF(r.left(), bottom));
        p.drawLine(QPointF(r.right(), top), QPointF(r.right(), bottom));
    }
    if (m_pendingGap) { // início de um trecho a apagar, esperando o fim
        const double px = secToX(*m_pendingGap);
        p.setPen(QPen(QColor(255, 150, 60), 2, Qt::DashLine));
        p.drawLine(QPointF(px, top - 4), QPointF(px, bottom + 4));
    }

    p.setPen(QPen(QColor(150, 150, 160), 1));
    for (double k : m_kf) {
        const double x = std::round(secToX(k)) + 0.5;
        p.drawLine(QPointF(x, bottom - 12), QPointF(x, bottom));
    }

    for (const Event &e : m_events) { // kill = verde, morte = roxo; losango pequeno no alto, com um traço fino até a base
        const double x = std::round(secToX(e.t)) + 0.5;
        const QColor col = e.kill ? QColor(70, 215, 120) : QColor(190, 100, 255);
        p.setPen(QPen(QColor(col.red(), col.green(), col.blue(), 110), 1));
        p.drawLine(QPointF(x, top + 6), QPointF(x, bottom - 12));
        QPolygonF d;
        d << QPointF(x, top - 1) << QPointF(x + 4.5, top + 4.5) << QPointF(x, top + 10) << QPointF(x - 4.5, top + 4.5);
        p.setPen(QPen(QColor(20, 20, 24), 1));
        p.setBrush(col);
        p.drawPolygon(d);
        p.setBrush(Qt::NoBrush);
    }

    if (m_ghost) {
        p.setPen(QPen(QColor(255, 160, 40), 2, Qt::DashLine));
        const double gx = secToX(*m_ghost);
        p.drawLine(QPointF(gx, top - 6), QPointF(gx, bottom + 6));
    }

    p.setPen(QPen(QColor(90, 170, 255), 3));
    p.drawLine(QPointF(xs, top - 8), QPointF(xs, bottom + 8));
    p.drawLine(QPointF(xs, top - 8), QPointF(xs + 8, top - 8));
    p.setPen(QPen(QColor(255, 110, 110), 3));
    p.drawLine(QPointF(xe, top - 8), QPointF(xe, bottom + 8));
    p.drawLine(QPointF(xe, top - 8), QPointF(xe - 8, top - 8));

    p.setPen(QPen(Qt::white, 2));
    const double px = secToX(m_pos);
    p.drawLine(QPointF(px, top), QPointF(px, bottom));

    const QColor accent = palette().color(QPalette::Highlight);
    if (m_zoom) {
        // moldura mostrando QUAL trecho da timeline está ampliado na régua de baixo
        const double a = std::clamp(secToX(m_win0), double(kMargin), double(width() - kMargin));
        const double b = std::clamp(secToX(m_win0 + m_span), double(kMargin), double(width() - kMargin));
        QColor fill = accent;
        fill.setAlpha(60);
        p.setPen(QPen(accent, 2));
        p.setBrush(fill);
        const double bw = std::max(14.0, b - a); // largura mínima visível, mesmo nos níveis de zoom mais finos
        p.drawRoundedRect(QRectF((a + b) / 2 - bw / 2, top - 3, bw, bottom - top + 6), 3, 3);
    }
}

void TimelineWidget::wheelEvent(QWheelEvent *e)
{
    // Shift pode virar rolagem horizontal (angleDelta.x): vale o eixo que veio.
    const QPoint d = e->angleDelta();
    m_wheelAcc += d.y() != 0 ? d.y() : d.x();
    const int notches = m_wheelAcc / 120;
    if (notches != 0) {
        m_wheelAcc -= notches * 120;
        emit scrolled(notches, e->modifiers());
    }
    e->accept();
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    if (m_dur <= 0) return;
    const double x = e->position().x();
    if (e->button() == Qt::RightButton) { // botão direito num trecho apagado = traz o trecho de volta
        emit gapRightClicked(xToSec(x));
        return;
    }
    const double ds = std::abs(x - secToX(m_start)), de = std::abs(x - secToX(m_end));
    if (std::min(ds, de) <= 9) {
        m_drag = ds <= de ? Drag::Start : Drag::End;
        m_dragT = m_drag == Drag::Start ? m_start : m_end;
        m_lastFrameKey = frameKey(m_dragT);
    } else {
        m_drag = Drag::Seek;
        m_dragT = xToSec(x);
        emit seekRequested(m_dragT);
        m_lastFrameKey = frameKey(m_dragT);
    }
    m_zoom = false;
    m_level = -1;
    update();
    emit dragStarted();
}

long long TimelineWidget::frameKey(double t) const
{
    return std::llround(t * (m_fps > 0 ? m_fps : 60.0));
}

void TimelineWidget::emitSeekIfFrameChanged(double t)
{
    // Mexer o mouse dentro do mesmo quadro (ou subir/descer, que só troca o nível de zoom) não muda nada na tela:
    // não há motivo para novo seek nem para tocar o áudio de novo.
    const long long k = frameKey(t);
    if (k == m_lastFrameKey)
        return;
    m_lastFrameKey = k;
    emit seekRequested(t);
}

void TimelineWidget::applyDrag(double t)
{
    t = std::max(t, m_floor); // o trecho corrompido não é alcançável
    switch (m_drag) {
    case Drag::Seek:  m_dragT = t; emitSeekIfFrameChanged(t); break;
    // Os marcadores também levam o vídeo junto enquanto são arrastados (scrubbing em tempo real).
    case Drag::Start: setStart(m_snapStart ? snapToKeyframe(t) : t); m_dragT = m_start; emit startChanged(m_start); emitSeekIfFrameChanged(m_start); break;
    case Drag::End:   setEnd(t); m_dragT = m_end; emit endChanged(m_end); emitSeekIfFrameChanged(m_end); break;
    default: break;
    }
}

void TimelineWidget::showLens()
{
    if (!m_lens) m_lens = new TimelineLens(this);
    m_lens->resize(width(), 108);
    // Sem espaço embaixo (tela cheia): a régua ampliada aparece ACIMA da timeline.
    m_lens->move(mapToGlobal(QPoint(0, roomBelow() >= 108 + 12 ? height() + 6 : -108 - 6)));
    LensState s;
    s.win0 = m_win0; s.span = m_span; s.dur = m_dur; s.start = m_start; s.end = m_end;
    s.cur = m_dragT; s.fps = m_fps; s.level = m_level; s.kf = &m_kf;
    m_lens->setState(s);
    if (!m_lens->isVisible()) m_lens->show();
}

double TimelineWidget::roomBelow() const
{
    const QScreen *sc = screen();
    if (!sc) return 400;
    return std::max(0.0, double(sc->geometry().bottom() - mapToGlobal(QPoint(0, height())).y()));
}

void TimelineWidget::showHint()
{
    if (!m_zoomEnabled) return;
    if (!m_hint) m_hint = new TimelineHint(this);
    const bool below = roomBelow() >= m_hint->height() + 12;
    m_hint->move(mapToGlobal(QPoint((width() - m_hint->width()) / 2, below ? height() + 6 : -m_hint->height() - 6)));
    if (!m_hint->isVisible()) m_hint->show();
}

void TimelineWidget::hideHint()
{
    if (m_hint) m_hint->hide();
}

void TimelineWidget::setZoomEnabled(bool on)
{
    m_zoomEnabled = on;
    if (!on) {
        if (m_zoom) leaveZoom();
        hideHint();
    }
}

void TimelineWidget::leaveZoom()
{
    m_zoom = false;
    m_level = -1;
    if (m_lens) m_lens->hide();
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Drag::None) return;
    const QPointF pos = e->position();
    if (!m_zoomEnabled) { // sem zoom de precisão: arrastar só anda na horizontal, não importa o quanto o mouse desça
        applyDrag(xToSec(pos.x()));
        return;
    }
    const double beyond = pos.y() - height(); // quanto o mouse foi puxado para baixo da timeline
    const double w = width() - 2.0 * kMargin;
    const double frac = std::clamp((pos.x() - kMargin) / w, 0.0, 1.0); // posição horizontal dentro da timeline/régua

    // Em tela cheia a timeline fica colada embaixo e o mouse não tem 170 px para descer: as faixas de zoom se ajustam
    // ao espaço que sobra até a borda da tela (de 55 px por nível até ~4 px, mas sempre os 4 níveis).
    const double room = roomBelow();
    const double enterBeyond = std::min(kEnterBeyond, std::max(2.0, room / 6));
    // O último nível começa em ~65% do espaço disponível (não no último pixel da tela): sempre dá para chegar nele.
    const double levelH = std::clamp((room * 0.65 - enterBeyond) / (kLevels - 1), 4.0, kLevelHeight);
    const double hysteresis = std::min(kHysteresis, levelH / 3);

    // Entra no zoom ao passar de enterBeyond e só sai ao voltar para dentro da timeline (histerese).
    const bool wantZoom = m_zoom ? beyond > 0 : beyond > enterBeyond;
    if (!wantZoom) {
        if (m_zoom) leaveZoom();
        applyDrag(xToSec(pos.x()));
        showHint();
        return;
    }

    hideHint();

    // Nível de zoom por faixas de altura, com folga para não ficar trocando na borda.
    auto bandStart = [&](int L) { return enterBeyond + levelH * L; };
    int level = m_level;
    if (level < 0 || beyond < bandStart(level) - hysteresis || (level < kLevels - 1 && beyond >= bandStart(level + 1) + hysteresis))
        level = std::clamp(int((beyond - enterBeyond) / levelH), 0, kLevels - 1);
    const double span = std::min(kSpans[level], std::max(0.4, m_dur));

    // (Re)ancora a janela de modo que a posição atual fique exatamente embaixo do mouse: sem saltos ao entrar
    // ou ao trocar de nível. Dentro de um nível o mapeamento é absoluto: mouse na coluna X = tempo daquela coluna.
    if (!m_zoom || level != m_level) {
        m_zoom = true;
        m_level = level;
        m_span = span;
        m_win0 = m_dragT - frac * span;
    }
    double t = std::clamp(m_win0 + frac * m_span, 0.0, m_dur);
    if (m_fps > 0 && m_span <= 2.0)
        t = std::clamp(std::round(t * m_fps) / m_fps, 0.0, m_dur); // zoom fino: anda de frame em frame
    applyDrag(t);
    showLens();
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
    const bool was = m_drag != Drag::None;
    // Soltou em cima de uma marca: a barra branca do preview vai para ela (início e fim se comportam igual).
    if (m_drag == Drag::Start) emit seekRequested(m_start);
    else if (m_drag == Drag::End) emit seekRequested(m_end);
    hideHint();
    if (m_zoom) leaveZoom();
    m_drag = Drag::None;
    update();
    if (was) emit dragFinished();
}

} // namespace shear

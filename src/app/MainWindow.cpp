#include "MainWindow.h"
#include "NavKeys.h"
#include "HelpDialog.h"
#include "SettingsDialog.h"
#include "core/CutPlanner.h"
#include "core/FfmpegCommandBuilder.h"
#include "core/OutputNaming.h"
#include "export/ExportJob.h"
#include "platform/Windows.h"
#include "VerticalFader.h"
#include "player/AudioScrubber.h"
#include "player/MpvWidget.h"
#include "timeline/DamageScanner.h"
#include "timeline/KeyframeScanner.h"
#include "timeline/TimelineWidget.h"
#include "util/Metrics.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <functional>
#include <QPalette>
#include <QPixmap>
#include <QWheelEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QAction>
#include <QListWidget>
#include <QMenu>
#include <QWidgetAction>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QSet>
#include <QStyledItemDelegate>
#include "ClipMeta.h"
#include "PluginInstaller.h"
#include "PluginsDialog.h"
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace shear {

// Barra de cima do editor: a imagem do mapa ao fundo, bem apagada, esmaecendo nas pontas. Os botões e textos ficam por cima.
class BannerBar : public QWidget {
public:
    using QWidget::QWidget;
    void setBanner(const QString &path)
    {
        if (path == m_path)
            return;
        m_path = path;
        m_cache = QPixmap();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_path.isEmpty() || width() <= 0 || height() <= 0)
            return;
        if (m_cache.isNull() || m_cache.size() != size()) {
            const QPixmap src(m_path);
            if (src.isNull())
                return;
            const QPixmap big = src.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            m_cache = QPixmap(size());
            m_cache.fill(Qt::transparent);
            QPainter q(&m_cache);
            q.drawPixmap(0, 0, big, (big.width() - width()) / 2, (big.height() - height()) / 2, width(), height());
            q.setCompositionMode(QPainter::CompositionMode_DestinationIn); // opacidade: ~30%, sumindo nas duas pontas
            QLinearGradient g(0, 0, width(), 0);
            g.setColorAt(0.0, QColor(0, 0, 0, 0));
            g.setColorAt(0.15, QColor(0, 0, 0, 80));
            g.setColorAt(0.85, QColor(0, 0, 0, 80));
            g.setColorAt(1.0, QColor(0, 0, 0, 0));
            q.fillRect(m_cache.rect(), g);
        }
        QPainter p(this);
        p.drawPixmap(0, 0, m_cache);
    }

private:
    QString m_path;
    QPixmap m_cache;
};

// Papéis de dados de cada linha da lista de clipes (ver ClipRowDelegate)
enum ClipRole { RoleBanner = Qt::UserRole + 1, RoleMap, RoleTags, RoleComment, RoleDate, RoleFresh, RoleTagHot };

// Linha da lista de clipes, em colunas alinhadas:
//   [ícone do agente]  nome do arquivo ........  data   tags   comentário   [faixa do mapa com o nome dele por cima]
class ClipRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        if (!idx.data(Qt::UserRole).toString().isEmpty())
            s.setHeight(42);
        return s;
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        if (idx.data(Qt::UserRole).toString().isEmpty()) { // linha de aviso ("nenhum clipe..."): desenho normal
            QStyledItemDelegate::paint(p, opt, idx);
            return;
        }
        // fundo, listras e seleção do estilo, sem o texto nem o ícone (isso é desenhado abaixo)
        QStyleOptionViewItem o = opt;
        initStyleOption(&o, idx);
        o.text.clear();
        o.icon = QIcon();
        o.state &= ~QStyle::State_HasFocus;
        QStyle *style = o.widget ? o.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);

        const QRect r = opt.rect;
        const bool sel = opt.state & QStyle::State_Selected;
        const QColor fg = opt.palette.color(sel ? QPalette::HighlightedText : QPalette::Text);
        QColor mute = sel ? fg : opt.palette.color(QPalette::PlaceholderText);
        if (sel) mute.setAlpha(190);
        const QColor accent = sel ? fg : opt.palette.color(QPalette::Highlight).lighter(160);
        const int pad = 12, iconSz = 28;

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setClipRect(r);

        // faixa do mapa (direita), com o nome do mapa por cima
        const int bw = std::min(250, r.width() / 3);
        const QString banner = idx.data(RoleBanner).toString();
        const QString map = idx.data(RoleMap).toString();
        if (!banner.isEmpty())
            p->drawPixmap(r.right() - bw + 1, r.top(), bannerPixmap(banner, bw, r.height()));
        if (!map.isEmpty()) {
            QFont mf = opt.font;
            mf.setBold(true);
            p->setFont(mf);
            const QRect tr(r.right() - bw, r.top(), bw - pad - 2, r.height());
            if (!banner.isEmpty()) { // sombra, para ler bem em cima da imagem
                p->setPen(QColor(0, 0, 0, 190));
                p->drawText(tr.translated(1, 1), Qt::AlignRight | Qt::AlignVCenter, map);
            }
            p->setPen(banner.isEmpty() ? fg : QColor(255, 255, 255));
            p->drawText(tr, Qt::AlignRight | Qt::AlignVCenter, map);
        }

        // colunas de texto, da direita para a esquerda (as larguras são fixas para as linhas ficarem alinhadas)
        const bool wide = r.width() > 760;
        const int xr = r.right() - bw - 6;
        const int commentW = wide ? 190 : 0, tagsW = 46, dateW = 118;
        const int commentX = xr - commentW, tagsX = commentX - tagsW, dateX = tagsX - dateW - 6;
        const int nameX = r.left() + pad + iconSz + 10;
        const QRect vr(0, r.top(), 0, r.height());
        auto col = [&](int x, int w) { return QRect(x, vr.top(), std::max(0, w), vr.height()); };

        // ícone do agente
        const QIcon ic = idx.data(Qt::DecorationRole).value<QIcon>();
        if (!ic.isNull())
            ic.paint(p, QRect(r.left() + pad, r.top() + (r.height() - iconSz) / 2, iconSz, iconSz));

        QFont f = opt.font;
        // nome do arquivo (com "● NOVO · há …" na frente no clipe recém-salvo)
        const QString fresh = idx.data(RoleFresh).toString();
        int nx = nameX;
        if (!fresh.isEmpty()) {
            QFont bf = f;
            bf.setBold(true);
            p->setFont(bf);
            p->setPen(accent);
            const int fw = QFontMetrics(bf).horizontalAdvance(fresh) + 10;
            p->drawText(col(nx, fw), Qt::AlignLeft | Qt::AlignVCenter, fresh);
            nx += fw;
        }
        p->setFont(f);
        p->setPen(fg);
        const int nameW = dateX - 14 - nx;
        p->drawText(col(nx, nameW), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(f).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, std::max(0, nameW)));

        // data
        p->setPen(mute);
        p->drawText(col(dateX, dateW), Qt::AlignLeft | Qt::AlignVCenter, idx.data(RoleDate).toString());
        // tags (5k...)
        const QString tags = idx.data(RoleTags).toString();
        if (!tags.isEmpty()) {
            QFont tf = f;
            tf.setBold(true);
            p->setFont(tf);
            // 5k ou mais: amarelo (as demais seguem a cor de destaque)
            const bool big = idx.data(RoleTagHot).toBool();
            p->setPen(big && !sel ? QColor(255, 204, 51) : accent);
            p->drawText(col(tagsX, tagsW), Qt::AlignLeft | Qt::AlignVCenter, tags);
        }
        // comentário
        const QString comment = idx.data(RoleComment).toString();
        if (!comment.isEmpty() && commentW > 0) {
            QFont cf = f;
            cf.setItalic(true);
            p->setFont(cf);
            p->setPen(mute);
            p->drawText(col(commentX, commentW - 8), Qt::AlignLeft | Qt::AlignVCenter,
                        QFontMetrics(cf).elidedText(comment, Qt::ElideRight, commentW - 8));
        }
        p->restore();
    }

private:
    // imagem do mapa cortada no tamanho da faixa, com um véu escuro e esmaecendo para a esquerda
    static const QPixmap &bannerPixmap(const QString &path, int w, int h)
    {
        static QHash<QString, QPixmap> cache;
        QPixmap &out = cache[path + "|" + QString::number(w) + "x" + QString::number(h)];
        if (out.isNull() && w > 0 && h > 0) {
            const QPixmap src(path);
            if (!src.isNull()) {
                const QPixmap big = src.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                out = QPixmap(w, h);
                out.fill(Qt::transparent);
                QPainter q(&out);
                q.drawPixmap(0, 0, big, (big.width() - w) / 2, (big.height() - h) / 2, w, h);
                q.fillRect(out.rect(), QColor(0, 0, 0, 80)); // véu: o nome do mapa fica legível por cima
                q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                QLinearGradient g(0, 0, w, 0);
                g.setColorAt(0, QColor(0, 0, 0, 0));
                g.setColorAt(0.55, QColor(0, 0, 0, 170));
                g.setColorAt(1, QColor(0, 0, 0, 235));
                q.fillRect(out.rect(), g);
            }
        }
        return out;
    }
};

// Começa a arrastar o arquivo (para o Discord, o Explorer ou o próprio shear): vai como uma lista de arquivos (CopyAction).
static void startFileDrag(QWidget *source, const QString &path)
{
    auto *drag = new QDrag(source);
    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    drag->setMimeData(mime);
    drag->setPixmap(source->grab());
    drag->exec(Qt::CopyAction);
}

// Item do menu de escolha de mapa: a imagem do mapa como fundo (escurecida), o nome por cima. Passar o mouse clareia e
// põe uma borda; o mapa atual tem um traço na esquerda.
class MapMenuItem : public QWidget {
public:
    MapMenuItem(const QString &name, const QString &image, bool current, std::function<void()> onPick, QWidget *parent = nullptr)
        : QWidget(parent), m_name(name), m_image(image), m_current(current), m_onPick(std::move(onPick))
    {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }
    QSize sizeHint() const override { return {250, 40}; }

protected:
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || !rect().contains(e->pos()))
            return;
        if (m_onPick) m_onPick();
        for (QWidget *w = parentWidget(); w; w = w->parentWidget()) // fecha o menu (e o menu-pai, se for o submenu)
            if (auto *m = qobject_cast<QMenu *>(w)) m->close();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(3, 2, -3, -2);
        QPainterPath clip;
        clip.addRoundedRect(r, 5, 5);
        p.setClipPath(clip);
        if (m_bg.isNull() || m_bg.size() != size()) {
            const QPixmap src(m_image);
            m_bg = QPixmap(size());
            m_bg.fill(QColor(40, 40, 44));
            if (!src.isNull()) {
                const QPixmap big = src.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                QPainter q(&m_bg);
                q.drawPixmap(0, 0, big, (big.width() - width()) / 2, (big.height() - height()) / 2, width(), height());
            }
        }
        p.drawPixmap(0, 0, m_bg);
        p.fillRect(rect(), QColor(0, 0, 0, m_hover ? 70 : 135)); // véu escuro: o nome lê bem; mais claro no hover
        if (m_current)
            p.fillRect(QRectF(r.left(), r.top(), 4, r.height()), palette().color(QPalette::Highlight));
        p.setClipping(false);
        if (m_hover) {
            p.setPen(QPen(QColor(255, 255, 255, 190), 1.5));
            p.drawRoundedRect(r, 5, 5);
        }
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRectF tr = r.adjusted(14, 0, -10, 0);
        p.setPen(QColor(0, 0, 0, 190));
        p.drawText(tr.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, m_name); // sombra
        p.setPen(QColor(250, 250, 250));
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, m_name);
    }

private:
    QString m_name, m_image;
    bool m_current, m_hover = false;
    std::function<void()> m_onPick;
    QPixmap m_bg;
};

class LinkLabel : public QLabel { // nome do arquivo: clicável (sublinha só ao passar o mouse), arrastável, com menu de contexto
public:
    std::function<void()> onClick;
    std::function<QString()> dragFile;               // arquivo que vai no arrasto ("" = não arrasta)
    std::function<void(const QPoint &)> onContext;   // posição global do clique direito
    using QLabel::QLabel;

protected:
    void enterEvent(QEnterEvent *e) override
    {
        QFont f = font();
        f.setUnderline(true);
        setFont(f);
        QLabel::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        QFont f = font();
        f.setUnderline(false);
        setFont(f);
        QLabel::leaveEvent(e);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) m_press = e->pos();
        m_dragged = false;
        QLabel::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if ((e->buttons() & Qt::LeftButton) && dragFile &&
            (e->pos() - m_press).manhattanLength() >= QApplication::startDragDistance()) {
            if (const QString path = dragFile(); !path.isEmpty()) {
                m_dragged = true;
                startFileDrag(this, path);
                return;
            }
        }
        QLabel::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (!m_dragged && e->button() == Qt::LeftButton && rect().contains(e->pos()) && onClick)
            onClick();
        m_dragged = false;
        QLabel::mouseReleaseEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent *e) override
    {
        if (onContext) onContext(e->globalPos()); else QLabel::contextMenuEvent(e);
    }

private:
    QPoint m_press;
    bool m_dragged = false;
};

class FileButton : public QPushButton { // nome do clipe exportado: clicável, arrastável, com menu de contexto
public:
    std::function<QString()> dragFile;
    std::function<void(const QPoint &)> onContext;
    using QPushButton::QPushButton;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) m_press = e->pos();
        QPushButton::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if ((e->buttons() & Qt::LeftButton) && dragFile &&
            (e->pos() - m_press).manhattanLength() >= QApplication::startDragDistance()) {
            if (const QString path = dragFile(); !path.isEmpty()) {
                setDown(false);
                startFileDrag(this, path);
                return;
            }
        }
        QPushButton::mouseMoveEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent *e) override
    {
        if (onContext) onContext(e->globalPos()); else QPushButton::contextMenuEvent(e);
    }

private:
    QPoint m_press;
};

// Lista de clipes: arrastar uma linha leva o arquivo (não os dados internos da lista).
class ClipListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        auto *m = new QMimeData;
        QList<QUrl> urls;
        for (const QListWidgetItem *i : items)
            if (const QString p = i->data(Qt::UserRole).toString(); !p.isEmpty())
                urls << QUrl::fromLocalFile(p);
        m->setUrls(urls);
        return m;
    }
};

} // namespace shear

namespace shear {

// Rótulo de uma linha só: o texto que não cabe vira "…" (o texto completo aparece na dica). Não força a largura da janela.
class ElideLabel : public QLabel {
public:
    explicit ElideLabel(QWidget *parent = nullptr) : QLabel(parent)
    {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QString full = text();
        const QString el = fontMetrics().elidedText(full, Qt::ElideRight, width());
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, el);
        const QString tip = el != full ? full : QString();
        if (toolTip() != tip) setToolTip(tip);
    }
};

// Ícones do próprio Windows (Segoe Fluent Icons no Windows 11, Segoe MDL2 Assets no 10). Vazio se não houver.
static QString iconFontFamily()
{
    static const QString family = [] {
        const QStringList all = QFontDatabase::families();
        for (const char *name : {"Segoe Fluent Icons", "Segoe MDL2 Assets"})
            if (all.contains(QString::fromLatin1(name)))
                return QString::fromLatin1(name);
        return QString();
    }();
    return family;
}

// Limite da opção "Até 20 MB": 20 MB de verdade (20.000.000 bytes), não 20 MiB.
static constexpr qint64 kLimitBytes = 20000000;

// Velocidades do preview (botão da barra de cima e teclas - / +).
static const double kSpeeds[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0};

static QString trackName(int i, int n)
{
    if (n >= 2 && i == 0) return "Desktop";
    if (n >= 2 && i == 1) return "Mic";
    return QString("Faixa %1").arg(i + 1);
}

// Clipe mais recente salvo há até 15 s: o app abre direto nele. Se foi há até 5 min: fica destacado no topo da lista.
static constexpr qint64 kFreshOpenSec = 15, kFreshHighlightSec = 300;

static QString formatAgo(qint64 s)
{
    s = std::max<qint64>(s, 0);
    return s < 60 ? QString("%1 s").arg(s) : QString("%1 min").arg(s / 60);
}

static const char *kVideoGlobs[] = {"*.mp4", "*.mkv", "*.mov", "*.webm", "*.avi"};

MainWindow::MainWindow()
{
    m_plugins = new PluginManager(this, this); // os plugins são carregados depois da janela aparecer (deferredInit)
    connect(m_plugins, &PluginManager::pluginLoaded, this, [this](const QString &id) {
        rebuildPluginShortcuts();
        if (!m_file.isEmpty())
            m_plugins->clipOpenedFor(id, {m_file, m_clipCreated, QFileInfo(m_file).size()}, m_timeline->duration());
        if (m_pluginBanner) m_pluginBanner->hide();
        refreshList();
    });
    setWindowTitle("shear");
    resize(1100, 740);
    setAcceptDrops(true);
    m_cfg = Config::load();
    m_rates = loadRates();

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildHome());
    // O editor (com o preview do mpv) só é criado ao abrir o 1º vídeo, para a janela inicial aparecer rápido.
    setCentralWidget(m_stack);

    auto sc = [this](const QKeySequence &k, auto fn) {
        auto *s = new QShortcut(k, this);
        connect(s, &QShortcut::activated, this, fn);
    };
    auto inEditor = [this] { return m_stack->currentIndex() == 1 && !(m_commentEdit && m_commentEdit->hasFocus()); };
    // F5 / Ctrl+R: relê a pasta e atualiza a lista de clipes (só na lista; no editor, Ctrl+R não faz nada)
    sc(Qt::Key_F5, [=, this] { if (m_stack->currentIndex() == 0) refreshRecent(false); });
    sc(QKeySequence(Qt::CTRL | Qt::Key_R), [=, this] { if (m_stack->currentIndex() == 0) refreshRecent(false); });
    sc(Qt::Key_F1, [=, this] { openHelp(); }); // ajuda com todos os atalhos (vale na lista de clipes e no editor)
    sc(Qt::Key_Space, [=, this] { if (inEditor()) userTogglePause(); });
    // X: apaga um trecho do MEIO (1º X = onde começa, 2º X = onde termina); o que sobra é emendado na exportação.
    sc(Qt::Key_X, [=, this] { if (inEditor()) toggleGap(); });
    // Velocidade do preview: - / + (ou =) mais lento / mais rápido; Backspace volta a 1×. Ctrl+0 tira o zoom do vídeo.
    sc(Qt::Key_Minus, [=, this] { if (inEditor()) stepSpeed(-1); });
    sc(Qt::Key_Plus, [=, this] { if (inEditor()) stepSpeed(+1); });
    sc(Qt::Key_Equal, [=, this] { if (inEditor()) stepSpeed(+1); });
    sc(Qt::Key_BracketLeft, [=, this] { if (inEditor()) stepSpeed(-1); });  // [ ] também (como no mpv/VLC)
    sc(Qt::Key_BracketRight, [=, this] { if (inEditor()) stepSpeed(+1); });
    sc(Qt::Key_Backspace, [=, this] { if (inEditor()) setSpeed(1.0); });
    sc(QKeySequence(Qt::CTRL | Qt::Key_0), [=, this] { if (inEditor()) m_mpv->resetZoom(); });
    sc(Qt::Key_I, [=, this] { if (inEditor()) markStart(m_mpv->position()); });
    sc(Qt::Key_O, [=, this] { if (inEditor()) markEnd(m_mpv->position()); });
    // Z e C = I e O (mão esquerda no teclado, junto de A/D e W/S)
    sc(Qt::Key_Z, [=, this] { if (inEditor()) markStart(m_mpv->position()); });
    sc(Qt::Key_C, [=, this] { if (inEditor()) markEnd(m_mpv->position()); });
    // Pular com o teclado (não pausa o vídeo): setas ou A/D = 1 s; Shift = 0,3 s; Ctrl = 5 s; Ctrl+Shift = 15 s.
    // Alt (ou , e .) = 1 quadro. O mapa de teclas fica em NavKeys.cpp (testado em tests/test_navkeys.cpp).
    registerNavShortcuts(this, [this](const NavAction &a) {
        if (m_commentEdit && m_commentEdit->hasFocus()) return; // digitando o comentário
        if (a.kind == NavAction::Kind::JumpSeconds)
            jumpBy(a.seconds);
        else
            stepOneFrame(a.dir);
    });
    sc(Qt::Key_Home, [=, this] { if (inEditor()) m_mpv->seek(0); });
    sc(Qt::Key_End, [=, this] {
        if (inEditor() && m_timeline->duration() > 0) // o último quadro (um quadro antes do fim exato)
            m_mpv->seek(std::max(0.0, m_timeline->duration() - 1.0 / (m_fps > 0 ? m_fps : 30.0)));
    });
    sc(Qt::Key_Up, [=, this] { if (inEditor()) changeVolume(+5); });
    sc(Qt::Key_Down, [=, this] { if (inEditor()) changeVolume(-5); });
    sc(Qt::Key_W, [=, this] { if (inEditor()) changeVolume(+5); }); // W/S = ↑/↓
    sc(Qt::Key_S, [=, this] { if (inEditor()) changeVolume(-5); });
    sc(Qt::Key_J, [=, this] { if (inEditor()) jumpBy(-5); });
    sc(Qt::Key_L, [=, this] { if (inEditor()) jumpBy(+5); });
    // Como no YouTube: 0 a 9 pulam para 0% a 90% do vídeo (continua tocando ou pausado, como estava).
    for (int n = 0; n <= 9; ++n)
        sc(QKeySequence(Qt::Key(int(Qt::Key_0) + n)), [=, this] {
            if (inEditor() && m_timeline->duration() > 0)
                m_mpv->seek(m_timeline->duration() * n / 10.0);
        });
    // Shift+Q: vai para o ponto I. Sem kills/mortes no clipe, Q também (como sempre foi).
    const auto gotoStart = [=, this] { if (inEditor()) m_mpv->seek(m_timeline->start()); };
    // Shift+E: vai para o ponto O; se o vídeo estava rodando, para em cima dele (para ver/ajustar o fim). Sem eventos, E também.
    const auto gotoEnd = [=, this] {
        if (!inEditor()) return;
        if (!m_mpv->paused())
            m_mpv->setPaused(true);
        m_mpv->seek(m_timeline->end());
    };
    sc(QKeySequence(Qt::SHIFT | Qt::Key_Q), gotoStart);
    sc(QKeySequence(Qt::SHIFT | Qt::Key_E), gotoEnd);
    // Q / E: evento (kill ou morte) anterior / próximo, quando o clipe tem eventos do Valorant
    sc(Qt::Key_Q, [=, this] { if (inEditor()) jumpToEvent(-1); });
    sc(Qt::Key_E, [=, this] { if (inEditor()) jumpToEvent(+1); });
    sc(QKeySequence::Undo, [=, this] { if (inEditor()) undo(); });
    sc(QKeySequence::Redo, [=, this] { if (inEditor()) redo(); }); // Ctrl+Y e Ctrl+Shift+Z
    sc(Qt::Key_Return, [=, this] { if (inEditor()) startExport(); });
    sc(Qt::Key_Enter, [=, this] { if (inEditor()) startExport(); });
    // Esc só cancela o trecho que está sendo marcado com X (não volta mais para a lista de clipes: use o botão "← Clipes").
    sc(Qt::Key_Escape, [=, this] {
        if (!inEditor() || m_job || !m_timeline->pendingGap()) return;
        pushUndo(currentState());
        m_timeline->setPendingGap(std::nullopt);
    });
}

QWidget *MainWindow::buildHome()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(32, 28, 32, 24);
    auto *title = new QLabel("<h1>shear</h1>");
    auto *hint = new QLabel("Abra um vídeo, arraste-o para esta janela, ou escolha um clipe recente:");
    auto *open = new QPushButton("Abrir vídeo…");
    auto *settings = new QPushButton("Configurações");
    open->setMinimumHeight(34);
    connect(open, &QPushButton::clicked, this, &MainWindow::promptOpen);
    connect(settings, &QPushButton::clicked, this, &MainWindow::openSettings);
    auto *bar = new QHBoxLayout;
    bar->addWidget(open);
    bar->addWidget(settings);
    bar->addStretch();
    m_recent = new ClipListWidget;
    m_recent->setDragEnabled(true); // arrastar uma linha leva o arquivo (Discord, Explorer, o próprio shear)
    m_recent->setDragDropMode(QAbstractItemView::DragOnly);
    m_recent->setDefaultDropAction(Qt::CopyAction);
    m_recent->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_recent, &QWidget::customContextMenuRequested, this, [this](const QPoint &p) { showListMenu(p); });
    m_recent->setAlternatingRowColors(true);
    m_recent->setIconSize(QSize(28, 28));
    m_recent->setUniformItemSizes(true);
    m_recent->setItemDelegate(new ClipRowDelegate(m_recent));
    connect(m_recent, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *i) { openFile(i->data(Qt::UserRole).toString()); });
    // Cabeçalho: a logo ao lado do nome
    auto *logo = new QLabel;
    {
        const qreal dpr = devicePixelRatioF();
        const int px = int(40 * dpr);
        QPixmap pm = QPixmap(":/logo.png").scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pm.setDevicePixelRatio(dpr);
        logo->setPixmap(pm);
        logo->setFixedSize(44, 40);
    }
    auto *head = new QHBoxLayout;
    head->setSpacing(10);
    head->addWidget(logo);
    head->addWidget(title);
    head->addStretch();
    l->addLayout(head);
    l->addWidget(hint);
    l->addLayout(bar);
    l->addWidget(m_recent, 1);
    return w;
}

QWidget *MainWindow::buildEditor()
{
    auto *w = new QWidget;
    auto *root = new QHBoxLayout(w);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(10);
    m_editorRoot = root;
    auto *l = new QVBoxLayout; // coluna principal: player, timeline, opções, exportar
    root->addLayout(l, 1);

    auto *top = new QHBoxLayout;
    m_topBar = top;
    auto *back = new QPushButton("← Clipes");
    back->setFocusPolicy(Qt::NoFocus);
    connect(back, &QPushButton::clicked, this, [this] { if (!m_job) showHome(); });
    auto *titleLink = new LinkLabel;
    m_title = titleLink;
    titleLink->setCursor(Qt::PointingHandCursor);
    titleLink->setToolTip("Abrir a pasta do arquivo");
    titleLink->onClick = [this] { if (!m_file.isEmpty()) revealInExplorer(m_file); };
    titleLink->dragFile = [this] { return m_file; };
    titleLink->onContext = [this](const QPoint &g) { showTitleMenu(g); };
    m_title->setForegroundRole(QPalette::PlaceholderText); // o nome do arquivo fica em cinza; o mapa/agente/kills é que ficam claros
    // Engrenagem (glifo E713 da fonte de ícones do Windows; sem ela, o emoji ⚙)
    auto *settingsBtn = new QPushButton;
    if (const QString family = iconFontFamily(); !family.isEmpty()) {
        QFont f(family);
        f.setPixelSize(16);
        settingsBtn->setFont(f);
        settingsBtn->setText(QString(QChar(0xE713)));
    } else {
        settingsBtn->setText(QString::fromUcs4(U"⚙"));
    }
    settingsBtn->setToolTip("Configurações");
    settingsBtn->setFixedWidth(38);
    settingsBtn->setFocusPolicy(Qt::NoFocus);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);
    top->addWidget(back);
    top->addWidget(m_title);
    // Textos dos plugins ("· Mapa · Agente · 3k"): discretos; os que o plugin deixa editar são clicáveis (abrem um menu)
    m_chipsBox = new QWidget;
    m_chipsLayout = new QHBoxLayout(m_chipsBox);
    m_chipsLayout->setContentsMargins(0, 0, 0, 0);
    m_chipsLayout->setSpacing(4);
    top->addWidget(m_chipsBox);
    // Comentário opcional (qualquer clipe), no mesmo estilo do mapa/agente/kills: ícone de editar (ou o texto), cinza e
    // clicável; o clique troca por um campo (Enter grava, Esc descarta). Também grava ao voltar para a lista e ao sair.
    m_commentSep = new QLabel("·");
    m_commentSep->setForegroundRole(QPalette::PlaceholderText);
    m_commentBtn = new QToolButton;
    m_commentBtn->setAutoRaise(true);
    m_commentBtn->setFocusPolicy(Qt::NoFocus);
    m_commentBtn->setCursor(Qt::PointingHandCursor);
    {
        QPalette pal = m_commentBtn->palette();
        pal.setColor(QPalette::ButtonText, palette().color(QPalette::PlaceholderText));
        m_commentBtn->setPalette(pal);
    }
    connect(m_commentBtn, &QToolButton::clicked, this, [this] {
        m_commentBtn->hide();
        m_commentEdit->setText(m_comment);
        m_commentEdit->show();
        m_commentEdit->setFocus();
        m_commentEdit->selectAll();
    });
    m_commentEdit = new QLineEdit;
    m_commentEdit->setPlaceholderText("comentário");
    m_commentEdit->setMaxLength(300);
    m_commentEdit->setFixedWidth(320);
    m_commentEdit->setStyleSheet("QLineEdit { border: none; border-bottom: 1px solid palette(mid); background: transparent; }");
    m_commentEdit->hide();
    m_commentEdit->installEventFilter(this); // Esc sai do campo
    connect(m_commentEdit, &QLineEdit::returnPressed, this, [this] { commitComment(); });
    connect(m_commentEdit, &QLineEdit::editingFinished, this, [this] { commitComment(); });
    top->addWidget(m_commentSep);
    top->addWidget(m_commentBtn);
    top->addWidget(m_commentEdit);
    top->addStretch(1);
    // Velocidade do preview: botão com menu (também as teclas - e +, Backspace volta a 1×)
    m_speedBtn = new QToolButton;
    m_speedBtn->setText("1×");
    m_speedBtn->setToolTip("Velocidade do preview. Botão direito volta a 1×.");
    m_speedBtn->setContextMenuPolicy(Qt::CustomContextMenu); // botão direito = velocidade normal
    connect(m_speedBtn, &QWidget::customContextMenuRequested, this, [this] { setSpeed(1.0); });
    m_speedBtn->installEventFilter(this); // roda do mouse sobre o botão também muda a velocidade
    m_speedBtn->setPopupMode(QToolButton::InstantPopup);
    m_speedBtn->setStyleSheet("QToolButton::menu-indicator { image: none; }");
    m_speedBtn->setFocusPolicy(Qt::NoFocus);
    m_speedBtn->setFixedWidth(46);
    auto *speedMenu = new QMenu(m_speedBtn);
    for (double s : kSpeeds) {
        auto *a = speedMenu->addAction(QString::number(s, 'g', 3).replace('.', ',') + "×");
        connect(a, &QAction::triggered, this, [this, s] { setSpeed(s); });
    }
    m_speedBtn->setMenu(speedMenu);
    top->addWidget(m_speedBtn);
    top->addWidget(settingsBtn);
    // a barra de cima ganha a imagem do mapa ao fundo (BannerBar)
    m_topBanner = new BannerBar;
    top->setContentsMargins(6, 3, 6, 3);
    m_topBanner->setLayout(top);
    l->addWidget(m_topBanner);

    // Sugestão de plugin: aparece ao abrir um clipe cujo nome/caminho combina com um plugin que ainda não está instalado
    m_pluginBanner = new QFrame;
    m_pluginBanner->setObjectName("pluginBanner");
    m_pluginBanner->setStyleSheet("#pluginBanner { background: rgba(60, 110, 200, 45); border-radius: 6px; }");
    m_pluginBanner->hide();
    auto *pbl = new QHBoxLayout(m_pluginBanner);
    pbl->setContentsMargins(10, 4, 10, 4);
    m_pluginBannerText = new QLabel;
    m_pluginBannerText->setWordWrap(true);
    m_pbInstall = new QPushButton("Instalar");
    m_pbLater = new QPushButton("Agora não");
    m_pbNever = new QPushButton("Não perguntar de novo");
    for (auto *b : {m_pbInstall, m_pbLater, m_pbNever}) b->setFocusPolicy(Qt::NoFocus);
    pbl->addWidget(m_pluginBannerText, 1);
    pbl->addWidget(m_pbInstall);
    pbl->addWidget(m_pbLater);
    pbl->addWidget(m_pbNever);
    l->addWidget(m_pluginBanner);
    connect(m_pbLater, &QPushButton::clicked, this, [this] {
        m_sessionDismissed.insert(m_bannerPlugin);
        m_pluginBanner->hide();
    });
    connect(m_pbNever, &QPushButton::clicked, this, [this] {
        PluginManager::setSuggestionDismissed(m_bannerPlugin, true);
        m_pluginBanner->hide();
        m_status->setText("Você pode instalar depois em Configurações → Plugins…");
    });
    connect(m_pbInstall, &QPushButton::clicked, this, [this] {
        if (!m_installer) {
            m_installer = new PluginInstaller(this);
            connect(m_installer, &PluginInstaller::status, this, [this](const QString &t) { m_pluginBannerText->setText(t); });
            connect(m_installer, &PluginInstaller::finished, this, [this](const QString &id, bool ok, const QString &error) {
                m_pbInstall->setEnabled(true);
                if (!ok) {
                    m_pluginBannerText->setText("Não foi possível instalar: " + error);
                    m_pbInstall->setText("Tentar de novo");
                    return;
                }
                QString err;
                const auto c = m_plugins->commitStaged(id, &err);
                if (c == PluginManager::Commit::Failed) {
                    m_pluginBannerText->setText("Não foi possível instalar: " + err);
                    m_pbInstall->setText("Tentar de novo");
                } else {
                    m_pluginBanner->hide(); // carregado: os chips, as marcas e os atalhos já aparecem (pluginLoaded)
                    m_status->setText("Plugin instalado.");
                }
            });
        }
        if (m_installer->busy()) return;
        m_pbInstall->setEnabled(false);
        m_installer->install(m_bannerPlugin);
    });

    m_mpv = new MpvWidget(m_cfg.gpuDecode);
    m_scrub = new AudioScrubber(this);
    m_mpv->setMinimumHeight(240);
    l->addWidget(m_mpv, 1);

    m_timeline = new TimelineWidget;
    m_timeline->setToolTip("Arraste para posicionar. Puxe o mouse para baixo durante o arrasto para ampliar (mais fundo = mais preciso).");
    l->addWidget(m_timeline);

    // Rodapé: controles à esquerda e o mixer de áudio, compacto, à direita (o player ocupa a largura toda)
    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(12);
    auto *bl = new QVBoxLayout;
    bottom->addLayout(bl, 1);

    // Linha de tempos + aviso do início antecipado (linha única, altura fixa: o player não se mexe)
    m_times = new QLabel;
    m_early = new QLabel(" ");
    m_early->setStyleSheet("color: #ffa028; font-weight: 600;");
    m_early->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *timesRow = new QHBoxLayout;
    timesRow->addWidget(m_times);
    timesRow->addStretch();
    timesRow->addWidget(m_early);
    bl->addLayout(timesRow);

    // --- Painel de áudio: um botão por faixa + um fader de volume só para o Mic (o desktop não precisa de ajuste) ---
    {
        const QPalette pal = palette();
        const QString accent = pal.color(QPalette::Highlight).name();
        const QString onAccent = pal.color(QPalette::HighlightedText).name();
        const QString text = pal.color(QPalette::WindowText).name();
        const QString mid = pal.color(QPalette::Mid).name();
        const QString base = pal.color(QPalette::Base).name();
        QColor dimc = pal.color(QPalette::WindowText);
        dimc.setAlpha(150);
        const QString dim = QString("rgba(%1,%2,%3,%4)").arg(dimc.red()).arg(dimc.green()).arg(dimc.blue()).arg(dimc.alpha());

        m_audioBox = new QFrame;
        m_audioBox->setObjectName("audioPanel");
        m_audioBox->setStyleSheet(QString(R"(
            #audioPanel { background: %1; border: 1px solid %2; border-radius: 10px; }
            #audioPanel QLabel { background: transparent; }
            #audioPanel QLabel[role="cap"] { color: %3; font-size: 11px; }
            #audioPanel QToolButton[role="pill"] { border: 1px solid %2; border-radius: 12px; padding: 5px 8px;
                                                  background: transparent; color: %3; }
            #audioPanel QToolButton[role="pill"]:checked { background: %5; border-color: %5; color: %6; font-weight: 600; }
            #audioPanel QToolButton[role="val"] { border: none; border-radius: 4px; padding: 2px 6px;
                                                 background: transparent; color: %4; font-weight: 600; }
            #audioPanel QToolButton[role="val"]:hover { background: %2; }
            #audioPanel QToolButton[role="val"]:disabled { color: %3; }
        )").arg(base, mid, dim, text, accent, onAccent));

        auto *pv = new QVBoxLayout(m_audioBox);
        pv->setContentsMargins(8, 8, 8, 8);
        pv->setSpacing(0);
        m_audioRows = new QVBoxLayout;
        m_audioRows->setSpacing(6);
        pv->addLayout(m_audioRows, 1);
        m_audioBox->hide();
        root->addWidget(m_audioBox);
    }
    m_volume = QSettings().value("player/volume", 100).toInt();
    m_mpv->setVolume(m_volume);
    m_scrub->setVolume(m_volume);
    connect(m_mpv, &MpvWidget::volumeWheel, this, [this](int d) { changeVolume(d); });
    m_audioTimer = new QTimer(this);
    m_audioTimer->setSingleShot(true);
    m_audioTimer->setInterval(150);
    connect(m_audioTimer, &QTimer::timeout, this, &MainWindow::applyPreviewAudio);

    // Uma linha de opções + uma de resultado (altura fixa), para o player ficar o maior possível.
    auto *opts = new QHBoxLayout;
    opts->setSpacing(10);
    // Seletor segmentado (dois botões lado a lado), em vez de menu dropdown
    auto *seg = new QWidget;
    auto *segLayout = new QHBoxLayout(seg);
    segLayout->setContentsMargins(0, 0, 0, 0);
    segLayout->setSpacing(0);
    m_fastBtn = new QToolButton;
    m_fastBtn->setText("Rápido");
    m_fastBtn->setToolTip("Corte instantâneo. O início encaixa nas marcas da timeline.");
    m_exactBtn = new QToolButton;
    m_exactBtn->setText("Exato");
    m_exactBtn->setToolTip("Corta exatamente no frame escolhido. A exportação demora mais.");
    m_fastBtn->setProperty("seg", "left");
    m_exactBtn->setProperty("seg", "right");
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    for (auto *b : {m_fastBtn, m_exactBtn}) {
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        m_modeGroup->addButton(b);
        segLayout->addWidget(b);
    }
    {
        const QPalette pal = palette();
        seg->setStyleSheet(QString(R"(
            QToolButton { border: 1px solid %1; padding: 5px 16px; background: transparent; color: %2; }
            QToolButton[seg="left"]  { border-top-left-radius: 7px;  border-bottom-left-radius: 7px;  border-right-width: 0; }
            QToolButton[seg="right"] { border-top-right-radius: 7px; border-bottom-right-radius: 7px; }
            QToolButton:checked { background: %3; border-color: %3; color: %4; font-weight: 600; }
        )").arg(pal.color(QPalette::Mid).name(), pal.color(QPalette::WindowText).name(),
                pal.color(QPalette::Highlight).name(), pal.color(QPalette::HighlightedText).name()));
    }
    m_warning = new ElideLabel; // resumo do que vai acontecer (uma linha; a dica mostra o texto inteiro)
    m_eta = new QLabel;
    m_eta->setToolTip("Previsão do tempo de exportação, calibrada com as suas exportações anteriores.");
    m_exportBtn = new QPushButton("Exportar (Enter)");
    m_exportBtn->setFixedSize(150, 30);
    m_exportBtn->setFocusPolicy(Qt::NoFocus);
    // Comprimir para caber em 20 MB (lembra a última escolha)
    m_limitCheck = new QCheckBox("Até 20 MB");
    m_limitCheck->setToolTip("Comprime o vídeo para o arquivo ficar com até 20 MB. Reduz a qualidade e, se o trecho for longo, "
                             "a resolução. O vídeo é recodificado (o modo fica em Exato).");
    m_limitCheck->setFocusPolicy(Qt::NoFocus);
    m_limitCheck->setChecked(m_cfg.limit20); // o padrão vem das Configurações; cada vídeo começa com ele
    connect(m_limitCheck, &QCheckBox::toggled, this, [this] { updateInfo(); });
    opts->addWidget(seg);
    opts->addWidget(m_limitCheck);
    opts->addWidget(m_warning, 1);
    opts->addWidget(m_eta);
    opts->addWidget(m_exportBtn);
    bl->addLayout(opts);

    auto *rowC = new QWidget; // resultado/progresso: altura reservada, nada pula quando aparece ou some
    rowC->setFixedHeight(30);
    auto *ex = new QHBoxLayout(rowC);
    ex->setContentsMargins(0, 0, 0, 0);
    m_status = new ElideLabel;
    m_bar = new QProgressBar;
    m_bar->setRange(0, 1000);
    m_bar->hide();
    m_cancelBtn = new QPushButton("Cancelar");
    m_cancelBtn->hide();
    // Copiar: só um ícone pequeno (glifo "Copiar" da fonte de ícones do Windows; sem ela, o emoji 📋).
    m_copyBtn = new QPushButton;
    if (const QString family = iconFontFamily(); !family.isEmpty()) {
        QFont f(family);
        f.setPixelSize(15);
        m_copyBtn->setFont(f);
        m_copyBtn->setText(QString(QChar(0xE8C8)));
    } else {
        m_copyBtn->setText(QString::fromUcs4(U"📋"));
    }
    m_copyBtn->setToolTip("Copiar o clipe para a área de transferência (Ctrl+V)");
    m_copyBtn->setFocusPolicy(Qt::NoFocus);
    m_keepBtn = new QPushButton("Manter clipe");
    m_keepBtn->setToolTip("Move o clipe para a pasta de clipes mantidos e impede que seja apagado ao fechar.\n"
                          "Segure Shift para manter o clipe e já apagar o vídeo original.");
    // Com Shift segurado o botão vira "Manter e apagar original". Largura fixa: nada pula quando o texto troca.
    m_keepBtn->setMinimumWidth(m_keepBtn->fontMetrics().horizontalAdvance("Manter e apagar original") + 28);
    auto *shiftPoll = new QTimer(this);
    shiftPoll->setInterval(60);
    connect(shiftPoll, &QTimer::timeout, this, [this] {
        if (!m_keepBtn->isVisible()) return;
        const bool shift = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
        const QString want = shift ? "Manter e apagar original" : "Manter clipe";
        if (m_keepBtn->text() != want) m_keepBtn->setText(want);
    });
    shiftPoll->start();
    // À vista: Copiar, Manter e (enquanto o clipe é temporário) apagar tudo. Abrir a pasta = clicar no nome do arquivo.
    m_deleteBothBtn = new QPushButton("Apagar original e clipe");
    m_deleteOrigBtn = new QPushButton("Apagar original"); // aparece depois de Manter
    m_deleteOrigBtn->setToolTip("Manda o vídeo original para a Lixeira (o clipe mantido fica).");
    connect(m_deleteOrigBtn, &QPushButton::clicked, this, &MainWindow::deleteOriginalOnly);
    m_deleteBothBtn->setToolTip("Manda o vídeo original para a Lixeira e apaga o clipe cortado.");
    auto *fileLink = new FileButton;
    m_fileLink = fileLink;
    m_fileLink->setFlat(true);
    m_fileLink->setCursor(Qt::PointingHandCursor);
    m_fileLink->setFocusPolicy(Qt::NoFocus);
    m_fileLink->setToolTip("Abrir o clipe no shear (botão direito: abrir local do arquivo)");
    m_fileLink->setStyleSheet("QPushButton { border: none; padding: 0 2px; } QPushButton:hover { text-decoration: underline; }");
    fileLink->dragFile = [this] { return m_lastOutput; };
    fileLink->onContext = [this](const QPoint &g) {
        if (m_lastOutput.isEmpty()) return;
        QMenu menu(this);
        auto *reveal = menu.addAction("Abrir local do arquivo");
        connect(reveal, &QAction::triggered, this, [this] { revealInExplorer(m_lastOutput); });
        menu.exec(g);
    };
    m_fileLink->hide();
    connect(m_fileLink, &QPushButton::clicked, this, [this] {
        if (m_job || m_lastOutput.isEmpty() || !QFileInfo::exists(m_lastOutput)) return;
        // clipe ainda temporário (não mantido): abrir no shear não pode fazê-lo sumir ao voltar para a lista
        m_tempFiles.removeAll(m_lastOutput);
        openFile(m_lastOutput);
    });
    connect(m_deleteBothBtn, &QPushButton::clicked, this, &MainWindow::deleteBoth);
    hideResultButtons();
    ex->addWidget(m_fileLink);
    ex->addWidget(m_status, 1);
    ex->addWidget(m_bar, 1);
    ex->addWidget(m_cancelBtn);
    ex->addWidget(m_copyBtn);
    ex->addWidget(m_keepBtn);
    ex->addWidget(m_deleteBothBtn);
    ex->addWidget(m_deleteOrigBtn);
    bl->addWidget(rowC);
    l->addLayout(bottom);

    // preencher com os padrões
    setCutMode(m_cfg.mode);

    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::startExport);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::cancelExport);
    connect(m_copyBtn, &QPushButton::clicked, this, [this] { copyFileToClipboard(m_lastOutput); });
    // Vale o que o botão mostra (ou o Shift no instante do clique): o que você vê é o que acontece.
    connect(m_keepBtn, &QPushButton::clicked, this, [this] {
        const bool shift = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier) ||
                           m_keepBtn->text() != "Manter clipe";
        keepLastOutput(shift);
    });
    // Botões do resultado todos do mesmo tamanho (o do maior texto).
    {
        const QFontMetrics fm = m_keepBtn->fontMetrics();
        const int w = std::max(fm.horizontalAdvance("Manter e apagar original"), fm.horizontalAdvance("Apagar original e clipe")) + 28;
        for (QPushButton *b : {m_cancelBtn, m_keepBtn, m_deleteBothBtn, m_deleteOrigBtn})
            b->setFixedSize(w, 28);
        m_copyBtn->setFixedSize(34, 28);
    }
    connect(m_modeGroup, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool on) { if (on) onModeChanged(); });
    onModeChanged();
    connect(m_mpv, &MpvWidget::fileLoaded, this, &MainWindow::applyPreviewAudio);

    connect(m_mpv, &MpvWidget::positionChanged, m_timeline, &TimelineWidget::setPosition);
    // Vídeo com começo corrompido: só mostra a imagem quando o seek para o trecho bom TERMINAR (a posição do mpv
    // já muda no início do seek, e o quadro que aparece antes ainda é o do trecho quebrado).
    connect(m_mpv, &MpvWidget::playbackRestarted, this, [this] {
        if (m_holdVideo && m_holdUntilPos > 0 && m_mpv->position() >= m_holdUntilPos - 0.05)
            QTimer::singleShot(40, this, [this] { releaseVideoHold(); });
    });
    connect(m_mpv, &MpvWidget::durationChanged, this, [this](double d) {
        m_timeline->setDuration(d);
        if (m_plugins) m_plugins->durationChanged(d);
        applySuggestedRange();
        updateInfo();
    });
    connect(m_mpv, &MpvWidget::firstFrame, this, [this] {
        metrics::log(QString("primeiro frame: %1 ms após abrir o arquivo").arg(m_openClock.elapsed()));
        // Plugins: só depois do primeiro quadro (nada disso atrasa a abertura do vídeo)
        QTimer::singleShot(0, this, [this] { notifyClipOpened(); });
    });
    connect(m_timeline, &TimelineWidget::dragStarted, this, [this] { m_commentEdit->clearFocus(); }); // sai do campo de comentário
    connect(m_timeline, &TimelineWidget::seekRequested, this, [this](double t) {
        m_mpv->seek(t);
        // Arrastando (vídeo parado): o som sai de um player só de áudio, que segue o mouse em qualquer velocidade.
        if (m_scrubbing && m_cfg.scrubAudio) {
            // Quanto maior o zoom, mais curto o trecho de som: sem zoom, depois cada nível (20 s, 5 s, 1,5 s, 0,4 s).
            static const int kSoundMs[] = {160, 130, 100, 75, 50};
            m_scrub->scrub(t, kSoundMs[std::clamp(m_timeline->zoomLevel() + 1, 0, 4)]);
        }
    });
    connect(m_mpv, &MpvWidget::clicked, this, &MainWindow::userTogglePause); // clicar no vídeo = play/pause
    connect(m_timeline, &TimelineWidget::gapRightClicked, this, &MainWindow::removeGapAt);
    // Tocando e chegou num trecho apagado: pula para o fim dele (o preview mostra o que vai ficar no arquivo).
    connect(m_mpv, &MpvWidget::positionChanged, this, [this](double p) {
        if (m_mpv->paused()) return; // parado: dá para olhar dentro do trecho
        for (const auto &g : m_timeline->gaps())
            if (p >= g.first && p < g.second - 0.02 && p >= m_timeline->start() && p <= m_timeline->end()) {
                m_mpv->seek(g.second);
                break;
            }
    });
    // Tocando e chegou na marca de FIM (O): para, como se o vídeo tivesse acabado (Espaço/play recomeça do I).
    // Só vale para a reprodução normal (passo curto de tempo); pular por cima do O com as setas não para nada.
    connect(m_mpv, &MpvWidget::positionChanged, this, [this](double p) {
        const double prev = m_prevPos;
        m_prevPos = p;
        const double end = m_timeline->end();
        if (m_mpv->paused() || end <= 0 || end >= m_mpv->duration() - 0.05) return; // O no fim do vídeo: o próprio fim vale
        if (prev < end && p >= end && p - prev < 0.5)
            m_mpv->setPaused(true);
    });
    connect(m_mpv, &MpvWidget::fpsChanged, m_timeline, &TimelineWidget::setFps);
    connect(m_mpv, &MpvWidget::fpsChanged, this, [this](double f) { m_fps = f; });
    connect(m_timeline, &TimelineWidget::startChanged, this, [this] { updateInfo(); });
    connect(m_timeline, &TimelineWidget::endChanged, this, [this] { updateInfo(); });
    // Enquanto o mouse está pressionado na timeline o vídeo fica pausado (a timeline pede o seek a cada movimento,
    // então você vê o quadro sob o mouse). Ao soltar, volta a tocar se estava tocando.
    // Roda do mouse na timeline = setas: 1 s por clique (Shift 0,3 s, Ctrl 5 s, Ctrl+Shift 15 s, Alt 1 quadro).
    // Para cima avança, para baixo volta (como W/S e as setas ↑/↓ aumentam).
    connect(m_timeline, &TimelineWidget::scrolled, this, [this](int notches, Qt::KeyboardModifiers m) {
        if (m & Qt::AltModifier) {
            for (int i = 0; i < std::abs(notches); ++i)
                stepOneFrame(notches > 0 ? +1 : -1);
            return;
        }
        const bool ctrl = m & Qt::ControlModifier, shift = m & Qt::ShiftModifier;
        jumpBy(notches * (ctrl && shift ? kJumpCtrlShift : ctrl ? kJumpCtrl : shift ? kJumpShift : kJumpPlain));
    });
    connect(m_timeline, &TimelineWidget::dragStarted, this, [this] {
        m_dragSnapshot = currentState();
        m_resumeAfterDrag = !m_mpv->paused();
        if (m_resumeAfterDrag)
            m_mpv->setPaused(true);
        m_scrubbing = true;
    });
    connect(m_timeline, &TimelineWidget::dragFinished, this, [this] {
        m_scrubbing = false;
        m_scrub->stop();
        const EditState now = currentState(); // moveu um marcador? então dá para desfazer
        if (std::abs(now.start - m_dragSnapshot.start) > 1e-6 || std::abs(now.end - m_dragSnapshot.end) > 1e-6)
            pushUndo(m_dragSnapshot);
        if (m_resumeAfterDrag)
            m_mpv->setPaused(false);
        m_resumeAfterDrag = false;
    });
    return w;
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    if (m_shownOnce) return;
    m_shownOnce = true;
    metrics::log(QString("janela exibida: %1 ms após criação do processo").arg(metrics::processAgeMs(), 0, 'f', 0));
    QTimer::singleShot(0, this, [this] {
        metrics::log(QString("janela pintada: %1 ms após criação do processo").arg(metrics::processAgeMs(), 0, 'f', 0));
        deferredInit();
    });
}

void MainWindow::deferredInit()
{
    cleanupTemp(true); // sobras de uma sessão anterior que travou
    migrateLegacyContextMenu(QCoreApplication::applicationFilePath()); // menu de contexto da versão antiga
    clipmeta::migrateFromValorant(); // comentários que a versão anterior guardava no clips.json do Valorant
    m_plugins->loadAll();            // plugins instalados (a lista de clipes já usa os dados deles)
    rebuildPluginShortcuts();
    refreshRecent(m_autoOpenFresh);
    detectNvenc();
    if (!m_file.isEmpty()) notifyClipOpened(); // clipe aberto antes de os plugins carregarem
}

// ---------------------------------------------------------------------------------------------------------------------
// Comentário (shear base)

void MainWindow::refreshCommentUi()
{
    if (!m_commentBtn) return;
    // o campo só existe enquanto você digita; fora disso, o ícone ✎ (ou o texto do comentário)
    if (!m_commentEdit->hasFocus()) {
        m_commentEdit->hide();
        m_commentBtn->show();
    }
    if (m_comment.isEmpty()) { // só o ícone de editar (glifo E70F da fonte de ícones do Windows; sem ela, ✎)
        if (const QString family = iconFontFamily(); !family.isEmpty()) {
            QFont f(family);
            f.setPixelSize(13);
            m_commentBtn->setFont(f);
            m_commentBtn->setText(QString(QChar(0xE70F)));
        } else {
            m_commentBtn->setFont(font());
            m_commentBtn->setText(QString::fromUcs4(U"✎"));
        }
        m_commentBtn->setToolTip("Clique para adicionar um comentário");
    } else {
        m_commentBtn->setFont(font());
        m_commentBtn->setText(QFontMetrics(font()).elidedText(m_comment, Qt::ElideRight, 320));
        m_commentBtn->setToolTip(m_comment);
    }
    QPalette pal = m_commentBtn->palette();
    const QColor c = palette().color(m_comment.isEmpty() ? QPalette::PlaceholderText : QPalette::WindowText);
    for (const auto role : {QPalette::ButtonText, QPalette::WindowText, QPalette::Text})
        pal.setColor(role, c);
    m_commentBtn->setPalette(pal);
    m_commentSep->setVisible(true);
}

// Grava o comentário que está sendo digitado (Enter, sair do campo, voltar para a lista, abrir outro clipe ou fechar o app).
// Sem campo aberto, não faz nada. (O arquivo é pequeno: grava na hora, então ao fechar nada se perde.)
void MainWindow::commitComment(bool /*wait*/)
{
    if (!m_commentEdit || !m_commentEdit->isVisible()) return;
    const QString c = m_commentEdit->text().trimmed();
    m_commentEdit->hide(); // antes de gravar, para o refreshCommentUi já mostrar o ícone no lugar do campo
    if (c != m_comment && !m_file.isEmpty()) {
        m_comment = c;
        const QFileInfo fi(m_file);
        clipmeta::setComment(clipmeta::key(fi.fileName(), fi.size()), c);
    }
    refreshCommentUi();
    m_commentEdit->clearFocus();
}

// ---------------------------------------------------------------------------------------------------------------------
// Plugins: o que o shear oferece (IHostBackend) e como mostra o que eles mandam

double MainWindow::position() const { return m_mpv ? m_mpv->position() : 0.0; }
double MainWindow::duration() const { return m_timeline ? m_timeline->duration() : 0.0; }
QString MainWindow::dataDir() const { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation); }
void MainWindow::log(const QString &line) { metrics::log(line); }
void MainWindow::showStatus(const QString &text) { m_status->setText(text); }
void MainWindow::pushUndo() { pushUndo(currentState()); }

void MainWindow::setInfoChips(const QString &pluginId, const QVector<plugin::InfoChip> &chips)
{
    for (ChipSet &set : m_chipSets)
        if (set.pluginId == pluginId) {
            set.chips = chips;
            rebuildChips();
            return;
        }
    m_chipSets.push_back({pluginId, chips});
    rebuildChips();
}

void MainWindow::setBackdrop(const QString &pluginId, const QString &imagePath)
{
    m_backdrops.insert(pluginId, imagePath);
    QString first;
    for (const QString &p : std::as_const(m_backdrops))
        if (!p.isEmpty()) { first = p; break; }
    if (m_topBanner) static_cast<BannerBar *>(m_topBanner)->setBanner(first);
}

void MainWindow::setMarkers(const QString &pluginId, const QVector<plugin::Marker> &markers)
{
    m_markers.insert(pluginId, markers);
    std::vector<TimelineWidget::Event> all;
    for (const auto &v : std::as_const(m_markers))
        for (const plugin::Marker &m : v)
            all.push_back({m.t, m.primary});
    m_timeline->setEvents(std::move(all));
}

void MainWindow::suggestRange(const QString &, double start, double end)
{
    m_suggested = std::make_pair(start, end);
    applySuggestedRange();
}

void MainWindow::refreshList()
{
    if (m_listRefreshQueued) return; // junta vários pedidos num só redesenho
    m_listRefreshQueued = true;
    QTimer::singleShot(300, this, [this] {
        m_listRefreshQueued = false;
        if (m_stack->currentIndex() == 0) refreshRecent(false);
    });
}

// I e O sugeridos por um plugin (ex.: o último round capturado). Só mexe se você ainda não mexeu em I/O/trechos; se o plugin
// sugerir outro depois (ex.: recalibrou), refaz, desde que I/O ainda sejam os que o shear pôs. Ctrl+Z desfaz.
void MainWindow::applySuggestedRange()
{
    const double dur = m_timeline->duration();
    if (!m_suggested || dur <= 0 || m_job)
        return;
    const double s0 = m_timeline->start(), e0 = m_timeline->end();
    const bool untouched =
        m_timeline->gaps().empty() && !m_timeline->pendingGap() &&
        (m_autoRange ? (std::abs(s0 - m_autoRange->first) < 0.05 && std::abs(e0 - m_autoRange->second) < 0.05)
                     : (s0 <= 0.01 && e0 >= dur - 0.01));
    if (!untouched)
        return;
    // Modo Rápido: o início só existe em keyframes; encaixa nele para não aparecer o aviso "o clipe vai começar X s antes".
    // (Os keyframes chegam aos poucos: esta função é chamada de novo a cada lote, e o encaixe se refina.) O início também
    // não pode cair no trecho corrompido do começo.
    const double start = snapIfFast(std::max(m_suggested->first, m_timeline->damagedUntil()));
    const double end = std::min(m_suggested->second, dur);
    if (std::abs(start - s0) < 0.001 && std::abs(end - e0) < 0.001)
        return;
    const std::optional<std::pair<double, double>> prev = m_autoRange;
    if (!prev)
        pushUndo(currentState()); // um só Ctrl+Z desfaz (os refinamentos seguintes não empilham)
    m_timeline->setRange(start, end);
    // Ao entrar no vídeo ele começa no I (e não no começo do arquivo). Só se o ponto de reprodução ainda estiver antes do I e
    // você não tiver ido a outro lugar: na primeira aplicação, ou se ele ainda estava parado no I anterior (refino do encaixe).
    const double pos = m_mpv->position();
    if (pos < m_timeline->start() - 0.001 && (!prev || pos >= prev->first - 0.05)) {
        m_mpv->seek(m_timeline->start());
        if (!prev && !m_userPaused && m_mpv->paused())
            m_mpv->setPaused(false); // entra tocando (a não ser que você mesmo tenha pausado)
    }
    m_autoRange = std::make_pair(m_timeline->start(), m_timeline->end());
    metrics::log(QString("I/O sugeridos por plugin: %1 a %2 s").arg(m_timeline->start(), 0, 'f', 1).arg(m_timeline->end(), 0, 'f', 1));
    updateInfo();
}

// Refaz os textos ao lado do título a partir do que os plugins mandaram.
void MainWindow::rebuildChips()
{
    if (!m_chipsLayout) return;
    while (QLayoutItem *it = m_chipsLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
    for (const ChipSet &set : std::as_const(m_chipSets))
        for (const plugin::InfoChip &chip : set.chips) {
            auto *sep = new QLabel("·");
            sep->setForegroundRole(QPalette::PlaceholderText);
            auto *btn = new QToolButton;
            btn->setAutoRaise(true);
            btn->setFocusPolicy(Qt::NoFocus);
            btn->setText(chip.text.isEmpty() ? chip.placeholder : chip.text);
            QPalette pal = btn->palette();
            // texto claro quando tem valor; cinza (e itálico) só nas dicas ("mapa", "agente", "kills"...)
            const QColor c = palette().color(chip.text.isEmpty() ? QPalette::PlaceholderText : QPalette::WindowText);
            for (const auto role : {QPalette::ButtonText, QPalette::WindowText, QPalette::Text})
                pal.setColor(role, c);
            btn->setPalette(pal);
            QFont f = btn->font();
            f.setItalic(chip.text.isEmpty());
            btn->setFont(f);
            if (!chip.iconPath.isEmpty()) {
                btn->setIcon(QIcon(chip.iconPath));
                btn->setIconSize(QSize(20, 20));
                btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            }
            btn->setToolTip(chip.tooltip);
            btn->setAttribute(Qt::WA_TransparentForMouseEvents, !chip.editable);
            btn->setCursor(chip.editable ? Qt::PointingHandCursor : Qt::ArrowCursor);
            if (chip.editable)
                connect(btn, &QToolButton::clicked, this, [this, pid = set.pluginId, chip, btn] { showChipMenu(pid, chip, btn); });
            m_chipsLayout->addWidget(sep);
            m_chipsLayout->addWidget(btn);
        }
}

// Menu de escolha de um chip (o plugin diz o que mostrar; o shear desenha e devolve a escolha).
void MainWindow::showChipMenu(const QString &pluginId, const plugin::InfoChip &chip, QToolButton *btn)
{
    const plugin::ChoiceMenu cm = m_plugins->chipMenu(pluginId, chip.id);
    QMenu menu(this);
    auto choose = [this, pluginId, id = chip.id](const QString &value) { m_plugins->chipChosen(pluginId, id, value); };
    if (!cm.clearTitle.isEmpty()) {
        connect(menu.addAction(cm.clearTitle), &QAction::triggered, this, [choose] { choose(QString()); });
        menu.addSeparator();
    }
    auto addChoice = [&](QMenu *m, const plugin::Choice &c) {
        const QString label = c.label.isEmpty() ? c.value : c.label;
        if (!c.bannerPath.isEmpty()) { // item com a imagem de fundo e o nome por cima
            auto *wa = new QWidgetAction(m);
            wa->setDefaultWidget(new MapMenuItem(label, c.bannerPath, c.checked, [choose, v = c.value] { choose(v); }));
            m->addAction(wa);
            return;
        }
        auto *a = m->addAction(label);
        if (!c.iconPath.isEmpty()) a->setIcon(QIcon(c.iconPath));
        a->setCheckable(true);
        a->setChecked(c.checked);
        connect(a, &QAction::triggered, this, [choose, v = c.value] { choose(v); });
    };
    for (const plugin::Choice &c : cm.items)
        addChoice(&menu, c);
    if (!cm.moreItems.isEmpty()) {
        QMenu *more = menu.addMenu(cm.moreTitle);
        for (const plugin::Choice &c : cm.moreItems)
            addChoice(more, c);
    }
    if (!cm.customTitle.isEmpty()) {
        if (!menu.isEmpty()) menu.addSeparator();
        connect(menu.addAction(cm.customTitle), &QAction::triggered, this, [this, cm, chip, choose] {
            bool ok = false;
            const QString t = QInputDialog::getText(this, chip.placeholder, cm.customPrompt, QLineEdit::Normal,
                                                    cm.customCurrent, &ok).trimmed();
            if (ok) choose(t);
        });
    }
    menu.exec(btn->mapToGlobal(btn->rect().bottomLeft()));
}

// Atalhos que os plugins registram (só valem no editor e não enquanto você digita o comentário).
void MainWindow::rebuildPluginShortcuts()
{
    qDeleteAll(m_pluginShortcuts);
    m_pluginShortcuts.clear();
    if (!m_plugins) return;
    for (const PluginManager::PluginAction &pa : m_plugins->actions()) {
        auto *sc = new QShortcut(pa.action.keys, this);
        connect(sc, &QShortcut::activated, this, [this, pa] {
            if (m_stack->currentIndex() != 1 || (m_commentEdit && m_commentEdit->hasFocus())) return;
            m_plugins->triggerAction(pa.pluginId, pa.action.id);
        });
        m_pluginShortcuts << sc;
    }
}

// Depois do primeiro quadro: comentário do clipe e aviso aos plugins (que consultam o jogo, etc. em segundo plano).
void MainWindow::notifyClipOpened()
{
    if (m_file.isEmpty()) return;
    const QFileInfo fi(m_file);
    m_clipCreated = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
    m_comment = clipmeta::comment(clipmeta::key(fi.fileName(), fi.size()));
    refreshCommentUi();
    if (m_plugins) {
        m_plugins->clipOpened({m_file, m_clipCreated, fi.size()});
        m_plugins->durationChanged(m_timeline->duration());
        // plugin disponível para este tipo de clipe (e ainda não instalado)? sugere, sem atrapalhar
        const QString sug = m_plugins->suggestionFor(m_file);
        if (!sug.isEmpty() && !m_sessionDismissed.contains(sug)) {
            m_bannerPlugin = sug;
            QString what = sug;
            for (const PluginInfo &c : PluginManager::builtinCatalog())
                if (c.id == sug) what = QString("Plugin do %1 disponível: %2").arg(c.name, c.description);
            m_pluginBannerText->setText(what);
            m_pbInstall->setText("Instalar");
            m_pbInstall->setEnabled(true);
            m_pluginBanner->show();
        }
    }
}

// Q / E: vai para a parada anterior / seguinte, que pode ser um evento (kill/morte) ou o ponto I / O. Paradas a menos de
// 1 s do ponto atual (ou seja, "em cima" dele) são ignoradas. Sem nenhuma parada naquele lado, vai direto para I / O.
void MainWindow::jumpToEvent(int dir)
{
    constexpr double kTol = 1.0;
    const double p = m_mpv->position(), s = m_timeline->start(), e = m_timeline->end();
    std::vector<double> stops = m_timeline->eventTimes();
    stops.push_back(s);
    stops.push_back(e);
    std::optional<double> target;
    for (const double t : stops) {
        if (dir < 0 && t < p - kTol && (!target || t > *target)) target = t;
        if (dir > 0 && t > p + kTol && (!target || t < *target)) target = t;
    }
    if (!target) {
        const double fallback = dir < 0 ? s : e;
        if (std::abs(fallback - p) <= kTol) return; // já está no I / O
        target = fallback;
    }
    if (std::abs(*target - e) < 1e-6 && !m_mpv->paused())
        m_mpv->setPaused(true); // chegando no O tocando: para em cima dele (para ver/ajustar o fim)
    m_mpv->seek(*target);
}

void MainWindow::detectNvenc()
{
    auto *p = new QProcess(this);
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus) {
        m_hasNvenc = code == 0;
        metrics::log(QString("NVENC: %1").arg(m_hasNvenc ? "disponível" : "indisponível (usará libx264)"));
        p->deleteLater();
        updateInfo();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError) { m_hasNvenc = false; p->deleteLater(); });
    p->start(ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "nullsrc=s=256x256",
                            "-frames:v", "1", "-c:v", "h264_nvenc", "-f", "null", "-"});
}

QString MainWindow::ffmpegPath() const
{
    const QString local = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    return QFileInfo::exists(local) ? local : QStringLiteral("ffmpeg");
}

// `created`: quando o arquivo foi CRIADO (é o que vale para "clipado há N min"); a data de modificação é só reserva.
struct RecentItem { QString path; QDateTime created; qint64 size; QString iconPath, bannerPath, bannerText, tag, comment; bool tagHot = false; };

void MainWindow::showHome()
{
    commitComment(true); // o comentário em edição não se perde ao voltar para a lista
    if (m_plugins) m_plugins->clipClosed();
    // Saiu do vídeo: para tudo, senão o áudio continuaria tocando em segundo plano.
    m_scrubbing = false;
    m_resumeAfterDrag = false;
    if (m_scrub)
        m_scrub->stop();
    if (m_mpv)
        m_mpv->setPaused(true);
    cleanupTemp(false); // clipes cortados que não foram mantidos somem ao sair do vídeo
    if (m_editor)
        hideResultButtons();
    m_lastOutput.clear();
    m_file.clear(); // nenhum vídeo aberto (assim o clipe recém-salvo pode ser aberto de novo)
    setWindowTitle("shear");
    m_stack->setCurrentIndex(0);
    refreshRecent(false); // um clipe novo pode ter sido salvo enquanto você editava
}

// Chave do resultado guardado da análise de começo corrompido: arquivo + tamanho + data (se o arquivo mudar, refaz).
static QString damageCacheKey(const QString &path)
{
    const QFileInfo fi(path);
    return "damage2/" + QString::fromLatin1(QCryptographicHash::hash(
        (fi.absoluteFilePath() + "|" + QString::number(fi.size()) + "|" + QString::number(fi.lastModified().toSecsSinceEpoch())).toUtf8(),
        QCryptographicHash::Md5).toHex());
}

void MainWindow::refreshRecent(bool autoOpen)
{
    const QString dir = m_cfg.effectiveShadowPlayDir();
    auto *t = QThread::create([this, dir, autoOpen] {
        std::vector<RecentItem> v;
        QDirIterator it(dir, {std::begin(kVideoGlobs), std::end(kVideoGlobs)}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            v.push_back({it.filePath(), fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified(), fi.size()});
        }
        std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.created > b.created; });
        if (v.size() > 40) v.resize(40);
        // Dados que os plugins têm de cada clipe (ícone, faixa, tag; só leitura de arquivos, sem rede) e o comentário
        const QJsonObject metas = clipmeta::loadAll();
        QVector<plugin::RowQuery> queries;
        for (const auto &r : v) queries.push_back({r.path, r.size});
        const QVector<plugin::RowInfo> infos = m_plugins ? m_plugins->rowInfos(queries) : QVector<plugin::RowInfo>(queries.size());
        for (size_t i = 0; i < v.size(); ++i) {
            RecentItem &r = v[i];
            const plugin::RowInfo &info = infos[int(i)];
            r.iconPath = info.iconPath;
            r.bannerPath = info.bannerPath;
            r.bannerText = info.bannerText;
            r.tag = info.tag;
            r.tagHot = info.tagHot;
            r.comment = metas.value(clipmeta::key(QFileInfo(r.path).fileName(), r.size)).toObject().value("comment").toString();
        }
        QMetaObject::invokeMethod(this, [this, v, autoOpen] {
            const QDateTime now = QDateTime::currentDateTime();
            const QColor accent = palette().color(QPalette::Highlight);
            m_recent->clear();
            QPixmap blank(28, 28);
            blank.fill(Qt::transparent); // linhas sem ícone ficam alinhadas com as que têm
            for (const auto &r : v) {
                const qint64 age = r.created.secsTo(now);
                const bool fresh = &r == &v.front() && age <= kFreshHighlightSec; // só o ÚLTIMO clipe, se tiver até 5 min
                auto *i = new QListWidgetItem(QFileInfo(r.path).fileName()); // o ClipRowDelegate desenha a linha em colunas
                i->setData(RoleDate, r.created.toString("dd/MM/yyyy HH:mm"));
                i->setData(RoleTags, r.tag);
                i->setData(RoleTagHot, r.tagHot);
                i->setData(RoleComment, r.comment);
                i->setData(RoleMap, r.bannerText);
                if (fresh) { // o clipe mais recente (salvo há até 5 min): destaque; a lista já vem do mais novo para o mais antigo
                    QColor bg = accent;
                    bg.setAlpha(70);
                    i->setBackground(bg);
                    i->setData(RoleFresh, QString("●  NOVO · há %1").arg(formatAgo(age)));
                }
                i->setData(Qt::UserRole, r.path);
                i->setToolTip(r.path);
                i->setIcon(r.iconPath.isEmpty() ? QIcon(blank) : QIcon(r.iconPath));
                i->setData(RoleBanner, r.bannerPath);
                m_recent->addItem(i);
            }
            if (v.empty())
                m_recent->addItem("Nenhum clipe encontrado em " + m_cfg.effectiveShadowPlayDir());
            else if (autoOpen && !m_job && v.front().created.secsTo(now) <= kFreshOpenSec && v.front().path != m_file)
                openFile(v.front().path); // clipado há segundos: já abre nele

            // Adianta, em segundo plano, a checagem de começo corrompido dos clipes mais recentes (até 8): quando você
            // abrir um deles, o resultado já está guardado e o vídeo aparece sem esperar a análise.
            if (!m_prefetch) {
                QStringList todo;
                for (size_t k = 0; k < v.size() && todo.size() < 8; ++k)
                    if (!QSettings().contains(damageCacheKey(v[k].path)) && v[k].path != m_file)
                        todo << v[k].path;
                if (!todo.isEmpty()) {
                    m_prefetch = new DamagePrefetch(todo);
                    connect(m_prefetch, &DamagePrefetch::analyzed, this, [](const QString &p, double clean) {
                        QSettings().setValue(damageCacheKey(p), clean);
                    });
                    connect(m_prefetch, &QThread::finished, this, [this] {
                        m_prefetch->deleteLater();
                        m_prefetch = nullptr;
                    });
                    QTimer::singleShot(2000, m_prefetch, [p = m_prefetch] { p->start(QThread::LowestPriority); }); // depois da abertura
                }
            }
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void MainWindow::promptOpen()
{
    const QString f = QFileDialog::getOpenFileName(this, "Abrir vídeo", m_cfg.effectiveShadowPlayDir(),
                                                   "Vídeos (*.mp4 *.mkv *.mov *.webm *.avi);;Todos (*.*)");
    if (!f.isEmpty()) openFile(f);
}

void MainWindow::openHelp()
{
    QVector<QPair<QString, QString>> rows;
    for (const PluginManager::PluginAction &pa : m_plugins->actions())
        rows.push_back({pa.action.keys.toString(QKeySequence::NativeText).replace('+', " + "), pa.action.text});
    HelpDialog d(rows, this);
    d.exec();
}

void MainWindow::openSettings()
{
    SettingsDialog d(m_cfg, this);
    if (d.exec() != QDialog::Accepted) {
        if (d.wantsHelp())
            openHelp(); // clicou em "Atalhos e controles…": as Configurações já fecharam; só a ajuda abre
        else if (d.wantsPlugins()) {
            PluginsDialog p(m_plugins, this); // "Plugins…": as Configurações já fecharam; só a tela de plugins abre
            p.exec();
        }
        return;
    }
    const CutMode oldDefault = m_cfg.mode;
    const bool oldLimit = m_cfg.limit20;
    m_cfg = d.config();
    m_cfg.save();
    if (m_editor && m_cfg.mode != oldDefault) // só muda o modo da edição se o padrão mudou
        setCutMode(m_cfg.mode);
    if (m_limitCheck && m_cfg.limit20 != oldLimit) // idem para o "Até 20 MB"
        m_limitCheck->setChecked(m_cfg.limit20);
    refreshRecent();
    updateInfo();
}

void MainWindow::openFile(const QString &path)
{
    if (path.isEmpty()) { // app já aberto e chamado de novo (atalho/menu): traz à frente e abre o clipe salvo há segundos
        raise();
        activateWindow();
        if (!m_job) refreshRecent(true);
        return;
    }
    if (!QFileInfo::exists(path)) {
        raise();
        activateWindow();
        return;
    }
    if (m_job) { // não troca de arquivo no meio de uma exportação
        raise();
        return;
    }
    commitComment(true); // abrindo outro clipe com um comentário em edição: grava no clipe de antes
    m_openClock.start();
    m_fileSize = QFileInfo(path).size();
    if (!m_editor) {
        m_editor = buildEditor();
        m_stack->addWidget(m_editor);
    }
    m_file = path;
    m_title->setText(QFileInfo(path).fileName());
    if (m_plugins) m_plugins->clipClosed(); // os plugins largam o clipe anterior
    m_pluginBanner->hide();
    m_chipSets.clear();
    m_backdrops.clear();
    m_markers.clear();
    rebuildChips();
    setBackdrop(QString(), QString());
    m_comment.clear();
    m_commentEdit->clear();
    m_commentEdit->hide();
    refreshCommentUi();
    m_userPaused = false;
    m_suggested.reset();
    m_autoRange.reset();
    setWindowTitle("shear"); // o título da janela é sempre só o nome do app
    m_timeline->reset(0);
    m_undo.clear();
    m_redo.clear();
    m_micClock.invalidate();
    m_lastMicGain = 0;
    m_status->clear();
    hideResultButtons();
    m_stack->setCurrentIndex(1);
    rebuildAudioRows(0);
    if (m_mpv->speed() != 1.0)
        setSpeed(1.0, false); // cada vídeo começa em 1×
    if (m_limitCheck)
        m_limitCheck->setChecked(m_cfg.limit20); // e com o “Até 20 MB” do jeito que as Configurações pedem
    m_mpv->resetAudio();
    m_scrub->resetAudio();
    m_scrub->loadFile(path);
    m_mpv->loadFile(path); // o vídeo vem primeiro; o resto depois

    if (m_scanner) { // descarta o scanner anterior
        m_scanner->disconnect(this);
        m_scanner->requestInterruption();
        connect(m_scanner, &QThread::finished, m_scanner, &QObject::deleteLater);
    }
    m_scanner = new KeyframeScanner(path);
    connect(m_scanner, &KeyframeScanner::probed, this, [this](double d, int a) {
        m_audioTracks = a;
        m_timeline->setDuration(d);
        rebuildAudioRows(a);
        applyPreviewAudio();
        updateInfo();
    });
    connect(m_scanner, &KeyframeScanner::keyframes, this, [this](QVector<double> b) {
        m_timeline->addKeyframes(b);
        if (m_damageT > 0) applyDamageFloor();
        applySuggestedRange();
        updateInfo();
    });
    connect(m_scanner, &QThread::finished, this, [this] {
        m_timeline->setScanDone(true);
        applyDamageFloor(); // com todos os keyframes lidos, o início utilizável cai num keyframe de verdade
        applySuggestedRange();
        metrics::log(QString("keyframes lidos: %1 ms após abrir o arquivo").arg(m_openClock.elapsed()));
        updateInfo();
    });
    QTimer::singleShot(0, m_scanner, [s = m_scanner] { s->start(QThread::LowPriority); });

    // Começo corrompido (quadros que não decodificam): detecta e pula, para não atrapalhar o resto do vídeo.
    m_mpv->setMinPosition(0);
    if (m_damage) {
        m_damage->disconnect(this);
        m_damage->requestInterruption();
        connect(m_damage, &QThread::finished, m_damage, &QObject::deleteLater);
    }
    m_damageT = 0;
    m_holdUntilPos = 0;
    // A análise leva alguns segundos num vídeo com dano: o resultado fica guardado (por arquivo, tamanho e data).
    const QString cacheKey = damageCacheKey(path);
    const QVariant cached = QSettings().value(cacheKey);
    if (cached.isValid()) {
        m_damage = nullptr;
        m_damageT = cached.toDouble();
        if (m_damageT > 0) { // dano conhecido: segura a imagem só até o seek para o trecho bom
            m_holdVideo = true;
            m_holdUntilPos = m_damageT;
            m_mpv->setHold(true);
            QTimer::singleShot(10000, this, [this, path] { if (m_file == path) releaseVideoHold(); });
            // com a lista de keyframes completa (fim do scanner) o início utilizável é aplicado
        } else {
            m_holdVideo = false;
            m_mpv->setHold(false);
        }
    } else {
        m_damage = new DamageScanner(path);
        m_holdVideo = true; // tela preta até saber se o começo presta (senão o 1º quadro quebrado aparece por um instante)
        m_mpv->setHold(true);
        QTimer::singleShot(10000, this, [this, path] { if (m_file == path) releaseVideoHold(); }); // segurança
        connect(m_damage, &DamageScanner::damaged, this, [this, cacheKey](double t) {
            QSettings().setValue(cacheKey, t);
            m_damageT = t;
            applyDamageFloor();
            m_holdUntilPos = m_damageT; // o player só aparece quando passar daqui
        });
        connect(m_damage, &QThread::finished, this, [this, cacheKey] {
            if (m_damageT <= 0) { // sem dano
                QSettings().setValue(cacheKey, 0.0);
                releaseVideoHold();
            }
        });
        QTimer::singleShot(0, m_damage, [s = m_damage] { s->start(QThread::NormalPriority); });
    }
    raise();
    activateWindow();
    updateInfo();
}

QString MainWindow::fmt(double s)
{
    s = std::max(0.0, s);
    const int m = int(s) / 60;
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s - m * 60, 4, 'f', 1, QChar('0')).replace('.', ',');
}

void MainWindow::releaseVideoHold()
{
    if (!m_holdVideo) return;
    m_holdVideo = false;
    if (m_mpv) m_mpv->setHold(false);
}

void MainWindow::applyDamageFloor()
{
    if (m_damageT <= 0 || m_file.isEmpty()) return;
    // Só decide com um keyframe de verdade: pular para um quadro que não é keyframe faria o player decodificar a
    // partir do trecho corrompido e mostrar lixo. (O scanner lê os pacotes em ordem: o primeiro keyframe depois do
    // dano já basta, não precisa esperar o arquivo inteiro.)
    const double base = std::max(m_damageT, m_timeline->damagedUntil());
    m_timeline->setDamagedUntil(base); // descarta os keyframes do trecho corrompido (antes de procurar o primeiro bom)
    const auto kf = m_timeline->firstKeyframeAtOrAfter(base);
    if (!kf && !m_timeline->scanDone()) return;
    if (kf && std::abs(*kf - m_timeline->damagedUntil()) < 1e-6 && m_timeline->damagedUntil() > m_damageT + 1e-6)
        return; // já aplicado
    double floor = base;
    if (kf)
        floor = *kf; // o corte rápido só começa em keyframe: o início utilizável é o primeiro depois do dano
    m_timeline->setDamagedUntil(floor);
    m_mpv->setMinPosition(floor);
    if (m_mpv->position() < floor)
        m_mpv->seek(floor);
    m_status->setText(QString("O início do vídeo está corrompido: os primeiros %1 s foram pulados.")
                          .arg(QString::number(floor, 'f', 1).replace('.', ',')));
    metrics::log(QString("início corrompido: pulando os primeiros %1 s (%2 ms após abrir o arquivo)")
                     .arg(floor, 0, 'f', 2).arg(m_openClock.elapsed()));
    updateInfo();
}

void MainWindow::markStart(double t)
{
    t = snapIfFast(t);
    if (std::abs(t - m_timeline->start()) > 1e-6)
        pushUndo(currentState());
    if (t >= m_timeline->end()) m_timeline->setEnd(m_timeline->duration());
    m_timeline->setStart(t);
    // Modo Rápido: o início encaixou num keyframe, então o playback vai para lá (o quadro mostrado é o do corte).
    if (!m_exactBtn->isChecked() && std::abs(m_timeline->start() - m_mpv->position()) > 1e-3)
        m_mpv->seek(m_timeline->start());
    updateInfo();
}

void MainWindow::syncModeWithGaps()
{
    if (m_syncingMode) return;
    m_syncingMode = true; // o setChecked abaixo chama onModeChanged -> updateInfo de novo: não reentra
    // Trechos apagados e a compressão "até 20 MB" recodificam o vídeo: o modo é Exato e o Rápido fica desabilitado.
    if (!m_timeline->gaps().empty() || (m_limitCheck && m_limitCheck->isChecked())) {
        if (!m_exactBtn->isChecked()) {
            m_fastBeforeGaps = true; // lembra que era Rápido, para voltar quando não houver mais trechos apagados
            m_exactBtn->setChecked(true);
        }
        m_fastBtn->setEnabled(false);
        m_fastBtn->setToolTip("Com trechos apagados ou “Até 20 MB” o corte é sempre Exato (o vídeo é recodificado).");
    } else {
        m_fastBtn->setEnabled(true);
        m_fastBtn->setToolTip("Corte instantâneo. O início encaixa nas marcas da timeline.");
        if (m_fastBeforeGaps) {
            m_fastBeforeGaps = false;
            m_fastBtn->setChecked(true);
        }
    }
    m_syncingMode = false;
}

void MainWindow::toggleGap()
{
    if (m_job) return;
    const double pos = std::clamp(m_mpv->position(), m_timeline->start(), m_timeline->end());
    if (const auto p = m_timeline->pendingGap()) {
        const double a = std::min(*p, pos), b = std::max(*p, pos);
        if (b - a < 0.05) {
            m_mpv->showText("Trecho curto demais", 900);
            return;
        }
        // Desfazer volta só o 2º X: o estado guardado ainda tem o início marcado (pendente).
        pushUndo(currentState());
        m_timeline->setPendingGap(std::nullopt);
        m_timeline->addGap(a, b);
        m_mpv->showText(QString("Trecho de %1 s apagado do meio").arg(QString::number(b - a, 'f', 1).replace('.', ',')), 1400);
        updateInfo();
    } else {
        pushUndo(currentState());
        m_timeline->setPendingGap(pos);
        m_mpv->showText("Apagar trecho: vá até onde ele termina e aperte X de novo (Esc cancela)", 2800);
    }
}

void MainWindow::removeGapAt(double sec)
{
    if (m_job) return;
    bool inside = false;
    for (const auto &g : m_timeline->gaps())
        inside = inside || (sec >= g.first && sec <= g.second);
    if (!inside) return;
    pushUndo(currentState());
    m_timeline->removeGapAt(sec);
    m_mpv->showText("Trecho de volta", 900);
    updateInfo();
}

void MainWindow::markEnd(double t)
{
    if (std::abs(t - m_timeline->end()) > 1e-6)
        pushUndo(currentState());
    if (t <= m_timeline->start()) m_timeline->setStart(0);
    m_timeline->setEnd(t);
    updateInfo();
}

MainWindow::EditState MainWindow::currentState() const
{
    return {m_timeline->start(), m_timeline->end(), m_micFader ? m_micFader->value() : 0, m_timeline->gaps(),
            m_timeline->pendingGap().value_or(-1.0), m_plugins ? m_plugins->saveState() : QVariantMap()};
}

void MainWindow::pushUndo(const EditState &s)
{
    if (m_restoring)
        return;
    m_redo.clear(); // uma alteração nova depois de desfazer invalida o "refazer"
    if (!m_undo.empty()) {
        const EditState &b = m_undo.back();
        if (b.start == s.start && b.end == s.end && b.micGain == s.micGain && b.gaps == s.gaps && b.pending == s.pending &&
            b.pluginState == s.pluginState)
            return;
    }
    m_undo.push_back(s);
    if (m_undo.size() > 100)
        m_undo.erase(m_undo.begin());
}

void MainWindow::applyState(const EditState &s)
{
    const double oldStart = m_timeline->start(), oldEnd = m_timeline->end();
    m_restoring = true;
    m_timeline->setRange(s.start, s.end);
    m_timeline->setGaps(s.gaps);
    m_timeline->setPendingGap(s.pending >= 0 ? std::optional<double>(s.pending) : std::nullopt);
    if (m_micFader)
        m_micFader->setValue(s.micGain);
    m_restoring = false;
    if (m_plugins) m_plugins->restoreState(s.pluginState); // estado dos plugins (ex.: calibração das marcas)
    m_micClock.invalidate(); // o próximo ajuste do mic começa um gesto novo
    updateInfo();
    applyPreviewAudio();
    if (std::abs(s.start - oldStart) > 1e-6)
        m_mpv->seek(s.start); // mostra o ponto que mudou
    else if (std::abs(s.end - oldEnd) > 1e-6)
        m_mpv->seek(s.end);
}

void MainWindow::undo()
{
    if (m_undo.empty() || m_job)
        return;
    const EditState s = m_undo.back();
    m_undo.pop_back();
    m_redo.push_back(currentState()); // para poder refazer
    applyState(s);
}

void MainWindow::redo()
{
    if (m_redo.empty() || m_job)
        return;
    const EditState s = m_redo.back();
    m_redo.pop_back();
    m_undo.push_back(currentState());
    applyState(s);
}

void MainWindow::jumpBy(double sec)
{
    if (m_stack->currentIndex() != 1)
        return;
    m_mpv->seekBy(sec); // não pausa: se estava tocando, continua tocando do ponto novo
    // animação "+5 s" / "−0,5 s" sobre o vídeo
    const double a = std::abs(sec);
    const QString num = (a == std::floor(a) ? QString::number(int(a)) : QString::number(a, 'f', 1)).replace('.', ',');
    m_mpv->flash(MpvWidget::Overlay::Skip, QString("%1%2 s").arg(sec < 0 ? QChar(0x2212) : QChar('+')).arg(num));
}

void MainWindow::userTogglePause()
{
    if (m_commentEdit) m_commentEdit->clearFocus();
    m_userPaused = true; // você mexeu no play/pause: o shear não despausa mais sozinho neste vídeo
    // Parado no fim do vídeo OU na marca de fim (O), play recomeça do início do clipe (a marca I; sem marca, o começo).
    const double dur = m_mpv->duration();
    const double pos = m_mpv->position(), eps = std::max(0.08, 1.5 / std::max(1.0, m_fps));
    const bool atVideoEnd = dur > 0 && pos >= dur - eps;
    const double end = m_timeline->end();
    const bool atMarkEnd = m_mpv->paused() && end > 0 && pos >= end - eps && pos <= end + 0.5; // logo depois do O, onde parou
    if (atVideoEnd || atMarkEnd) {
        m_mpv->seek(m_timeline->start());
        m_mpv->setPaused(false);
        m_mpv->flash(MpvWidget::Overlay::Play);
        return;
    }
    const bool willPause = !m_mpv->paused();
    m_mpv->togglePause();
    m_mpv->flash(willPause ? MpvWidget::Overlay::Pause : MpvWidget::Overlay::Play);
}

void MainWindow::setSpeed(double speed, bool osd)
{
    m_mpv->setSpeed(speed);
    const QString txt = QString::number(speed, 'g', 3).replace('.', ',') + "×";
    if (m_speedBtn)
        m_speedBtn->setText(txt); // mesma fonte em qualquer velocidade
    if (osd)
        m_mpv->showText("Velocidade " + txt, 800);
}

void MainWindow::stepSpeed(int dir)
{
    int idx = 3; // 1×
    for (int i = 0; i < 8; ++i)
        if (std::abs(kSpeeds[i] - m_mpv->speed()) < 1e-6) idx = i;
    setSpeed(kSpeeds[std::clamp(idx + dir, 0, 7)]);
}

void MainWindow::stepOneFrame(int dir)
{
    if (m_stack->currentIndex() != 1)
        return;
    m_mpv->setPaused(true); // para ver o quadro é preciso parar
    m_mpv->stepFrame(dir > 0);
}

ExportRequest MainWindow::currentRequest() const
{
    ExportRequest r;
    r.inputPath = m_file;
    r.startSec = m_timeline->start();
    r.endSec = m_timeline->end();
    r.mode = m_exactBtn->isChecked() ? CutMode::Exact : CutMode::Fast;
    r.hasNvenc = m_hasNvenc;
    r.gaps = m_timeline->gaps();
    r.fadeSec = m_cfg.fadeJoins ? kJoinFadeSec : 0.0; // opção das Configurações (ligada por padrão)
    if (m_limitCheck && m_limitCheck->isChecked()) {
        r.maxBytes = kLimitBytes;
        r.bitrateScale = m_limitScale; // < 1 depois de uma tentativa que passou do limite
    }
    r.audio = currentAudio();
    if (auto k = m_timeline->keyframeAtOrBefore(r.startSec))
        r.keyframeBeforeStart = *k;
    else if (m_timeline->scanDone())
        r.keyframeBeforeStart = 0.0;
    else // keyframe ainda desconhecido: o ffmpeg busca sozinho o keyframe anterior
        r.keyframeBeforeStart = r.startSec;
    return r;
}

void MainWindow::updateInfo()
{
    if (m_file.isEmpty()) return;
    syncModeWithGaps();
    const auto r = currentRequest();
    const auto plan = planCut(r);
    m_times->setText(QString("Início %1    Fim %2    Duração %3%4")
                         .arg(fmt(r.startSec), fmt(r.endSec), fmt(keptSeconds(r)),
                              m_timeline->scanDone() ? "" : "    (lendo keyframes…)"));
    m_timeline->setGhostStart(plan.earlyBySec >= 0.05 ? std::optional<double>(plan.actualStartSec)
                                                      : std::nullopt);

    QStringList msg;
    const QString w = earlyStartWarning(plan.earlyBySec);
    m_early->setText(w.isEmpty() ? QStringLiteral(" ")
                                 : "⚠ " + w);
    if (r.maxBytes > 0)
        msg << "Comprime para caber em 20 MB";
    else if (plan.assembled)
        msg << "Trechos apagados: recodifica o vídeo";
    else if (r.mode == CutMode::Exact)
        msg << "Corta no quadro marcado (demora mais)"; // no Rápido não precisa de texto
    m_warning->setText(msg.join("   ·   "));

    // previsão do tempo de exportação
    const ExportKind kind = exportKind(r, plan);
    const double clipSec = plan.assembled ? keptSeconds(r) : std::max(0.0, r.endSec - plan.actualStartSec);
    const double total = m_timeline->duration();
    const double clipBytes = total > 0 ? double(m_fileSize) * clipSec / total : 0.0;
    m_eta->setText("Exportar: " + formatDuration(estimateSeconds(kind, r.hasNvenc, clipSec, clipBytes, m_rates)));
}

void MainWindow::startExport()
{
    if (m_job || m_file.isEmpty()) return;
    if (!m_retrying) { // exportação pedida pelo usuário (e não uma nova tentativa da compressão)
        m_limitScale = 1.0;
        m_limitTries = 0;
    }
    m_retrying = false;
    auto r = currentRequest();
    m_exportMaxBytes = r.maxBytes;
    if (keptSeconds(r) < 0.1) {
        m_status->setText(r.gaps.empty() ? "Selecione um trecho maior." : "Os trechos apagados cobrem tudo: nada sobrou para exportar.");
        return;
    }
    const auto plan = planCut(r);
    const QString dir = tempDir(); // exports são temporários até o usuário clicar em Manter
    QDir().mkpath(dir);
    // Nome: um plugin pode dar o nome (ex.: "Valorant aaaa.mm.dd - Mapa_Agente_Nk"); senão, "<original>_shear"
    plugin::ExportContext ec;
    ec.clipPath = m_file;
    ec.clipCreated = m_clipCreated;
    ec.start = plan.actualStartSec; // modo Rápido: o arquivo começa no keyframe
    ec.end = r.endSec;
    ec.duration = m_timeline->duration();
    for (const auto &g : r.gaps) ec.gaps.push_back({g.first, g.second});
    m_exportStem = m_plugins ? m_plugins->exportStem(ec) : QString();
    if (m_exportStem.isEmpty())
        m_exportStem = fallbackStem(QFileInfo(m_file).completeBaseName());
    r.outputPath = uniquePath(dir, m_exportStem, [](const QString &p) { return QFileInfo::exists(p); });
    const auto args = buildFfmpegArgs(r, plan);
    metrics::log("ffmpeg " + args.join(' '));

    m_exportKind = exportKind(r, plan);
    m_exportClipSec = plan.assembled ? keptSeconds(r) : r.endSec - plan.actualStartSec;
    const double total = m_timeline->duration();
    m_exportClipBytes = total > 0 ? double(m_fileSize) * m_exportClipSec / total : 0.0;
    m_exportEstimate = estimateSeconds(m_exportKind, r.hasNvenc, m_exportClipSec, m_exportClipBytes, m_rates);

    m_job = new ExportJob(ffmpegPath(), args, m_exportClipSec, r.outputPath, this);
    auto *clock = new QElapsedTimer;
    clock->start();
    connect(m_job, &ExportJob::progress, this, [this, clock](double p) {
        m_bar->setValue(int(p * 1000));
        const double elapsed = clock->elapsed() / 1000.0;
        const QString eta = (p > 0.04 && p < 0.995) ? " · faltam " + formatDuration(elapsed * (1 - p) / p) : QString();
        m_bar->setFormat("Exportando… %p%" + eta);
    });
    connect(m_job, &QObject::destroyed, this, [clock] { delete clock; });
    connect(m_job, &ExportJob::finished, this, &MainWindow::onExportFinished);
    m_bar->setValue(0);
    m_bar->setFormat("Exportando… %p%");
    m_status->hide();
    m_bar->show();
    m_cancelBtn->show();
    m_exportBtn->setEnabled(false);
    hideResultButtons();
    m_job->start();
}

QString MainWindow::tempDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/Shear-exports";
}

void MainWindow::setResultLink(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    m_fileLink->setText(m_fileLink->fontMetrics().elidedText(name, Qt::ElideMiddle, 360));
    m_fileLink->setToolTip("Abrir o clipe no shear (botão direito: abrir local do arquivo): " + name);
    m_fileLink->adjustSize();
}

void MainWindow::hideResultButtons()
{
    for (QWidget *b : {static_cast<QWidget *>(m_copyBtn), static_cast<QWidget *>(m_keepBtn), static_cast<QWidget *>(m_deleteBothBtn), static_cast<QWidget *>(m_deleteOrigBtn), static_cast<QWidget *>(m_fileLink)})
        b->hide();
}

static void clearClipboardIfOurs(const QStringList &paths)
{
    auto *cb = QGuiApplication::clipboard();
    const auto *md = cb->mimeData();
    if (!md || !md->hasUrls()) return;
    for (const QUrl &u : md->urls())
        if (paths.contains(u.toLocalFile(), Qt::CaseInsensitive)) { cb->clear(); return; }
}

// wholeDir=true: limpa a pasta inteira (sobras de sessões antigas); senão só o que esta sessão criou.
void MainWindow::cleanupTemp(bool wholeDir)
{
    clearClipboardIfOurs(m_tempFiles);
    for (const QString &f : std::as_const(m_tempFiles))
        QFile::remove(f);
    m_tempFiles.clear();
    if (wholeDir)
        QDir(tempDir()).removeRecursively();
}

// Solta o vídeo (player e preview de áudio seguram o arquivo) e manda o original para a Lixeira.
bool MainWindow::trashOriginal()
{
    const QString orig = m_file;
    m_mpv->unload();
    m_scrub->unload();
    QThread::msleep(150);
    if (!QFile::moveToTrash(orig)) {
        m_mpv->loadFile(orig);
        m_status->setText("Não foi possível apagar o original.");
        return false;
    }
    metrics::log("original enviado para a Lixeira: " + orig);
    return true;
}

// Pergunta com botões nomeados (o padrão é sempre o seguro, "Não").
static bool confirmDestructive(QWidget *parent, const QString &title, const QString &text, const QString &yesText,
                               const QString &noText)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, parent);
    auto *yes = box.addButton(yesText, QMessageBox::DestructiveRole);
    auto *no = box.addButton(noText, QMessageBox::RejectRole);
    box.setDefaultButton(no);
    box.exec();
    return box.clickedButton() == yes;
}

// Botão direito numa linha da lista de clipes
void MainWindow::showListMenu(const QPoint &pos)
{
    QListWidgetItem *it = m_recent->itemAt(pos);
    const QString path = it ? it->data(Qt::UserRole).toString() : QString();
    if (path.isEmpty())
        return;
    m_recent->setCurrentItem(it);
    QMenu menu(this);
    auto *reveal = menu.addAction("Abrir local do arquivo");
    menu.addSeparator();
    auto *del = menu.addAction("Excluir");
    del->setEnabled(!m_job);
    connect(reveal, &QAction::triggered, this, [path] { revealInExplorer(path); });
    connect(del, &QAction::triggered, this, [this, path] { trashFromList(path); });
    menu.exec(m_recent->viewport()->mapToGlobal(pos));
}

// "Apagar original" na lista: confirma e manda para a Lixeira (dá para recuperar de lá).
void MainWindow::trashFromList(const QString &path)
{
    if (m_job || path.isEmpty() || path == m_file)
        return;
    if (!confirmDestructive(this, "Excluir", "Mandar para a Lixeira o vídeo?\n\n" + QDir::toNativeSeparators(path),
                            "Excluir", "Cancelar"))
        return;
    if (QFile::moveToTrash(path))
        metrics::log("original enviado para a Lixeira: " + path);
    else
        QMessageBox::warning(this, "Excluir", "Não foi possível mandar o arquivo para a Lixeira.");
    refreshRecent(false);
}

// Botão direito no nome do arquivo do editor. "Abrir local do arquivo" só existe na lista de clipes (aqui, clicar no nome
// já abre a pasta).
void MainWindow::showTitleMenu(const QPoint &global)
{
    if (m_file.isEmpty())
        return;
    QMenu menu(this);
    const bool temp = m_tempFiles.contains(m_lastOutput); // clipe ainda não mantido: apagar o original leva o clipe junto
    auto *del = menu.addAction(temp ? "Apagar original e clipe" : "Apagar original");
    del->setEnabled(!m_job);
    connect(del, &QAction::triggered, this, [this, temp] { if (temp) deleteBoth(); else deleteOriginalOnly(); });
    menu.exec(global);
}

// Botão "Apagar original e clipe": só existe enquanto o clipe cortado é temporário.
void MainWindow::deleteBoth()
{
    if (m_file.isEmpty() || m_job || !m_tempFiles.contains(m_lastOutput)) return;
    if (!confirmDestructive(this, "Apagar original e clipe",
                            "Apagar o vídeo original (vai para a Lixeira) e o clipe cortado?\n\n" +
                                QDir::toNativeSeparators(m_file) + "\n" + QDir::toNativeSeparators(m_lastOutput) +
                                "\n\nVocê perde os dois.",
                            "Apagar os dois", "Cancelar"))
        return;
    if (!trashOriginal()) return;
    cleanupTemp(false); // o clipe temporário
    m_lastOutput.clear();
    showHome();
}

// Botão "Apagar original", que aparece depois de Manter: o clipe mantido fica, só o original vai para a Lixeira.
void MainWindow::deleteOriginalOnly()
{
    if (m_file.isEmpty() || m_job) return;
    if (!confirmDestructive(this, "Apagar o original",
                            "Mandar para a Lixeira o vídeo original?\n\n" + QDir::toNativeSeparators(m_file) +
                                "\n\nO clipe mantido não é apagado.",
                            "Apagar original", "Cancelar"))
        return;
    if (trashOriginal())
        showHome();
}

void MainWindow::keepLastOutput(bool deleteOriginalToo)
{
    if (m_lastOutput.isEmpty() || !m_tempFiles.contains(m_lastOutput)) return;
    // Shift: não pergunta "quer apagar o original?", mas ainda pede confirmação (antes de mexer em qualquer arquivo).
    if (deleteOriginalToo && !m_file.isEmpty() &&
        !confirmDestructive(this, "Manter e apagar o original",
                            "Manter o clipe cortado e mandar o vídeo original para a Lixeira?\n\n" +
                                QDir::toNativeSeparators(m_file),
                            "Manter e apagar original", "Cancelar"))
        return;
    const QString dir = m_cfg.outputDir.isEmpty() ? QFileInfo(m_file).absolutePath() : m_cfg.outputDir;
    QDir().mkpath(dir);
    const QFileInfo src(m_lastOutput);
    // nunca sobrescreve: repetido vira "..._2", "..._3"... (conta a partir do nome-base, não do nome temporário)
    const QString dest = uniquePath(dir, m_exportStem.isEmpty() ? src.completeBaseName() : m_exportStem,
                                    [](const QString &p) { return QFileInfo::exists(p); });
    if (!QFile::rename(m_lastOutput, dest)) {
        m_status->setText("Não foi possível mover o clipe para " + QDir::toNativeSeparators(dir));
        return;
    }
    m_tempFiles.removeAll(m_lastOutput);
    m_lastOutput = dest;
    // o clipe mantido leva os dados dos plugins (já no tempo dele) e o comentário
    const QFileInfo destInfo(dest);
    if (m_plugins) m_plugins->exportKept(dest, destInfo.size());
    if (!m_file.isEmpty()) {
        const QFileInfo orig(m_file);
        clipmeta::copyComment(clipmeta::key(orig.fileName(), orig.size()), clipmeta::key(destInfo.fileName(), destInfo.size()));
    }
    copyFileToClipboard(dest); // o Ctrl+V passa a apontar para o arquivo mantido
    m_keepBtn->hide();
    m_deleteBothBtn->hide(); // já é um arquivo mantido: nada de apagar por engano
    setResultLink(dest);
    m_status->setText("· Mantido");
    if (m_file.isEmpty() || !QFileInfo::exists(m_file)) return;
    if (deleteOriginalToo) {
        if (trashOriginal())
            showHome();
        return;
    }
    m_deleteOrigBtn->show(); // sem perguntar: quem quiser apagar o original usa o botão
}

void MainWindow::cancelExport()
{
    if (m_job) m_job->cancel();
}

void MainWindow::onExportFinished(bool ok, const QString &path, const QString &err, double sec)
{
    m_job->deleteLater();
    m_job = nullptr;
    m_bar->hide();
    m_status->show();
    m_cancelBtn->hide();
    m_exportBtn->setEnabled(true);
    if (!ok) {
        m_status->setText("Falhou: " + err.simplified());
        metrics::log("exportação falhou: " + err);
        return;
    }
    const qint64 size = QFileInfo(path).size();
    // "Até 20 MB": passou do limite? Apaga e tenta de novo com bitrate menor (até 2 vezes).
    if (m_exportMaxBytes > 0 && size > m_exportMaxBytes && m_limitTries < 2) {
        QFile::remove(path);
        m_limitScale *= std::max(0.35, double(m_exportMaxBytes) * 0.96 / double(size));
        ++m_limitTries;
        m_status->setText(QString("Ajustando para caber em %1 MB…").arg(m_exportMaxBytes / 1000000));
        metrics::log(QString("compressão: %1 bytes passou do limite; nova tentativa (escala %2)").arg(size).arg(m_limitScale, 0, 'f', 3));
        m_retrying = true;
        QTimer::singleShot(0, this, &MainWindow::startExport);
        return;
    }
    // calibra a previsão com o tempo real desta exportação
    const double outBytes = double(size);
    m_rates = calibrate(m_rates, m_exportKind, m_hasNvenc, m_exportClipSec,
                        m_exportKind == ExportKind::VideoReencode ? m_exportClipBytes : outBytes, sec);
    saveRates(m_rates);
    m_lastOutput = path;
    m_tempFiles << path;
    copyFileToClipboard(path);
    for (QWidget *b : {static_cast<QWidget *>(m_copyBtn), static_cast<QWidget *>(m_keepBtn), static_cast<QWidget *>(m_deleteBothBtn), static_cast<QWidget *>(m_fileLink)})
        b->show();
    setResultLink(path);
    QString done = QString("· Pronto em %1 s").arg(QString::number(sec, 'f', 1).replace('.', ','));
    if (m_exportMaxBytes > 0) {
        const QString mb = QString::number(double(size) / 1000000.0, 'f', 1).replace('.', ',') + " MB";
        done += size > m_exportMaxBytes ? QString(" · ficou com %1 (não coube em %2 MB; use um trecho menor)").arg(mb).arg(m_exportMaxBytes / 1000000)
                                         : " · " + mb;
    }
    m_status->setText(done);
    updateInfo(); // a próxima previsão já usa a taxa recalibrada
    metrics::log(QString("exportação concluída em %1 s: %2").arg(sec, 0, 'f', 2).arg(path));
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    for (const auto &u : e->mimeData()->urls())
        if (u.isLocalFile()) { openFile(u.toLocalFile()); break; }
}

// Roda do mouse sobre o botão de velocidade: para cima acelera, para baixo desacelera (um passo por "clique" da roda).
bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_commentEdit && e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(e)->key();
        if (key == Qt::Key_Escape) {
            m_commentEdit->setText(m_comment); // descarta o que foi digitado
            commitComment();
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            commitComment(); // grava e sai do campo
            return true;
        }
    }
    if (obj == m_speedBtn && e->type() == QEvent::Wheel) {
        auto *w = static_cast<QWheelEvent *>(e);
        m_speedWheelAcc += w->angleDelta().y();
        const int notches = m_speedWheelAcc / 120;
        if (notches != 0) {
            m_speedWheelAcc -= notches * 120;
            for (int i = 0; i < std::abs(notches); ++i)
                stepSpeed(notches > 0 ? +1 : -1);
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    commitComment(true); // nem ao sair do programa
    if (m_job) m_job->cancel();
    cleanupTemp(false);
    if (m_scanner) {
        m_scanner->disconnect(this);
        m_scanner->requestInterruption();
        m_scanner->wait(2000);
    }
    if (m_damage) {
        m_damage->disconnect(this);
        m_damage->requestInterruption();
        m_damage->wait(2000);
    }
    if (m_prefetch) {
        m_prefetch->disconnect(this);
        m_prefetch->requestInterruption();
        m_prefetch->wait(3000);
    }
    QMainWindow::closeEvent(e);
}

} // namespace shear

namespace shear {

std::vector<AudioTrack> MainWindow::currentAudio() const
{
    std::vector<AudioTrack> v;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const double gain = (i == 1 && m_micFader) ? double(m_micFader->value()) : 0.0;
        v.push_back({m_rows[i].on->isChecked(), gain});
    }
    return v;
}

void MainWindow::rebuildAudioRows(int n)
{
    m_rows.clear();
    delete m_soloAudioBtn; // o botão de áudio da barra de cima (caso de 1 faixa só)
    m_soloAudioBtn = nullptr;
    m_micFader = nullptr;
    m_micVal = nullptr;
    while (auto *it = m_audioRows->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (int i = 0; i < n; ++i) {
        // um botão só por faixa: aceso = você ouve e vai no vídeo; apagado = fora
        auto *on = new QToolButton;
        on->setCheckable(true);
        on->setChecked(i == 0); // padrão: só o desktop; o mic fica desligado
        on->setFocusPolicy(Qt::NoFocus);
        if (n == 1) {
            // Uma faixa só: sem painel lateral. Um alto-falante na barra de cima liga/desliga o som (preview e vídeo).
            m_soloAudioBtn = on;
            const QString family = iconFontFamily();
            on->setFixedSize(38, on->sizeHint().height());
            if (!family.isEmpty()) {
                QFont f(family);
                f.setPixelSize(16);
                on->setFont(f);
            }
            const auto refresh = [on, family] {
                const bool isOn = on->isChecked();
                on->setText(!family.isEmpty() ? QString(QChar(isOn ? 0xE767 : 0xE74F))
                                              : QString::fromUcs4(isOn ? U"🔊" : U"🔇"));
                on->setToolTip(isOn ? "Áudio ligado: você ouve e ele vai no vídeo exportado (clique para tirar o áudio)"
                                    : "Áudio desligado: o vídeo exportado fica sem som (clique para ligar)");
            };
            refresh();
            connect(on, &QToolButton::toggled, on, refresh);
            m_topBar->insertWidget(m_topBar->count() - 1, on); // antes da engrenagem
        } else {
            on->setText(trackName(i, n));
            on->setProperty("role", "pill");
            on->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            on->setToolTip(i == 1 && n >= 2
                ? "Ligue para ouvir o microfone e juntá-lo ao desktop numa faixa só no vídeo exportado."
                : "Ligada: você ouve esta faixa e ela vai no vídeo exportado.");
            m_audioRows->addWidget(on);
        }
        m_rows.push_back({on});
        connect(on, &QToolButton::toggled, this, [this, i](bool isOn) {
            if (i == 1 && m_micFader) {
                m_micFader->setEnabled(isOn);
                m_micVal->setEnabled(isOn);
            }
            updateInfo();
            applyPreviewAudio();
        });
    }

    if (n >= 2) { // só o mic tem ajuste de volume
        auto *cap = new QLabel("Vol. do mic");
        cap->setProperty("role", "cap");
        cap->setAlignment(Qt::AlignHCenter);
        m_micFader = new VerticalFader;
        m_micVal = new QToolButton;
        m_micVal->setText("0 dB");
        m_micVal->setProperty("role", "val");
        m_micVal->setFocusPolicy(Qt::NoFocus);
        m_micVal->setToolTip("Clique para zerar");
        const bool micOn = m_rows[1].on->isChecked();
        m_micFader->setEnabled(micOn);
        m_micVal->setEnabled(micOn);
        m_audioRows->addSpacing(4);
        m_audioRows->addWidget(cap);
        m_audioRows->addWidget(m_micFader, 1); // o fader ocupa toda a altura disponível: grande e sem espaço vazio
        m_audioRows->addWidget(m_micVal, 0, Qt::AlignHCenter);
        m_lastMicGain = 0;
        connect(m_micFader, &VerticalFader::valueChanged, this, [this](int g) {
            if (!m_restoring)
                m_redo.clear();
            if (!m_restoring && (!m_micClock.isValid() || m_micClock.elapsed() > 500)) {
                EditState before = currentState(); // início de um gesto: guarda o volume de ANTES
                before.micGain = m_lastMicGain;
                pushUndo(before);
            }
            if (!m_restoring)
                m_micClock.restart();
            m_lastMicGain = g;
            m_micVal->setText(QString("%1%2 dB").arg(g > 0 ? "+" : "").arg(g));
            updateInfo();
            m_audioTimer->start(); // evita reconfigurar o filtro a cada pixel arrastado
        });
        connect(m_micVal, &QToolButton::clicked, m_micFader, [this] { m_micFader->setValue(0); });
    } else {
        m_audioRows->addStretch(1);
    }
    m_audioBox->setFixedWidth(96);
    m_audioBox->setMaximumHeight(QWIDGETSIZE_MAX);
    m_audioBox->setVisible(n >= 2); // 2+ faixas: painel lateral; 1 faixa: só o alto-falante na barra de cima
}

void MainWindow::applyPreviewAudio()
{
    if (m_rows.empty() || !m_mpv) return;
    const QString graph = buildAudioMixGraph(currentAudio(), true, "ao"); // o que você ouve = o que vai no arquivo
    m_mpv->setAudioGraph(graph);
    m_scrub->setAudioGraph(graph);
}

void MainWindow::setCutMode(CutMode m)
{
    (m == CutMode::Exact ? m_exactBtn : m_fastBtn)->setChecked(true);
}

double MainWindow::snapIfFast(double t) const
{
    return m_exactBtn->isChecked() ? t : m_timeline->snapToKeyframe(t);
}

void MainWindow::onModeChanged()
{
    const bool fast = !m_exactBtn->isChecked();
    m_timeline->setSnapStart(fast);
    if (fast) { // no modo Rápido o início só existe em keyframes: encaixa o marcador que já estava lá
        const double s = m_timeline->snapToKeyframe(m_timeline->start());
        if (s != m_timeline->start()) {
            pushUndo(currentState());
            m_timeline->setStart(s);
            m_mpv->seek(s);
        }
    }
    updateInfo();
}

void MainWindow::changeVolume(int delta)
{
    m_volume = std::clamp(m_volume + delta, 0, 100);
    m_mpv->setVolume(m_volume);
    m_scrub->setVolume(m_volume);
    m_mpv->showText(QString("Volume %1%").arg(m_volume));
    QSettings().setValue("player/volume", m_volume);
}

} // namespace shear

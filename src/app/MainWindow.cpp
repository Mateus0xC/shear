#include "MainWindow.h"
#include "SettingsDialog.h"
#include "core/CutPlanner.h"
#include "core/FfmpegCommandBuilder.h"
#include "core/OutputNaming.h"
#include "export/ExportJob.h"
#include "platform/Windows.h"
#include "SoundToggle.h"
#include "VerticalFader.h"
#include "player/AudioScrubber.h"
#include "player/MpvWidget.h"
#include "timeline/KeyframeScanner.h"
#include "timeline/TimelineWidget.h"
#include "util/Metrics.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QPainter>
#include <QPalette>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QCoreApplication>
#include <QDateTime>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

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

static QString trackName(int i, int n)
{
    if (n >= 2 && i == 0) return "Desktop";
    if (n >= 2 && i == 1) return "Mic";
    return QString("Faixa %1").arg(i + 1);
}

// Clipe mais recente salvo há até 10 s: o app abre direto nele. Se foi há até 5 min: fica destacado no topo da lista.
static constexpr qint64 kFreshOpenSec = 10, kFreshHighlightSec = 300;

static QString formatAgo(qint64 s)
{
    s = std::max<qint64>(s, 0);
    return s < 60 ? QString("%1 s").arg(s) : QString("%1 min").arg(s / 60);
}

static const char *kVideoGlobs[] = {"*.mp4", "*.mkv", "*.mov", "*.webm", "*.avi"};

MainWindow::MainWindow()
{
    setWindowTitle("Shear");
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
    auto inEditor = [this] { return m_stack->currentIndex() == 1; };
    sc(Qt::Key_Space, [=, this] { if (inEditor()) m_mpv->togglePause(); });
    sc(Qt::Key_I, [=, this] { if (inEditor()) markStart(m_mpv->position()); });
    sc(Qt::Key_O, [=, this] { if (inEditor()) markEnd(m_mpv->position()); });
    sc(Qt::Key_Comma, [=, this] { if (inEditor()) { m_mpv->setPaused(true); m_mpv->stepFrame(false); } });
    sc(Qt::Key_Period, [=, this] { if (inEditor()) { m_mpv->setPaused(true); m_mpv->stepFrame(true); } });
    sc(Qt::Key_Left, [=, this] { if (inEditor()) { m_mpv->setPaused(true); m_mpv->stepFrame(false); } });
    sc(Qt::Key_Right, [=, this] { if (inEditor()) { m_mpv->setPaused(true); m_mpv->stepFrame(true); } });
    sc(Qt::Key_Up, [=, this] { if (inEditor()) changeVolume(+5); });
    sc(Qt::Key_Down, [=, this] { if (inEditor()) changeVolume(-5); });
    sc(Qt::Key_J, [=, this] { if (inEditor()) m_mpv->seek(std::max(0.0, m_mpv->position() - 5)); });
    sc(Qt::Key_L, [=, this] { if (inEditor()) m_mpv->seek(m_mpv->position() + 5); });
    sc(Qt::Key_K, [=, this] { if (inEditor()) snapStartToKeyframe(); });
    sc(Qt::Key_Return, [=, this] { if (inEditor()) startExport(); });
    sc(Qt::Key_Enter, [=, this] { if (inEditor()) startExport(); });
    sc(Qt::Key_Escape, [=, this] { if (inEditor() && !m_job) showHome(); });
}

QWidget *MainWindow::buildHome()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(32, 28, 32, 24);
    auto *title = new QLabel("<h1>Shear</h1>");
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
    m_recent = new QListWidget;
    m_recent->setAlternatingRowColors(true);
    connect(m_recent, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *i) { openFile(i->data(Qt::UserRole).toString()); });
    l->addWidget(title);
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
    auto *back = new QPushButton("← Clipes");
    back->setFocusPolicy(Qt::NoFocus);
    connect(back, &QPushButton::clicked, this, [this] { if (!m_job) showHome(); });
    m_title = new QLabel;
    top->addWidget(back);
    top->addWidget(m_title, 1);
    l->addLayout(top);

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
        pv->addSpacing(10);
        m_scrubBtn = new SoundToggle;
        m_scrubBtn->setChecked(QSettings().value("player/scrubAudio", true).toBool());
        m_scrubBtn->setToolTip("Som ao arrastar: ouve o áudio enquanto você arrasta na timeline, com o vídeo parado. "
                               "Clique para ligar ou desligar.");
        pv->addWidget(m_scrubBtn);
        connect(m_scrubBtn, &QAbstractButton::toggled, this, [](bool on) { QSettings().setValue("player/scrubAudio", on); });
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
    opts->addWidget(seg);
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
    m_copyBtn = new QPushButton("Copiar");
    m_copyBtn->setToolTip("Copiar o arquivo de novo para a área de transferência (Ctrl+V no Discord)");
    m_openFolderBtn = new QPushButton("Abrir pasta");
    m_keepBtn = new QPushButton("Manter");
    m_keepBtn->setToolTip("Move o clipe para a pasta de clipes mantidos e impede que seja apagado ao fechar.");
    m_deleteBtn = new QPushButton("Apagar");
    hideResultButtons();
    ex->addWidget(m_status, 1);
    ex->addWidget(m_bar, 1);
    ex->addWidget(m_cancelBtn);
    ex->addWidget(m_copyBtn);
    ex->addWidget(m_keepBtn);
    ex->addWidget(m_deleteBtn);
    ex->addWidget(m_openFolderBtn);
    bl->addWidget(rowC);
    l->addLayout(bottom);

    // preencher com os padrões
    setCutMode(m_cfg.mode);

    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::startExport);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::cancelExport);
    connect(m_copyBtn, &QPushButton::clicked, this, [this] { copyFileToClipboard(m_lastOutput); });
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this] { revealInExplorer(m_lastOutput); });
    connect(m_keepBtn, &QPushButton::clicked, this, &MainWindow::keepLastOutput);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MainWindow::deleteLastOutput);
    connect(m_modeGroup, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool on) { if (on) onModeChanged(); });
    onModeChanged();
    connect(m_mpv, &MpvWidget::fileLoaded, this, &MainWindow::applyPreviewAudio);

    connect(m_mpv, &MpvWidget::positionChanged, m_timeline, &TimelineWidget::setPosition);
    connect(m_mpv, &MpvWidget::durationChanged, this, [this](double d) {
        m_timeline->setDuration(d);
        updateInfo();
    });
    connect(m_mpv, &MpvWidget::firstFrame, this, [this] {
        metrics::log(QString("primeiro frame: %1 ms após abrir o arquivo").arg(m_openClock.elapsed()));
    });
    connect(m_timeline, &TimelineWidget::seekRequested, this, [this](double t) {
        m_mpv->seek(t);
        // Arrastando (vídeo parado): o som sai de um player só de áudio, que segue o mouse em qualquer velocidade.
        if (m_scrubbing && m_scrubBtn->isChecked())
            m_scrub->scrub(t);
    });
    connect(m_mpv, &MpvWidget::fpsChanged, m_timeline, &TimelineWidget::setFps);
    connect(m_timeline, &TimelineWidget::startChanged, this, [this] { updateInfo(); });
    connect(m_timeline, &TimelineWidget::endChanged, this, [this] { updateInfo(); });
    // Enquanto o mouse está pressionado na timeline o vídeo fica pausado (a timeline pede o seek a cada movimento,
    // então você vê o quadro sob o mouse). Ao soltar, volta a tocar se estava tocando.
    connect(m_timeline, &TimelineWidget::dragStarted, this, [this] {
        m_resumeAfterDrag = !m_mpv->paused();
        if (m_resumeAfterDrag)
            m_mpv->setPaused(true);
        m_scrubbing = true;
    });
    connect(m_timeline, &TimelineWidget::dragFinished, this, [this] {
        m_scrubbing = false;
        m_scrub->stop();
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
    refreshRecent(m_autoOpenFresh);
    detectNvenc();
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
struct RecentItem { QString path; QDateTime created; qint64 size; };

void MainWindow::showHome()
{
    m_stack->setCurrentIndex(0);
    refreshRecent(false); // um clipe novo pode ter sido salvo enquanto você editava
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
        QMetaObject::invokeMethod(this, [this, v, autoOpen] {
            const QDateTime now = QDateTime::currentDateTime();
            const QColor accent = palette().color(QPalette::Highlight);
            m_recent->clear();
            for (const auto &r : v) {
                const qint64 age = r.created.secsTo(now);
                const bool fresh = &r == &v.front() && age <= kFreshHighlightSec; // só o ÚLTIMO clipe, se tiver até 5 min
                const QString base = QString("%1    %2    %3 MB")
                                         .arg(QFileInfo(r.path).fileName(), r.created.toString("dd/MM/yyyy HH:mm"))
                                         .arg(r.size / 1048576);
                auto *i = new QListWidgetItem(fresh ? QString("●  NOVO · há %1    %2").arg(formatAgo(age), base) : base);
                if (fresh) { // o clipe mais recente (salvo há até 5 min): destaque; a lista já vem do mais novo para o mais antigo
                    QColor bg = accent;
                    bg.setAlpha(70);
                    i->setBackground(bg);
                    QFont f = i->font();
                    f.setBold(true);
                    i->setFont(f);
                }
                i->setData(Qt::UserRole, r.path);
                i->setToolTip(r.path);
                m_recent->addItem(i);
            }
            if (v.empty())
                m_recent->addItem("Nenhum clipe encontrado em " + m_cfg.effectiveShadowPlayDir());
            else if (autoOpen && !m_job && v.front().created.secsTo(now) <= kFreshOpenSec && v.front().path != m_file)
                openFile(v.front().path); // clipado há segundos: já abre nele
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

void MainWindow::openSettings()
{
    SettingsDialog d(m_cfg, this);
    if (d.exec() != QDialog::Accepted) return;
    m_cfg = d.config();
    m_cfg.save();
    if (m_editor) {
        setCutMode(m_cfg.mode);
    }
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
    m_openClock.start();
    m_fileSize = QFileInfo(path).size();
    if (!m_editor) {
        m_editor = buildEditor();
        m_stack->addWidget(m_editor);
    }
    m_file = path;
    m_title->setText(QFileInfo(path).fileName());
    setWindowTitle("Shear — " + QFileInfo(path).fileName());
    m_timeline->reset(0);
    m_status->clear();
    hideResultButtons();
    m_stack->setCurrentIndex(1);
    rebuildAudioRows(0);
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
        updateInfo();
    });
    connect(m_scanner, &QThread::finished, this, [this] {
        m_timeline->setScanDone(true);
        metrics::log(QString("keyframes lidos: %1 ms após abrir o arquivo").arg(m_openClock.elapsed()));
        updateInfo();
    });
    QTimer::singleShot(0, m_scanner, [s = m_scanner] { s->start(QThread::LowPriority); });
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

void MainWindow::markStart(double t)
{
    t = snapIfFast(t);
    if (t >= m_timeline->end()) m_timeline->setEnd(m_timeline->duration());
    m_timeline->setStart(t);
    updateInfo();
}

void MainWindow::markEnd(double t)
{
    if (t <= m_timeline->start()) m_timeline->setStart(0);
    m_timeline->setEnd(t);
    updateInfo();
}

void MainWindow::snapStartToKeyframe()
{
    if (auto k = m_timeline->nearestKeyframe(m_timeline->start())) {
        m_timeline->setStart(*k);
        m_mpv->seek(*k);
        updateInfo();
    }
}

ExportRequest MainWindow::currentRequest() const
{
    ExportRequest r;
    r.inputPath = m_file;
    r.startSec = m_timeline->start();
    r.endSec = m_timeline->end();
    r.mode = m_exactBtn->isChecked() ? CutMode::Exact : CutMode::Fast;
    r.hasNvenc = m_hasNvenc;
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
    const auto r = currentRequest();
    const auto plan = planCut(r);
    m_times->setText(QString("Início %1    Fim %2    Duração %3%4")
                         .arg(fmt(r.startSec), fmt(r.endSec), fmt(r.endSec - r.startSec),
                              m_timeline->scanDone() ? "" : "    (lendo keyframes…)"));
    m_timeline->setGhostStart(plan.earlyBySec >= 0.05 ? std::optional<double>(plan.actualStartSec)
                                                      : std::nullopt);

    QStringList msg;
    const QString w = earlyStartWarning(plan.earlyBySec);
    m_early->setText(w.isEmpty() ? QStringLiteral(" ")
                                 : "⚠ " + w + " — K encaixa no keyframe");
    msg << (r.mode == CutMode::Fast
                ? "Rápido: corte instantâneo; o início encaixa nas marcas da timeline."
                : "Exato: corta no frame escolhido; a exportação demora um pouco mais.");
    if (!r.audio.empty() && enabledTrackCount(r.audio) == 0)
        msg << "⚠ Nenhuma faixa de áudio ligada: o clipe ficará sem som.";
    m_warning->setText(msg.join("   ·   "));

    // previsão do tempo de exportação
    const ExportKind kind = exportKind(r, plan);
    const double clipSec = std::max(0.0, r.endSec - plan.actualStartSec);
    const double total = m_timeline->duration();
    const double clipBytes = total > 0 ? double(m_fileSize) * clipSec / total : 0.0;
    m_eta->setText("Exportar: " + formatDuration(estimateSeconds(kind, r.hasNvenc, clipSec, clipBytes, m_rates)));
}

void MainWindow::startExport()
{
    if (m_job || m_file.isEmpty()) return;
    auto r = currentRequest();
    if (r.endSec - r.startSec < 0.1) {
        m_status->setText("Selecione um trecho maior.");
        return;
    }
    const auto plan = planCut(r);
    const QString dir = tempDir(); // exports são temporários até o usuário clicar em Manter
    QDir().mkpath(dir);
    r.outputPath = uniqueOutputPath(dir, QFileInfo(m_file).completeBaseName(), QTime::currentTime(),
                                    [](const QString &p) { return QFileInfo::exists(p); });
    const auto args = buildFfmpegArgs(r, plan);
    metrics::log("ffmpeg " + args.join(' '));

    m_exportKind = exportKind(r, plan);
    m_exportClipSec = r.endSec - plan.actualStartSec;
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

void MainWindow::hideResultButtons()
{
    for (auto *b : {m_copyBtn, m_keepBtn, m_deleteBtn, m_openFolderBtn})
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

void MainWindow::deleteLastOutput()
{
    if (m_lastOutput.isEmpty()) return;
    clearClipboardIfOurs({m_lastOutput});
    QFile::remove(m_lastOutput);
    m_tempFiles.removeAll(m_lastOutput);
    m_status->setText("Clipe apagado.");
    m_lastOutput.clear();
    hideResultButtons();
}

void MainWindow::keepLastOutput()
{
    if (m_lastOutput.isEmpty() || !m_tempFiles.contains(m_lastOutput)) return;
    const QString dir = m_cfg.outputDir.isEmpty() ? QFileInfo(m_file).absolutePath() : m_cfg.outputDir;
    QDir().mkpath(dir);
    const QFileInfo src(m_lastOutput);
    QString dest = dir + "/" + src.fileName();
    for (int i = 2; QFileInfo::exists(dest); ++i) // nunca sobrescreve
        dest = dir + "/" + src.completeBaseName() + QString("_%1.").arg(i) + src.suffix();
    if (!QFile::rename(m_lastOutput, dest)) {
        m_status->setText("Não foi possível mover o clipe para " + QDir::toNativeSeparators(dir));
        return;
    }
    m_tempFiles.removeAll(m_lastOutput);
    m_lastOutput = dest;
    copyFileToClipboard(dest); // o Ctrl+V passa a apontar para o arquivo mantido
    m_keepBtn->hide();
    m_deleteBtn->hide();
    m_status->setText("Mantido em " + QDir::toNativeSeparators(dest));
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
    // calibra a previsão com o tempo real desta exportação
    const double outBytes = double(QFileInfo(path).size());
    m_rates = calibrate(m_rates, m_exportKind, m_hasNvenc, m_exportClipSec,
                        m_exportKind == ExportKind::VideoReencode ? m_exportClipBytes : outBytes, sec);
    saveRates(m_rates);
    m_lastOutput = path;
    m_tempFiles << path;
    copyFileToClipboard(path);
    for (auto *b : {m_copyBtn, m_keepBtn, m_deleteBtn, m_openFolderBtn})
        b->show();
    m_status->setText(QString("Pronto em %1 s (previsto %2) · copiado: Ctrl+V no Discord · temporário: apagado ao fechar, "
                              "a menos que clique em Manter · %3")
                          .arg(QString::number(sec, 'f', 1).replace('.', ','), formatDuration(m_exportEstimate),
                               QFileInfo(path).fileName()));
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

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_job) m_job->cancel();
    cleanupTemp(false);
    if (m_scanner) {
        m_scanner->disconnect(this);
        m_scanner->requestInterruption();
        m_scanner->wait(2000);
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
    m_micFader = nullptr;
    m_micVal = nullptr;
    while (auto *it = m_audioRows->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (int i = 0; i < n; ++i) {
        // um botão só por faixa: aceso = você ouve e vai no vídeo; apagado = fora
        auto *on = new QToolButton;
        on->setText(trackName(i, n));
        on->setCheckable(true);
        on->setChecked(i == 0); // padrão: só o desktop; o mic fica desligado
        on->setProperty("role", "pill");
        on->setFocusPolicy(Qt::NoFocus);
        on->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        on->setToolTip(i == 1 && n >= 2
            ? "Ligue para ouvir o microfone e juntá-lo ao desktop numa faixa só no vídeo exportado."
            : "Ligada: você ouve esta faixa e ela vai no vídeo exportado.");
        m_audioRows->addWidget(on);
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
        connect(m_micFader, &VerticalFader::valueChanged, this, [this](int g) {
            m_micVal->setText(QString("%1%2 dB").arg(g > 0 ? "+" : "").arg(g));
            updateInfo();
            m_audioTimer->start(); // evita reconfigurar o filtro a cada pixel arrastado
        });
        connect(m_micVal, &QToolButton::clicked, m_micFader, [this] { m_micFader->setValue(0); });
    } else {
        m_audioRows->addStretch(1);
    }
    m_audioBox->setFixedWidth(96);
    m_audioBox->setMaximumHeight(n >= 2 ? QWIDGETSIZE_MAX : 14 + n * 32 + 50);
    // 2+ faixas: o cartão preenche a altura toda; com 1 faixa só, um cartão mínimo no topo
    m_editorRoot->setAlignment(m_audioBox, n >= 2 ? Qt::Alignment() : Qt::AlignTop);
    m_audioBox->setVisible(n > 0);
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

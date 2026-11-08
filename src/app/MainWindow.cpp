#include "MainWindow.h"
#include "SettingsDialog.h"
#include "core/CutPlanner.h"
#include "core/FfmpegCommandBuilder.h"
#include "core/OutputNaming.h"
#include "export/ExportJob.h"
#include "platform/Windows.h"
#include "player/MpvWidget.h"
#include "timeline/KeyframeScanner.h"
#include "timeline/TimelineWidget.h"
#include "util/Metrics.h"

#include <QComboBox>
#include <QFrame>
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

static QToolButton *toggleButton(const QString &text, const QString &tip, bool checked)
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setChecked(checked);
    b->setProperty("role", "toggle");
    b->setFocusPolicy(Qt::NoFocus);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return b;
}

static const char *kVideoGlobs[] = {"*.mp4", "*.mkv", "*.mov", "*.webm", "*.avi"};

MainWindow::MainWindow()
{
    setWindowTitle("Shear");
    resize(1100, 740);
    setAcceptDrops(true);
    m_cfg = Config::load();

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildHome());
    // O editor (com o QOpenGLWidget do mpv) só é criado ao abrir o 1º vídeo:
    // um QOpenGLWidget, mesmo escondido, faz a janela inteira pagar o custo do OpenGL no show().
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
    sc(Qt::Key_J, [=, this] { if (inEditor()) m_mpv->seek(std::max(0.0, m_mpv->position() - 5)); });
    sc(Qt::Key_L, [=, this] { if (inEditor()) m_mpv->seek(m_mpv->position() + 5); });
    sc(Qt::Key_K, [=, this] { if (inEditor()) snapStartToKeyframe(); });
    sc(Qt::Key_Return, [=, this] { if (inEditor()) startExport(); });
    sc(Qt::Key_Enter, [=, this] { if (inEditor()) startExport(); });
    sc(Qt::Key_Escape, [=, this] { if (inEditor() && !m_job) m_stack->setCurrentIndex(0); });
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
    auto *l = new QVBoxLayout; // coluna principal: player, timeline, opções, exportar
    root->addLayout(l, 1);

    auto *top = new QHBoxLayout;
    auto *back = new QPushButton("← Clipes");
    back->setFocusPolicy(Qt::NoFocus);
    connect(back, &QPushButton::clicked, this, [this] { if (!m_job) m_stack->setCurrentIndex(0); });
    m_title = new QLabel;
    top->addWidget(back);
    top->addWidget(m_title, 1);
    l->addLayout(top);

    m_mpv = new MpvWidget;
    m_mpv->setMinimumHeight(240);
    l->addWidget(m_mpv, 1);

    m_timeline = new TimelineWidget;
    l->addWidget(m_timeline);

    // Linha de tempos + aviso do início antecipado (linha única, altura fixa: o player não se mexe)
    m_times = new QLabel;
    m_early = new QLabel(" ");
    m_early->setStyleSheet("color: #ffa028; font-weight: 600;");
    m_early->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *timesRow = new QHBoxLayout;
    timesRow->addWidget(m_times);
    timesRow->addStretch();
    timesRow->addWidget(m_early);
    l->addLayout(timesRow);

    // --- Mixer de áudio (painel lateral): um fader vertical por faixa + o do preview ---
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
            #audioPanel QLabel[role="title"] { font-weight: 700; font-size: 13px; }
            #audioPanel QLabel[role="name"] { font-weight: 600; }
            #audioPanel QLabel[role="sub"], #audioPanel QLabel[role="hint"] { color: %3; font-size: 11px; }
            #audioPanel QSlider::groove:vertical { width: 6px; border-radius: 3px; background: %2; }
            #audioPanel QSlider::handle:vertical { height: 16px; width: 26px; margin: 0 -10px;
                                                  border-radius: 6px; background: %4; border: 1px solid %2; }
            #audioPanel QSlider::handle:vertical:hover { background: %5; }
            #audioPanel QToolButton[role="toggle"] { border: 1px solid %2; border-radius: 6px; padding: 5px 4px;
                                                    background: transparent; color: %3; }
            #audioPanel QToolButton[role="toggle"]:checked { background: %5; border-color: %5; color: %6; font-weight: 600; }
            #audioPanel QToolButton[role="val"] { border: none; border-radius: 4px; padding: 2px 6px; background: transparent;
                                                 color: %4; font-weight: 600; }
            #audioPanel QToolButton[role="val"]:hover { background: %2; }
            #audioPanel QFrame[role="sep"] { background: %2; max-width: 1px; border: none; }
        )").arg(base, mid, dim, text, accent, onAccent));

        auto *pv = new QVBoxLayout(m_audioBox);
        pv->setContentsMargins(10, 10, 10, 10);
        pv->setSpacing(8);
        auto *title = new QLabel("Áudio");
        title->setProperty("role", "title");
        pv->addWidget(title);

        auto *cols = new QHBoxLayout;
        cols->setSpacing(4);
        // faixas (rolagem horizontal só se houver muitas)
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // o painel cresce com o nº de faixas
        scroll->setStyleSheet("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }");
        m_audioRowsHost = new QWidget;
        m_audioRows = new QHBoxLayout(m_audioRowsHost);
        m_audioRows->setContentsMargins(0, 0, 0, 0);
        m_audioRows->setSpacing(4);
        scroll->setWidget(m_audioRowsHost);
        cols->addWidget(scroll, 1);

        auto *sep = new QFrame;
        sep->setProperty("role", "sep");
        sep->setFrameShape(QFrame::VLine);
        cols->addWidget(sep);

        // coluna do volume do preview (não vai para o arquivo)
        auto *mc = new QVBoxLayout;
        mc->setContentsMargins(0, 0, 0, 0);
        mc->setSpacing(4);
        auto *mname = new QLabel("Preview");
        mname->setProperty("role", "name");
        mname->setAlignment(Qt::AlignHCenter);
        auto *msub = new QLabel("só p/ ouvir");
        msub->setProperty("role", "sub");
        msub->setAlignment(Qt::AlignHCenter);
        msub->setToolTip("Volume do preview: não muda o arquivo exportado.");
        m_volSlider = new QSlider(Qt::Vertical);
        m_volSlider->setRange(0, 100);
        m_volSlider->setFocusPolicy(Qt::NoFocus);
        m_volLabel = new QToolButton;
        m_volLabel->setProperty("role", "val");
        m_volLabel->setFocusPolicy(Qt::NoFocus);
        m_volLabel->setToolTip("Clique para voltar a 100%");
        const int savedVol = QSettings().value("player/volume", 100).toInt();
        m_volSlider->setValue(savedVol);
        m_volLabel->setText(QString("%1%").arg(savedVol));
        m_mpv->setVolume(savedVol);
        mc->addWidget(mname);
        mc->addWidget(msub);
        mc->addWidget(m_volSlider, 1, Qt::AlignHCenter);
        mc->addWidget(m_volLabel, 0, Qt::AlignHCenter);
        m_muteBtn = toggleButton("Mudo", "Silenciar o preview (o nível do fader é mantido)", false);
        mc->addWidget(m_muteBtn);
        auto *spacer = new QWidget; // alinha o fim da coluna com os botões Ouvir/Exportar das faixas
        spacer->setFixedHeight(m_muteBtn->sizeHint().height());
        mc->addWidget(spacer);
        auto *mcw = new QWidget;
        mcw->setLayout(mc);
        mcw->setFixedWidth(76);
        cols->addWidget(mcw);
        pv->addLayout(cols, 1);

        m_audioHint = new QLabel;
        m_audioHint->setProperty("role", "hint");
        m_audioHint->setWordWrap(true);
        m_audioHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_audioHint->setMinimumHeight(m_audioHint->fontMetrics().lineSpacing() * 4);
        pv->addWidget(m_audioHint);

        m_audioBox->hide();
        root->addWidget(m_audioBox);
    }
    m_audioTimer = new QTimer(this);
    m_audioTimer->setSingleShot(true);
    m_audioTimer->setInterval(150);
    connect(m_audioTimer, &QTimer::timeout, this, &MainWindow::applyPreviewAudio);
    connect(m_volSlider, &QSlider::valueChanged, this, [this](int v) {
        m_volLabel->setText(QString("%1%").arg(v));
        if (m_muteBtn->isChecked()) { // mexer no fader tira do mudo
            QSignalBlocker b(m_muteBtn);
            m_muteBtn->setChecked(false);
        }
        m_mpv->setVolume(v);
        QSettings().setValue("player/volume", v);
    });
    connect(m_muteBtn, &QToolButton::toggled, this,
            [this](bool on) { m_mpv->setVolume(on ? 0 : m_volSlider->value()); });
    connect(m_volLabel, &QToolButton::clicked, m_volSlider, [this] { m_volSlider->setValue(100); });

    auto *opts = new QHBoxLayout;
    m_presetBox = new QComboBox;
    m_presetBox->addItems({"Corte rápido (copia)", "Mixar mic + desktop"});
    m_modeBox = new QComboBox;
    m_modeBox->addItems({"Início: Rápido", "Início: Exato", "Início: Automático"});
    for (auto *c : {m_presetBox, m_modeBox}) {
        c->setFocusPolicy(Qt::TabFocus);
        opts->addWidget(c);
    }
    opts->addStretch();
    l->addLayout(opts);

    m_warning = new QLabel;
    m_warning->setWordWrap(true);
    m_warning->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_warning->setMinimumHeight(m_warning->fontMetrics().lineSpacing() * 2 + 4); // reserva 2 linhas
    l->addWidget(m_warning);

    auto *ex = new QHBoxLayout;
    m_exportBtn = new QPushButton("Exportar (Enter)");
    m_exportBtn->setMinimumHeight(34);
    m_exportBtn->setFocusPolicy(Qt::NoFocus);
    m_cancelBtn = new QPushButton("Cancelar");
    m_cancelBtn->hide();
    m_bar = new QProgressBar;
    m_bar->setRange(0, 1000);
    m_bar->hide();
    m_copyBtn = new QPushButton("Copiar de novo");
    m_openFolderBtn = new QPushButton("Abrir pasta");
    m_keepBtn = new QPushButton("Manter");
    m_keepBtn->setToolTip("Move o clipe para a pasta de clipes mantidos e impede que seja apagado ao fechar.");
    m_deleteBtn = new QPushButton("Apagar");
    hideResultButtons();
    ex->addWidget(m_exportBtn);
    ex->addWidget(m_bar, 1);
    ex->addWidget(m_cancelBtn);
    ex->addWidget(m_copyBtn);
    ex->addWidget(m_keepBtn);
    ex->addWidget(m_deleteBtn);
    ex->addWidget(m_openFolderBtn);
    l->addLayout(ex);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_status->setMinimumHeight(m_status->fontMetrics().lineSpacing() * 2 + 4); // reserva 2 linhas
    l->addWidget(m_status);

    // preencher com os padrões
    m_presetBox->setCurrentIndex(int(m_cfg.preset));
    m_modeBox->setCurrentIndex(int(m_cfg.mode));

    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::startExport);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::cancelExport);
    connect(m_copyBtn, &QPushButton::clicked, this, [this] { copyFileToClipboard(m_lastOutput); });
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this] { revealInExplorer(m_lastOutput); });
    connect(m_keepBtn, &QPushButton::clicked, this, &MainWindow::keepLastOutput);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MainWindow::deleteLastOutput);
    connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this] { updateInfo(); });
    connect(m_presetBox, &QComboBox::currentIndexChanged, this, [this] {
        applyExportDefaults();
        updateInfo();
        applyPreviewAudio();
    });
    connect(m_mpv, &MpvWidget::fileLoaded, this, &MainWindow::applyPreviewAudio);

    connect(m_mpv, &MpvWidget::positionChanged, m_timeline, &TimelineWidget::setPosition);
    connect(m_mpv, &MpvWidget::durationChanged, this, [this](double d) {
        m_timeline->setDuration(d);
        updateInfo();
    });
    connect(m_mpv, &MpvWidget::firstFrame, this, [this] {
        metrics::log(QString("primeiro frame: %1 ms após abrir o arquivo").arg(m_openClock.elapsed()));
    });
    connect(m_timeline, &TimelineWidget::seekRequested, m_mpv, &MpvWidget::seek);
    connect(m_timeline, &TimelineWidget::startChanged, this, [this] { updateInfo(); });
    connect(m_timeline, &TimelineWidget::endChanged, this, [this] { updateInfo(); });
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
    refreshRecent();
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

struct RecentItem { QString path; QDateTime mtime; qint64 size; };

void MainWindow::refreshRecent()
{
    const QString dir = m_cfg.effectiveShadowPlayDir();
    auto *t = QThread::create([this, dir] {
        std::vector<RecentItem> v;
        QDirIterator it(dir, {std::begin(kVideoGlobs), std::end(kVideoGlobs)}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            v.push_back({it.filePath(), it.fileInfo().lastModified(), it.fileInfo().size()});
        }
        std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.mtime > b.mtime; });
        if (v.size() > 40) v.resize(40);
        QMetaObject::invokeMethod(this, [this, v] {
            m_recent->clear();
            for (const auto &r : v) {
                auto *i = new QListWidgetItem(QString("%1    %2    %3 MB")
                                                  .arg(QFileInfo(r.path).fileName(),
                                                       r.mtime.toString("dd/MM/yyyy HH:mm"))
                                                  .arg(r.size / 1048576));
                i->setData(Qt::UserRole, r.path);
                i->setToolTip(r.path);
                m_recent->addItem(i);
            }
            if (v.empty())
                m_recent->addItem("Nenhum clipe encontrado em " + m_cfg.effectiveShadowPlayDir());
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
        m_presetBox->setCurrentIndex(int(m_cfg.preset));
        m_modeBox->setCurrentIndex(int(m_cfg.mode));
        applyExportDefaults();
    }
    refreshRecent();
    updateInfo();
}

void MainWindow::openFile(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        raise();
        activateWindow();
        return;
    }
    if (m_job) { // não troca de arquivo no meio de uma exportação
        raise();
        return;
    }
    m_openClock.start();
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
        applyExportDefaults();
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
    r.mode = static_cast<CutMode>(m_modeBox->currentIndex());
    r.preset = static_cast<Preset>(m_presetBox->currentIndex());
    r.autoThresholdSec = m_cfg.autoThresholdSec;
    r.hasNvenc = m_hasNvenc;
    r.audio = currentAudio();
    if (auto k = m_timeline->keyframeAtOrBefore(r.startSec))
        r.keyframeBeforeStart = *k;
    else if (m_timeline->scanDone())
        r.keyframeBeforeStart = 0.0;
    else // keyframe ainda desconhecido: Automático cai no Exato, Rápido deixa o ffmpeg buscar
        r.keyframeBeforeStart = r.mode == CutMode::Auto ? r.startSec - 1000.0 : r.startSec;
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
    if (!plan.copyVideo) {
        msg << QString("Início exato: o vídeo será recodificado (%1).").arg(m_hasNvenc ? "NVENC" : "libx264");
    }
    QStringList hint;
    const int nExp = exportedTrackCount(r.audio);
    if (r.audio.empty())
        hint << "Este vídeo não tem áudio.";
    else if (nExp == 0)
        hint << "⚠ Nenhuma faixa em Exportar: o clipe ficará sem áudio.";
    else if (r.preset == Preset::QuickCut) {
        hint << (quickCutAudioIsCopy(r.audio) ? "Áudio copiado, sem recodificar."
                                              : "Áudio recodificado (AAC): volume alterado.");
        if (nExp > 1)
            hint << "Discord e a maioria dos players tocam só a 1ª faixa.";
    } else {
        hint << QString("%1 faixa(s) mixada(s) em uma só (AAC 320k).").arg(nExp);
    }
    m_audioHint->setText(hint.join("\n"));
    m_warning->setText(msg.join("<br>"));
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

    m_job = new ExportJob(ffmpegPath(), args, r.endSec - plan.actualStartSec, r.outputPath, this);
    connect(m_job, &ExportJob::progress, this, [this](double p) { m_bar->setValue(int(p * 1000)); });
    connect(m_job, &ExportJob::finished, this, &MainWindow::onExportFinished);
    m_bar->setValue(0);
    m_bar->show();
    m_cancelBtn->show();
    m_exportBtn->setEnabled(false);
    hideResultButtons();
    m_status->setText("Exportando…");
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
    m_cancelBtn->hide();
    m_exportBtn->setEnabled(true);
    if (!ok) {
        m_status->setText("Falhou: " + err);
        return;
    }
    m_lastOutput = path;
    m_tempFiles << path;
    copyFileToClipboard(path);
    for (auto *b : {m_copyBtn, m_keepBtn, m_deleteBtn, m_openFolderBtn})
        b->show();
    m_status->setText(QString("Pronto em %1 s — copiado (Ctrl+V no Discord). TEMPORÁRIO: será apagado ao fechar o Shear, "
                              "a menos que você clique em Manter. %2")
                          .arg(QString::number(sec, 'f', 1).replace('.', ','), QFileInfo(path).fileName()));
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

static QString trackName(int i, int n)
{
    if (n >= 2 && i == 0) return "Desktop";
    if (n >= 2 && i == 1) return "Mic";
    return QString("Faixa %1").arg(i + 1);
}

std::vector<AudioTrack> MainWindow::currentAudio() const
{
    std::vector<AudioTrack> v;
    for (const auto &r : m_rows)
        v.push_back({r.exp->isChecked(), r.listen->isChecked(), double(r.gain->value())});
    return v;
}

void MainWindow::rebuildAudioRows(int n)
{
    m_rows.clear();
    while (auto *it = m_audioRows->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (int i = 0; i < n; ++i) {
        auto *col = new QWidget;
        col->setFixedWidth(76);
        auto *v = new QVBoxLayout(col);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);

        auto *name = new QLabel(trackName(i, n));
        name->setProperty("role", "name");
        name->setAlignment(Qt::AlignHCenter);
        auto *sub = new QLabel(QString("faixa %1").arg(i + 1));
        sub->setProperty("role", "sub");
        sub->setAlignment(Qt::AlignHCenter);

        auto *gain = new QSlider(Qt::Vertical);
        gain->setRange(-30, 30);
        gain->setFocusPolicy(Qt::NoFocus);
        gain->setToolTip(i < 2 && n >= 2 ? "Volume em dB. No preset de mixagem soma-se ao ganho do seu filtro."
                                         : "Volume em dB");
        auto *val = new QToolButton;
        val->setText("0 dB");
        val->setProperty("role", "val");
        val->setFocusPolicy(Qt::NoFocus);
        val->setToolTip("Clique para zerar o volume desta faixa");

        auto *listen = toggleButton("Ouvir", "Ouvir esta faixa no preview (não afeta a exportação)", true);
        auto *exp = toggleButton("Exportar", "Incluir esta faixa no arquivo exportado", true);

        v->addWidget(name);
        v->addWidget(sub);
        v->addWidget(gain, 1, Qt::AlignHCenter);
        v->addWidget(val, 0, Qt::AlignHCenter);
        v->addWidget(listen);
        v->addWidget(exp);
        m_audioRows->addWidget(col);
        m_rows.push_back({exp, listen, gain, val});

        connect(exp, &QToolButton::toggled, this, [this] { updateInfo(); });
        connect(listen, &QToolButton::toggled, this, [this] { m_audioTimer->start(); });
        connect(gain, &QSlider::valueChanged, this, [this, val](int g) {
            val->setText(QString("%1%2 dB").arg(g > 0 ? "+" : "").arg(g));
            updateInfo();
            m_audioTimer->start(); // evita reconfigurar o filtro a cada pixel arrastado
        });
        connect(val, &QToolButton::clicked, gain, [gain] { gain->setValue(0); });
    }
    m_audioBox->setFixedWidth(std::clamp(32 + (n + 1) * 80 + 8, 230, 700));
    m_audioBox->setVisible(n > 0);
}

void MainWindow::applyExportDefaults()
{
    const bool mix = static_cast<Preset>(m_presetBox->currentIndex()) == Preset::MixMic;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        QSignalBlocker b(m_rows[i].exp);
        m_rows[i].exp->setChecked(mix || m_cfg.tracks == AudioTracks::All || i == 0);
    }
}

void MainWindow::applyPreviewAudio()
{
    if (m_rows.empty() || !m_mpv) return;
    const auto preset = static_cast<Preset>(m_presetBox->currentIndex());
    m_mpv->setAudioGraph(buildAudioMixGraph(currentAudio(), preset, true, true, "ao"));
}

} // namespace shear

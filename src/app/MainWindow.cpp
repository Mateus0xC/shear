#include "MainWindow.h"
#include "NavKeys.h"
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
#include <QPainter>
#include <QPalette>
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

// Velocidades do preview (botão da barra de cima e teclas - / +).
static const double kSpeeds[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0};

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
    auto inEditor = [this] { return m_stack->currentIndex() == 1; };
    sc(Qt::Key_Space, [=, this] { if (inEditor()) userTogglePause(); });
    // X: apaga um trecho do MEIO (1º X = onde começa, 2º X = onde termina); o que sobra é emendado na exportação.
    sc(Qt::Key_X, [=, this] { if (inEditor()) toggleGap(); });
    // Velocidade do preview: - / + (ou =) mais lento / mais rápido; Backspace volta a 1×. Ctrl+0 tira o zoom do vídeo.
    sc(Qt::Key_Minus, [=, this] { if (inEditor()) stepSpeed(-1); });
    sc(Qt::Key_Plus, [=, this] { if (inEditor()) stepSpeed(+1); });
    sc(Qt::Key_Equal, [=, this] { if (inEditor()) stepSpeed(+1); });
    sc(Qt::Key_Backspace, [=, this] { if (inEditor()) setSpeed(1.0); });
    sc(QKeySequence(Qt::CTRL | Qt::Key_0), [=, this] { if (inEditor()) m_mpv->resetZoom(); });
    sc(Qt::Key_I, [=, this] { if (inEditor()) markStart(m_mpv->position()); });
    sc(Qt::Key_O, [=, this] { if (inEditor()) markEnd(m_mpv->position()); });
    // Z e C = I e O (mão esquerda no teclado, junto de A/D e W/S)
    sc(Qt::Key_Z, [=, this] { if (inEditor()) markStart(m_mpv->position()); });
    sc(Qt::Key_C, [=, this] { if (inEditor()) markEnd(m_mpv->position()); });
    // Pular com o teclado (não pausa o vídeo): setas ou A/D = 1 s; Shift = 0,5 s; Ctrl = 5 s; Ctrl+Shift = 15 s.
    // Alt (ou , e .) = 1 quadro. O mapa de teclas fica em NavKeys.cpp (testado em tests/test_navkeys.cpp).
    registerNavShortcuts(this, [this](const NavAction &a) {
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
    sc(Qt::Key_Q, [=, this] { if (inEditor()) m_mpv->seek(m_timeline->start()); }); // vai para o ponto I
    sc(Qt::Key_E, [=, this] { if (inEditor()) m_mpv->seek(m_timeline->end()); });   // vai para o ponto O
    sc(QKeySequence::Undo, [=, this] { if (inEditor()) undo(); });
    sc(QKeySequence::Redo, [=, this] { if (inEditor()) redo(); }); // Ctrl+Y e Ctrl+Shift+Z
    sc(Qt::Key_Return, [=, this] { if (inEditor()) startExport(); });
    sc(Qt::Key_Enter, [=, this] { if (inEditor()) startExport(); });
    sc(Qt::Key_Escape, [=, this] {
        if (!inEditor() || m_job) return;
        if (m_timeline->pendingGap()) { // cancela o trecho que estava sendo marcado
            m_timeline->setPendingGap(std::nullopt);
            return;
        }
        showHome();
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
    m_topBar = top;
    auto *back = new QPushButton("← Clipes");
    back->setFocusPolicy(Qt::NoFocus);
    connect(back, &QPushButton::clicked, this, [this] { if (!m_job) showHome(); });
    m_title = new QLabel;
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
    top->addWidget(m_title, 1);
    // Velocidade do preview: botão com menu (também as teclas - e +, Backspace volta a 1×)
    m_speedBtn = new QToolButton;
    m_speedBtn->setText("1×");
    m_speedBtn->setToolTip("Velocidade do preview (teclas - e +; Backspace volta a 1×). Não muda o arquivo exportado.");
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
    m_fadeCheck = new QCheckBox("Fade");
    m_fadeCheck->setToolTip("Esmaece o início, o fim e as emendas (0,5 s). Recodifica o vídeo, então exporta um pouco mais devagar que o corte rápido.");
    m_fadeCheck->setFocusPolicy(Qt::NoFocus);
    connect(m_fadeCheck, &QCheckBox::toggled, this, [this] { updateInfo(); });
    opts->addWidget(seg);
    opts->addWidget(m_fadeCheck);
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
    m_fileLink = new QPushButton;
    m_fileLink->setFlat(true);
    m_fileLink->setCursor(Qt::PointingHandCursor);
    m_fileLink->setFocusPolicy(Qt::NoFocus);
    m_fileLink->setToolTip("Abrir a pasta do clipe");
    m_fileLink->setStyleSheet("QPushButton { border: none; padding: 0 2px; text-decoration: underline; }");
    m_fileLink->hide();
    connect(m_fileLink, &QPushButton::clicked, this, [this] { revealInExplorer(m_lastOutput); });
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
        updateInfo();
    });
    connect(m_mpv, &MpvWidget::firstFrame, this, [this] {
        metrics::log(QString("primeiro frame: %1 ms após abrir o arquivo").arg(m_openClock.elapsed()));
    });
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
    connect(m_mpv, &MpvWidget::fpsChanged, m_timeline, &TimelineWidget::setFps);
    connect(m_mpv, &MpvWidget::fpsChanged, this, [this](double f) { m_fps = f; });
    connect(m_timeline, &TimelineWidget::startChanged, this, [this] { updateInfo(); });
    connect(m_timeline, &TimelineWidget::endChanged, this, [this] { updateInfo(); });
    // Enquanto o mouse está pressionado na timeline o vídeo fica pausado (a timeline pede o seek a cada movimento,
    // então você vê o quadro sob o mouse). Ao soltar, volta a tocar se estava tocando.
    // Roda do mouse na timeline = setas: 1 s por clique (Shift 0,5 s, Ctrl 5 s, Ctrl+Shift 15 s, Alt 1 quadro).
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
    return "damage/" + QString::fromLatin1(QCryptographicHash::hash(
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

void MainWindow::openSettings()
{
    SettingsDialog d(m_cfg, this);
    if (d.exec() != QDialog::Accepted) return;
    const CutMode oldDefault = m_cfg.mode;
    m_cfg = d.config();
    m_cfg.save();
    if (m_editor && m_cfg.mode != oldDefault) // só muda o modo da edição se o padrão mudou
        setCutMode(m_cfg.mode);
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
        updateInfo();
    });
    connect(m_scanner, &QThread::finished, this, [this] {
        m_timeline->setScanDone(true);
        applyDamageFloor(); // com todos os keyframes lidos, o início utilizável cai num keyframe de verdade
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

void MainWindow::toggleGap()
{
    if (m_job) return;
    const double pos = std::clamp(m_mpv->position(), m_timeline->start(), m_timeline->end());
    if (const auto p = m_timeline->pendingGap()) {
        const double a = std::min(*p, pos), b = std::max(*p, pos);
        m_timeline->setPendingGap(std::nullopt);
        if (b - a < 0.05) {
            m_mpv->showText("Trecho curto demais", 900);
            return;
        }
        pushUndo(currentState());
        m_timeline->addGap(a, b);
        m_mpv->showText(QString("Trecho de %1 s apagado do meio").arg(QString::number(b - a, 'f', 1).replace('.', ',')), 1400);
        updateInfo();
    } else {
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
    return {m_timeline->start(), m_timeline->end(), m_micFader ? m_micFader->value() : 0, m_timeline->gaps()};
}

void MainWindow::pushUndo(const EditState &s)
{
    if (m_restoring)
        return;
    m_redo.clear(); // uma alteração nova depois de desfazer invalida o "refazer"
    if (!m_undo.empty()) {
        const EditState &b = m_undo.back();
        if (b.start == s.start && b.end == s.end && b.micGain == s.micGain && b.gaps == s.gaps)
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
    if (m_micFader)
        m_micFader->setValue(s.micGain);
    m_restoring = false;
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
    const bool willPause = !m_mpv->paused();
    m_mpv->togglePause();
    m_mpv->flash(willPause ? MpvWidget::Overlay::Pause : MpvWidget::Overlay::Play);
}

void MainWindow::setSpeed(double speed, bool osd)
{
    m_mpv->setSpeed(speed);
    const QString txt = QString::number(speed, 'g', 3).replace('.', ',') + "×";
    if (m_speedBtn) {
        m_speedBtn->setText(txt);
        QFont f = m_speedBtn->font();
        f.setBold(speed != 1.0); // fora de 1× o botão fica em negrito
        m_speedBtn->setFont(f);
    }
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
    r.fadeSec = (m_fadeCheck && m_fadeCheck->isChecked()) ? 0.5 : 0.0;
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
                         .arg(fmt(r.startSec), fmt(r.endSec), fmt(keptSeconds(r)),
                              m_timeline->scanDone() ? "" : "    (lendo keyframes…)"));
    m_timeline->setGhostStart(plan.earlyBySec >= 0.05 ? std::optional<double>(plan.actualStartSec)
                                                      : std::nullopt);

    QStringList msg;
    const QString w = earlyStartWarning(plan.earlyBySec);
    m_early->setText(w.isEmpty() ? QStringLiteral(" ")
                                 : "⚠ " + w);
    if (plan.assembled)
        msg << "Com trechos apagados ou fade, o vídeo é recodificado (corte exato; exporta um pouco mais devagar).";
    else
        msg << (r.mode == CutMode::Fast
                    ? "Rápido: corte instantâneo; o início encaixa nas marcas da timeline."
                    : "Exato: corta no frame escolhido; a exportação demora um pouco mais.");
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
    auto r = currentRequest();
    if (keptSeconds(r) < 0.1) {
        m_status->setText(r.gaps.empty() ? "Selecione um trecho maior." : "Os trechos apagados cobrem tudo: nada sobrou para exportar.");
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
    m_fileLink->setToolTip("Abrir a pasta do clipe: " + name);
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
    // calibra a previsão com o tempo real desta exportação
    const double outBytes = double(QFileInfo(path).size());
    m_rates = calibrate(m_rates, m_exportKind, m_hasNvenc, m_exportClipSec,
                        m_exportKind == ExportKind::VideoReencode ? m_exportClipBytes : outBytes, sec);
    saveRates(m_rates);
    m_lastOutput = path;
    m_tempFiles << path;
    copyFileToClipboard(path);
    for (QWidget *b : {static_cast<QWidget *>(m_copyBtn), static_cast<QWidget *>(m_keepBtn), static_cast<QWidget *>(m_deleteBothBtn), static_cast<QWidget *>(m_fileLink)})
        b->show();
    setResultLink(path);
    m_status->setText(QString("· Pronto em %1 s").arg(QString::number(sec, 'f', 1).replace('.', ',')));
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

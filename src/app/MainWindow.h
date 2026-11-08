#pragma once
#include "core/CutTypes.h"
#include "core/ExportEstimate.h"
#include "util/Settings.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <vector>

class QButtonGroup;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QVBoxLayout;
class QSlider;
class QTimer;
class QToolButton;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;

namespace shear {

class ExportJob;
class KeyframeScanner;
class MpvWidget;
class TimelineWidget;
class VerticalFader;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void openFile(const QString &path);
    void deferredInit(); // trabalho pesado, depois da janela aparecer

protected:
    void showEvent(QShowEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void closeEvent(QCloseEvent *) override;

private:
    QWidget *buildHome();
    QWidget *buildEditor();
    void promptOpen();
    void openSettings();
    void refreshRecent();
    void detectNvenc();
    QString ffmpegPath() const;

    void markStart(double t);
    void markEnd(double t);
    void snapStartToKeyframe();
    void updateInfo();
    ExportRequest currentRequest() const;
    void keepLastOutput();
    void deleteLastOutput();
    void cleanupTemp(bool wholeDir);
    static QString tempDir();
    void hideResultButtons();
    void rebuildAudioRows(int n);
    void changeVolume(int delta);
    void setCutMode(CutMode m);
    void onModeChanged();
    double snapIfFast(double t) const;
    void applyPreviewAudio();
    std::vector<AudioTrack> currentAudio() const;
    void startExport();
    void cancelExport();
    void onExportFinished(bool ok, const QString &path, const QString &err, double sec);
    static QString fmt(double s);

    Config m_cfg;
    QStackedWidget *m_stack;
    QWidget *m_editor = nullptr;
    QListWidget *m_recent;
    MpvWidget *m_mpv;
    TimelineWidget *m_timeline;
    QLabel *m_title, *m_times, *m_early, *m_warning, *m_status;
    QToolButton *m_fastBtn = nullptr, *m_exactBtn = nullptr; // seletor Rápido | Exato
    QButtonGroup *m_modeGroup = nullptr;
    struct TrackRow { QToolButton *on; };
    QFrame *m_audioBox = nullptr;      // painel lateral fino, só com as faixas
    QVBoxLayout *m_audioRows = nullptr;
    QHBoxLayout *m_editorRoot = nullptr; // [coluna principal | cartão de áudio]
    VerticalFader *m_micFader = nullptr; // volume só do mic (faixa 2); o desktop não precisa de ajuste
    QToolButton *m_micVal = nullptr;
    int m_volume = 100;                // volume do preview (roda do mouse / setas ↑↓)
    QTimer *m_audioTimer = nullptr;
    std::vector<TrackRow> m_rows;
    QPushButton *m_exportBtn, *m_cancelBtn, *m_openFolderBtn, *m_copyBtn, *m_keepBtn, *m_deleteBtn;
    QProgressBar *m_bar;
    QLabel *m_eta = nullptr; // previsão do tempo de exportação
    ExportRates m_rates;
    ExportKind m_exportKind = ExportKind::Copy;
    double m_exportClipSec = 0, m_exportClipBytes = 0, m_exportEstimate = 0;
    qint64 m_fileSize = 0;
    QToolButton *m_scrubBtn = nullptr; // liga/desliga o áudio ao arrastar
    bool m_scrubbing = false;          // mouse pressionado na timeline
    QElapsedTimer m_burstClock;
    bool m_resumeAfterDrag = false; // o vídeo estava tocando quando o mouse foi pressionado na timeline

    QString m_file, m_lastOutput;
    QStringList m_tempFiles; // exports ainda temporários (apagados ao fechar)
    int m_audioTracks = 2;
    bool m_hasNvenc = true;
    bool m_shownOnce = false;
    KeyframeScanner *m_scanner = nullptr;
    ExportJob *m_job = nullptr;
    QElapsedTimer m_openClock;
};

} // namespace shear

#pragma once
#include "core/CutTypes.h"
#include "util/Settings.h"
#include <QElapsedTimer>
#include <QMainWindow>

class QComboBox;
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
    QLabel *m_title, *m_times, *m_warning, *m_status;
    QComboBox *m_presetBox, *m_modeBox, *m_tracksBox;
    QPushButton *m_exportBtn, *m_cancelBtn, *m_openFolderBtn, *m_copyBtn, *m_keepBtn, *m_deleteBtn;
    QProgressBar *m_bar;

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

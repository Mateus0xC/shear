#pragma once
#include "core/CutTypes.h"
#include "core/ExportEstimate.h"
#include "util/Settings.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <vector>

class QAction;
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
class AudioScrubber;
class MpvWidget;
class TimelineWidget;
class VerticalFader;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void openFile(const QString &path);
    // Ao abrir o app sem arquivo: se um clipe foi salvo há poucos segundos, já abre nele.
    void setAutoOpenFresh(bool v) { m_autoOpenFresh = v; }
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
    void refreshRecent(bool autoOpen = false);
    void showHome();
    void detectNvenc();
    QString ffmpegPath() const;

    void markStart(double t);
    void markEnd(double t);
    // Desfazer (Ctrl+Z): marcadores de início/fim e volume do mic
    struct EditState { double start = 0, end = 0; int micGain = 0; };
    EditState currentState() const;
    void pushUndo(const EditState &s); // guarda o estado ANTES de uma alteração
    void undo();
    void redo();
    void applyState(const EditState &s);
    void jumpBy(double sec);           // pula para frente/trás SEM pausar o vídeo
    void stepOneFrame(int dir);        // 1 quadro: pausa, senão o quadro não fica na tela
    void updateInfo();
    ExportRequest currentRequest() const;
    void keepLastOutput(bool deleteOriginalToo = false);
    bool trashOriginal();
    void setResultLink(const QString &path);
    void deleteBoth();
    void deleteOriginalOnly();
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
    QHBoxLayout *m_topBar = nullptr;   // barra de cima do editor (voltar, título, áudio de 1 faixa, engrenagem)
    QToolButton *m_soloAudioBtn = nullptr; // alto-falante: só existe quando o vídeo tem uma única faixa
    QFrame *m_audioBox = nullptr;      // painel lateral fino, só com as faixas
    QVBoxLayout *m_audioRows = nullptr;
    QHBoxLayout *m_editorRoot = nullptr; // [coluna principal | cartão de áudio]
    VerticalFader *m_micFader = nullptr; // volume só do mic (faixa 2); o desktop não precisa de ajuste
    QToolButton *m_micVal = nullptr;
    int m_volume = 100;                // volume do preview (roda do mouse / setas ↑↓)
    QTimer *m_audioTimer = nullptr;
    std::vector<TrackRow> m_rows;
    QPushButton *m_exportBtn, *m_cancelBtn, *m_copyBtn, *m_keepBtn;
    QPushButton *m_deleteBothBtn, *m_deleteOrigBtn, *m_fileLink; // resultado do corte

    QProgressBar *m_bar;
    QLabel *m_eta = nullptr; // previsão do tempo de exportação
    ExportRates m_rates;
    ExportKind m_exportKind = ExportKind::Copy;
    double m_exportClipSec = 0, m_exportClipBytes = 0, m_exportEstimate = 0;
    qint64 m_fileSize = 0;
    bool m_scrubbing = false;          // mouse pressionado na timeline
    AudioScrubber *m_scrub = nullptr; // player só de áudio para o "áudio ao arrastar"
    std::vector<EditState> m_undo, m_redo;
    EditState m_dragSnapshot;           // estado no momento em que o mouse foi pressionado na timeline
    bool m_restoring = false;           // desfazendo: as mudanças não entram na pilha
    QElapsedTimer m_micClock;           // agrupa os ajustes de volume do mic de um mesmo gesto
    int m_lastMicGain = 0;
    double m_fps = 0;
    bool m_resumeAfterDrag = false; // o vídeo estava tocando quando o mouse foi pressionado na timeline

    QString m_file, m_lastOutput;
    QStringList m_tempFiles; // exports ainda temporários (apagados ao fechar)
    int m_audioTracks = 2;
    bool m_hasNvenc = true;
    bool m_shownOnce = false;
    bool m_autoOpenFresh = false;
    KeyframeScanner *m_scanner = nullptr;
    ExportJob *m_job = nullptr;
    QElapsedTimer m_openClock;
};

} // namespace shear

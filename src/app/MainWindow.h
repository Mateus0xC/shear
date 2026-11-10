#pragma once
#include "core/CutTypes.h"
#include "core/ExportEstimate.h"
#include "core/MatchEvents.h"
#include "util/Settings.h"
#include <QElapsedTimer>
#include <limits>
#include <optional>
#include <QMainWindow>
#include <vector>

class QAction;
class QButtonGroup;
class QCheckBox;
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

namespace valorant { class ValorantService; }
class ExportJob;
class DamagePrefetch;
class DamageScanner;
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
    bool eventFilter(QObject *obj, QEvent *e) override; // roda do mouse sobre o botão de velocidade

private:
    QWidget *buildHome();
    QWidget *buildEditor();
    void promptOpen();
    void openSettings();
    void openHelp();                    // "Atalhos e controles" (F1)
    void refreshRecent(bool autoOpen = false);
    void showHome();
    void detectNvenc();
    QString ffmpegPath() const;

    void markStart(double t);
    void markEnd(double t);
    // Desfazer (Ctrl+Z): marcadores de início/fim e volume do mic
    struct EditState {
        double start = 0, end = 0;
        int micGain = 0;
        std::vector<std::pair<double, double>> gaps;
        double pending = -1; // início de um trecho do meio ainda sem fim (X apertado uma vez); < 0 = nenhum
    };
    void syncModeWithGaps();                // com trechos apagados o modo é Exato (forçado); sem eles, volta ao que era
    bool m_syncingMode = false, m_fastBeforeGaps = false;
    void toggleGap();                       // X: marca o início / o fim de um trecho do meio a apagar
    void removeGapAt(double sec);           // botão direito num trecho apagado
    EditState currentState() const;
    void pushUndo(const EditState &s); // guarda o estado ANTES de uma alteração
    void undo();
    void redo();
    void applyState(const EditState &s);
    void userTogglePause();            // play/pause pedido pelo usuário (com a animação sobre o vídeo)
    void setSpeed(double speed, bool osd = true); // velocidade do preview
    void stepSpeed(int dir);           // próxima velocidade mais lenta (-1) ou mais rápida (+1)
    QCheckBox *m_limitCheck = nullptr;     // "Até 20 MB": comprime o vídeo para caber nesse tamanho
    qint64 m_exportMaxBytes = 0;           // limite da exportação em andamento (0 = sem limite)
    double m_limitScale = 1.0;             // ajuste do bitrate depois de uma tentativa que passou do limite
    int m_limitTries = 0;
    bool m_retrying = false;               // startExport chamado pela nova tentativa, não pelo usuário
    QToolButton *m_speedBtn = nullptr;
    int m_speedWheelAcc = 0;           // soma da roda do mouse sobre o botão de velocidade
    double m_prevPos = 0;              // posição anterior do playback (para detectar a chegada na marca de fim)
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
    DamageScanner *m_damage = nullptr; // detecta começo corrompido e manda pular
    DamagePrefetch *m_prefetch = nullptr; // adianta essa checagem para os clipes recentes, em segundo plano
    double m_damageT = 0;              // fim do trecho corrompido segundo o decodificador (0 = vídeo sadio)
    void applyDamageFloor();
    void releaseVideoHold();           // mostra o vídeo depois da checagem do começo
    bool m_holdVideo = false;
    double m_holdUntilPos = 0;         // com dano: segura a imagem até o playback chegar aqui           // o início utilizável vira o primeiro keyframe depois do trecho corrompido
    ExportJob *m_job = nullptr;
    QElapsedTimer m_openClock;
    QLabel *m_gameInfo = nullptr;                // "Mapa · Agente" (Valorant), discreto, ao lado do título
    valorant::ValorantService *m_valorant = nullptr; // criado só na primeira consulta
    QVector<shear::valorant::MatchEvent> m_vEvents;  // meus kills/mortes da partida (horário absoluto)
    qint64 m_clipEndMs = 0, m_clipSize = 0;          // fim do clipe (data de criação) e tamanho, para alinhar e guardar
    QString m_gameText;                              // "Mapa · Agente" sem as tags
    void applyValorantEvents();                      // alinha os eventos ao clipe e desenha na timeline
    void jumpToEvent(int dir);
    QVector<qint64> m_roundStarts;                   // início de cada round da partida (ms absoluto)
    qint64 m_matchEndMs = 0;
    std::optional<std::pair<double, double>> m_autoRange; // I/O que o shear pôs sozinho (para refazer se recalibrar)
    bool m_userPaused = false;                       // você pausou/despausou neste vídeo (o shear não mexe mais)
    void applyAutoRange(double dur, double offset);  // I/O = último round capturado
    void calibrateEvents();                          // K: a marca mais próxima do ponto atual passa a cair aqui
    QString m_matchId;
    double m_matchOffset = std::numeric_limits<double>::quiet_NaN(); // ajuste da partida (NaN = padrão)
    double currentOffset() const;
    valorant::ValorantService *ensureValorant();     // cria e liga o serviço na primeira vez
    QStringList m_vTags;                             // últimas tags guardadas para o clipe aberto
};

} // namespace shear

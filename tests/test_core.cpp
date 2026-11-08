#include <QtTest>
#include "core/CutPlanner.h"
#include "core/FfmpegCommandBuilder.h"
#include "core/OutputNaming.h"

using namespace shear;

class CoreTest : public QObject {
    Q_OBJECT
    static ExportRequest base()
    {
        ExportRequest r;
        r.inputPath = "in.mp4";
        r.outputPath = "out.mp4";
        r.startSec = 60;
        r.endSec = 120;
        r.keyframeBeforeStart = 58.6;
        return r;
    }
private slots:
    void auto_mode_threshold()
    {
        auto r = base(); // gap 1.4 > 0.5 -> exato
        QVERIFY(!planCut(r).copyVideo);
        r.keyframeBeforeStart = 59.8; // gap 0.2 -> rápido
        auto p = planCut(r);
        QVERIFY(p.copyVideo);
        QCOMPARE(p.actualStartSec, 59.8);
    }
    void fast_mode_warning()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        auto p = planCut(r);
        QVERIFY(p.copyVideo);
        QVERIFY(qAbs(p.earlyBySec - 1.4) < 1e-9);
        QCOMPARE(earlyStartWarning(p.earlyBySec), QString("O clipe vai começar 1,4 s antes"));
        QVERIFY(earlyStartWarning(0.0).isEmpty());
    }
    void quickcut_copy_args()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        const auto a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("copy"));
        QVERIFY(a.contains("make_zero"));
        QVERIFY(a.indexOf("-ss") < a.indexOf("-i"));
        QVERIFY(a.indexOf("-t") < a.indexOf("-i"));
        QCOMPARE(a[a.indexOf("-ss") + 1], QString("58.600"));
        QCOMPARE(a[a.indexOf("-t") + 1], QString("61.400"));
        QVERIFY(a.contains("0:a:0") && !a.contains("0:a:1"));
    }
    void quickcut_all_tracks()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.tracks = AudioTracks::All;
        const auto a = buildFfmpegArgs(r, planCut(r));
        QCOMPARE(a[a.indexOf("-map") + 1], QString("0"));
    }
    void mix_filter_matches_user()
    {
        auto r = base();
        r.preset = Preset::MixMic;
        r.mode = CutMode::Fast;
        const auto a = buildFfmpegArgs(r, planCut(r));
        QCOMPARE(a[a.indexOf("-filter_complex") + 1], QString(kMixFilter));
        QVERIFY(a.contains("320k"));
        QCOMPARE(a[a.indexOf("-c:v") + 1], QString("copy"));
    }
    void exact_reencodes_with_nvenc_or_x264()
    {
        auto r = base(); // auto, gap grande -> exato
        auto a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("h264_nvenc"));
        r.hasNvenc = false;
        a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("libx264"));
    }
    void naming_never_overwrites()
    {
        QSet<QString> taken{"D:/c/clip_shear_01-02-03.mp4", "D:/c/clip_shear_01-02-03_2.mp4"};
        auto ex = [&](const QString &p) { return taken.contains(p); };
        QCOMPARE(uniqueOutputPath("D:/c", "clip", QTime(1, 2, 3), ex),
                 QString("D:/c/clip_shear_01-02-03_3.mp4"));
    }
};

QTEST_APPLESS_MAIN(CoreTest)
#include "test_core.moc"

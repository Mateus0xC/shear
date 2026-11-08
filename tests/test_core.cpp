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
    static std::vector<AudioTrack> two() { return {AudioTrack{}, AudioTrack{}}; }
    void quickcut_gain_reencodes_audio_only()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = two();
        r.audio[1].gainDb = 6;
        const auto a = buildFfmpegArgs(r, planCut(r));
        QCOMPARE(a[a.indexOf("-c:v") + 1], QString("copy"));
        QCOMPARE(a[a.indexOf("-c:a") + 1], QString("aac"));
        QCOMPARE(a[a.indexOf("-filter_complex") + 1], QString("[0:a:0]anull[ao0];[0:a:1]volume=6dB[ao1]"));
        QVERIFY(a.contains("[ao0]") && a.contains("[ao1]"));
    }
    void no_tracks_exported_means_no_audio()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = two();
        r.audio[0].exportIt = r.audio[1].exportIt = false;
        QVERIFY(buildFfmpegArgs(r, planCut(r)).contains("-an"));
    }
    void mix_graph_default_matches_user_filter()
    {
        const auto g = buildAudioMixGraph(two(), Preset::MixMic, false, false, "aout");
        QVERIFY(g.contains("[0:a:1]highpass=f=100,lowpass=f=8000,volume=20dB,agate=range=0:threshold=0.03:release=200[mic]"));
        QVERIFY(g.contains("[0:a:0]volume=10dB[desktop]"));
        QVERIFY(g.endsWith("[desktop][mic]amix=2:longest[aout]"));
    }
    void mix_graph_gain_is_offset_on_base()
    {
        auto t = two();
        t[1].gainDb = -3.5;
        QVERIFY(buildAudioMixGraph(t, Preset::MixMic, false, false, "aout").contains("volume=16.5dB,agate"));
    }
    void mix_graph_single_track_and_empty()
    {
        auto t = two();
        t[1].exportIt = false;
        QCOMPARE(buildAudioMixGraph(t, Preset::MixMic, false, false, "aout"), QString("[0:a:0]volume=10dB[aout]"));
        t[0].exportIt = false;
        QVERIFY(buildAudioMixGraph(t, Preset::MixMic, false, false, "aout").isEmpty());
    }
    void preview_graph_uses_listen_flags_and_aid_pads()
    {
        auto t = two();
        t[0].listen = false;
        t[1].exportIt = false; // não afeta o preview
        QCOMPARE(buildAudioMixGraph(t, Preset::QuickCut, true, true, "ao"), QString("[aid2]anull[ao]"));
    }
    void quickcut_copy_args()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = two();
        r.audio[1].exportIt = false; // só desktop
        const auto a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("copy"));
        QVERIFY(a.contains("make_zero"));
        QVERIFY(a.indexOf("-ss") < a.indexOf("-i"));
        QVERIFY(a.indexOf("-t") < a.indexOf("-i"));
        QCOMPARE(a[a.indexOf("-ss") + 1], QString("58.600"));
        QCOMPARE(a[a.indexOf("-t") + 1], QString("61.400"));
        QVERIFY(a.contains("0:a:0") && !a.contains("0:a:1"));
    }
    void quickcut_all_tracks_kept_separate()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = two();
        const auto a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("0:a:0") && a.contains("0:a:1"));
        QVERIFY(!a.contains("-filter_complex"));
    }
    void mix_preset_export_args()
    {
        auto r = base();
        r.preset = Preset::MixMic;
        r.mode = CutMode::Fast;
        r.audio = two();
        const auto a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a[a.indexOf("-filter_complex") + 1].endsWith("amix=2:longest[aout]"));
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

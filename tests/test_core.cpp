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
    // desktop ligado, mic desligado (o padrão do app)
    static std::vector<AudioTrack> desktopOnly()
    {
        AudioTrack mic;
        mic.enabled = false;
        return {AudioTrack{}, mic};
    }
    static std::vector<AudioTrack> both() { return {AudioTrack{}, AudioTrack{}}; }

    void default_desktop_only_copies_audio()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = desktopOnly();
        const auto a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("copy"));
        QVERIFY(a.contains("make_zero"));
        QVERIFY(a.indexOf("-ss") < a.indexOf("-i"));
        QVERIFY(a.indexOf("-t") < a.indexOf("-i"));
        QCOMPARE(a[a.indexOf("-ss") + 1], QString("58.600"));
        QCOMPARE(a[a.indexOf("-t") + 1], QString("61.400"));
        QVERIFY(a.contains("0:a:0") && !a.contains("0:a:1"));
        QCOMPARE(a[a.indexOf("-c:a") + 1], QString("copy"));
        QVERIFY(!a.contains("-filter_complex"));
    }
    void single_track_with_gain_reencodes_audio_only()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = desktopOnly();
        r.audio[0].gainDb = 6;
        const auto a = buildFfmpegArgs(r, planCut(r));
        QCOMPARE(a[a.indexOf("-c:v") + 1], QString("copy"));
        QCOMPARE(a[a.indexOf("-c:a") + 1], QString("aac"));
        QCOMPARE(a[a.indexOf("-filter_complex") + 1], QString("[0:a:0]volume=6dB[aout]"));
    }
    void only_mic_enabled_is_plain_single_track()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = desktopOnly();
        r.audio[0].enabled = false;
        r.audio[1].enabled = true;
        const auto a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("0:a:1") && !a.contains("-filter_complex"));
    }
    void enabling_mic_merges_into_one_track_with_user_recipe()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = both();
        const auto a = buildFfmpegArgs(r, planCut(r));
        const QString g = a[a.indexOf("-filter_complex") + 1];
        QVERIFY(g.contains("[0:a:1]highpass=f=100,lowpass=f=8000,volume=20dB,agate=range=0:threshold=0.03:release=200[mic]"));
        QVERIFY(g.contains("[0:a:0]volume=10dB[desktop]"));
        QVERIFY(g.endsWith("[desktop][mic]amix=2:longest[aout]"));
        QCOMPARE(a.count("-map"), 2); // vídeo + uma única faixa de áudio
        QVERIFY(a.contains("[aout]") && a.contains("320k"));
        QCOMPARE(a[a.indexOf("-c:v") + 1], QString("copy"));
    }
    void mix_gain_is_offset_on_recipe_base()
    {
        auto t = both();
        t[1].gainDb = -3.5;
        t[0].gainDb = 2;
        const auto g = buildAudioMixGraph(t, false, "aout");
        QVERIFY(g.contains("volume=16.5dB,agate"));
        QVERIFY(g.contains("[0:a:0]volume=12dB[desktop]"));
    }
    void no_tracks_enabled_means_no_audio()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = both();
        r.audio[0].enabled = r.audio[1].enabled = false;
        QVERIFY(buildFfmpegArgs(r, planCut(r)).contains("-an"));
        QVERIFY(buildAudioMixGraph(r.audio, false, "aout").isEmpty());
    }
    void preview_graph_uses_aid_pads_and_matches_export()
    {
        QCOMPARE(buildAudioMixGraph(desktopOnly(), true, "ao"), QString("[aid1]anull[ao]"));
        const auto g = buildAudioMixGraph(both(), true, "ao");
        QVERIFY(g.contains("[aid1]volume=10dB[desktop]") && g.contains("[aid2]highpass"));
        QVERIFY(g.endsWith("amix=2:longest[ao]"));
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

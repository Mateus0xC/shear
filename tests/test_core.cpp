#include <QtTest>
#include "core/CleanStart.h"
#include "core/CutPlanner.h"
#include "core/ExportEstimate.h"
#include "core/FfmpegCommandBuilder.h"
#include "core/MatchEvents.h"
#include "core/OutputNaming.h"
#include "core/ValorantParse.h"

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
    void fast_copies_and_exact_reencodes()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        auto p = planCut(r);
        QVERIFY(p.copyVideo);
        QCOMPARE(p.actualStartSec, 58.6); // cai no keyframe anterior
        r.mode = CutMode::Exact;
        p = planCut(r);
        QVERIFY(!p.copyVideo);
        QCOMPARE(p.actualStartSec, 60.0); // no frame pedido
    }
    void kept_segments_remove_the_middle()
    {
        auto r = base(); // 60..120
        r.gaps = {{80, 90}};
        auto s = keptSegments(r);
        QCOMPARE(int(s.size()), 2);
        QVERIFY((s[0] == Range{60, 80}));
        QVERIFY((s[1] == Range{90, 120}));
        QCOMPARE(keptSeconds(r), 50.0);
        // vários trechos, fora de ordem, sobrepostos, e um que sai do intervalo escolhido
        r.gaps = {{100, 110}, {80, 90}, {85, 95}, {50, 65}, {118, 130}};
        s = keptSegments(r);
        QCOMPARE(int(s.size()), 3);
        QVERIFY((s[0] == Range{65, 80}));
        QVERIFY((s[1] == Range{95, 100}));
        QVERIFY((s[2] == Range{110, 118}));
        // apagar tudo não deixa pedaço nenhum
        r.gaps = {{0, 500}};
        QVERIFY(keptSegments(r).empty());
    }
    void gaps_and_fade_force_reencode_exactly()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.gaps = {{80, 90}};
        auto p = planCut(r);
        QVERIFY(p.assembled && !p.copyVideo);
        QCOMPARE(p.actualStartSec, 60.0); // corte exato, sem "começa antes"
        QCOMPARE(p.earlyBySec, 0.0);
        r.gaps.clear();
        r.fadeSec = kJoinFadeSec; // fade só vale nas emendas: sem trecho apagado não há emenda, então nada muda
        QVERIFY(!planCut(r).assembled);
    }
    void assembled_args_join_segments_with_concat_and_fades()
    {
        auto r = base();
        r.gaps = {{80, 90}};
        r.fadeSec = 0.2;
        r.audio = {{true, 0.0}};
        const auto args = buildFfmpegArgs(r, planCut(r));
        QCOMPARE(args.count("-i"), 2); // um -i por pedaço (o que foi apagado nem é lido)
        const int ss0 = args.indexOf("-ss");
        QCOMPARE(args[ss0 + 1], QString("60.000"));
        // o áudio do 1º pedaço é lido 0,2 s além do fim (o crossfade consome esse trecho): 20 + 0,2
        QCOMPARE(args[ss0 + 3], QString("20.200"));
        QCOMPARE(args[args.indexOf("-ss", ss0 + 1) + 3], QString("30.000")); // o último pedaço não precisa de sobra
        const QString g = args[args.indexOf("-filter_complex") + 1];
        QVERIFY(g.contains("[1:a:0]"));                       // o áudio de cada pedaço vem do seu -i
        // vídeo: fade só na emenda (o 1º pedaço sai com fade, o 2º entra com fade; começo e fim limpos)
        QVERIFY(g.contains("[0:V:0]trim=duration=20,setpts=PTS-STARTPTS,fade=t=out:st=19.8:d=0.2[v0]"));
        QVERIFY(g.contains("[1:V:0]trim=duration=30,setpts=PTS-STARTPTS,fade=t=in:st=0:d=0.2[v1]"));
        QVERIFY(!g.contains("fade=t=in:st=0:d=0.2[v0]"));
        // áudio: crossfade (não "fade out + fade in")
        QVERIFY(g.contains("[a0][a1]acrossfade=d=0.2:c1=tri:c2=tri[aout]"));
        QVERIFY(!g.contains("afade"));
        QVERIFY(g.contains("concat=n=2:v=1:a=0[vout]"));
        QVERIFY(args.contains("aac"));
        // sem fade, com áudio: tudo no concat, sem sobra de áudio
        r.fadeSec = 0;
        const auto a1 = buildFfmpegArgs(r, planCut(r));
        QCOMPARE(a1[a1.indexOf("-ss") + 3], QString("20.000"));
        QVERIFY(a1[a1.indexOf("-filter_complex") + 1].contains("concat=n=2:v=1:a=1[vout][aout]"));
        // sem fade e sem áudio
        r.fadeSec = 0;
        r.audio = {{false, 0.0}};
        const auto a2 = buildFfmpegArgs(r, planCut(r));
        const QString g2 = a2[a2.indexOf("-filter_complex") + 1];
        QVERIFY(!g2.contains("fade"));
        QVERIFY(g2.contains("concat=n=2:v=1:a=0[vout]"));
        QVERIFY(a2.contains("-an"));
    }
    void compression_picks_bitrate_and_resolution_for_the_limit()
    {
        auto r = base(); // 60..120 = 60 s
        r.maxBytes = 20000000;
        r.audio = {{true, 0.0}};
        auto c = planCompression(r, 60);
        QCOMPARE(c.audioKbps, 96);
        QVERIFY(c.videoKbps > 2300 && c.videoKbps < 2700); // 20 MB * 0,94 / 60 s ~ 2,5 Mbps, menos o áudio
        QCOMPARE(c.height, 720);
        QVERIFY(c.cap30fps == false || c.videoKbps < 2500);
        // vídeo longo: bitrate baixo, resolução menor, 30 fps
        c = planCompression(r, 300);
        QVERIFY(c.videoKbps < 600);
        QCOMPARE(c.height, 360);
        QVERIFY(c.cap30fps);
        // 'bitrateScale' reduz o bitrate (usado quando o 1º resultado passou do limite)
        r.bitrateScale = 0.8;
        QVERIFY(planCompression(r, 60).videoKbps < c.videoKbps * 10);
        // sem limite: nada
        r.maxBytes = 0;
        QCOMPARE(planCompression(r, 60).videoKbps, 0);
    }
    void compression_args_reencode_audio_and_video_with_fixed_bitrate()
    {
        auto r = base();
        r.maxBytes = 20000000;
        r.mode = CutMode::Fast;
        r.audio = {{true, 0.0}};
        const auto plan = planCut(r);
        QVERIFY(!plan.copyVideo);
        QCOMPARE(plan.actualStartSec, 60.0);
        const auto args = buildFfmpegArgs(r, plan);
        QVERIFY(!args.contains("copy"));              // nem vídeo nem áudio são copiados
        QVERIFY(args.contains("-b:v") && args.contains("-maxrate") && args.contains("cbr"));
        QVERIFY(args.contains("aac"));
        const int vf = args.indexOf("-vf");
        QVERIFY(vf >= 0);
        QVERIFY(args[vf + 1].contains("scale=-2:min(720\\,ih)"));
        // com trecho apagado a compressão vem depois do concat
        r.gaps = {{80, 90}};
        const auto a2 = buildFfmpegArgs(r, planCut(r));
        const QString g = a2[a2.indexOf("-filter_complex") + 1];
        QVERIFY(g.contains("concat=n=2:v=1:a=1[vcat][aout]"));
        QVERIFY(g.contains("[vcat]scale=-2:min("));
        QVERIFY(a2.contains("-b:v"));
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
        QVERIFY(g.contains("[0:a:0]anull[desktop]")); // desktop no nível original: sem boost
        QVERIFY(g.endsWith("[desktop][mic]amix=inputs=2:duration=longest:normalize=0,alimiter=limit=0.95:level=0[aout]"));
        QVERIFY(!g.contains("volume=10dB"));
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
        QVERIFY(g.contains("[0:a:0]volume=2dB[desktop]"));
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
        QVERIFY(g.contains("[aid1]anull[desktop]") && g.contains("[aid2]highpass"));
        QVERIFY(g.endsWith("amix=inputs=2:duration=longest:normalize=0,alimiter=limit=0.95:level=0[ao]"));
    }
    void exact_reencodes_with_nvenc_or_x264()
    {
        auto r = base();
        r.mode = CutMode::Exact;
        auto a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("h264_nvenc"));
        r.hasNvenc = false;
        a = buildFfmpegArgs(r, planCut(r));
        QVERIFY(a.contains("libx264"));
    }
    void export_kind_detection()
    {
        auto r = base();
        r.mode = CutMode::Fast;
        r.audio = desktopOnly();
        QCOMPARE(int(exportKind(r, planCut(r))), int(ExportKind::Copy));
        r.audio[0].gainDb = 3;
        QCOMPARE(int(exportKind(r, planCut(r))), int(ExportKind::AudioReencode));
        r.audio = both();
        QCOMPARE(int(exportKind(r, planCut(r))), int(ExportKind::AudioReencode));
        r.mode = CutMode::Exact;
        QCOMPARE(int(exportKind(r, planCut(r))), int(ExportKind::VideoReencode));
    }
    void estimate_orders_kinds_and_scales_with_length()
    {
        const ExportRates rt;
        const double mb90 = 90e6;
        const double copy = estimateSeconds(ExportKind::Copy, true, 60, mb90, rt);
        const double aud = estimateSeconds(ExportKind::AudioReencode, true, 60, mb90, rt);
        const double vid = estimateSeconds(ExportKind::VideoReencode, true, 60, mb90, rt);
        QVERIFY(copy < aud && aud < vid);
        QVERIFY(qAbs(vid - (0.6 + 60.0 / 6.0)) < 1e-9);
        QVERIFY(estimateSeconds(ExportKind::VideoReencode, true, 120, 0, rt) > vid);
        QVERIFY(estimateSeconds(ExportKind::VideoReencode, false, 60, mb90, rt) > vid); // x264 mais lento
    }
    void calibrate_moves_rates_toward_observation()
    {
        ExportRates rt;
        rt = calibrate(rt, ExportKind::VideoReencode, true, 60, 90e6, 3.0); // bem mais rápido que o previsto
        QVERIFY(qAbs(rt.nvencRealtime - (0.5 * 6 + 0.5 * (60.0 / 2.4))) < 1e-9);
        QCOMPARE(rt.x264Realtime, 2.5); // não mexe nas outras taxas
        const double before = estimateSeconds(ExportKind::VideoReencode, true, 60, 90e6, ExportRates{});
        QVERIFY(estimateSeconds(ExportKind::VideoReencode, true, 60, 90e6, rt) < before);
    }
    void format_duration_text()
    {
        QCOMPARE(formatDuration(0.4), QString("menos de 1 s"));
        QCOMPARE(formatDuration(3.2), QString("~3 s"));
        QCOMPARE(formatDuration(80), QString("~1 min 20 s"));
    }
    void clean_start_skips_damaged_beginning()
    {
        // 60 fps; os primeiros 44 s só têm 20% dos quadros, o resto está inteiro
        auto frames = [](double damagedUntil, double dur, double badRatio) {
            std::vector<double> v;
            for (int s = 0; s < int(dur); ++s) {
                const int n = s < damagedUntil ? int(60 * badRatio) : 60;
                for (int i = 0; i < n; ++i) v.push_back(s + double(i) / n);
            }
            return v;
        };
        QCOMPARE(shear::firstCleanTime(frames(44, 120, 0.2), 60, 120), 44.0);
        QCOMPARE(shear::firstCleanTime(frames(0, 120, 0.2), 60, 120), 0.0);   // sem dano
        // quedas isoladas (1 ou 2 segundos ruins seguidos) não contam
        auto v = frames(0, 60, 0.2);
        v = frames(0, 60, 1);
        std::vector<double> w;
        for (double t : v) { const int s = int(t); if (s != 10 && s != 30 && s != 31) w.push_back(t); }
        QCOMPARE(shear::firstCleanTime(w, 60, 60), 0.0);
        QCOMPARE(shear::firstCleanTime(frames(60, 60, 0.2), 60, 60), 0.0);    // dano até o fim: nada a aproveitar
        QCOMPARE(shear::firstCleanTime({}, 60, 2), 0.0);                       // vídeo curto demais
    }
    void naming_never_overwrites()
    {
        QSet<QString> taken{"D:/c/clip_shear_01-02-03.mp4", "D:/c/clip_shear_01-02-03_2.mp4"};
        auto ex = [&](const QString &p) { return taken.contains(p); };
        QCOMPARE(uniqueOutputPath("D:/c", "clip", QTime(1, 2, 3), ex),
                 QString("D:/c/clip_shear_01-02-03_3.mp4"));
    }
    void valorant_lockfile_and_log()
    {
        QVERIFY(!valorant::parseLockfile("lixo").valid());
        QVERIFY(!valorant::parseLockfile("a:1:0:x:https").valid());
        const auto l2 = valorant::parseLockfile("Riot Client:1234:5678:senha:https\n");
        QCOMPARE(l2.port, 5678);
        QCOMPARE(l2.password, QString("senha"));
        const auto r = valorant::parseShooterLog(
            "x CI server version: release-13.09-shipping-18-5804228\n y https://glz-br-1.na.a.pvp.net/z");
        QCOMPARE(r.region, QString("br"));
        QCOMPARE(r.shard, QString("na"));
        QCOMPARE(r.version, QString("release-13.09-shipping-18-5804228"));
        QVERIFY(!valorant::parseShooterLog("nada").valid());
    }
    void valorant_coregame_and_names()
    {
        QCOMPARE(valorant::parseMatchId(R"({"Subject":"a","MatchID":"m1","Version":1})"), QString("m1"));
        QCOMPARE(valorant::parseMatchId("{}"), QString());
        const auto c = valorant::parseCoreGame(
            R"({"MapID":"/Game/Maps/Ascent/Ascent","ModeID":"m","Players":[{"Subject":"X","CharacterID":"AAA"},{"Subject":"ME","CharacterID":"ABC-1"}]})",
            "me");
        QVERIFY(c.valid());
        QCOMPARE(c.agentId, QString("ABC-1"));
        QVERIFY(!valorant::parseCoreGame(R"({"MapID":"m","Players":[]})", "me").valid());
        const QByteArray agents = R"({"data":[{"uuid":"abc-1","displayName":"Jett"}]})";
        const QByteArray maps = R"({"data":[{"mapUrl":"/Game/Maps/Ascent/Ascent","displayName":"Ascent"}]})";
        QCOMPARE(valorant::agentNameFor(agents, "ABC-1"), QString("Jett"));
        QCOMPARE(valorant::agentNameFor(agents, "zzz"), QString());
        QCOMPARE(valorant::mapNameFor(maps, "/Game/Maps/Ascent/Ascent"), QString("Ascent"));
        QCOMPARE(valorant::clipKey("a.mp4", 10), QString("a.mp4|10"));
        QVERIFY(valorant::looksLikeValorantClip("D:/NVIDIA/Valorant/Valorant 2026.mp4"));
        QVERIFY(!valorant::looksLikeValorantClip("D:/NVIDIA/Outro/x.mp4"));
    }
    void valorant_history_and_details()
    {
        const QByteArray hist = R"({"History":[{"MatchID":"novo","GameStartTime":3000},{"MatchID":"meio","GameStartTime":2000},{"MatchID":"velho","GameStartTime":1000}]})";
        QCOMPARE(valorant::pickMatchForClip(hist, 2500), QString("meio"));
        QCOMPARE(valorant::pickMatchForClip(hist, 9000), QString("novo"));
        QCOMPARE(valorant::pickMatchForClip(hist, 500), QString());
        const auto m = valorant::parseMatchDetails(
            R"({"matchInfo":{"mapId":"/Game/Maps/Ascent/Ascent","gameStartMillis":2000,"gameLengthMillis":1000},"players":[{"subject":"X","characterId":"a"},{"subject":"ME","characterId":"b"}]})",
            "me");
        QVERIFY(m.valid());
        QCOMPARE(m.agentId, QString("b"));
        QVERIFY(m.covers(2500, 0));
        QVERIFY(!m.covers(3500, 0));
        QVERIFY(m.covers(3500, 600));
        QVERIFY(!m.covers(1500, 600));
    }
    void valorant_events_extract_align_export_tags()
    {
        using valorant::MatchEvent;
        const QByteArray json = R"({"matchInfo":{"gameStartMillis":1000000},"kills":[
            {"killer":"ME","victim":"a","gameTime":10000,"round":1,"finishingDamage":{"damageItem":"w1"}},
            {"killer":"b","victim":"ME","gameTime":30000,"round":1,"finishingDamage":{"damageItem":""}},
            {"killer":"x","victim":"y","gameTime":5000,"round":1},
            {"killer":"me","victim":"c","gameTime":20000,"round":1}]})";
        const auto ev = valorant::extractMyEvents(json, "me");
        QCOMPARE(ev.size(), 3);
        QCOMPARE(ev[0].absMs, qint64(1010000));
        QCOMPARE(int(ev[0].kind), int(MatchEvent::Kill));
        QCOMPARE(ev[0].weaponId, QString("w1"));
        QCOMPARE(int(ev[2].kind), int(MatchEvent::Death));
        // clipe termina em 1040000 e dura 40 s: começa em 1000000
        auto al = valorant::alignToClip(ev, 1040000, 40.0);
        QCOMPARE(al.size(), 3);
        QCOMPARE(al[0].t, 10.0);
        QCOMPARE(al[2].t, 30.0);
        // clipe de 15 s (começa em 1025000): só a morte, aos 5 s
        al = valorant::alignToClip(ev, 1040000, 15.0);
        QCOMPARE(al.size(), 1);
        QCOMPARE(al[0].t, 5.0);
        // offset desloca todos
        al = valorant::alignToClip(ev, 1040000, 40.0, 2.0);
        QCOMPARE(al[0].t, 12.0);
        // exportado: início 5, fim 35, trecho apagado 15..25 (o kill das 20 s some; a morte aos 30 vira 30-5-10 = 15)
        const auto full = valorant::alignToClip(ev, 1040000, 40.0);
        const auto ex = valorant::toExported(full, 5.0, 35.0, {{15.0, 25.0}});
        QCOMPARE(ex.size(), 2);
        QCOMPARE(ex[0].t, 5.0);
        QCOMPARE(ex[1].t, 15.0);
        // tags
        QCOMPARE(valorant::makeTags(full), QStringList({"2k", "morreu"}));
        QCOMPARE(valorant::makeTags({}), QStringList());
        QVector<valorant::ClipEvent> ace;
        for (int i = 0; i < 5; ++i) ace.push_back({MatchEvent::Kill, double(i), 7, {}});
        QCOMPARE(valorant::makeTags(ace), QStringList({"ace"}));
        ace.resize(3);
        QCOMPARE(valorant::makeTags(ace), QStringList({"3k"}));
    }
};

QTEST_APPLESS_MAIN(CoreTest)
#include "test_core.moc"

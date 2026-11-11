#include <QtTest>
#include "MatchEvents.h"
#include "ValorantNaming.h"
#include "ValorantParse.h"
#include "core/OutputNaming.h"

using namespace shear;
using namespace shear::valorant;

// Testes do plugin do Valorant (código puro: parsers da API, eventos, alinhamento, nomes).
class ValorantTest : public QObject {
    Q_OBJECT
private slots:
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
        QCOMPARE(valorant::makeTags(full), QStringList({"2k"}));
        QCOMPARE(valorant::makeTags({}), QStringList());
        QVector<valorant::ClipEvent> ace;
        for (int i = 0; i < 5; ++i) ace.push_back({MatchEvent::Kill, double(i), 7, {}});
        QCOMPARE(valorant::makeTags(ace), QStringList({"5k"}));
        for (int i = 5; i < 7; ++i) ace.push_back({MatchEvent::Kill, double(i), 8, {}}); // + 2 kills de OUTRO round
        QCOMPARE(valorant::makeTags(ace), QStringList({"5k"}));                          // não somam: continua 5k
        for (int i = 7; i < 12; ++i) ace.push_back({MatchEvent::Kill, double(i), 8, {}}); // round 8 agora com 7 kills
        QCOMPARE(valorant::makeTags(ace), QStringList({"7k"}));
        ace.resize(5);
        ace.resize(3);
        QCOMPARE(valorant::makeTags(ace), QStringList({"3k"}));
    }
    void valorant_round_starts_and_last_round_span()
    {
        const QByteArray json = R"({"matchInfo":{"gameStartMillis":1000000},"kills":[
            {"killer":"a","victim":"b","gameTime":40000,"roundTime":10000,"round":0},
            {"killer":"a","victim":"b","gameTime":60000,"roundTime":30000,"round":0},
            {"killer":"a","victim":"b","gameTime":150000,"roundTime":20000,"round":1},
            {"killer":"a","victim":"b","gameTime":300000,"roundTime":50000,"round":2}]})";
        const auto starts = valorant::extractRoundStarts(json);
        QCOMPARE(starts, QVector<qint64>({1030000, 1130000, 1250000}));
        // clipe de 100 s que termina em 1200000 (começa em 1100000), sem ajuste; partida acaba em 1500000
        // round 1 = [1130000, 1250000) -> 30..100 s no clipe (70 s dentro do clipe): é ele
        auto sp = valorant::pickLastRoundSpan(starts, 1500000, 1200000, 100.0, 0.0);
        QVERIFY(sp);
        QCOMPARE(sp->first, 30.0);
        QCOMPARE(sp->second, 100.0);
        // clipe termina em 1135000: o round 1 só tem 5 s lá dentro (< 10) -> vale o round 0
        sp = valorant::pickLastRoundSpan(starts, 1500000, 1135000, 100.0, 0.0);
        QVERIFY(sp);
        QCOMPARE(sp->first, 0.0);  // o round 0 começou antes do clipe
        QCOMPARE(sp->second, 95.0); // termina onde o round 1 começa (1130000 -> 95 s do clipe)
        // com offset: tudo desloca
        sp = valorant::pickLastRoundSpan(starts, 1500000, 1200000, 100.0, 10.0);
        QVERIFY(sp);
        QCOMPARE(sp->first, 40.0);
        // clipe antes de qualquer round -> nada
        QVERIFY(!valorant::pickLastRoundSpan(starts, 1500000, 900000, 50.0, 0.0));
        QVERIFY(!valorant::pickLastRoundSpan({}, 1500000, 1200000, 100.0, 0.0));
    }
    void valorant_round0_start_from_shootergame_log()
    {
        const QByteArray log =
            "[2026.11.08-16.00.10:217][  0]LogGameFlowStateManager: Pregame\n"
            "[2026.11.08-16.02.01:411][  1]LogLoadTimeMetrics: Display: [Match Load Times] MatchId: abc-123, LoadingScreenTime: 17.08\n"
            "[2026.11.08-16.02.30:919][  2]LogRNetVoiceManager: Warning: algo\n"
            "[2026.11.08-16.01.00:000][  3]LogShooterGameState: Warning: Gameplay started at local time 0.0 (server time 0.0)\n"
            "[2026.11.08-16.02.46:107][  4]LogShooterGameState: Warning: Gameplay started at local time 45.101562 (server time 45.292187)\n"
            "[2026.11.08-16.03.47:491][  5]LogShooterGameState: Warning: Gameplay started at local time 99.0 (server time 0.0)\n";
        const auto t = valorant::findRound0StartMs(log, "abc-123");
        QVERIFY(t.has_value());
        // 16:02:46.107 UTC de 08/11/2026
        QCOMPARE(*t, QDateTime(QDate(2026, 11, 8), QTime(16, 2, 46, 107), QTimeZone::UTC).toMSecsSinceEpoch());
        QVERIFY(!valorant::findRound0StartMs(log, "outra-partida").has_value());
        QVERIFY(!valorant::findRound0StartMs(log, "").has_value());
        // partida achada, mas o log termina antes do round 0 começar
        QVERIFY(!valorant::findRound0StartMs(log.left(log.indexOf("[2026.11.08-16.02.30")), "abc-123").has_value());
    }
    void valorant_clip_names()
    {
        const QDate d(2026, 11, 10);
        QCOMPARE(valorantStem(d, "Ascent", "Jett"), QString("Valorant 2026.11.10 - Ascent_Jett"));
        QCOMPARE(valorantStem(d, "Ascent", ""), QString("Valorant 2026.11.10 - Ascent"));
        QCOMPARE(valorantStem(d, "", "Jett"), QString("Valorant 2026.11.10 - Jett"));
        QCOMPARE(valorantStem(d, "", ""), QString());
        QCOMPARE(valorantStem(d, "Ascent", "Jett", "4k"), QString("Valorant 2026.11.10 - Ascent_Jett_4k"));
        QCOMPARE(valorantStem(d, "Ascent", "", "3k"), QString("Valorant 2026.11.10 - Ascent_3k"));
        QCOMPARE(valorantStem(d, "", "", "5k"), QString()); // só a tag não basta: sem mapa e agente vale o nome do original
        QCOMPARE(valorantStem(d, "Skirmish A", "KAY/O"), QString("Valorant 2026.11.10 - Skirmish A_KAYO"));
        QCOMPARE(valorantStem(d, "A:B*C", ""), QString("Valorant 2026.11.10 - ABC"));
                QCOMPARE(uniquePath("D:/c", valorantStem(d, "Ascent", "Jett"), [](const QString &) { return false; }),
                 QString("D:/c/Valorant 2026.11.10 - Ascent_Jett.mp4"));
    }
};

QTEST_APPLESS_MAIN(ValorantTest)
#include "test_valorant.moc"

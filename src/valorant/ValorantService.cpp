#include "ValorantService.h"
#include "ValorantStore.h"
#include "core/ValorantParse.h"
#include "util/Metrics.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>

namespace shear::valorant {

struct ValorantService::Ctx {
    int gen = 0;
    QString clipPath, key;
    QDateTime created;
    QElapsedTimer clock;
    Lockfile lock;
    RegionInfo region;
    // Só em memória: nunca vão para disco nem para log.
    QString accessToken, entitlement, puuid;
    QString matchId;
    CoreGameInfo game;
    QString mapName, agentName;
    bool live = false; // tenta primeiro a partida em andamento
    bool detailsOnly = false; // só baixa os detalhes de `pending` (sem procurar a partida)
    QStringList pending;
};

static QJsonArray eventsToJson(const QVector<MatchEvent> &ev)
{
    QJsonArray a;
    for (const MatchEvent &e : ev)
        a.push_back(QJsonObject{{"k", e.kind == MatchEvent::Kill ? "kill" : "death"}, {"t", double(e.absMs)},
                                {"round", e.round}, {"weapon", e.weaponId}});
    return a;
}

static QVector<MatchEvent> eventsFromJson(const QJsonArray &a);

static MatchData matchDataFromJson(const QString &id, const QJsonObject &m)
{
    MatchData d;
    d.matchId = id;
    d.events = eventsFromJson(m.value("events").toArray());
    for (const auto &v : m.value("rounds").toArray())
        d.roundStartsMs.push_back(qint64(v.toDouble()));
    d.endMs = qint64(m.value("startMs").toDouble()) + qint64(m.value("lengthMs").toDouble());
    if (m.contains("offsetSec"))
        d.offsetSec = m.value("offsetSec").toDouble();
    if (m.contains("autoOffsetSec"))
        d.autoOffsetSec = m.value("autoOffsetSec").toDouble();
    return d;
}

static QVector<MatchEvent> eventsFromJson(const QJsonArray &a)
{
    QVector<MatchEvent> out;
    for (const auto &v : a) {
        const QJsonObject o = v.toObject();
        MatchEvent e;
        e.kind = o.value("k").toString() == "kill" ? MatchEvent::Kill : MatchEvent::Death;
        e.absMs = qint64(o.value("t").toDouble());
        e.round = o.value("round").toInt();
        e.weaponId = o.value("weapon").toString();
        out.push_back(e);
    }
    return out;
}

using Headers = QList<QPair<QByteArray, QByteArray>>;

// Platform fixo documentado em valapidocs.techchrism.me (base64 de um JSON de plataforma PC/Windows).
static const QByteArray kPlatform =
    "ew0KCSJwbGF0Zm9ybVR5cGUiOiAiUEMiLA0KCSJwbGF0Zm9ybU9TIjogIldpbmRvd3MiLA0KCSJwbGF0Zm9ybU9TVmVyc2lvbiI6ICIxMC4wLjE5MDQyLjEuMjU2LjY0Yml0IiwNCgkicGxhdGZvcm1DaGlwc2V0IjogIlVua25vd24iDQp9";

// Roda `work` numa thread curta e entrega o resultado de volta na thread da interface.
template <typename Fn, typename Done>
static void runAsync(QObject *ctx, Fn work, Done done)
{
    auto *t = QThread::create([ctx, work, done] {
        auto result = work();
        QMetaObject::invokeMethod(
            ctx, [done, result = std::move(result)]() mutable { done(std::move(result)); }, Qt::QueuedConnection);
    });
    QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start(QThread::LowPriority);
}

ValorantService::ValorantService(QObject *parent) : QObject(parent) {}

bool ValorantService::stale(const Ctx &c) const { return c.gen >= 0 && c.gen != m_gen; }

void ValorantService::get(const QString &url, const Headers &headers, bool local, Reply cb, int timeoutMs)
{
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    QNetworkRequest rq{QUrl(url)};
    rq.setTransferTimeout(timeoutMs);
    for (const auto &h : headers)
        rq.setRawHeader(h.first, h.second);
    QNetworkReply *r = m_nam->get(rq);
    if (local) // certificado autoassinado só para 127.0.0.1; os servidores remotos têm validação normal
        connect(r, &QNetworkReply::sslErrors, r, [r] {
            if (r->url().host() == QLatin1String("127.0.0.1"))
                r->ignoreSslErrors();
        });
    connect(r, &QNetworkReply::finished, this, [r, cb = std::move(cb)] {
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = status ? r->readAll() : QByteArray();
        r->deleteLater();
        cb(status, body);
    });
}

void ValorantService::finish(std::shared_ptr<Ctx> c, const QString &why)
{
    metrics::log(QString("valorant: %1 (%2 ms)").arg(why).arg(c->clock.elapsed()));
}

void ValorantService::identify(const QString &clipPath, const QDateTime &created, qint64 size)
{
    auto c = std::make_shared<Ctx>();
    c->gen = ++m_gen;
    c->clipPath = clipPath;
    c->created = created;
    c->key = clipKey(QFileInfo(clipPath).fileName(), size);
    c->clock.start();

    // Já associado antes (vale mesmo com o jogo fechado) ou ainda dá para associar à partida em andamento?
    runAsync(this, [c] { return store::loadClips().value(c->key).toObject(); }, [this, c](QJsonObject known) {
        if (stale(*c))
            return;
        // dados digitados à mão valem para qualquer clipe (também os de outros jogos)
        emit manualLoaded(c->clipPath, known.value("manual").toObject());
        if (!looksLikeValorantClip(c->clipPath)) { // clipe de outro jogo: nem toca na rede
            metrics::log("valorant: ignorado (o caminho do clipe não contém \"valorant\")");
            return;
        }
        if (!known.value("matchId").toString().isEmpty()) {
            const QString map = known.value("map").toString(), agent = known.value("agent").toString();
            if (!map.isEmpty() || !agent.isEmpty())
                emit identified(c->clipPath, map, agent);
            c->matchId = known.value("matchId").toString();
            finish(c, "clipe já conhecido");
            return loadKnownEvents(c);
        }
        // Clipe recente: tenta a partida em andamento. Clipe antigo (ou partida já acabou): acha a partida pelo horário.
        const qint64 age = c->created.secsTo(QDateTime::currentDateTime());
        c->live = age >= 0 && age <= kAssociateMaxAgeSec;
        step1(c);
    });
}

// Clipe já associado: usa os eventos guardados; se ainda não há (a partida estava em andamento), baixa os detalhes.
void ValorantService::loadKnownEvents(std::shared_ptr<Ctx> c)
{
    if (c->matchId.isEmpty())
        return;
    runAsync(this, [c] { return store::loadMatch(c->matchId); }, [this, c](QJsonObject match) {
        if (stale(*c))
            return;
        if (match.contains("events")) {
            emit matchDataReady(c->clipPath, matchDataFromJson(c->matchId, match));
            if (match.contains("rounds"))
                return startAutoOffset(c, c->matchId);
        }
        // sem eventos (partida estava em andamento) ou sem os rounds (guardado numa versão anterior): baixa de novo
        c->detailsOnly = true;
        c->pending = {c->matchId};
        step1(c);
    });
}

// Lockfile e ShooterGame.log (arquivos locais, fora da thread da interface)
void ValorantService::step1(std::shared_ptr<Ctx> c)
{
    struct Prep {
        Lockfile lock;
        RegionInfo region;
    };
    runAsync(
        this,
        [] {
            Prep p;
            const QString local = qEnvironmentVariable("LOCALAPPDATA");
            QFile lf(local + "/Riot Games/Riot Client/Config/lockfile");
            if (lf.open(QIODevice::ReadOnly | QIODevice::ExistingOnly))
                p.lock = parseLockfile(lf.readAll());
            if (p.lock.valid()) {
                QFile log(local + "/VALORANT/Saved/Logs/ShooterGame.log"); // host e versão aparecem nos primeiros KB
                if (log.open(QIODevice::ReadOnly | QIODevice::ExistingOnly))
                    p.region = parseShooterLog(QString::fromUtf8(log.read(512 * 1024)));
            }
            return p;
        },
        [this, c](Prep p) {
            if (stale(*c))
                return;
            if (!p.lock.valid())
                return finish(c, "jogo fechado");
            if (!p.region.valid())
                return finish(c, "região não encontrada no log");
            c->lock = p.lock;
            c->region = p.region;
            step2(c);
        });
}

// Tokens e PUUID pelo servidor local
void ValorantService::step2(std::shared_ptr<Ctx> c)
{
    const QByteArray auth = "Basic " + QByteArray("riot:" + c->lock.password.toUtf8()).toBase64();
    const QString base = QString("https://127.0.0.1:%1").arg(c->lock.port);
    get(base + "/entitlements/v1/token", {{"Authorization", auth}}, true,
        [this, c, auth, base](int st, const QByteArray &b) {
            if (stale(*c))
                return;
            const QJsonObject o = QJsonDocument::fromJson(b).object();
            c->accessToken = o.value("accessToken").toString();
            c->entitlement = o.value("token").toString();
            c->puuid = o.value("subject").toString();
            if (st != 200 || c->accessToken.isEmpty() || c->puuid.isEmpty())
                return finish(c, "sem tokens do cliente");
            if (!c->region.version.isEmpty())
                return proceed(c);
            // sem versão no log: usa a da sessão do produto "valorant"
            get(base + "/product-session/v1/external-sessions", {{"Authorization", auth}}, true,
                [this, c](int, const QByteArray &sb) {
                    if (stale(*c))
                        return;
                    const QJsonObject all = QJsonDocument::fromJson(sb).object();
                    for (auto it = all.begin(); it != all.end(); ++it) {
                        const QJsonObject s = it.value().toObject();
                        if (s.value("productId").toString() == "valorant")
                            c->region.version = s.value("version").toString();
                    }
                    if (c->region.version.isEmpty())
                        return finish(c, "versão do cliente não encontrada");
                    proceed(c);
                });
        });
}

void ValorantService::proceed(std::shared_ptr<Ctx> c)
{
    if (c->detailsOnly)
        stepDetails(c);
    else if (c->live)
        step3(c);
    else
        stepHistory(c);
}

// Detalhes de uma ou mais partidas já associadas (uma de cada vez)
void ValorantService::stepDetails(std::shared_ptr<Ctx> c)
{
    if (c->pending.isEmpty())
        return finish(c, "detalhes das partidas atualizados");
    const QString id = c->pending.takeFirst();
    const QString pd = QString("https://pd.%1.a.pvp.net").arg(c->region.shard);
    get(pd + "/match-details/v1/matches/" + id, remoteHeaders(*c), false, [this, c, id](int st, const QByteArray &d) {
        if (stale(*c))
            return;
        if (st != 200) // partida ainda em andamento ou fora do alcance: tenta de novo numa próxima vez
            return stepDetails(c);
        applyDetails(c, id, d, [this, c] { stepDetails(c); });
    });
}

// Extrai meus eventos, guarda em matches/<id>.json e avisa quem espera por eles
void ValorantService::applyDetails(std::shared_ptr<Ctx> c, const QString &matchId, const QByteArray &body,
                                   std::function<void()> then)
{
    const QString puuid = c->puuid;
    runAsync(
        this,
        [matchId, body, puuid] {
            const QVector<MatchEvent> ev = extractMyEvents(body, puuid);
            const MatchSummary m = parseMatchDetails(body, puuid);
            QJsonArray rounds;
            for (const qint64 r : extractRoundStarts(body))
                rounds.push_back(double(r));
            store::saveMatch(matchId, {{"events", eventsToJson(ev)}, {"rounds", rounds}, {"startMs", double(m.startMs)},
                                       {"lengthMs", double(m.lengthMs)}});
            return matchDataFromJson(matchId, store::loadMatch(matchId));
        },
        [this, c, matchId, then](MatchData d) {
            if (!stale(*c) && !c->clipPath.isEmpty() && matchId == c->matchId) {
                emit matchDataReady(c->clipPath, d);
                startAutoOffset(c, matchId);
            }
            then();
        });
}

// Deslocamento exato entre os horários da API e o relógio do PC, lido do ShooterGame.log (uma vez por partida; o resultado,
// ou o fato de não ter achado, fica em matches/<id>.json). Os logs são procurados do mais novo para o mais antigo.
static std::optional<double> autoOffsetFromLogs(const QString &matchId, qint64 apiRound0Ms)
{
    QDir dir(qEnvironmentVariable("LOCALAPPDATA") + "/VALORANT/Saved/Logs");
    QStringList files{"ShooterGame.log"};
    QStringList backups = dir.entryList({"ShooterGame-backup-*.log"}, QDir::Files, QDir::Name | QDir::Reversed);
    files += backups;
    for (const QString &name : files) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::ExistingOnly))
            continue;
        QByteArray owned;
        const uchar *p = f.map(0, f.size());
        QByteArrayView view;
        if (p) {
            view = QByteArrayView(reinterpret_cast<const char *>(p), f.size());
        } else {
            owned = f.readAll();
            view = owned;
        }
        if (const auto t = findRound0StartMs(view, matchId.toLatin1())) {
            const double off = double(*t - apiRound0Ms) / 1000.0;
            if (off > 0 && off < 400) // bem fora disso é outra coisa (ex.: a API não trouxe o round 0)
                return off;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void ValorantService::startAutoOffset(std::shared_ptr<Ctx> c, const QString &matchId)
{
    runAsync(
        this,
        [matchId] {
            std::optional<MatchData> out;
            const QJsonObject m = store::loadMatch(matchId);
            const QJsonArray rounds = m.value("rounds").toArray();
            if (m.contains("autoOffsetTried") || rounds.isEmpty())
                return out;
            const auto off = autoOffsetFromLogs(matchId, qint64(rounds.first().toDouble()));
            QJsonObject upd{{"autoOffsetTried", true}};
            if (off)
                upd.insert("autoOffsetSec", *off);
            store::saveMatch(matchId, upd);
            if (off)
                out = matchDataFromJson(matchId, store::loadMatch(matchId));
            return out;
        },
        [this, c](std::optional<MatchData> d) {
            if (d && !stale(*c) && !c->clipPath.isEmpty())
                emit matchDataReady(c->clipPath, *d);
            metrics::log(d ? QString("valorant: alinhamento automático pelo log do jogo (%1 s)").arg(d->autoOffsetSec, 0, 'f', 2)
                           : QString("valorant: sem alinhamento automático (log da partida não encontrado ou já feito)"));
        });
}

void ValorantService::backfill()
{
    auto c = std::make_shared<Ctx>();
    c->gen = -1; // independe das consultas de clipe (nunca fica "velha")
    c->detailsOnly = true;
    c->clock.start();
    runAsync(this, [] {
        QStringList todo;
        const QJsonObject clips = store::loadClips();
        for (auto it = clips.begin(); it != clips.end() && todo.size() < 5; ++it) {
            const QString id = it.value().toObject().value("matchId").toString();
            if (!id.isEmpty() && !todo.contains(id) && !store::loadMatch(id).contains("rounds"))
                todo << id;
        }
        return todo;
    }, [this, c](QStringList todo) {
        if (todo.isEmpty())
            return;
        c->pending = todo;
        step1(c);
    });
}

void ValorantService::saveMatchOffset(const QString &matchId, double offsetSec)
{
    runAsync(this, [matchId, offsetSec] { return store::saveMatch(matchId, {{"offsetSec", offsetSec}}); }, [](bool) {});
}

// nome -> {uuid, url da imagem}, lido das listas do valorant-api.com que já estão em cache (recarrega se o arquivo mudou)
void ValorantService::loadImageIndex()
{
    const QString dir = store::dir();
    const QDateTime stamp = qMax(QFileInfo(dir + "/maps.json").lastModified(), QFileInfo(dir + "/agents.json").lastModified());
    if (stamp == m_imgIdxStamp && stamp.isValid())
        return;
    m_imgIdxStamp = stamp;
    const struct { int idx; const char *file; const char *urlField; } kinds[] = {{0, "agents.json", "displayIcon"},
                                                                              {1, "maps.json", "listViewIcon"}};
    for (const auto &k : kinds) {
        m_imgIdx[k.idx].clear();
        for (const auto &v : QJsonDocument::fromJson(store::readCache(k.file)).object().value("data").toArray()) {
            const QJsonObject o = v.toObject();
            const QString name = o.value("displayName").toString(), url = o.value(k.urlField).toString();
            if (!name.isEmpty() && !url.isEmpty() && !m_imgIdx[k.idx].contains(name))
                m_imgIdx[k.idx].insert(name, {o.value("uuid").toString(), url});
        }
    }
}

static QString imagePath(const QString &kind, const QString &uuid)
{
    return store::dir() + "/img/" + kind + "_" + uuid + ".png";
}

QString ValorantService::imageFile(const QString &kind, const QString &name)
{
    loadImageIndex();
    const ImgRef ref = m_imgIdx[kind == "map" ? 1 : 0].value(name);
    if (ref.uuid.isEmpty())
        return {};
    const QString path = imagePath(kind, ref.uuid);
    return QFile::exists(path) ? path : QString();
}

void ValorantService::requestImages(const QString &kind, const QStringList &names)
{
    loadImageIndex();
    for (const QString &name : names) {
        const ImgRef ref = m_imgIdx[kind == "map" ? 1 : 0].value(name);
        if (ref.uuid.isEmpty() || QFile::exists(imagePath(kind, ref.uuid)))
            continue;
        const QString key = kind + "|" + ref.uuid;
        if (m_imgPending.contains(key))
            continue;
        m_imgPending.insert(key);
        get(ref.url, {}, false, [this, kind, name, ref, key](int st, const QByteArray &bytes) {
            if (st != 200) {
                m_imgPending.remove(key); // tenta de novo numa próxima vez
                return;
            }
            runAsync(
                this,
                [kind, ref, bytes] {
                    QImage img = QImage::fromData(bytes);
                    if (img.isNull())
                        return QString();
                    // guarda reduzida: o ícone do agente vem com ~570 KB e a faixa do mapa em tamanho grande
                    img = kind == "map" ? img.scaledToWidth(640, Qt::SmoothTransformation)
                                        : img.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    const QString path = imagePath(kind, ref.uuid);
                    QDir().mkpath(QFileInfo(path).absolutePath());
                    return img.save(path, "PNG") ? path : QString();
                },
                [this, kind, name, key](QString path) {
                    m_imgPending.remove(key);
                    if (!path.isEmpty())
                        emit imageReady(kind, name, path);
                });
        }, 30000);
    }
}

void ValorantService::saveManual(const QString &clipPath, qint64 size, const QJsonObject &manual, bool wait)
{
    const QString key = clipKey(QFileInfo(clipPath).fileName(), size);
    const QString date = QFileInfo(clipPath).birthTime().toString(Qt::ISODate);
    auto work = [key, manual, date] {
        QJsonObject e = store::loadClips().value(key).toObject();
        if (manual.isEmpty())
            e.remove("manual");
        else
            e.insert("manual", manual);
        if (!e.contains("date"))
            e.insert("date", date);
        return store::saveClip(key, e);
    };
    if (wait)
        work();
    else
        runAsync(this, work, [](bool) {});
}

void ValorantService::saveTags(const QString &clipPath, qint64 size, const QStringList &tags)
{
    const QString key = clipKey(QFileInfo(clipPath).fileName(), size);
    runAsync(this, [key, tags] {
        QJsonObject e = store::loadClips().value(key).toObject();
        if (e.isEmpty())
            return false; // clipe sem partida associada
        e.insert("tags", QJsonArray::fromStringList(tags));
        return store::saveClip(key, e);
    }, [](bool) {});
}

Headers ValorantService::remoteHeaders(const Ctx &c) const
{
    return {{"Authorization", "Bearer " + c.accessToken.toUtf8()},
            {"X-Riot-Entitlements-JWT", c.entitlement.toUtf8()},
            {"X-Riot-ClientVersion", c.region.version.toUtf8()},
            {"X-Riot-ClientPlatform", kPlatform}};
}

// Partida atual (404 = fora de partida)
void ValorantService::step3(std::shared_ptr<Ctx> c)
{
    const QString glz = QString("https://glz-%1-1.%2.a.pvp.net").arg(c->region.region, c->region.shard);
    get(glz + "/core-game/v1/players/" + c->puuid, remoteHeaders(*c), false, [this, c](int st, const QByteArray &b) {
        if (stale(*c))
            return;
        c->matchId = st == 200 ? parseMatchId(b) : QString();
        if (c->matchId.isEmpty()) // fora de partida (ou a partida já acabou): procura pelo horário do clipe
            return stepHistory(c);
        step4(c);
    });
}

// Partida já encerrada: histórico -> a partida que começou antes do fim do clipe -> detalhes (confere se o clipe cabe nela)
void ValorantService::stepHistory(std::shared_ptr<Ctx> c)
{
    const QString pd = QString("https://pd.%1.a.pvp.net").arg(c->region.shard);
    const qint64 clipEndMs = c->created.toMSecsSinceEpoch();
    get(pd + "/match-history/v1/history/" + c->puuid + "?startIndex=0&endIndex=20", remoteHeaders(*c), false,
        [this, c, pd, clipEndMs](int st, const QByteArray &b) {
            if (stale(*c))
                return;
            c->matchId = st == 200 ? pickMatchForClip(b, clipEndMs) : QString();
            if (c->matchId.isEmpty())
                return finish(c, QString("nenhuma partida no histórico para o horário do clipe (HTTP %1)").arg(st));
            get(pd + "/match-details/v1/matches/" + c->matchId, remoteHeaders(*c), false,
                [this, c, clipEndMs](int st2, const QByteArray &d) {
                    if (stale(*c))
                        return;
                    const MatchSummary m = st2 == 200 ? parseMatchDetails(d, c->puuid) : MatchSummary();
                    constexpr qint64 kSlackMs = 10 * 60 * 1000; // clipe salvo até 10 min depois do fim da partida
                    if (!m.valid() || !m.covers(clipEndMs, kSlackMs))
                        return finish(c, "o clipe não cai em nenhuma partida do histórico");
                    c->game.mapId = m.mapId;
                    c->game.agentId = m.agentId;
                    applyDetails(c, c->matchId, d, [this, c] {
                        c->accessToken.clear();
                        c->entitlement.clear();
                        resolveNames(c);
                    });
                });
        });
}

// Mapa e agente da partida
void ValorantService::step4(std::shared_ptr<Ctx> c)
{
    const QString glz = QString("https://glz-%1-1.%2.a.pvp.net").arg(c->region.region, c->region.shard);
    get(glz + "/core-game/v1/matches/" + c->matchId, remoteHeaders(*c), false, [this, c](int st, const QByteArray &b) {
        if (stale(*c))
            return;
        c->game = st == 200 ? parseCoreGame(b, c->puuid) : CoreGameInfo();
        if (!c->game.valid())
            return finish(c, QString("dados da partida indisponíveis (HTTP %1)").arg(st));
        c->accessToken.clear(); // não precisa mais
        c->entitlement.clear();
        resolveNames(c);
    });
}

// Nomes (cache local; baixa do valorant-api.com só se o ID for desconhecido), depois grava
void ValorantService::resolveNames(std::shared_ptr<Ctx> c)
{
    auto save = [this, c] {
        runAsync(
            this,
            [c] {
                const QJsonObject match{{"matchId", c->matchId}, {"mapId", c->game.mapId}, {"map", c->mapName},
                                        {"agentId", c->game.agentId}, {"agent", c->agentName},
                                        {"modeId", c->game.modeId},
                                        {"savedAt", QDateTime::currentDateTime().toString(Qt::ISODate)}};
                store::saveMatch(c->matchId, match);
                // junta ao que já existe: não apaga o que foi digitado à mão nem as tags já calculadas
                QJsonObject e = store::loadClips().value(c->key).toObject();
                const QJsonObject fresh{{"matchId", c->matchId}, {"mapId", c->game.mapId}, {"map", c->mapName},
                                        {"agentId", c->game.agentId}, {"agent", c->agentName},
                                        {"date", c->created.toString(Qt::ISODate)}};
                for (auto it = fresh.begin(); it != fresh.end(); ++it)
                    e.insert(it.key(), it.value());
                if (!e.contains("tags"))
                    e.insert("tags", QJsonArray());
                store::saveClip(c->key, e);
                return true;
            },
            [this, c](bool) {
                if (!stale(*c) && (!c->mapName.isEmpty() || !c->agentName.isEmpty()))
                    emit identified(c->clipPath, c->mapName, c->agentName);
                finish(c, QString("partida associada (%1 · %2)").arg(c->mapName, c->agentName));
            });
    };
    runAsync(
        this,
        [c] {
            c->mapName = mapNameFor(store::readCache("maps.json"), c->game.mapId);
            c->agentName = agentNameFor(store::readCache("agents.json"), c->game.agentId);
            return c->mapName.isEmpty() || c->agentName.isEmpty(); // precisa baixar?
        },
        [this, c, save](bool needDownload) {
            if (stale(*c))
                return;
            if (!needDownload)
                return save();
            // uma única vez por consulta: mapas e agentes
            get("https://valorant-api.com/v1/maps", {}, false, [this, c, save](int s1, const QByteArray &maps) {
                if (stale(*c))
                    return;
                get("https://valorant-api.com/v1/agents?isPlayableCharacter=true", {}, false,
                    [this, c, save, s1, maps](int s2, const QByteArray &agents) {
                        if (stale(*c))
                            return;
                        runAsync(
                            this,
                            [c, s1, maps, s2, agents] {
                                if (s1 == 200) store::writeCache("maps.json", maps);
                                if (s2 == 200) store::writeCache("agents.json", agents);
                                c->mapName = mapNameFor(s1 == 200 ? maps : store::readCache("maps.json"), c->game.mapId);
                                c->agentName = agentNameFor(s2 == 200 ? agents : store::readCache("agents.json"),
                                                            c->game.agentId);
                                return true;
                            },
                            [save](bool) { save(); });
                    });
            });
        });
}

} // namespace shear::valorant

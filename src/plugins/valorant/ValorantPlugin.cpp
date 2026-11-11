#include "ValorantPlugin.h"
#include "ValorantNaming.h"
#include "ValorantParse.h"
#include "ValorantStore.h"
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace shear::valorant {

using plugin::Choice;
using plugin::ChoiceMenu;
using plugin::InfoChip;
using plugin::Marker;

namespace {

struct NameEntry {
    QString name;
    bool bomb; // mapa do modo bomba (competitivo/padrão); o resto (treino, mata-mata...) vai para o submenu
};

QVector<NameEntry> cachedNames(const QByteArray &json, bool onlyPlayable, bool isMap)
{
    QVector<NameEntry> out;
    QSet<QString> seen;
    for (const auto &v : QJsonDocument::fromJson(json).object().value("data").toArray()) {
        const QJsonObject o = v.toObject();
        if (onlyPlayable && !o.value("isPlayableCharacter").toBool(true))
            continue;
        const QString n = o.value("displayName").toString();
        if (n.isEmpty() || seen.contains(n))
            continue;
        seen.insert(n);
        // os mapas do modo bomba têm "A/B Sites" (ou "A/B/C Sites"); Range, Skirmish, Team Deathmatch etc. não
        out.push_back({n, !isMap || !o.value("tacticalDescription").toString().isEmpty()});
    }
    std::sort(out.begin(), out.end(), [](const NameEntry &a, const NameEntry &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QHash<QString, QString> nameToUuid(const QByteArray &json)
{
    QHash<QString, QString> out;
    for (const auto &v : QJsonDocument::fromJson(json).object().value("data").toArray()) {
        const QJsonObject o = v.toObject();
        const QString n = o.value("displayName").toString();
        if (!n.isEmpty() && !out.contains(n))
            out.insert(n, o.value("uuid").toString());
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// ciclo de vida

ValorantService *ValorantPlugin::svc()
{
    if (m_svc)
        return m_svc;
    m_svc = new ValorantService(this);
    connect(m_svc, &ValorantService::identified, this, [this](const QString &path, const QString &map, const QString &agent) {
        if (path != m_file) return;
        m_apiMap = map;
        m_apiAgent = agent;
        apply();
    });
    connect(m_svc, &ValorantService::imageReady, this, [this] {
        publishChips();
        if (!m_listRefreshQueued) { // junta várias imagens num só redesenho da lista
            m_listRefreshQueued = true;
            QTimer::singleShot(300, this, [this] {
                m_listRefreshQueued = false;
                m_host->refreshList();
            });
        }
    });
    connect(m_svc, &ValorantService::keptLoaded, this, [this](const QString &path, const QJsonObject &entry) {
        if (path != m_file) return;
        m_keptEvents.clear();
        for (const auto &v : entry.value("events").toArray()) {
            const QJsonObject o = v.toObject();
            ClipEvent e;
            e.kind = o.value("k").toString() == "kill" ? MatchEvent::Kill : MatchEvent::Death;
            e.t = o.value("t").toDouble();
            e.round = o.value("round").toInt();
            e.weaponId = o.value("weapon").toString();
            m_keptEvents.push_back(e);
        }
        m_keptTags.clear();
        for (const auto &t : entry.value("tags").toArray())
            m_keptTags << t.toString();
        apply();
    });
    connect(m_svc, &ValorantService::manualLoaded, this, [this](const QString &path, const QJsonObject &manual) {
        if (path != m_file) return;
        m_manual = manual;
        m_manual.remove("comment"); // o comentário agora é do shear (clipmeta.json)
        apply();
    });
    connect(m_svc, &ValorantService::matchDataReady, this, [this](const QString &path, const MatchData &d) {
        if (path != m_file) return;
        m_matchId = d.matchId;
        m_matchOffset = d.offsetSec;
        m_autoOffset = d.autoOffsetSec;
        m_vEvents = d.events;
        m_roundStarts = d.roundStartsMs;
        m_matchEndMs = d.endMs;
        apply();
    });
    return m_svc;
}

void ValorantPlugin::init(plugin::IShearHost *host)
{
    m_host = host;
    setLogSink([host](const QString &line) { host->log(line); });
    // baixa, em segundo plano e uma de cada vez, os detalhes das partidas já associadas que ainda não os têm
    QTimer::singleShot(3000, this, [this] { svc()->backfill(); });
}

QVector<plugin::Action> ValorantPlugin::actions() const
{
    return {{"calibrate", "Valorant: alinhar as marcas à kill/morte do ponto atual (guarda para a partida)",
             QKeySequence(Qt::Key_K)},
            {"reset", "Valorant: desfazer a calibração (volta ao ajuste automático)",
             QKeySequence(Qt::SHIFT | Qt::Key_K)}};
}

void ValorantPlugin::triggerAction(const QString &id)
{
    if (id == "calibrate")
        calibrate();
    else if (id == "reset")
        resetCalibration();
}

void ValorantPlugin::resetState()
{
    m_file.clear();
    m_clipEndMs = m_clipSize = 0;
    m_duration = 0;
    m_apiMap.clear();
    m_apiAgent.clear();
    m_manual = QJsonObject();
    m_vEvents.clear();
    m_roundStarts.clear();
    m_matchEndMs = 0;
    m_matchId.clear();
    m_matchOffset = std::numeric_limits<double>::quiet_NaN();
    m_autoOffset = std::numeric_limits<double>::quiet_NaN();
    m_keptEvents.clear();
    m_keptTags.clear();
    m_autoTags.clear();
    m_vTags.clear();
}

void ValorantPlugin::clipClosed()
{
    resetState();
    if (!m_host)
        return;
    m_host->setInfoChips({});
    m_host->setBackdrop({});
    m_host->setMarkers({});
}

void ValorantPlugin::clipOpened(const plugin::ClipContext &clip)
{
    resetState();
    m_file = clip.path;
    m_clipEndMs = clip.created.toMSecsSinceEpoch();
    m_clipSize = clip.size;
    m_duration = m_host->duration();
    publishChips();
    svc()->identify(clip.path, clip.created, clip.size);
}

void ValorantPlugin::durationChanged(double seconds)
{
    m_duration = seconds;
    apply();
}

double ValorantPlugin::currentOffset() const
{
    if (!std::isnan(m_matchOffset))
        return m_matchOffset; // o que você acertou com a tecla K
    if (!std::isnan(m_autoOffset))
        return m_autoOffset; // o que o shear achou no log do jogo
    return QSettings().value("valorant/offsetSec", kDefaultClipOffsetSec).toDouble();
}

// ---------------------------------------------------------------------------------------------------------------------
// eventos -> marcas, tags, I/O e chips

// Alinha os eventos da partida ao clipe (que cobre ~[fim − duração, fim] + offset) e publica marcas, tags, I/O e chips.
void ValorantPlugin::apply()
{
    if (!m_host || m_file.isEmpty())
        return;
    const double dur = m_duration;
    QVector<Marker> marks;
    QStringList tags;
    if (!m_vEvents.isEmpty() && dur > 0 && m_clipEndMs > 0) {
        const double offset = currentOffset();
        const auto aligned = alignToClip(m_vEvents, m_clipEndMs, dur, offset);
        for (const auto &e : aligned)
            marks.push_back({e.t, e.kind == MatchEvent::Kill});
        tags = makeTags(aligned);
        m_host->log(QString("valorant: %1 de %2 eventos caem no clipe (duração %3 s, offset %4 s)")
                        .arg(aligned.size()).arg(m_vEvents.size()).arg(dur, 0, 'f', 1).arg(offset));
        if (tags != m_vTags) {
            m_vTags = tags;
            svc()->saveTags(m_file, m_clipSize, tags);
        }
    }
    if (m_vEvents.isEmpty() && !m_keptEvents.isEmpty()) { // clipe mantido: eventos e tags já no tempo/nome dele
        marks.clear();
        for (const auto &e : m_keptEvents)
            if (e.t >= 0 && (dur <= 0 || e.t <= dur))
                marks.push_back({e.t, e.kind == MatchEvent::Kill});
        tags = m_keptTags;
    }
    m_host->setMarkers(marks);
    if (!m_roundStarts.isEmpty() && dur > 0 && m_clipEndMs > 0) // I/O no último round capturado
        if (const auto span = pickLastRoundSpan(m_roundStarts, m_matchEndMs, m_clipEndMs, dur, currentOffset()))
            m_host->suggestRange(span->first, span->second);
    m_autoTags = tags;
    publishChips();
}

// Textos do topo: o que o jogo informou aparece como texto; o que ele não sabe fica clicável e vale o que você escolher.
void ValorantPlugin::publishChips()
{
    if (!m_host)
        return;
    const bool vclip = looksLikeValorantClip(m_file);
    // Só fica fixo o que o jogo achou (2k ou mais no clipe). Se ele não achou kills, dá para escolher à mão, de 1k em diante.
    const bool killsEditable = m_autoTags.isEmpty();
    QString killsText;
    if (!killsEditable)
        killsText = m_autoTags.first();
    else if (const int k = m_manual.value("kills").toInt(0); k >= 1)
        killsText = QString("%1k").arg(k);

    const QString mapName = m_apiMap.isEmpty() ? m_manual.value("map").toString() : m_apiMap;
    const QString agentName = m_apiAgent.isEmpty() ? m_manual.value("agent").toString() : m_apiAgent;
    QString mapImg, agentImg;
    if (!mapName.isEmpty()) {
        mapImg = svc()->imageFile("map", mapName);
        if (mapImg.isEmpty()) svc()->requestImages("map", {mapName});
    }
    if (!agentName.isEmpty()) {
        agentImg = svc()->imageFile("agent", agentName);
        if (agentImg.isEmpty()) svc()->requestImages("agent", {agentName});
    }
    m_host->setBackdrop(mapImg); // imagem do mapa ao fundo da barra de cima

    QVector<InfoChip> chips;
    auto add = [&](const QString &id, const QString &text, const QString &placeholder, const QString &icon, bool editable,
                   const QString &tip) {
        if (text.isEmpty() && !(editable && vclip))
            return;
        chips.push_back({id, text, placeholder, icon, editable, editable ? tip : QStringLiteral("Identificado pelo jogo")});
    };
    add("map", mapName, "mapa", QString(), m_apiMap.isEmpty(), "Clique para escolher o mapa");
    add("agent", agentName, "agente", agentImg, m_apiAgent.isEmpty(), "Clique para escolher o agente");
    add("kills", killsText, "kills", QString(), killsEditable, "Clique para dizer quantas kills (no mesmo round)");
    m_host->setInfoChips(chips);
}

void ValorantPlugin::commitManual()
{
    if (m_file.isEmpty())
        return;
    svc()->saveManual(m_file, m_clipSize ? m_clipSize : QFileInfo(m_file).size(), m_manual);
    publishChips();
}

// ---------------------------------------------------------------------------------------------------------------------
// menus dos chips

ChoiceMenu ValorantPlugin::chipMenu(const QString &chipId)
{
    ChoiceMenu menu;
    if (chipId == "kills") {
        const int current = m_manual.value("kills").toInt(0);
        if (current > 0)
            menu.clearTitle = "Limpar";
        for (int k = 1; k <= 10; ++k)
            menu.items.push_back({QString::number(k), QString("%1k").arg(k), {}, {}, k == current});
        return menu;
    }
    if (chipId != "map" && chipId != "agent")
        return menu;
    const bool isMap = chipId == "map";
    const QString current = m_manual.value(chipId).toString();
    if (!current.isEmpty())
        menu.clearTitle = isMap ? "Limpar o mapa" : "Limpar o agente";
    const auto names = cachedNames(store::readCache(isMap ? "maps.json" : "agents.json"), !isMap, isMap);
    QStringList missing;
    auto choice = [&](const QString &n) {
        const QString img = svc()->imageFile(chipId, n);
        if (img.isEmpty()) missing << n;
        Choice c;
        c.value = n;
        c.checked = n == current;
        if (isMap) c.bannerPath = img; // mapa: a imagem é o fundo do item, com o nome por cima
        else c.iconPath = img;
        return c;
    };
    for (const NameEntry &n : names)
        if (n.bomb) menu.items.push_back(choice(n.name));
    for (const NameEntry &n : names)
        if (!n.bomb) {
            menu.moreTitle = "Outros modos"; // mapas de outros modos (treino, mata-mata...) ficam num submenu
            menu.moreItems.push_back(choice(n.name));
        }
    svc()->requestImages(chipId, missing); // o que ainda não tem imagem é baixado para a próxima vez
    menu.customTitle = "Outro…";
    menu.customPrompt = isMap ? "Nome do mapa:" : "Nome do agente:";
    menu.customCurrent = current;
    return menu;
}

void ValorantPlugin::chipChosen(const QString &chipId, const QString &value)
{
    if (chipId == "kills") {
        if (value.isEmpty()) m_manual.remove("kills");
        else m_manual.insert("kills", value.toInt());
    } else if (chipId == "map" || chipId == "agent") {
        if (value.isEmpty()) m_manual.remove(chipId);
        else m_manual.insert(chipId, value);
    } else {
        return;
    }
    commitManual();
}

// ---------------------------------------------------------------------------------------------------------------------
// lista de clipes (thread de fundo): lê só o clips.json e as imagens já guardadas; o que falta é baixado depois

QVector<plugin::RowInfo> ValorantPlugin::rowInfos(const QVector<plugin::RowQuery> &rows) const
{
    QVector<plugin::RowInfo> out(rows.size());
    const QJsonObject known = store::loadClips();
    if (known.isEmpty())
        return out;
    const QHash<QString, QString> mapIds = nameToUuid(store::readCache("maps.json"));
    const QHash<QString, QString> agentIds = nameToUuid(store::readCache("agents.json"));
    QStringList needAgents, needMaps;
    for (int i = 0; i < rows.size(); ++i) {
        const QJsonObject e = known.value(clipKey(QFileInfo(rows[i].path).fileName(), rows[i].size)).toObject();
        if (e.isEmpty())
            continue;
        const QJsonObject man = e.value("manual").toObject();
        auto pick = [&](const char *k) {
            return man.value(k).toString().isEmpty() ? e.value(k).toString() : man.value(k).toString();
        };
        const QString mapName = pick("map"), agentName = pick("agent");
        plugin::RowInfo &r = out[i];
        r.bannerText = mapName;
        if (const int kills = man.value("kills").toInt(0); kills > 0) {
            r.tag = QString("%1k").arg(kills); // à mão vale até 1k; só as achadas sozinhas exigem 2k ou mais
        } else {
            for (const auto &t : e.value("tags").toArray())
                if (const QString tag = t.toString(); tag.endsWith('k') && tag.chopped(1).toInt() >= 2)
                    r.tag = tag; // só "Nk" (guardados de versões antigas podem ter "morreu"/"ace")
        }
        r.tagHot = !r.tag.isEmpty() && r.tag.chopped(1).toInt() >= 5;
        if (const QString uuid = agentIds.value(agentName); !uuid.isEmpty()) {
            const QString p = ValorantService::imageFileFor("agent", uuid);
            if (QFileInfo::exists(p)) r.iconPath = p; else needAgents << agentName;
        }
        if (const QString uuid = mapIds.value(mapName); !uuid.isEmpty()) {
            const QString p = ValorantService::imageFileFor("map", uuid);
            if (QFileInfo::exists(p)) r.bannerPath = p; else needMaps << mapName;
        }
    }
    if (!needAgents.isEmpty() || !needMaps.isEmpty()) { // baixa as que faltam (na thread da interface); a lista se atualiza depois
        auto *self = const_cast<ValorantPlugin *>(this);
        QMetaObject::invokeMethod(self, [self, needAgents, needMaps] {
            self->svc()->requestImages("agent", needAgents);
            self->svc()->requestImages("map", needMaps);
        }, Qt::QueuedConnection);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// exportar e manter

// Dados do Valorant: o nome leva mapa e agente e os eventos viram os do arquivo exportado.
QString ValorantPlugin::exportStem(const plugin::ExportContext &ctx)
{
    m_exportMeta = {};
    m_exportMeta.map = m_apiMap.isEmpty() ? m_manual.value("map").toString() : m_apiMap;
    m_exportMeta.agent = m_apiAgent.isEmpty() ? m_manual.value("agent").toString() : m_apiAgent;
    QVector<std::pair<double, double>> gaps;
    for (const auto &g : ctx.gaps)
        gaps.push_back({g.first, g.second});
    if (!m_vEvents.isEmpty() && ctx.duration > 0 && m_clipEndMs > 0) {
        const auto aligned = alignToClip(m_vEvents, m_clipEndMs, ctx.duration, currentOffset());
        m_exportMeta.events = toExported(aligned, ctx.start, ctx.end, gaps); // modo Rápido: começa no keyframe
    } else if (!m_keptEvents.isEmpty()) { // reexportando um clipe mantido: os eventos dele, recortados de novo
        m_exportMeta.events = toExported(m_keptEvents, ctx.start, ctx.end, gaps);
    }
    if (const auto tags = makeTags(m_exportMeta.events); !tags.isEmpty())
        m_exportMeta.tag = tags.first();
    if (m_exportMeta.tag.isEmpty()) // sem kills achadas: vale o que foi escolhido à mão
        if (const int k = m_manual.value("kills").toInt(0); k >= 1)
            m_exportMeta.tag = QString("%1k").arg(k);
    const QDate date = ctx.clipCreated.isValid() ? ctx.clipCreated.date() : QDate::currentDate();
    return valorantStem(date, m_exportMeta.map, m_exportMeta.agent, m_exportMeta.tag);
}

// clips.json: o clipe mantido guarda mapa, agente, tags e os eventos já no tempo dele (e o que foi escolhido à mão)
void ValorantPlugin::exportKept(const QString &finalPath, qint64 size)
{
    if (m_exportMeta.map.isEmpty() && m_exportMeta.agent.isEmpty() && m_exportMeta.tag.isEmpty() && m_manual.isEmpty())
        return;
    QJsonArray evs;
    for (const auto &e : m_exportMeta.events)
        evs.push_back(QJsonObject{{"k", e.kind == MatchEvent::Kill ? "kill" : "death"}, {"t", e.t}, {"round", e.round},
                                  {"weapon", e.weaponId}});
    QJsonObject entry{{"kept", true}, {"map", m_exportMeta.map}, {"agent", m_exportMeta.agent},
                      {"date", QDateTime::currentDateTime().toString(Qt::ISODate)},
                      {"tags", m_exportMeta.tag.isEmpty() || m_exportMeta.events.isEmpty() ? QJsonArray()
                                                                                          : QJsonArray{m_exportMeta.tag}},
                      {"events", evs}};
    if (!m_manual.isEmpty())
        entry.insert("manual", m_manual);
    svc()->saveKept(finalPath, size, entry);
}

// ---------------------------------------------------------------------------------------------------------------------
// calibração (K / Shift+K) e desfazer

// K: o evento mais próximo do ponto atual (até 60 s) passa a cair exatamente aqui. Vale para a partida toda.
void ValorantPlugin::calibrate()
{
    const double dur = m_host->duration();
    if (m_vEvents.isEmpty() || dur <= 0 || m_clipEndMs <= 0) {
        m_host->showStatus("Sem kills/mortes do Valorant neste clipe para calibrar.");
        return;
    }
    const double pos = m_host->position(), cur = currentOffset();
    // tempos sem o corte em [0, duração]: a marca certa pode estar fora do clipe se o offset estiver muito errado
    const double clipStartMs = double(m_clipEndMs) - dur * 1000.0;
    double best = 1e18, bestT = 0;
    for (const auto &e : m_vEvents) {
        const double t = (double(e.absMs) - clipStartMs) / 1000.0 + cur;
        if (std::abs(t - pos) < best) {
            best = std::abs(t - pos);
            bestT = t;
        }
    }
    if (best > 60.0) {
        m_host->showStatus("Nenhuma kill/morte a menos de 60 s do ponto atual.");
        return;
    }
    m_host->pushUndo(); // Ctrl+Z desfaz
    m_matchOffset = cur + (pos - bestT);
    apply();
    if (!m_matchId.isEmpty())
        svc()->saveMatchOffset(m_matchId, m_matchOffset);
    m_host->showStatus(QString("Marcas alinhadas (ajuste %1 s, guardado para esta partida).").arg(m_matchOffset, 0, 'f', 1));
}

// Shift+K: desfaz a calibração (K) desta partida e volta ao ajuste automático (ou ao padrão).
void ValorantPlugin::resetCalibration()
{
    if (std::isnan(m_matchOffset)) {
        m_host->showStatus("As marcas já estão no ajuste automático.");
        return;
    }
    m_host->pushUndo(); // Ctrl+Z traz o ajuste de volta
    m_matchOffset = std::numeric_limits<double>::quiet_NaN();
    if (!m_matchId.isEmpty())
        svc()->clearMatchOffset(m_matchId);
    apply();
    m_host->showStatus("Marcas de volta ao ajuste automático.");
}

QVariant ValorantPlugin::saveState() const
{
    return std::isnan(m_matchOffset) ? QVariant() : QVariant(m_matchOffset);
}

void ValorantPlugin::restoreState(const QVariant &state)
{
    const double n = state.isValid() ? state.toDouble() : std::numeric_limits<double>::quiet_NaN();
    const bool same = (std::isnan(n) && std::isnan(m_matchOffset)) || n == m_matchOffset;
    if (same)
        return;
    m_matchOffset = n; // ajuste das marcas (K / Shift+K): volta ao que era, na tela e no arquivo da partida
    if (!m_matchId.isEmpty() && m_svc) {
        if (std::isnan(n)) m_svc->clearMatchOffset(m_matchId);
        else m_svc->saveMatchOffset(m_matchId, n);
    }
    apply();
}

} // namespace shear::valorant

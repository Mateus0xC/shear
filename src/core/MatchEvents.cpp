#include "MatchEvents.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <map>

namespace shear::valorant {

QVector<MatchEvent> extractMyEvents(const QByteArray &json, const QString &puuid)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const qint64 start = qint64(o.value("matchInfo").toObject().value("gameStartMillis").toDouble());
    QVector<MatchEvent> out;
    if (start <= 0)
        return out;
    for (const auto &v : o.value("kills").toArray()) {
        const QJsonObject k = v.toObject();
        const bool killer = k.value("killer").toString().compare(puuid, Qt::CaseInsensitive) == 0;
        const bool victim = k.value("victim").toString().compare(puuid, Qt::CaseInsensitive) == 0;
        if (!killer && !victim)
            continue;
        MatchEvent e;
        e.kind = killer ? MatchEvent::Kill : MatchEvent::Death; // suicídio (os dois): conta como morte
        if (victim)
            e.kind = MatchEvent::Death;
        e.absMs = start + qint64(k.value("gameTime").toDouble());
        e.round = k.value("round").toInt();
        e.weaponId = k.value("finishingDamage").toObject().value("damageItem").toString();
        out.push_back(e);
    }
    std::stable_sort(out.begin(), out.end(), [](const MatchEvent &a, const MatchEvent &b) { return a.absMs < b.absMs; });
    return out;
}

QVector<ClipEvent> alignToClip(const QVector<MatchEvent> &events, qint64 clipEndMs, double durationSec, double offsetSec)
{
    QVector<ClipEvent> out;
    if (durationSec <= 0)
        return out;
    const double clipStartMs = double(clipEndMs) - durationSec * 1000.0;
    for (const MatchEvent &e : events) {
        const double t = (double(e.absMs) - clipStartMs) / 1000.0 + offsetSec;
        if (t < 0 || t > durationSec)
            continue;
        out.push_back({e.kind, t, e.round, e.weaponId});
    }
    return out;
}

QVector<ClipEvent> toExported(const QVector<ClipEvent> &events, double start, double end,
                              const QVector<std::pair<double, double>> &gaps)
{
    // só vale a parte dos trechos apagados que está dentro de [start, end]
    QVector<std::pair<double, double>> g;
    for (const auto &p : gaps) {
        const double a = std::max(p.first, start), b = std::min(p.second, end);
        if (b > a)
            g.push_back({a, b});
    }
    QVector<ClipEvent> out;
    for (ClipEvent e : events) {
        if (e.t < start || e.t > end)
            continue;
        bool inGap = false;
        double removed = 0;
        for (const auto &p : g) {
            if (e.t >= p.first && e.t < p.second) {
                inGap = true;
                break;
            }
            if (p.second <= e.t)
                removed += p.second - p.first;
        }
        if (inGap)
            continue;
        e.t = e.t - start - removed;
        out.push_back(e);
    }
    return out;
}

QStringList makeTags(const QVector<ClipEvent> &events)
{
    int kills = 0;
    bool died = false;
    std::map<int, int> perRound;
    for (const ClipEvent &e : events) {
        if (e.kind == MatchEvent::Kill) {
            ++kills;
            ++perRound[e.round];
        } else {
            died = true;
        }
    }
    int bestRound = 0;
    for (const auto &r : perRound)
        bestRound = std::max(bestRound, r.second);
    QStringList tags;
    if (bestRound >= 5)
        tags << "ace";
    else if (kills >= 4)
        tags << "4k";
    else if (kills == 3)
        tags << "3k";
    else if (kills == 2)
        tags << "2k";
    if (died)
        tags << "morreu";
    return tags;
}

} // namespace shear::valorant

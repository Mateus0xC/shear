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

QVector<qint64> extractRoundStarts(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const qint64 start = qint64(o.value("matchInfo").toObject().value("gameStartMillis").toDouble());
    std::map<int, qint64> byRound;
    if (start > 0) {
        for (const auto &v : o.value("kills").toArray()) {
            const QJsonObject k = v.toObject();
            const qint64 s = start + qint64(k.value("gameTime").toDouble()) - qint64(k.value("roundTime").toDouble());
            const int round = k.value("round").toInt();
            auto it = byRound.find(round);
            if (it == byRound.end() || s < it->second)
                byRound[round] = s;
        }
    }
    QVector<qint64> out;
    for (const auto &r : byRound)
        out.push_back(r.second);
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<std::pair<double, double>> pickLastRoundSpan(const QVector<qint64> &starts, qint64 matchEndMs,
                                                           qint64 clipEndMs, double dur, double offsetSec, double minSec)
{
    if (starts.isEmpty() || dur <= 0)
        return std::nullopt;
    const double clipStartMs = double(clipEndMs) - dur * 1000.0;
    const auto toClip = [&](qint64 abs) { return (double(abs) - clipStartMs) / 1000.0 + offsetSec; };
    const auto span = [&](int i) {
        const double a = toClip(starts[i]);
        const double b = i + 1 < starts.size() ? toClip(starts[i + 1]) : toClip(matchEndMs);
        return std::make_pair(std::max(a, 0.0), std::min(b, dur)); // só a parte que está dentro do clipe
    };
    int last = -1; // último round que começa antes do fim do clipe
    for (int i = 0; i < starts.size(); ++i)
        if (toClip(starts[i]) < dur)
            last = i;
    if (last < 0)
        return std::nullopt;
    auto s = span(last);
    if (s.second - s.first < minSec && last > 0)
        s = span(last - 1);
    if (s.second - s.first <= 0)
        return std::nullopt;
    return s;
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

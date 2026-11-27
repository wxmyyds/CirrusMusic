#include "discovery_mapper.h"
#include "music_mapping.h"
#include <QHash>
#include <QJsonArray>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
bool absentOr(const QJsonValue& value, bool (QJsonValue::*is)() const)
{
    return value.isUndefined() || value.isNull() || (value.*is)();
}

// 心动模式's list (docs/api/heartbeat-mode.md): [{id, songInfo, alg, recommended}] in play
// order. A recommended song without usable details is left out (the mix is one song
// shorter); one of the playlist's own may come without them. A song listed twice keeps
// its first place and its last entry, as the official app's map does.
MusicResult heartbeatList(const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    const auto values = object.value(QStringLiteral("data"));
    if (!values.isArray() || values.toArray().size() > 2 * HeartbeatQuery::MaximumCount) {
        mapping::rejectedBecause("heartbeat.data");
        return invalid;
    }
    HeartbeatList list;
    QHash<QString, qsizetype> places;
    qsizetype row = -1;
    for (const auto value : values.toArray()) {
        ++row;
        const auto o = value.toObject();
        const auto id = mapping::providerId(o.value(QStringLiteral("id")));
        const auto info = o.value(QStringLiteral("songInfo"));
        const auto alg = o.value(QStringLiteral("alg"));
        const auto recommended = o.value(QStringLiteral("recommended"));
        if (!value.isObject() || id.isEmpty() || !absentOr(info, &QJsonValue::isObject) ||
            !absentOr(alg, &QJsonValue::isString) || !absentOr(recommended, &QJsonValue::isBool)) {
            mapping::rejectedBecause("heartbeat.row", row);
            return invalid;
        }
        HeartbeatEntry entry{{id}, std::nullopt, alg.toString(), recommended.toBool()};
        if (info.isObject()) {
            const char* why = "heartbeat.song_id";
            auto song = mapping::songFromExplained(info.toObject(), why);
            if (song && song->id.value == id)
                entry.details = std::move(song);
            else if (entry.recommended)
                mapping::rejectedBecause(why, row);
        }
        if (entry.recommended && !entry.details)
            continue; // left out, see above
        if (const auto at = places.constFind(id); at != places.cend()) {
            list.entries[*at] = std::move(entry);
        } else {
            places.insert(id, list.entries.size());
            list.entries.append(std::move(entry));
        }
    }
    return list;
}
} // namespace

MusicResult mapDiscoveryResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (std::holds_alternative<HeartbeatQuery>(query))
        return heartbeatList(object);
    if (std::holds_alternative<PersonalFmQuery>(query)) {
        const auto values = object.value(QStringLiteral("data"));
        if (!values.isArray() || values.toArray().size() > 20)
            return invalid;
        PersonalFmBatch batch;
        for (const auto value : values.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto song = mapping::songFrom(value.toObject());
            if (!song)
                return invalid;
            batch.songs.append(*song);
        }
        return batch;
    }
    if (!std::holds_alternative<DailyRecommendationsQuery>(query) ||
        !object.value(QStringLiteral("data")).isObject())
        return invalid;
    const auto data = object.value(QStringLiteral("data")).toObject();
    const auto values = data.value(QStringLiteral("dailySongs"));
    if (!values.isArray() || values.toArray().size() > 500)
        return invalid;
    DailyRecommendations daily;
    for (const auto value : values.toArray()) {
        if (!value.isObject())
            return invalid;
        const auto o = value.toObject();
        const auto song = mapping::songFrom(o);
        const auto reason = o.value(QStringLiteral("reason"));
        if (!song || !mapping::textField(reason))
            return invalid;
        daily.songs.append(
            {*song, reason.isString() ? std::optional<QString>(reason.toString()) : std::nullopt});
    }
    const auto reasons = data.value(QStringLiteral("recommendReasons"));
    if (!reasons.isUndefined() && !reasons.isNull()) {
        if (!reasons.isArray() || reasons.toArray().size() > 500)
            return invalid;
        for (const auto value : reasons.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto o = value.toObject();
            const auto id = mapping::providerId(o.value(QStringLiteral("songId")));
            const auto reason = o.value(QStringLiteral("reason"));
            if (id.isEmpty() || !reason.isString() || !mapping::textField(reason))
                return invalid;
            daily.reasons.append({{id}, reason.toString()});
        }
    }
    return daily; // receipt time is stamped by gateway, never invented as a provider day
}
} // namespace cirrus::netease

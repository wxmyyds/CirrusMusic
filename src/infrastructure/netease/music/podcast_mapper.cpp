#include "podcast_mapper.h"
#include "music_mapping.h"
#include "spoken_mapping.h"
#include <QJsonArray>
namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }
bool number(const QJsonValue& v, std::optional<qint64>& out)
{
    if (absent(v))
        return true;
    out = nonnegative(v);
    return out.has_value();
}
std::optional<Program> programFrom(const QJsonObject& o)
{
    Program p;
    p.id = {providerId(o.value(QStringLiteral("id")))};
    const auto title = o.value(QStringLiteral("name")),
               desc = o.value(QStringLiteral("description"));
    if (p.id.value.isEmpty() || !textField(title) || !textField(desc) ||
        !number(o.value(QStringLiteral("duration")), p.providerDuration) ||
        !number(o.value(QStringLiteral("serialNum")), p.serialNumber))
        return std::nullopt;
    p.title = title.toString();
    if (!absent(desc))
        p.description = desc.toString();
    p.cover = imageFrom(o.value(QStringLiteral("coverUrl")));
    const auto radio = o.value(QStringLiteral("radio"));
    if (!absent(radio)) {
        if (!radio.isObject())
            return std::nullopt;
        p.podcast = podcastFrom(radio.toObject());
        if (!p.podcast)
            return std::nullopt;
    }
    p.track = trackFrom(o);
    return p;
}
} // namespace
std::optional<PodcastRequest> podcastRequest(const MusicQuery& query)
{
    if (!validMusicQuery(query))
        return std::nullopt;
    if (const auto* q = std::get_if<PodcastDetailQuery>(&query))
        return PodcastRequest{ApiOperation::PodcastDetails,
                              {{QStringLiteral("id"), q->podcast.value}}};
    if (const auto* q = std::get_if<ProgramDetailQuery>(&query))
        return PodcastRequest{ApiOperation::ProgramDetails,
                              {{QStringLiteral("id"), q->program.value}}};
    if (const auto* q = std::get_if<PodcastProgramsQuery>(&query))
        return PodcastRequest{ApiOperation::PodcastPrograms,
                              {{QStringLiteral("radioId"), q->podcast.value},
                               {QStringLiteral("limit"), q->page.limit},
                               {QStringLiteral("offset"), q->page.offset},
                               {QStringLiteral("asc"), q->order == ProgramOrder::OldestFirst}}};
    return std::nullopt;
}
MusicResult mapPodcastResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!isPodcastQuery(query) || !validMusicQuery(query) ||
        object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (const auto* q = std::get_if<PodcastDetailQuery>(&query)) {
        // The authorized public probe observed root.data, NOT legacy root.djRadio.
        const auto data = object.value(QStringLiteral("data"));
        if (!data.isObject())
            return invalid;
        const auto p = mapping::podcastFrom(data.toObject());
        if (!p || p->id.value != q->podcast.value)
            return invalid;
        return PodcastDetails{*q, *p};
    }
    if (const auto* q = std::get_if<ProgramDetailQuery>(&query)) {
        // Candidate legacy-consumption schema; no claim of VoiceId correspondence.
        const auto data = object.value(QStringLiteral("program"));
        if (!data.isObject())
            return invalid;
        const auto p = programFrom(data.toObject());
        if (!p || p->id.value != q->program.value)
            return invalid;
        return ProgramDetails{*q, *p};
    }
    const auto& q = std::get<PodcastProgramsQuery>(query);
    const auto raw = object.value(QStringLiteral("programs"));
    PodcastProgramsPage page;
    page.query = q;
    if (!raw.isArray() || raw.toArray().size() > q.page.limit ||
        !number(object.value(QStringLiteral("count")), page.total) ||
        (page.total && *page.total < raw.toArray().size()))
        return invalid;
    const auto more = object.value(QStringLiteral("more"));
    if (!absent(more)) {
        if (!more.isBool())
            return invalid;
        page.hasMore = more.toBool();
    }
    for (const auto v : raw.toArray()) {
        if (!v.isObject())
            return invalid;
        const auto p = programFrom(v.toObject());
        if (!p || (p->podcast && p->podcast->id.value != q.podcast.value))
            return invalid;
        page.programs.append(*p);
    }
    return page;
}
} // namespace cirrus::netease

#include "backend_stats.h"

#include "backend_mapping.h"

#include "application/playback/listening_session.h"
#include "infrastructure/runtime/backend_runtime.h"
#include "infrastructure/storage/listening_state_service.h"
#include "logging/log.h"

#include <QPointer>
#include <QTimer>

namespace cirrus::ui
{

namespace
{
using mapping::coverKey;
using mapping::songOf;

StatsAmount amountOf(int plays, qint64 playedMs) { return {playedMs, plays}; }

StatsOrigin originOf(const domain::StatsOrigin& row)
{
    using Kind = domain::PlayOriginKind;
    StatsOrigin origin;
    origin.key = row.origin.id;
    origin.name = row.origin.name;
    origin.amount = amountOf(row.plays, row.playedMs);
    if (row.mode == domain::ListeningMode::PersonalFm) {
        origin.kind = StatsOrigin::Fm;
        return origin;
    }
    switch (row.origin.kind) {
    case Kind::Playlist:
        origin.kind = StatsOrigin::Playlist;
        break;
    case Kind::Album:
        origin.kind = StatsOrigin::Album;
        break;
    case Kind::Artist:
        origin.kind = StatsOrigin::Artist;
        break;
    case Kind::DailySongs:
        origin.kind = StatsOrigin::Daily;
        break;
    case Kind::Search:
        origin.kind = StatsOrigin::Search;
        break;
    case Kind::Recent:
        origin.kind = StatsOrigin::Recent;
        break;
    case Kind::Ranking:
        origin.kind = StatsOrigin::Ranking;
        break;
    case Kind::Heartbeat:
        origin.kind = StatsOrigin::Heartbeat;
        break;
    default:
        origin.kind = StatsOrigin::Other;
    }
    return origin;
}

ListeningStats statsOf(const domain::ListeningStats& stats)
{
    ListeningStats out;
    out.total = amountOf(stats.totals.plays, stats.totals.playedMs);
    out.records = stats.totals.records;
    out.songs = stats.totals.songs;
    out.artists = stats.totals.artists;
    out.albums = stats.totals.albums;
    out.newSongs = stats.totals.newSongs;
    out.days = stats.totals.days;
    for (const auto& period : stats.periods)
        out.periods.append({period.start, amountOf(period.plays, period.playedMs)});
    for (size_t i = 0; i < stats.weekHours.size(); ++i)
        out.weekHours[i] = amountOf(stats.weekHours[i].plays, stats.weekHours[i].playedMs);
    for (const auto& row : stats.songs)
        out.topSongs.append({songOf(row.song), row.known, amountOf(row.plays, row.playedMs)});
    for (const auto& row : stats.artists)
        out.topArtists.append({row.artist.id ? row.artist.id->value : QString(), row.artist.name,
                               QString(), amountOf(row.plays, row.playedMs), row.songs});
    for (const auto& row : stats.albums)
        out.topAlbums.append({row.album.id ? row.album.id->value : QString(), row.album.title,
                              coverKey(row.album.cover), amountOf(row.plays, row.playedMs),
                              row.songs});
    for (const auto& row : stats.origins)
        out.topOrigins.append(originOf(row));
    out.streak = stats.streak.current;
    out.longestStreak = stats.streak.longest;
    out.firstRecordAtMs = stats.firstRecordAtMs.value_or(-1);
    out.withoutDetails = stats.songsWithoutDetails;
    return out;
}
} // namespace

BackendStats::BackendStats(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_runtime(runtime)
{
    auto* service = m_runtime.listeningState();
    if (!service)
        return;
    connect(
        service, &storage::ListeningStateService::statsReady, this,
        [this](quint64 id, storage::StorageError error, const domain::ListeningStats& stats) {
            const auto reply = m_pending.take(id);
            if (!reply)
                return;
            if (error == storage::StorageError::None) {
                reply(Outcome<ListeningStats>{statsOf(stats)});
                return;
            }
            log::warn(lcUiBackend, "stats.failed", {"err", error});
            reply(Outcome<ListeningStats>{
                std::nullopt, false,
                Failure{error == storage::StorageError::NotOpen ? failure::Busy : failure::Unknown,
                        std::nullopt}});
        });
    connect(service, &storage::ListeningStateService::storedHistoryChanged, this, [this] {
        if (m_changed)
            m_changed();
    });
}

Request BackendStats::stats(const StatsQuery& query, Reply<ListeningStats> reply)
{
    domain::ListeningStatsQuery request;
    request.fromMs = query.fromMs;
    request.toMs = query.toMs;
    request.periodSize = domain::ListeningPeriodSize(query.periodSize);
    request.order =
        query.byTime ? domain::ListeningRankOrder::Time : domain::ListeningRankOrder::Plays;
    request.limit = query.limit;
    auto* service = m_runtime.listeningState();
    // stats() answers later through statsReady; 0 is a refusal (not ready, busy).
    const quint64 id = service ? service->stats(request) : 0;
    if (!id) {
        auto alive = std::make_shared<bool>(true);
        QTimer::singleShot(0, this, [alive, reply = std::move(reply)] {
            if (*alive)
                reply(Outcome<ListeningStats>{std::nullopt, false,
                                              Failure{failure::Busy, std::nullopt}});
        });
        return Request([alive] { *alive = false; });
    }
    m_pending.insert(id, std::move(reply));
    return Request([self = QPointer<BackendStats>(this), id] {
        if (self)
            self->m_pending.remove(id);
    });
}

Request BackendStats::clear(Reply<bool> reply)
{
    // The session forgets its records and the store erases the account's, in order
    // before anything heard afterwards; storedHistoryChanged follows once it is done.
    m_runtime.listening().clearHistory();
    log::info(lcUiBackend, "stats.cleared");
    auto alive = std::make_shared<bool>(true);
    QTimer::singleShot(0, this, [alive, reply = std::move(reply)] {
        if (*alive)
            reply(Outcome<bool>{true});
    });
    return Request([alive] { *alive = false; });
}

void BackendStats::setChangedHandler(std::function<void()> changed)
{
    m_changed = std::move(changed);
}

} // namespace cirrus::ui

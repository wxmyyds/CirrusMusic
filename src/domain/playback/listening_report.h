#pragma once
#include "domain/music/catalog_types.h"
#include "domain/music/podcast_types.h"
#include "logging/log.h"
#include <QUuid>

namespace cirrus::domain
{
// Explicit individual weblog action; no timing policy or automatic two-request sequence.
// ActiveClient is the web player's daily "this client is in use" log, about no song
// (docs/api/explicit-listening-reports.md §4c).
enum class ListeningReportAction { StartPlay, Play, ActiveClient };
CIRRUS_LOG_ENUM(ListeningReportAction, StartPlay, Play, ActiveClient)
// Where the song was played from, as the web client names it (`source`): a playlist,
// album, artist page, chart, daily recommendations, a search (sourceId is the
// keywords) or a song page. Evidence: docs/ui/integration.md §6.1; the PC client's
// values are not verified (docs/api/explicit-listening-reports.md §2). DjRadio: a podcast's
// program (sourceId the podcast, possibly empty), as the Android app names it.
enum class ListeningReportSource {
    List,
    Album,
    Artist,
    Toplist,
    DailyRecommend,
    Search,
    Song,
    DjRadio
};
CIRRUS_LOG_ENUM(ListeningReportSource, List, Album, Artist, Toplist, DailyRecommend, Search, Song,
                DjRadio)
// Why the listening ended (`end`): played to the end, the user went on, interrupted
// (stopped, paused for long, closed), an error.
enum class ListeningReportEnd { PlayEnd, Ui, Interrupt, Exception };
CIRRUS_LOG_ENUM(ListeningReportEnd, PlayEnd, Ui, Interrupt, Exception)
inline QString sourceName(ListeningReportSource source)
{
    switch (source) {
    case ListeningReportSource::List:
        return QStringLiteral("list");
    case ListeningReportSource::Album:
        return QStringLiteral("album");
    case ListeningReportSource::Artist:
        return QStringLiteral("artist");
    case ListeningReportSource::Toplist:
        return QStringLiteral("toplist");
    case ListeningReportSource::DailyRecommend:
        return QStringLiteral("dailySongRecommend");
    case ListeningReportSource::Search:
        return QStringLiteral("search");
    case ListeningReportSource::Song:
        return QStringLiteral("song");
    case ListeningReportSource::DjRadio:
        return QStringLiteral("djradio");
    }
    return {};
}
inline QString endName(ListeningReportEnd end)
{
    switch (end) {
    case ListeningReportEnd::PlayEnd:
        return QStringLiteral("playend");
    case ListeningReportEnd::Ui:
        return QStringLiteral("ui");
    case ListeningReportEnd::Interrupt:
        return QStringLiteral("interrupt");
    case ListeningReportEnd::Exception:
        return QStringLiteral("exception");
    }
    return {};
}
// A source id for `source`: a provider id, search keywords (printable, at most 256
// characters), or anything (possibly empty) for the daily recommendations, whose value
// the web client doesn't show.
inline bool validSourceId(ListeningReportSource source, const QString& id)
{
    if (source == ListeningReportSource::DailyRecommend || source == ListeningReportSource::DjRadio)
        return id.size() <= 64 && (id.isEmpty() || validMusicId(id));
    if (source == ListeningReportSource::Search) {
        if (id.trimmed().isEmpty() || id.size() > 256)
            return false;
        for (const QChar c : id)
            if (c.category() == QChar::Other_Control)
                return false;
        return true;
    }
    return validMusicId(id);
}
struct ListeningReport {
    QUuid eventId; // local correlation only; NOT server idempotency
    SongId song;
    QString sourceId; // see validSourceId(); unknown/FM origin is not fabricated
    ListeningReportAction action = ListeningReportAction::Play;
    qint64 playedSeconds = 0; // caller must supply genuine observed time, never final position
    ListeningReportSource source = ListeningReportSource::List;
    ListeningReportEnd end = ListeningReportEnd::PlayEnd; // Play only
    qint64 durationSeconds = 0;                           // the song's length, when known (0: not)
    QString quality;        // the level played ("exhigh", "lossless"…), when known
    qint64 startedAtMs = 0; // when the load began playing (epoch ms), when known
    // A podcast program played through `song` (its track), reported as the program; its
    // source is DjRadio. Empty for songs.
    ProgramId program;
    [[nodiscard]] bool isValid() const
    {
        if (action == ListeningReportAction::ActiveClient)
            return !eventId.isNull() && playedSeconds == 0;
        if (durationSeconds < 0 || durationSeconds > 86400 || quality.size() > 16)
            return false;
        for (const QChar c : quality)
            if (!c.isLetterOrNumber())
                return false;
        if (!program.value.isEmpty() &&
            (!validMusicId(program.value) || source != ListeningReportSource::DjRadio))
            return false;
        return !eventId.isNull() && validMusicId(song.value) && validSourceId(source, sourceId) &&
               ((action == ListeningReportAction::StartPlay && playedSeconds == 0) ||
                (action == ListeningReportAction::Play && playedSeconds > 0 &&
                 playedSeconds <= 604800));
    }
};
enum class ReportOutcome { NotSent, Acknowledged, Rejected, Unknown };
CIRRUS_LOG_ENUM(ReportOutcome, NotSent, Acknowledged, Rejected, Unknown)
enum class ReportError {
    None,
    InvalidRequest,
    AuthenticationRequired,
    StaleContext,
    Busy,
    Canceled,
    Network,
    Timeout,
    RateLimited,
    InvalidResponse,
    ProviderRejected,
    ProtocolFailure
};
CIRRUS_LOG_ENUM(ReportError, None, InvalidRequest, AuthenticationRequired, StaleContext, Busy,
                Canceled, Network, Timeout, RateLimited, InvalidResponse, ProviderRejected,
                ProtocolFailure)
struct ListeningReportResult {
    QUuid eventId;
    ListeningReportAction action = ListeningReportAction::Play;
    ReportOutcome outcome = ReportOutcome::NotSent;
    ReportError error = ReportError::None;
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs; // information only; never auto retry a write
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::ListeningReportResult)

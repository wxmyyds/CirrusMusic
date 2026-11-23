#include "backend_playback.h"
#include "logging/log.h"

#include "app_settings.h"
#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "application/playback/listening_session.h"
#include "infrastructure/cache/cached_music_query_service.h"
#include "infrastructure/runtime/backend_runtime.h"
#include "infrastructure/storage/listening_state_service.h"

#include <QSet>
#include <QSettings>
#include <QTimer>

#include <algorithm>

namespace cirrus::ui
{
namespace
{

using application::ListeningSession;
using domain::PlaybackState;

// Positions within this of the clock's extrapolation don't notify.
constexpr qint64 kDriftMs = 150;
// Details kept of songs that played.
constexpr qsizetype kDetailsKept = 256;
// While playing, where it is gets saved this often (and on pause, a new song, a stop).
constexpr qint64 kSavePositionMs = 5000;

// Where the account's queue was, for when it comes back at the next start: the backend
// saves the queue but not the position (integration.md §6 D7).
QString positionKey(const QString& account)
{
    return QStringLiteral("playback/position/") +
           (account.isEmpty() ? QStringLiteral("guest") : account);
}

// Where the account's queue entries came from (BackendPlayback::originOf).
QString originsKey(const QString& account)
{
    return QStringLiteral("playback/origins/") +
           (account.isEmpty() ? QStringLiteral("guest") : account);
}

// Where a song came from, as the backend keeps it for the local statistics
// (domain::PlayOrigin): a chart is a playlist; search keywords stay here (reports only).
// Always valid: the backend refuses a queue with an invalid origin.
domain::PlayOrigin storedOrigin(const PlayOrigin& origin)
{
    using Kind = domain::PlayOriginKind;
    QString name = origin.name;
    name.remove(QChar::Null);
    if (name.size() > 256) {
        name.truncate(256);
        if (name.back().isHighSurrogate())
            name.chop(1);
    }
    if (!name.isValidUtf16())
        name.clear();
    const auto named = [&](Kind kind) {
        return domain::validMusicId(origin.id) ? domain::PlayOrigin{kind, origin.id, name}
                                               : domain::PlayOrigin{Kind::Other, {}, {}};
    };
    switch (origin.kind) {
    case playback::FromPlaylist:
    case playback::FromChart:
        return named(Kind::Playlist);
    case playback::FromAlbum:
        return named(Kind::Album);
    case playback::FromArtist:
        return named(Kind::Artist);
    case playback::FromRanking:
        return named(Kind::Ranking);
    case playback::FromDaily:
        return {Kind::DailySongs, {}, {}};
    case playback::FromSearch:
        return {Kind::Search, {}, {}};
    case playback::FromRecent:
        return {Kind::Recent, {}, {}};
    case playback::FromSong:
        return {Kind::Other, {}, {}};
    default:
        return {};
    }
}

domain::AudioQuality quality()
{
    const int value = AppSettings().audioQuality();
    return domain::AudioQuality(
        std::clamp(value, int(domain::AudioQuality::Standard), int(domain::AudioQuality::HiRes)));
}

failure::Kind kindOf(domain::PlaybackError error)
{
    using domain::PlaybackError;
    switch (error) {
    case PlaybackError::Network:
    case PlaybackError::Expired:
        return failure::Network;
    case PlaybackError::Timeout:
        return failure::Timeout;
    case PlaybackError::RateLimited:
        return failure::RateLimited;
    case PlaybackError::InvalidRequest:
        return failure::Invalid;
    case PlaybackError::Unavailable:
    case PlaybackError::NeedPermission:
    case PlaybackError::ProtocolKeyMissing:
    case PlaybackError::InvalidResponse:
    case PlaybackError::UnsafeSource:
    case PlaybackError::TooLarge:
    case PlaybackError::UnsupportedFormat:
    case PlaybackError::Decode:
        return failure::Rejected;
    case PlaybackError::StaleContext:
    case PlaybackError::Canceled:
    case PlaybackError::Storage:
    case PlaybackError::NoOutput:
        break;
    }
    return failure::Unknown;
}

playback::Status statusOf(const domain::PlaybackSnapshot& play)
{
    switch (play.state) {
    case PlaybackState::Empty:
        return playback::Empty;
    case PlaybackState::Resolving:
    case PlaybackState::Loading:
        return playback::Loading;
    case PlaybackState::Ready:
        // Loaded; playing starts unless it was paused meanwhile.
        return play.intent == domain::TransportIntent::Play ? playback::Loading : playback::Paused;
    case PlaybackState::Playing:
        return playback::Playing;
    case PlaybackState::Paused:
        return playback::Paused;
    case PlaybackState::Ended:
        return playback::Ended;
    case PlaybackState::Failed:
        return playback::Failed;
    }
    return playback::Empty;
}

// Where the snapshot's clock is at `now`.
qint64 positionAt(const PlaybackSnapshot& s, qint64 now)
{
    const qint64 at = s.positionMs + qint64(double(now - s.positionAt) * s.rate);
    return s.durationMs >= 0 ? std::min(at, s.durationMs) : at;
}

// Everything but the clock; `current` by key (its details notify on their own).
bool sameState(const PlaybackSnapshot& a, const PlaybackSnapshot& b)
{
    const auto failureKind = [](const PlaybackSnapshot& s) {
        return s.failure ? int(s.failure->kind) : -1;
    };
    return a.status == b.status && a.provider == b.provider && a.generation == b.generation &&
           a.current.key == b.current.key &&
           failureKind(a) == failureKind(b) && a.rate == b.rate && a.durationMs == b.durationMs &&
           a.seekable == b.seekable && a.previewStartMs == b.previewStartMs &&
           a.volume == b.volume && a.order == b.order && a.repeat == b.repeat &&
           a.canNext == b.canNext && a.canPrevious == b.canPrevious && a.mode == b.mode &&
           a.fmWaiting == b.fmWaiting && a.queue == b.queue && a.queueIndex == b.queueIndex;
}

std::optional<LyricTrack> trackOf(const domain::LyricTrack& track)
{
    if (track.availability != domain::LyricAvailability::Present)
        return std::nullopt;
    LyricTrack out;
    switch (track.timing) {
    case domain::LyricTiming::PlainText:
        out.timing = LyricTrack::PlainText;
        break;
    case domain::LyricTiming::Lines:
        out.timing = LyricTrack::Lines;
        break;
    case domain::LyricTiming::Words:
        out.timing = LyricTrack::Words;
        break;
    }
    out.offsetMs = track.providerOffsetMs;
    out.lines.reserve(track.lines.size());
    for (const auto& line : track.lines) {
        LyricLine l{line.startMs.value_or(-1), line.endMs.value_or(-1), line.text, {}};
        l.words.reserve(line.words.size());
        for (const auto& word : line.words)
            l.words.append({word.startMs, word.startMs + word.durationMs, word.text});
        out.lines.append(std::move(l));
    }
    return out;
}

LanguageLyrics languageOf(const domain::LyricVersions& versions)
{
    return {trackOf(versions.standard), trackOf(versions.enhanced)};
}

SongLyrics lyricsOf(const domain::SongLyrics& lyrics)
{
    return {languageOf(lyrics.original), languageOf(lyrics.translation),
            languageOf(lyrics.romanization), lyrics.providerNoLyric.value_or(false),
            lyrics.instrumental.value_or(false)};
}

template <class T> Request replyLater(QObject* context, Reply<T> reply, Outcome<T> outcome)
{
    auto alive = std::make_shared<bool>(true);
    QTimer::singleShot(0, context, [alive, reply = std::move(reply), outcome = std::move(outcome)] {
        if (*alive)
            reply(outcome);
    });
    return Request([alive] { *alive = false; });
}

} // namespace

BackendPlayback::BackendPlayback(runtime::BackendRuntime& runtime, CatalogSource& catalog,
                                 QObject* parent)
    : QObject(parent), m_runtime(runtime), m_session(&runtime.listening()), m_facade(&runtime.facade()),
      m_catalog(catalog)
{
    connect(&runtime, &runtime::BackendRuntime::biliAccountChanged, this, [this] {
        if (m_biliChanged)
            m_biliChanged();
    });
    const auto changed = [this] { update(); };
    connect(m_session, &ListeningSession::queueChanged, this, changed);
    connect(m_session, &ListeningSession::playbackChanged, this, changed);
    connect(m_session, &ListeningSession::fmChanged, this, changed);
    connect(m_session, &ListeningSession::failureStreakExceeded, this, [this] {
        if (m_notice)
            m_notice(tr("多首歌曲无法播放，已停止"));
    });
    // A session change retires the queue and playback.
    connect(&runtime.facade(), &application::BackendFacade::sessionChanged, this,
            [this](const domain::SessionSnapshot&) {
                applyVolume(); // set before the backend could take it (startup)
                applyNormalization();
                applyTransition();
                update();
            });
    connect(&runtime.facade(), &application::BackendFacade::stateChanged, this, [this] {
        applyNormalization();
        applyTransition();
        update();
    });
    // A sign-in goes through several session changes in a row: resume once it has
    // settled for a moment, not at a Guest phase between them.
    m_resume.setSingleShot(true);
    m_resume.setInterval(500);
    connect(&m_resume, &QTimer::timeout, this, [this] {
        if (!canResume())
            return;
        m_kept->resume = false;
        replay(m_kept->index);
    });
    // The last queue comes back at startup; played on when the user wants that.
    if (auto* state = runtime.listeningState())
        connect(state, &storage::ListeningStateService::queueRestored, this, [this](bool applied) {
            const bool first = !m_started;
            m_started = true;
            if (applied && first && AppSettings().resumeOnStart())
                QMetaObject::invokeMethod(
                    this,
                    [this] {
                        if (!m_live && !m_kept)
                            playPause();
                    },
                    Qt::QueuedConnection);
        });
}

BackendPlayback::~BackendPlayback() = default;

void BackendPlayback::update()
{
    if (!m_session)
        return;
    const ListeningSession& session = *m_session;
    const auto play = session.playback();
    const qint64 now = playback::now();
    const bool newLoad = !(play.token == m_load);
    const bool live = play.state != PlaybackState::Empty || session.waitingForFm();
    if (!live && m_live)
        keep(now);
    m_live = live;
    if (live) {
        m_kept.reset();
        m_started = true;
    }

    PlaybackSnapshot next;
    // A Personal FM restored at startup shows as one; it plays on with resumeFm().
    const bool restoredFm = session.restoredFm();
    next.mode = session.mode() == domain::ListeningMode::PersonalFm || restoredFm
                    ? playback::PersonalFm
                    : playback::Queue;
    next.fmWaiting = session.waitingForFm();
    if (next.mode == playback::Queue) {
        const auto queue = session.queue();
        QHash<QUuid, QString> songs;
        for (const auto& entry : queue.entries)
            songs.insert(entry.id, entry.song.value);
        m_entries = session.playOrder();
        for (const QUuid& id : std::as_const(m_entries))
            next.queue.append(songs.value(id));
        next.queueIndex = int(m_entries.indexOf(queue.active));
        next.order =
            queue.order == domain::QueueOrder::Shuffle ? playback::Shuffle : playback::Sequential;
        next.repeat = queue.repeat == domain::QueueRepeat::All   ? playback::RepeatAll
                      : queue.repeat == domain::QueueRepeat::One ? playback::RepeatOne
                                                                 : playback::RepeatOff;
    } else {
        // Its songs so far, oldest first; the one playing (or the last, while the next is
        // fetched) current.
        const auto songs = restoredFm ? session.queue() : session.fmQueue();
        m_entries.clear();
        for (const auto& entry : songs.entries) {
            m_entries.append(entry.id);
            next.queue.append(entry.song.value);
        }
        next.queueIndex = int(m_entries.indexOf(songs.active));
        // The queue's modes stay for when it comes back.
        next.order = m_state.order;
        next.repeat = m_state.repeat;
    }

    next.generation = play.token.generation;
    next.provider = play.provider == domain::SourceProvider::Bilibili ? playback::Bilibili
                                                                     : playback::Netease;
    next.status = statusOf(play);
    if (next.status == playback::Empty && next.fmWaiting)
        next.status = session.fmBlocked() ? playback::Ended : playback::Loading;
    if (next.status == playback::Failed && play.failure)
        next.failure = Failure{kindOf(play.failure->error), std::nullopt};
    next.current = songInfo(play.song.value);
    if (next.fmWaiting && session.fmBlocked() && next.queueIndex >= 0) {
        next.current = songInfo(next.queue.at(next.queueIndex));
        next.provider = m_state.provider;
    }
    next.durationMs = play.durationMs.value_or(-1);
    next.seekable = play.seekable;
    next.previewStartMs = play.preview ? play.preview->startMs : -1;
    next.volume = m_volume.value_or(play.volumeLinear.value_or(m_state.volume));
    // The player's reported volume isn't the user's (it was set before the backend could
    // take it): set it again, once per load.
    if (m_volume && play.volumeLinear && std::abs(*play.volumeLinear - *m_volume) > 1e-3 &&
        !(m_volumeFixed == play.token)) {
        m_volumeFixed = play.token;
        applyVolume();
    }

    // The clock: the session's base when it moved, else where ours is now.
    next.rate = next.status == playback::Playing ? 1 : 0;
    if (newLoad || play.positionRevision != m_positionRevision) {
        next.positionMs = play.positionMs;
        next.positionAt = newLoad ? now : session.positionObservedAtMs().value_or(now);
    } else {
        next.positionMs = positionAt(m_state, now);
        next.positionAt = now;
    }
    m_load = play.token;
    m_positionRevision = play.positionRevision;

    if (!live && m_kept)
        showKept(next, now);
    else if (!live && (next.mode == playback::Queue || restoredFm) && next.queueIndex >= 0)
        showRestored(next, now);
    if (restoredFm) {
        next.canNext = true; // the next of its songs, or a new one
        next.canPrevious = next.queueIndex > 0;
    } else if (next.mode == playback::PersonalFm) {
        next.canNext = session.canGoNext();
        next.canPrevious = session.canGoPrevious();
    } else {
        // By play order (next(), previous()), not the session's history.
        const bool wraps = next.repeat != playback::RepeatOff && next.queue.size() > 1;
        next.canNext = live ? session.canGoNext()
                            : next.queueIndex + 1 < next.queue.size() || wraps ||
                                  (next.queueIndex < 0 && !next.queue.isEmpty());
        next.canPrevious = next.queueIndex > 0 || (next.queueIndex == 0 && wraps);
    }

    if (live) {
        seekWhereKept(play);
        savePosition(next, now, false);
    } else if (canResume() && !m_resume.isActive())
        m_resume.start();

    const bool changed = m_detailsArrived || !sameState(m_state, next) ||
                         std::abs(positionAt(m_state, now) - positionAt(next, now)) > kDriftMs;
    m_detailsArrived = false;
    m_state = std::move(next);
    if (changed)
        notify();
}

SongInfo BackendPlayback::songInfo(const QString& key)
{
    if (key.isEmpty())
        return {};
    const auto known = m_details.constFind(key);
    if (known != m_details.cend())
        return *known;
    fetchDetails(key);
    SongInfo info;
    info.key = key;
    return info;
}

void BackendPlayback::keep(qint64 now)
{
    // The backend stopped it: a session change (signing in or out) retires the queue and
    // stops playback. Keep where it was, and play on once the change is over if it was
    // playing. The user removed the last song: keep that song, from its start. The user
    // cleared the queue: nothing to keep.
    if (m_clearing)
        return;
    Kept kept;
    kept.current = m_state.current;
    kept.provider = m_state.provider;
    kept.durationMs = m_state.durationMs;
    kept.previewStartMs = m_state.previewStartMs;
    if (!m_removing) {
        kept.keys = m_state.queue;
        kept.index = m_state.queueIndex;
        for (qsizetype i = 0; i < kept.keys.size(); ++i)
            kept.origins.append(i < m_entries.size() ? m_origins.value(m_entries.at(i))
                                                     : PlayOrigin{});
        const bool returning = !m_seekKey.isEmpty() && m_seekKey == kept.current.key;
        kept.songPositionMs =
            returning ? m_seekSongMs // stopped again before getting back there
                      : positionAt(m_state, now) + std::max<qint64>(m_state.previewStartMs, 0);
        kept.resume =
            returning || m_state.status == playback::Playing || m_state.status == playback::Loading;
    }
    if (!kept.current.key.isEmpty())
        m_kept = std::move(kept);
    savePosition(m_state, now, true);
}

void BackendPlayback::savePosition(const PlaybackSnapshot& state, qint64 now, bool stopped)
{
    // Stopped by a session change, the session is already another one's.
    if (!stopped && settled())
        m_positionAccount = m_facade->snapshot().accountId;
    const QString& key = state.current.key;
    if (key.isEmpty() || (!stopped && !settled()))
        return;
    const bool due = stopped || key != m_savedKey || state.status != m_savedStatus ||
                     now - m_savedAt >= kSavePositionMs;
    if (!due || state.status == playback::Loading)
        return;
    const qint64 songMs = state.status == playback::Ended
                              ? 0
                              : positionAt(state, now) + std::max<qint64>(state.previewStartMs, 0);
    m_savedKey = key;
    m_savedStatus = state.status;
    m_savedAt = now;
    QSettings().setValue(positionKey(m_positionAccount),
                         QVariantMap{{QStringLiteral("song"), key},
                                     {QStringLiteral("positionMs"), songMs},
                                     {QStringLiteral("durationMs"), state.durationMs},
                                     {QStringLiteral("previewStartMs"), state.previewStartMs}});
}

std::optional<BackendPlayback::Saved> BackendPlayback::saved(const QString& songKey) const
{
    if (!m_facade)
        return std::nullopt;
    const QVariantMap map = QSettings().value(positionKey(m_facade->snapshot().accountId)).toMap();
    if (map.value(QStringLiteral("song")).toString() != songKey)
        return std::nullopt;
    return Saved{std::max<qint64>(map.value(QStringLiteral("positionMs")).toLongLong(), 0),
                 map.value(QStringLiteral("durationMs"), -1).toLongLong(),
                 map.value(QStringLiteral("previewStartMs"), -1).toLongLong()};
}

void BackendPlayback::showRestored(PlaybackSnapshot& next, qint64 now)
{
    // The queue came back at startup (the backend never plays it by itself): the song it
    // was on, paused where it was, as long as it was then (a preview's).
    next.current = songInfo(next.queue.at(next.queueIndex));
    next.status = playback::Paused;
    next.failure.reset();
    const auto last = saved(next.current.key);
    next.durationMs = last && last->durationMs > 0 ? last->durationMs : next.current.durationMs;
    next.previewStartMs = last ? last->previewStartMs : -1;
    next.seekable = next.durationMs > 0;
    next.positionMs =
        last ? std::max<qint64>(last->songMs - std::max<qint64>(next.previewStartMs, 0), 0) : 0;
    if (next.durationMs > 0)
        next.positionMs = std::min(next.positionMs, next.durationMs);
    next.positionAt = now;
    next.rate = 0;
}

void BackendPlayback::playRestored()
{
    const QUuid active = m_session ? m_session->queue().active : QUuid();
    if (active.isNull())
        return;
    const QString key = m_state.current.key;
    m_seekKey.clear();
    if (const auto last = saved(key); last && last->songMs > 0) {
        m_seekKey = key;
        m_seekSongMs = last->songMs;
    }
    if (m_session->restoredFm())
        m_session->resumeFm(active, quality());
    else
        m_session->select(active);
}

bool BackendPlayback::resumeRestoredFm(int step)
{
    if (!m_session || !m_session->restoredFm())
        return false;
    const int row = m_state.queueIndex + step;
    if (step < 0 && row < 0)
        return true; // nothing before the first
    m_seekKey.clear();
    m_session->resumeFm(row >= 0 && row < m_entries.size() ? m_entries.at(row) : QUuid(),
                        quality());
    return true;
}

void BackendPlayback::showKept(PlaybackSnapshot& next, qint64 now)
{
    Kept& kept = *m_kept;
    if (kept.current.title.isEmpty()) // its details may have come since
        kept.current = songInfo(kept.current.key);
    next.status = kept.songPositionMs > 0 || kept.resume ? playback::Paused : playback::Ended;
    next.failure.reset();
    next.current = kept.current;
    next.provider = kept.provider;
    next.generation = 0;
    next.durationMs = kept.durationMs;
    next.previewStartMs = kept.previewStartMs;
    next.seekable = kept.durationMs > 0;
    next.positionMs =
        std::max<qint64>(kept.songPositionMs - std::max<qint64>(kept.previewStartMs, 0), 0);
    next.positionAt = now;
    next.rate = 0;
    // Over a queue the backend restored for the account signed in to meanwhile: what
    // was just playing stays.
    if (next.mode == playback::Queue) {
        next.queue = kept.keys;
        next.queueIndex = kept.index;
    }
}

bool BackendPlayback::canResume() const
{
    return !m_live && m_kept && m_kept->resume && settled() && m_session;
}

bool BackendPlayback::settled() const
{
    if (!m_facade || m_facade->state() != application::BackendState::Ready)
        return false;
    const auto phase = m_facade->snapshot().phase;
    return phase == domain::SessionPhase::Guest || phase == domain::SessionPhase::Authenticated;
}

void BackendPlayback::replay(int index)
{
    if (!m_session || !m_kept)
        return;
    const Kept& kept = *m_kept;
    if (kept.keys.isEmpty()) {
        startQueue({kept.current.key}, 0, {});
        return;
    }
    index = std::clamp(index, 0, int(kept.keys.size()) - 1);
    m_seekKey.clear();
    if (index == kept.index && kept.songPositionMs > 0) {
        m_seekKey = kept.keys.at(index);
        m_seekSongMs = kept.songPositionMs;
    }
    startQueue(kept.keys, index, kept.origins);
}

void BackendPlayback::seekWhereKept(const domain::PlaybackSnapshot& play)
{
    if (m_seekKey.isEmpty())
        return;
    if (play.song.value != m_seekKey) {
        if (!play.song.value.isEmpty())
            m_seekKey.clear(); // another song now
        return;
    }
    if (!play.seekable ||
        (play.state != PlaybackState::Ready && play.state != PlaybackState::Playing &&
         play.state != PlaybackState::Paused))
        return;
    m_seekKey.clear();
    qint64 target = m_seekSongMs - (play.preview ? play.preview->startMs : 0);
    if (play.durationMs)
        target = std::min(target, *play.durationMs - 1000);
    if (target > 0)
        QMetaObject::invokeMethod(
            this,
            [this, target] {
                if (m_session)
                    m_session->seek(target);
            },
            Qt::QueuedConnection);
}

void BackendPlayback::fetchDetails(const QString& key)
{
    if (m_detailsKey == key)
        return; // asked (once per song, also when it failed)
    m_detailsKey = key;
    m_detailsRequest =
        m_catalog.songDetails({key}, [this](const Outcome<QList<SongInfo>>& outcome) {
            m_detailsRequest.release();
            if (!outcome.value)
                return;
            if (m_details.size() + outcome.value->size() > kDetailsKept)
                m_details.clear();
            for (const SongInfo& info : *outcome.value)
                m_details.insert(info.key, info);
            m_detailsArrived = true;
            update();
        });
}

void BackendPlayback::notify()
{
    if (m_notifyPending)
        return;
    m_notifyPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_notifyPending = false;
            if (m_changed)
                m_changed();
        },
        Qt::QueuedConnection);
}

QUuid BackendPlayback::entryAt(int index) const
{
    return index >= 0 && index < m_entries.size() ? m_entries.at(index) : QUuid();
}

void BackendPlayback::playSongs(const QStringList& keys, int index, const PlayOrigin& origin)
{
    startQueue(keys, index, QList<PlayOrigin>(keys.size(), origin));
}

void BackendPlayback::startQueue(const QStringList& keys, int index,
                                 const QList<PlayOrigin>& origins)
{
    if (!m_session || index < 0 || index >= keys.size())
        return;
    QList<domain::SongId> ids;
    QList<PlayOrigin> idOrigins;
    qsizetype start = -1;
    for (qsizetype i = 0; i < keys.size(); ++i) {
        if (!domain::validMusicId(keys.at(i)))
            continue;
        if (i == index)
            start = ids.size();
        ids.append(domain::SongId{keys.at(i)});
        idOrigins.append(origins.value(i));
    }
    // What the user asked to play (docs/tasks/logging.md §8); the session logs the rest.
    const PlayOrigin origin = origins.value(index);
    log::info(lcUi, "player.play",
              {{"count", keys.size()},
               {"valid", ids.size()},
               {"index", index},
               {"origin", playback::Origin(origin.kind)},
               {"origin_id", origin.kind == playback::FromSearch
                                 ? QStringLiteral("len:%1").arg(origin.id.size())
                                 : origin.id}});
    if (start < 0)
        return;
    // The backend's queue holds so many; keep those around the one chosen.
    constexpr qsizetype most = domain::MaximumSavedQueueEntries;
    if (ids.size() > most) {
        const qsizetype from = std::clamp<qsizetype>(start - most / 2, 0, ids.size() - most);
        ids = ids.mid(from, most);
        idOrigins = idOrigins.mid(from, most);
        start -= from;
    }
    loadOrigins(); // this account's, before replacing them
    QList<domain::PlayOrigin> stored;
    for (const auto& origin : std::as_const(idOrigins))
        stored.append(storedOrigin(origin));
    if (!m_session->replaceQueue(ids, start, quality(), stored) || !m_session) {
        log::warn(lcUi, "player.play_refused");
        return;
    }
    // The new entries, in the order given.
    const auto entries = m_session->queue().entries;
    m_origins.clear();
    for (qsizetype i = 0; i < entries.size() && i < idOrigins.size(); ++i)
        if (idOrigins.at(i).kind != playback::NoOrigin)
            m_origins.insert(entries.at(i).id, idOrigins.at(i));
    saveOrigins();
}

PlayOrigin BackendPlayback::originOf(QUuid entry)
{
    loadOrigins();
    return m_origins.value(entry);
}

void BackendPlayback::loadOrigins()
{
    const QString account = m_facade ? m_facade->snapshot().accountId : QString();
    if (m_originsLoaded && account == m_originsAccount)
        return;
    m_originsLoaded = true;
    m_originsAccount = account;
    m_origins.clear();
    const QVariantList groups = QSettings().value(originsKey(account)).toList();
    for (const QVariant& value : groups) {
        const QVariantMap group = value.toMap();
        const PlayOrigin origin{group.value(QStringLiteral("o")).toInt(),
                                group.value(QStringLiteral("i")).toString(),
                                group.value(QStringLiteral("n")).toString()};
        if (origin.kind <= playback::NoOrigin || origin.kind > playback::FromRanking)
            continue;
        for (const QString& entry : group.value(QStringLiteral("e")).toStringList())
            if (const QUuid id(entry); !id.isNull())
                m_origins.insert(id, origin);
    }
}

void BackendPlayback::saveOrigins() const
{
    // Grouped by origin: a queue usually has one.
    QList<std::pair<PlayOrigin, QStringList>> groups;
    for (auto it = m_origins.cbegin(); it != m_origins.cend(); ++it) {
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [&](const auto& g) { return g.first == it.value(); });
        if (group == groups.end()) {
            groups.append({it.value(), {}});
            group = groups.end() - 1;
        }
        group->second.append(it.key().toString(QUuid::WithoutBraces));
    }
    QVariantList list;
    for (const auto& [origin, entries] : groups)
        list.append(QVariantMap{{QStringLiteral("o"), origin.kind},
                                {QStringLiteral("i"), origin.id},
                                {QStringLiteral("n"), origin.name},
                                {QStringLiteral("e"), entries}});
    QSettings settings;
    if (list.isEmpty())
        settings.remove(originsKey(m_originsAccount));
    else
        settings.setValue(originsKey(m_originsAccount), list);
}

void BackendPlayback::playPause()
{
    if (!m_session)
        return;
    ListeningSession& session = *m_session;
    const auto play = session.playback();
    log::info(
        lcUi, "player.play_pause",
        {{"state", play.state}, {"intent", play.intent}, {"kept", bool(m_kept)}, {"live", m_live}});
    switch (play.state) {
    case PlaybackState::Empty:
    case PlaybackState::Ended:
    case PlaybackState::Failed:
        // Again from the start (a failed one tries again), or on from where it was kept.
        if (session.mode() == domain::ListeningMode::PersonalFm && session.waitingForFm())
            session.resume();
        else if (m_kept)
            replay(m_kept->index);
        else if (play.state == PlaybackState::Empty && !session.queue().active.isNull())
            playRestored();
        else if (const QUuid active = session.queue().active; !active.isNull())
            session.select(active);
        else if (!m_entries.isEmpty())
            session.select(m_entries.first());
        return;
    default:
        if (play.intent == domain::TransportIntent::Play)
            session.pause();
        else
            session.resume();
    }
}

bool BackendPlayback::showingKept() const
{
    return m_kept && !m_live && m_state.mode == playback::Queue;
}

// The song `step` places from the current one in play order, wrapping with repeat.
int BackendPlayback::stepped(int step) const
{
    const int count = int(m_state.queue.size());
    int row = m_state.queueIndex + step;
    if (m_state.queueIndex < 0)
        row = step > 0 ? 0 : -1;
    if ((row < 0 || row >= count) && m_state.repeat != playback::RepeatOff && count > 1)
        row = (row + count) % count;
    return row >= 0 && row < count && row != m_state.queueIndex ? row : -1;
}

void BackendPlayback::next()
{
    if (!m_session)
        return;
    if (showingKept()) {
        if (const int row = stepped(1); row >= 0)
            replay(row);
        return;
    }
    if (resumeRestoredFm(1))
        return;
    m_session->next();
}

void BackendPlayback::previous()
{
    if (!m_session)
        return;
    ListeningSession& session = *m_session;
    if (resumeRestoredFm(-1))
        return;
    if (session.mode() == domain::ListeningMode::PersonalFm) {
        session.previous(); // the song before (on the first one, back to its start)
        return;
    }
    // The one before in play order, as the queue shows it, always (not back to the start
    // of this one first). The session's own previous goes back through what played, which
    // after starting mid-list is nothing.
    const int row = stepped(-1);
    if (row < 0)
        return;
    if (showingKept())
        replay(row);
    else if (const QUuid entry = entryAt(row); !entry.isNull())
        session.select(entry);
}

void BackendPlayback::seek(qint64 positionMs)
{
    if (!m_session)
        return;
    positionMs = std::max<qint64>(positionMs, 0);
    if (m_kept && !m_live) {
        m_kept->songPositionMs = positionMs + std::max<qint64>(m_kept->previewStartMs, 0);
        update();
        return;
    }
    if (!m_live && !m_state.current.key.isEmpty()) {
        // The restored queue: where play starts.
        PlaybackSnapshot at = m_state;
        at.positionMs = positionMs;
        at.positionAt = playback::now();
        at.rate = 0;
        m_savedKey.clear(); // due now
        savePosition(at, at.positionAt, false);
        update();
        return;
    }
    m_session->seek(positionMs);
}

void BackendPlayback::playAt(int index)
{
    if (!m_session)
        return;
    if (showingKept()) {
        if (index >= 0 && index < m_state.queue.size())
            replay(index);
        return;
    }
    if (index == m_state.queueIndex && m_session->restoredFm()) {
        playRestored(); // where it was
        return;
    }
    if (resumeRestoredFm(index - m_state.queueIndex))
        return;
    if (const QUuid entry = entryAt(index); !entry.isNull())
        m_session->select(entry);
}

void BackendPlayback::removeAt(int index)
{
    if (!m_session || index < 0 || index >= m_state.queue.size())
        return;
    if (showingKept()) {
        Kept& kept = *m_kept;
        kept.keys.removeAt(index);
        if (index < kept.index) {
            --kept.index;
        } else if (index == kept.index) {
            // The next one is the one kept now, from its start.
            if (kept.index >= kept.keys.size())
                kept.index = kept.keys.isEmpty() ? -1 : 0;
            kept.current = songInfo(kept.index >= 0 ? kept.keys.at(kept.index) : QString());
            kept.songPositionMs = 0;
            kept.durationMs = kept.current.durationMs;
            kept.previewStartMs = -1;
            if (kept.current.key.isEmpty())
                m_kept.reset();
        }
        update();
        return;
    }
    const QUuid entry = entryAt(index);
    const bool last = index == m_entries.size() - 1;
    const bool playing = index == m_state.queueIndex;
    m_removing = true; // a stop now is the user's
    const bool removed = !entry.isNull() && m_session->remove(entry);
    m_removing = false;
    if (!removed)
        return;
    // The session stops after the last one; go on with the first instead.
    if (playing && last && !m_entries.isEmpty() && m_entries.first() != entry)
        if (const auto order = m_session->playOrder(); !order.isEmpty())
            m_session->select(order.first());
}

bool BackendPlayback::addSongs(const QStringList& keys, bool next, const PlayOrigin& origin)
{
    if (!m_session || m_state.mode == playback::PersonalFm)
        return false;
    QList<domain::SongId> ids;
    QStringList valid;
    for (const QString& key : keys)
        if (domain::validMusicId(key)) {
            ids.append(domain::SongId{key});
            valid << key;
        }
    if (ids.isEmpty())
        return false;
    log::info(lcUi, "player.add",
              {{"count", ids.size()}, {"next", next}, {"origin", playback::Origin(origin.kind)}});
    if (m_state.queue.isEmpty()) {
        startQueue(valid, 0, QList<PlayOrigin>(valid.size(), origin));
        return true;
    }
    if (showingKept()) {
        // Held over a session change: into what plays on once it is over.
        Kept& kept = *m_kept;
        const qsizetype at = next ? kept.index + 1 : kept.keys.size();
        for (qsizetype i = 0; i < valid.size(); ++i) {
            kept.keys.insert(at + i, valid.at(i));
            kept.origins.insert(std::min(at + i, kept.origins.size()), origin);
        }
        update();
        return true;
    }
    loadOrigins();
    QSet<QUuid> before;
    for (const auto& entry : m_session->queue().entries)
        before.insert(entry.id);
    if (!m_session->append(ids, next, storedOrigin(origin))) {
        log::warn(lcUi, "player.add_refused");
        return false;
    }
    if (origin.kind != playback::NoOrigin) {
        for (const auto& entry : m_session->queue().entries)
            if (!before.contains(entry.id))
                m_origins.insert(entry.id, origin);
        saveOrigins();
    }
    return true;
}

void BackendPlayback::clearQueue()
{
    if (!m_session || m_state.mode != playback::Queue)
        return;
    log::info(lcUi, "player.clear", {"count", m_state.queue.size()});
    m_kept.reset();
    m_clearing = true; // the stop it makes is the user's
    m_session->clear();
    m_clearing = false;
    m_kept.reset();
    loadOrigins();
    m_origins.clear();
    saveOrigins();
    update();
}

void BackendPlayback::move(int from, int to)
{
    const int count = int(m_state.queue.size());
    if (!m_session || from < 0 || from >= count || to < 0 || to >= count || from == to)
        return;
    if (showingKept()) {
        Kept& kept = *m_kept;
        kept.keys.move(from, to);
        if (kept.index == from)
            kept.index = to;
        else if (from < kept.index && to >= kept.index)
            --kept.index;
        else if (from > kept.index && to <= kept.index)
            ++kept.index;
        update();
        return;
    }
    if (const QUuid entry = entryAt(from); !entry.isNull())
        m_session->move(entry, to);
}

void BackendPlayback::applyVolume()
{
    // Straight to the facade: the session would count it as a user's command, and a
    // queue restored after one is refused.
    if (m_volume && m_facade && settled())
        static_cast<void>(m_facade->setPlaybackVolume(float(*m_volume)));
}

// A setting for the engine, not a user's command: straight to the facade, again on
// every session change (it refuses until ready; the engine ignores a repeat).
void BackendPlayback::applyNormalization()
{
    if (m_normalize && m_facade)
        static_cast<void>(m_facade->setPlaybackNormalization(*m_normalize));
}

BiliAccountState BackendPlayback::biliAccount() const
{
    const auto state = m_runtime.biliAccount();
    return {int(state.status), state.name};
}

void BackendPlayback::saveBiliCookie(const QString& cookie)
{
    m_runtime.saveBiliCookie(cookie.toUtf8());
}

void BackendPlayback::clearBiliCookie() { m_runtime.clearBiliCookie(); }

void BackendPlayback::setBiliSourceEnabled(bool on)
{
    m_facade->setAllowBiliSource(on);
}

void BackendPlayback::setNormalization(bool on)
{
    m_normalize = on;
    applyNormalization();
}

void BackendPlayback::applyTransition()
{
    if (m_transition && m_facade)
        static_cast<void>(
            m_facade->setPlaybackTransition(m_transition->mode, m_transition->crossfadeMs));
}

void BackendPlayback::setTransition(int mode, int crossfadeSeconds)
{
    // AppSettings::trackTransition holds the backend's TransitionMode.
    using domain::TransitionMode;
    const auto known = mode >= int(TransitionMode::Smart) && mode <= int(TransitionMode::Gapless);
    m_transition = Transition{known ? TransitionMode(mode) : TransitionMode::Smart,
                              qint64(std::clamp(crossfadeSeconds, 1, 12)) * 1000};
    applyTransition();
}

void BackendPlayback::setVolume(double volume)
{
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_session)
        m_session->setVolume(float(*m_volume));
    update();
}

void BackendPlayback::setOrder(playback::Order order)
{
    if (m_session)
        m_session->setModes(order == playback::Shuffle ? domain::QueueOrder::Shuffle
                                                       : domain::QueueOrder::Sequential,
                            m_session->queue().repeat);
}

void BackendPlayback::setRepeat(playback::Repeat repeat)
{
    if (m_session)
        m_session->setModes(m_session->queue().order,
                            repeat == playback::RepeatAll   ? domain::QueueRepeat::All
                            : repeat == playback::RepeatOne ? domain::QueueRepeat::One
                                                            : domain::QueueRepeat::Off);
}

void BackendPlayback::startFm()
{
    if (m_session)
        m_session->startFm(quality());
}

struct BackendLyrics::Job {
    QString key;
    Reply<SongLyrics> reply;
    quint64 generation = 0;
    quint64 query = 0; // the cache's
    Request service;   // the service's
    void finish(const Outcome<SongLyrics>& outcome)
    {
        if (auto r = std::exchange(reply, {}))
            r(outcome);
    }
};

BackendLyrics::BackendLyrics(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_session(&runtime.listening()), m_queries(runtime.resourceQueries()),
      m_requests(new MusicRequests(runtime.facade(), this)), m_facade(&runtime.facade())
{
    if (m_queries)
        connect(m_queries, &cache::CachedMusicQueryService::completed, this,
                &BackendLyrics::completed);
    connect(m_session, &ListeningSession::lyricsChanged, this, &BackendLyrics::sessionLyrics);
    connect(m_facade, &application::BackendFacade::playbackSourceSelected, this,
            [this] { sessionLyrics(); });
    connect(m_facade, &application::BackendFacade::playbackChanged, this,
            [this] { sessionLyrics(); });
}

BackendLyrics::~BackendLyrics() = default;

Request BackendLyrics::lyrics(const QString& songKey, Reply<SongLyrics> reply)
{
    if (!domain::validMusicId(songKey))
        return replyLater(
            this, std::move(reply),
            Outcome<SongLyrics>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    auto job = std::make_shared<Job>(Job{songKey, std::move(reply)});
    QTimer::singleShot(0, this, [this, job] {
        if (job->reply && !routePlayback(job))
            start(job);
    });
    return Request([self = QPointer<BackendLyrics>(this), job] {
        job->reply = {};
        job->service.cancel();
        if (!self)
            return;
        self->m_waiting.removeOne(job);
        if (job->query && self->m_pending.remove(job->query) && self->m_queries)
            self->m_queries->cancel(job->query);
    });
}

bool BackendLyrics::routePlayback(const std::shared_ptr<Job>& job)
{
    const auto generation = m_facade ? m_facade->playbackLyricGeneration({job->key}) : 0;
    if (job->generation && generation != job->generation) {
        job->finish(Outcome<SongLyrics>{std::nullopt, false,
                                       Failure{failure::Rejected, std::nullopt}});
        return true;
    }
    if (!generation)
        return false;
    job->generation = generation;
    if (m_session && m_session->playback().token.generation == generation) {
        const auto own = m_session->lyrics();
        if (own && own->song.value == job->key)
            job->finish(Outcome<SongLyrics>{lyricsOf(*own)});
        else if (m_session->lyricsState() == domain::LyricsState::Loading ||
                 m_session->playback().state == domain::PlaybackState::Resolving)
            m_waiting.append(job);
        else
            job->finish(Outcome<SongLyrics>{std::nullopt, false,
                                           Failure{failure::Rejected, std::nullopt}});
        return true;
    }
    if (!m_facade->playbackSource(generation)) {
        m_waiting.append(job);
        return true;
    }
    job->service.cancel();
    job->service = m_requests->submit(
        [generation](application::BackendFacade& facade) { return facade.playbackLyrics(generation); },
        [job](const application::MusicResult& result) {
            if (const auto* lyrics = std::get_if<domain::SongLyrics>(&result))
                job->finish(Outcome<SongLyrics>{lyricsOf(*lyrics)});
            else
                job->finish(Outcome<SongLyrics>{std::nullopt, false,
                                               Failure{failure::Rejected, std::nullopt}});
        },
        [job](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            job->finish(Outcome<SongLyrics>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
    return true;
}

void BackendLyrics::sessionLyrics()
{
    for (const auto& job : std::exchange(m_waiting, {}))
        if (job->reply && !routePlayback(job))
            job->finish(Outcome<SongLyrics>{std::nullopt, false,
                                           Failure{failure::Rejected, std::nullopt}});
}

void BackendLyrics::start(const std::shared_ptr<Job>& job)
{
    job->query =
        m_queries ? m_queries->lyrics(domain::SongId{job->key}, domain::CacheReadMode::PreferCache)
                  : 0;
    if (job->query)
        m_pending.insert(job->query, job);
    else
        fromService(job); // no cache, or it's busy
}

void BackendLyrics::completed(const cache::CachedMusicResult& result)
{
    const auto job = m_pending.take(result.id);
    if (!job)
        return;
    job->query = 0;
    if (!job->reply || routePlayback(job))
        return;
    if (result.value) {
        if (const auto* lyrics = std::get_if<domain::SongLyrics>(&*result.value)) {
            job->finish(Outcome<SongLyrics>{lyricsOf(*lyrics)});
            return;
        }
        if (const auto* failed = std::get_if<application::MusicFailure>(&*result.value)) {
            if (const auto kind = MusicRequests::kindOf(failed->error)) {
                job->finish(
                    Outcome<SongLyrics>{std::nullopt, false, Failure{*kind, failed->retryAfterMs}});
                return;
            }
        }
    }
    fromService(job); // overtaken by a session change: the service waits for the next
}

void BackendLyrics::fromService(const std::shared_ptr<Job>& job)
{
    if (!job->reply || routePlayback(job))
        return;
    const domain::SongId id{job->key};
    job->service = m_requests->submit(
        [id](application::BackendFacade& facade) { return facade.lyrics(id); },
        [this, job](const application::MusicResult& result) {
            if (!job->reply || routePlayback(job))
                return;
            if (const auto* lyrics = std::get_if<domain::SongLyrics>(&result))
                job->finish(Outcome<SongLyrics>{lyricsOf(*lyrics)});
            else
                job->finish(Outcome<SongLyrics>{std::nullopt, false,
                                                Failure{failure::Rejected, std::nullopt}});
        },
        [job](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            job->finish(Outcome<SongLyrics>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

} // namespace cirrus::ui

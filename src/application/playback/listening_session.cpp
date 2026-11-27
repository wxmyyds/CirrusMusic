#include "listening_session.h"
#include "application/music/personal_fm_source.h"
#include "logging/log.h"
#include "vocal_spans.h"
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <utility>

namespace cirrus::application
{
using namespace domain;
namespace
{
bool sameSource(const PlaybackSourceSelection& a, const PlaybackSourceSelection& b)
{
    return a.token == b.token && a.song == b.song && a.provider == b.provider && a.bili == b.bili;
}
} // namespace
ListeningSession::ListeningSession(BackendFacade& backend, QObject* parent)
    : QObject(parent), m_backend(backend)
{
    Q_ASSERT(backend.thread() == thread());
    connect(&backend, &BackendFacade::playbackChanged, this, &ListeningSession::receive);
    connect(&backend, &BackendFacade::playbackSourceSelected, this,
            &ListeningSession::sourceSelected);
    connect(&backend, &BackendFacade::acknowledged, this, [this] {
        if (const auto revision = std::exchange(m_pendingFailureStop, std::nullopt))
            QTimer::singleShot(0, this, [this, revision] { stopForFailures(*revision); });
    });
    connect(&backend, &BackendFacade::stateChanged, this, [this](BackendState state) {
        if (state == BackendState::Stopping || state == BackendState::Stopped ||
            state == BackendState::Failed)
            reset();
    });
    connect(&backend, &BackendFacade::sessionChanged, this, [this](SessionSnapshot value) {
        if (m_queueContext.isValid() &&
            (value.token != m_queueContext || value.accountId != m_sourceSession.accountId ||
             value.phase != m_sourceSession.phase))
            reset();
    });
    connect(&backend, &BackendFacade::musicCompleted, this,
            [this](quint64 id, SessionToken context, MusicResult result) {
                if (id && id == m_preloadLyricRequest) {
                    m_preloadLyricRequest = 0;
                    const auto* lyrics = std::get_if<SongLyrics>(&result);
                    if (lyrics && m_preload && lyrics->song == m_preload->song &&
                        context == m_token.session)
                        static_cast<void>(m_backend.setPlaybackVocals(m_preload->generation,
                                                                      vocalSpans(*lyrics)));
                    return;
                }
                if (!id || id != m_lyricRequest)
                    return;
                m_lyricRequest = 0;
                if (!current() || context != m_token.session)
                    return;
                const auto revision = m_revision;
                const auto sourceRevision = m_lyricSourceRevision;
                const QPointer<ListeningSession> guard(this);
                const auto* lyrics = std::get_if<SongLyrics>(&result);
                if (!lyrics || lyrics->song != m_playback.song) {
                    const auto* failure = std::get_if<MusicFailure>(&result);
                    m_lyricsState = LyricsState::Failed;
                    emit lyricsFailed(failure ? *failure
                                              : MusicFailure{MusicError::InvalidResponse});
                    if (guard && revision == m_revision && sourceRevision == m_lyricSourceRevision)
                        emit lyricsChanged(m_lyricsState);
                    return;
                }
                m_lyrics = *lyrics;
                m_timeline.reset(*lyrics);
                m_lyricsState = LyricsState::Ready;
                // Where it's sung, for the transition out of it (docs/tasks/automix.md §7).
                static_cast<void>(m_backend.setPlaybackVocals(m_playback.token.generation,
                                                              vocalSpans(*lyrics, m_userOffset)));
                if (!guard || revision != m_revision || sourceRevision != m_lyricSourceRevision)
                    return;
                emit lyricsChanged(m_lyricsState);
                if (guard && revision == m_revision && sourceRevision == m_lyricSourceRevision)
                    refreshCursors();
            });
}
ListeningSession::~ListeningSession()
{
    if (m_fm) {
        disconnect(m_fm, nullptr, this, nullptr);
        if (m_mode == ListeningMode::PersonalFm)
            m_fm->stop();
    }
    cancelLyrics();
}
bool ListeningSession::current() const
{
    return m_backend.state() == BackendState::Ready && m_token.generation &&
           m_backend.snapshot().token == m_token.session && m_backend.playback().token == m_token;
}
bool ListeningSession::acceptLoadCommand(quint64 id, quint64 revision)
{
    if (revision != m_revision)
        return false;
    if (!id) {
        --m_revision; // local rejection must not invalidate an already scheduled accepted intent
        return false;
    }
    if (m_backend.playback().token.generation != id) {
        reset(); // a reentrant direct facade command won; never adopt its load
        return false;
    }
    return true;
}
void ListeningSession::cancelPreloadLyrics()
{
    const auto id = std::exchange(m_preloadLyricRequest, 0);
    if (m_preload) {
        m_preload->lyricSource.reset();
        ++m_preload->lyricSourceRevision;
    }
    if (id)
        m_backend.cancelMusic(id);
}
void ListeningSession::cancelLyrics()
{
    const auto id = std::exchange(m_lyricRequest, 0);
    m_lyricSource.reset();
    ++m_lyricSourceRevision;
    m_lyrics.reset();
    m_timeline.clear();
    m_lyricsState = LyricsState::None;
    const QPointer<ListeningSession> guard(this);
    cancelPreloadLyrics();
    if (guard && id)
        m_backend.cancelMusic(id);
}
void ListeningSession::reset()
{
    const auto revision = ++m_revision;
    const bool wasFm = m_mode == ListeningMode::PersonalFm;
    if (m_backend.state() != BackendState::Ready ||
        m_history.context() != m_backend.snapshot().token)
        m_history.reset();
    else
        m_history.finish(ListeningEnd::Detached);
    clearFmState();
    m_queue.clear();
    m_playback = {};
    m_token = {};
    m_queueContext = {};
    m_sourceSession = {};
    m_positionObservedAt.reset();
    m_auto = false;
    m_autoVisited.clear();
    m_terminalHandled = false;
    m_failureStreak = 0;
    m_failureStopNotified = false;
    m_pendingFailureStop.reset();
    m_heard = false;
    m_lastObservedPlayedMs = 0;
    m_preload.reset();
    m_preloadChecked = false;
    cancelLyrics();
    if (revision != m_revision)
        return;
    if (wasFm && m_fm)
        m_fm->stop();
    if (revision != m_revision)
        return;
    if (!notifyHistory())
        return;
    if (revision != m_revision)
        return;
    emit fmChanged();
    if (revision != m_revision)
        return;
    emit queueChanged(m_queue.snapshot());
    if (revision != m_revision)
        return;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return;
    emit lyricsChanged(m_lyricsState);
    if (revision == m_revision)
        refreshCursors();
}
bool ListeningSession::begin(PlaybackQueue candidate, TransportIntent intent, AudioQuality quality,
                             bool automatic, bool fm)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto entry = candidate.current();
    if (!entry || m_backend.state() != BackendState::Ready) {
        log::warn(lcPlayback, "queue.begin_refused",
                  {"reason", entry ? "backend_not_ready" : "no_entry"});
        return false;
    }
    const auto sourceSession = m_backend.snapshot();
    const auto revision = ++m_revision;
    m_starting = true;
    const auto id = m_backend.playSong(entry->song, quality, intent,
                                       entry->program.value.isEmpty() && m_permitBiliSource);
    m_starting = false;
    log::debug(lcPlayback, "queue.begin",
               {{"song", entry->song.value},
                {"intent", intent},
                {"automatic", automatic},
                {"fm", fm},
                {"command", id}});
    if (!acceptLoadCommand(id, revision)) {
        log::warn(lcPlayback, "queue.begin_refused", {{"reason", "command"}, {"command", id}});
        return false;
    }
    commit(std::move(candidate), intent, quality, automatic, fm, sourceSession, revision);
    return true;
}
void ListeningSession::commit(PlaybackQueue candidate, TransportIntent intent, AudioQuality quality,
                              bool automatic, bool fm, SessionSnapshot sourceSession,
                              quint64 revision)
{
    const auto entry = candidate.current();
    m_preload.reset();
    m_preloadChecked = false;
    const auto oldLyricRequest = std::exchange(m_lyricRequest, 0);
    const auto oldPreloadLyricRequest = std::exchange(m_preloadLyricRequest, 0);
    m_lyricSource.reset();
    ++m_lyricSourceRevision;
    m_lyrics.reset();
    m_timeline.clear();
    m_lyricsState = LyricsState::Loading;
    const bool leaveFm = !fm && m_mode == ListeningMode::PersonalFm;
    m_restoredFm = false;
    if (!fm)
        clearFmState();
    else {
        m_fmWaiting = false;
        m_fmBlocked = false;
        m_fmPending.reset();
    }
    m_queue = std::move(candidate);
    m_playback = m_backend.playback();
    m_token = m_playback.token;
    m_queueContext = m_token.session;
    m_sourceSession = sourceSession;
    m_positionObservedAt.reset();
    m_history.begin(m_token, m_backend.snapshot().accountId, entry->song,
                    fm ? ListeningMode::PersonalFm : ListeningMode::Queue, fm ? QUuid{} : entry->id,
                    fm ? PlayOrigin{} : entry->origin, fm ? ProgramId{} : entry->program,
                    fm ? QString() : entry->alg);
    m_quality = quality;
    m_intent = intent;
    m_auto = automatic;
    m_terminalHandled = false;
    m_heard = false;
    m_lastObservedPlayedMs = 0;
    if (!automatic || fm)
        m_autoVisited.clear();
    else
        m_autoVisited.insert(entry->id);
    if (leaveFm && m_fm)
        m_fm->stop();
    // Captured old query must retire even when source-stop observers submit a newer intent.
    if (oldLyricRequest)
        m_backend.cancelMusic(oldLyricRequest);
    if (oldPreloadLyricRequest)
        m_backend.cancelMusic(oldPreloadLyricRequest);
    if (revision != m_revision)
        return;
    if (entry->program.value.isEmpty())
        requestLyrics();
    else
        m_lyricsState = LyricsState::None;
    if (revision != m_revision)
        return;
    if (!notifyHistory())
        return;
    if (revision != m_revision)
        return;
    emit fmChanged();
    if (revision != m_revision)
        return;
    emit queueChanged(queue());
    if (revision != m_revision)
        return;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return;
    emit lyricsChanged(m_lyricsState);
    if (revision == m_revision)
        refreshCursors();
    return;
}
bool ListeningSession::replaceQueue(const QList<SongId>& songs, qsizetype startIndex,
                                    AudioQuality quality, const QList<PlayOrigin>& origins,
                                    const QList<ProgramId>& programs)
{
    log::info(lcPlayback, "queue.replace",
              {{"count", songs.size()}, {"index", startIndex}, {"quality", quality}});
    if (startIndex < 0 || startIndex >= songs.size())
        return false;
    auto candidate = m_queue;
    if (!candidate.replace(songs, origins, programs) ||
        !candidate.select(candidate.snapshot().entries[startIndex].id))
        return false;
    return begin(std::move(candidate), TransportIntent::Play, quality, false);
}
bool ListeningSession::enterHeartbeat(const PlaylistId& playlist, const QList<QueueEntry>& mix,
                                      bool keepPlaying, AudioQuality quality,
                                      const QString& activeAlg)
{
    log::info(lcPlayback, "queue.heartbeat",
              {{"count", mix.size()}, {"keep", keepPlaying}, {"quality", quality}});
    if (m_mode == ListeningMode::PersonalFm || (m_queueContext.isValid() && !queueContextMatches()))
        return false;
    if (keepPlaying) {
        if (!m_queue.enterHeartbeat(playlist, mix, true, activeAlg))
            return false;
        if (current())
            m_history.setAlg(m_queue.snapshot().active, activeAlg);
        recordControlIntent();
        refreshPreload();
        emit queueChanged(m_queue.snapshot());
        return true;
    }
    auto candidate = m_queue;
    if (!candidate.enterHeartbeat(playlist, mix, false) ||
        !candidate.select(candidate.snapshot().entries.first().id))
        return false;
    return begin(std::move(candidate), TransportIntent::Play, quality, false);
}
bool ListeningSession::restoreQueue(const QueueSnapshot& saved, SessionSnapshot expected,
                                    std::pair<quint64, quint64> expectedIntent, ListeningMode mode)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_backend.state() != BackendState::Ready || !expected.token.isValid() ||
        m_backend.snapshot() != expected || intentRevision() != expectedIntent || current() ||
        m_mode != ListeningMode::Queue || !m_queue.snapshot().entries.isEmpty() ||
        m_revision == std::numeric_limits<quint64>::max())
        return false;
    auto candidate = m_queue;
    if (!candidate.restore(saved))
        return false;
    log::info(lcPlayback, "queue.restored", {{"count", saved.entries.size()}, {"mode", mode}});
    const auto revision = ++m_revision;
    m_queue = std::move(candidate);
    // Personal FM needs the account it was played with (signed in); else a plain queue.
    m_restoredFm =
        mode == ListeningMode::PersonalFm && m_fm && expected.phase == SessionPhase::Authenticated;
    m_queueContext = expected.token;
    m_sourceSession = expected;
    m_intent = TransportIntent::Play; // only a future explicit user command may start media
    const QPointer<ListeningSession> guard(this);
    emit queueChanged(m_queue.snapshot());
    return guard && m_revision == revision && m_backend.snapshot() == expected;
}
bool ListeningSession::append(const QList<SongId>& songs, bool playNext, const PlayOrigin& origin,
                              const QList<ProgramId>& programs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_mode == ListeningMode::PersonalFm || m_backend.state() != BackendState::Ready ||
        !m_backend.snapshot().token.isValid())
        return false;
    if (m_queueContext.isValid() && !queueContextMatches())
        return false;
    if (!m_queue.append(songs, playNext, origin, programs))
        return false;
    m_queueContext = m_backend.snapshot().token;
    m_sourceSession = m_backend.snapshot();
    refreshPreload();
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::select(QUuid entry)
{
    log::info(lcPlayback, "queue.select", {"entry", entry});
    if (m_mode == ListeningMode::PersonalFm) // one of its songs so far
        return queueContextMatches() && playFm(entry, false);
    if (!queueContextMatches())
        return false;
    auto candidate = m_queue;
    return candidate.select(entry) &&
           begin(std::move(candidate),
                 m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                 false);
}
bool ListeningSession::next()
{
    log::info(lcPlayback, "queue.next", {"mode", m_mode});
    if (m_mode == ListeningMode::PersonalFm) {
        // On through the songs so far after going back, then a new one.
        if (const QUuid after = m_fmWaiting ? QUuid{} : fmNeighbour(1); !after.isNull())
            return playFm(after, false);
        return !m_fmWaiting && enterFmWait(false, true);
    }
    if (!queueContextMatches())
        return false;
    auto candidate = m_queue;
    return candidate.next() &&
           begin(std::move(candidate),
                 m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                 false);
}
bool ListeningSession::canGoNext() const
{
    if (m_backend.state() != BackendState::Ready || !queueContextMatches())
        return false;
    return m_mode == ListeningMode::PersonalFm ? current() && !m_fmWaiting && m_fm
                                               : m_queue.canNext();
}
bool ListeningSession::canGoPrevious() const
{
    if (m_backend.state() != BackendState::Ready || !queueContextMatches())
        return false;
    const bool restart = current() && m_playback.positionMs > 3000 && m_playback.seekable;
    if (m_mode == ListeningMode::PersonalFm)
        return current() && (m_fmWaiting ? !m_queue.snapshot().active.isNull()
                                         : !fmNeighbour(-1).isNull() || restart);
    return restart || m_queue.canPrevious();
}
bool ListeningSession::previous()
{
    log::info(lcPlayback, "queue.previous", {"mode", m_mode});
    if (m_mode == ListeningMode::PersonalFm) {
        if (!current())
            return false;
        // The song before, from its start; while the next one is fetched, the last one.
        if (const QUuid before = m_fmWaiting ? m_queue.snapshot().active : fmNeighbour(-1);
            !before.isNull())
            return playFm(before, false);
        return !m_fmWaiting && m_playback.positionMs > 3000 && m_playback.seekable && seek(0);
    }
    if (!queueContextMatches())
        return false;
    if (current() && m_playback.positionMs > 3000 && m_playback.seekable)
        return seek(0);
    auto candidate = m_queue;
    return candidate.previous() &&
           begin(std::move(candidate),
                 m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                 false);
}
bool ListeningSession::remove(QUuid entry)
{
    if (m_mode == ListeningMode::PersonalFm || !queueContextMatches())
        return false;
    auto candidate = m_queue;
    const bool active = candidate.snapshot().active == entry;
    const auto successor = candidate.successor();
    if (!candidate.remove(entry))
        return false;
    if (active && !successor.isNull()) {
        candidate.select(successor);
        return begin(std::move(candidate),
                     m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play,
                     m_quality, false);
    }
    const auto revision = m_revision;
    if (active) {
        if (!stop())
            return false;
        if (m_revision != revision + 1)
            return true; // a later observer intent wins
    }
    m_queue = std::move(candidate);
    refreshPreload();
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::clear()
{
    const auto source = m_backend.snapshot();
    const auto revision = m_revision;
    if (current()) {
        if (!stop())
            return false;
        if (m_revision != revision + 1)
            return true;
    }
    reset();
    if (m_backend.state() == BackendState::Ready && m_backend.snapshot() == source)
        emit queueCleared(source);
    return true;
}
bool ListeningSession::stop() { return stopPlayback(false); }

bool ListeningSession::stopPlayback(bool keepFmQueue)
{
    if (!current())
        return false;
    log::info(lcPlayback, "queue.stop");
    const auto revision = ++m_revision;
    m_starting = true;
    const auto id = m_backend.stopPlayback();
    m_starting = false;
    if (!acceptLoadCommand(id, revision))
        return false;
    m_history.finish(ListeningEnd::Stopped);
    if (m_mode == ListeningMode::PersonalFm && !keepFmQueue) {
        reset();
        return true;
    }
    if (m_mode == ListeningMode::PersonalFm) {
        m_fmWaiting = true;
        m_fmBlocked = true;
        m_fmPending.reset();
    }
    m_preload.reset();
    cancelLyrics();
    if (revision != m_revision)
        return true;
    m_token = m_backend.playback().token;
    m_playback = m_backend.playback();
    m_positionObservedAt.reset();
    m_intent = TransportIntent::Stop;
    m_auto = false;
    m_autoVisited.clear();
    if (!notifyHistory())
        return true;
    if (revision != m_revision)
        return true;
    if (m_mode == ListeningMode::PersonalFm)
        emit fmChanged();
    if (revision != m_revision)
        return true;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return true;
    emit lyricsChanged(m_lyricsState);
    if (revision == m_revision)
        refreshCursors();
    return true;
}
bool ListeningSession::pause()
{
    if (current() && m_mode == ListeningMode::PersonalFm &&
        (m_fmWaiting || m_playback.state == PlaybackState::Ended ||
         (m_auto && m_playback.state == PlaybackState::Failed))) {
        if (m_intent == TransportIntent::Pause) {
            recordControlIntent();
            return true;
        }
        recordControlIntent();
        m_intent = TransportIntent::Pause;
        m_playback.intent = m_intent;
        emit playbackChanged(m_playback);
        return true;
    }
    if (!current() || m_playback.state == PlaybackState::Empty ||
        m_playback.state == PlaybackState::Failed || m_playback.state == PlaybackState::Ended)
        return false;
    if (!m_backend.pausePlayback())
        return false;
    recordControlIntent();
    m_intent = TransportIntent::Pause;
    m_playback.intent = m_intent;
    emit playbackChanged(m_playback);
    return true;
}
bool ListeningSession::resume()
{
    if (!queueContextMatches())
        return false;
    if (m_mode == ListeningMode::PersonalFm && m_fmWaiting) {
        if (m_intent == TransportIntent::Play && !m_fmBlocked) {
            recordControlIntent();
            return true;
        }
        recordControlIntent();
        m_intent = TransportIntent::Play;
        m_playback.intent = m_intent;
        emit playbackChanged(m_playback);
        return retryFm();
    }
    if (current() && m_playback.state != PlaybackState::Empty &&
        m_playback.state != PlaybackState::Failed && m_playback.state != PlaybackState::Ended) {
        if (!m_backend.resumePlayback())
            return false;
        recordControlIntent();
        m_intent = TransportIntent::Play;
        m_playback.intent = m_intent;
        emit playbackChanged(m_playback);
        return true;
    }
    return begin(m_queue, TransportIntent::Play, m_quality, false,
                 m_mode == ListeningMode::PersonalFm);
}
bool ListeningSession::seek(qint64 position)
{
    if (!current() || !m_backend.seekPlayback(position))
        return false;
    recordControlIntent();
    return true;
}
bool ListeningSession::setVolume(float linear)
{
    if (!m_backend.setPlaybackVolume(linear))
        return false;
    recordControlIntent();
    return true;
}
bool ListeningSession::move(QUuid entry, qsizetype to)
{
    if (m_mode == ListeningMode::PersonalFm || (m_queueContext.isValid() && !queueContextMatches()))
        return false;
    if (!m_queue.move(entry, to))
        return false;
    recordControlIntent();
    refreshPreload();
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::setModes(QueueOrder order, QueueRepeat repeat)
{
    if (m_mode == ListeningMode::PersonalFm || (m_queueContext.isValid() && !queueContextMatches()))
        return false;
    if (!m_queue.setModes(order, repeat))
        return false;
    recordControlIntent();
    refreshPreload();
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::setLyricOffset(qint64 offset)
{
    if (offset < -120000 || offset > 120000)
        return false;
    m_userOffset = offset;
    emit lyricOffsetChanged(offset); // even an empty/unchanged cursor needs a new observation
    refreshCursors();
    return true;
}
bool ListeningSession::notifyHistory(bool notifyView)
{
    const QPointer<ListeningSession> guard(this);
    const auto epoch = m_historyClearEpoch;
    const auto updates = m_history.takeUpdates();
    const bool overflow = m_history.takeUpdateOverflow();
    for (const auto& update : updates) {
        emit listeningRecordChanged(update.record);
        if (!guard || m_historyClearEpoch != epoch)
            return bool(guard);
        emit reportingRecordChanged(update.record, update.provider);
        if (!guard || m_historyClearEpoch != epoch)
            return bool(guard);
    }
    if (overflow)
        emit historyStorageOverflow();
    if (!guard)
        return false;
    if (notifyView)
        emit historyChanged();
    return bool(guard);
}
void ListeningSession::finishHistoryForStorage()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_history.finish(ListeningEnd::Detached);
    notifyHistory(false); // no player command or restored reporting event
}
void ListeningSession::clearHistory()
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QPointer<ListeningSession> guard(this);
    const auto source = m_backend.snapshot();
    const bool ready = m_backend.state() == BackendState::Ready;
    const bool changed = !m_history.records().isEmpty();
    ++m_historyClearEpoch;
    if (m_history.context() != source.token || !ready)
        m_history.reset();
    else
        m_history.clear();
    if (ready && source.token.isValid())
        emit historyCleared(source); // delete precedes any newly reentrant post-clear load
    if (guard)
        notifyHistory(changed);
}
void ListeningSession::setBiliSourcePermitted(bool permit)
{
    if (permit == m_permitBiliSource)
        return;
    m_permitBiliSource = permit;
    m_preload.reset();
    cancelPreloadLyrics();
    if (current())
        static_cast<void>(m_backend.preloadSong({}, m_quality, false));
    refreshPreload();
}

void ListeningSession::requestLyrics()
{
    if (const auto selection = m_backend.playbackSource(m_token.generation))
        sourceSelected(*selection);
}

void ListeningSession::sourceSelected(PlaybackSourceSelection selection)
{
    const auto selected = m_backend.playbackSource(selection.token.generation);
    if (!current() || !selected || !sameSource(selection, *selected))
        return;
    const auto revision = m_revision;
    const QPointer<ListeningSession> guard(this);
    auto* backend = &m_backend; // facade outlives this session, including reentrant deletion
    if (selection.token == m_token) {
        const auto entry = m_queue.current();
        if (!entry || !entry->program.value.isEmpty() || selection.song != m_playback.song ||
            m_terminalHandled || m_playback.state == PlaybackState::Failed ||
            m_playback.state == PlaybackState::Empty ||
            (m_lyricSource && sameSource(*m_lyricSource, selection)))
            return;
        const bool replacing = m_lyricSource.has_value();
        const auto old = std::exchange(m_lyricRequest, 0);
        m_lyricSource = selection;
        const auto sourceRevision = ++m_lyricSourceRevision;
        m_lyrics.reset();
        m_timeline.clear();
        const bool hadCursors = m_cursors != LyricCursors{};
        m_cursors = {};
        m_lyricsState = LyricsState::Loading;
        const auto valid = [&] {
            return guard && m_revision == revision && m_lyricSourceRevision == sourceRevision &&
                   current();
        };
        if (old)
            backend->cancelMusic(old); // request id was retired before cancellation can reenter
        if (!valid())
            return;
        if (replacing)
            static_cast<void>(backend->setPlaybackVocals(selection.token.generation, {}));
        if (!valid())
            return;
        emit lyricsChanged(m_lyricsState); // invalidate the published document before querying
        if (!valid())
            return;
        if (hadCursors)
            emit cursorsChanged(m_cursors);
        if (!valid())
            return;
        const auto id = backend->playbackLyrics(selection.token.generation);
        if (!valid()) {
            if (id)
                backend->cancelMusic(id);
            return;
        }
        m_lyricRequest = id;
        if (!id) {
            m_lyricsState = LyricsState::Failed;
            emit lyricsChanged(m_lyricsState);
        }
        return;
    }
    const auto entry = m_preload ? m_preload->queue.current() : std::nullopt;
    if (!m_preload || (!m_preload->fromFm && (!entry || !entry->program.value.isEmpty())) ||
        selection.token.generation != m_preload->generation ||
        selection.token.session != m_token.session || selection.song != m_preload->song ||
        (m_preload->lyricSource && sameSource(*m_preload->lyricSource, selection)))
        return;
    const bool replacing = m_preload->lyricSource.has_value();
    const auto old = std::exchange(m_preloadLyricRequest, 0);
    m_preload->lyricSource = selection;
    const auto sourceRevision = ++m_preload->lyricSourceRevision;
    const auto valid = [&] {
        return guard && m_revision == revision && current() && m_preload &&
               m_preload->generation == selection.token.generation &&
               m_preload->lyricSourceRevision == sourceRevision;
    };
    if (old)
        backend->cancelMusic(old);
    if (!valid())
        return;
    if (replacing)
        static_cast<void>(backend->setPlaybackVocals(selection.token.generation, {}));
    if (!valid())
        return;
    const auto id = backend->playbackLyrics(selection.token.generation);
    if (!valid()) {
        if (id)
            backend->cancelMusic(id);
        return;
    }
    m_preloadLyricRequest = id;
}

void ListeningSession::refreshCursors()
{
    LyricCursors cursor;
    if (current() && m_lyricsState == LyricsState::Ready &&
        (m_playback.state == PlaybackState::Ready || m_playback.state == PlaybackState::Playing ||
         m_playback.state == PlaybackState::Paused)) {
        constexpr qint64 maximum = 7LL * 24 * 60 * 60 * 1000;
        const auto start = m_playback.preview ? m_playback.preview->startMs : 0;
        if (start >= 0 && start <= maximum && m_playback.positionMs >= 0 &&
            m_playback.positionMs <= maximum - start)
            cursor = m_timeline.at(m_playback.positionMs + start, m_userOffset);
    }
    if (cursor != m_cursors) {
        m_cursors = std::move(cursor);
        emit cursorsChanged(m_cursors);
    }
}
void ListeningSession::receive(PlaybackSnapshot snapshot)
{
    if (m_starting)
        return;
    if (!m_token.generation)
        return;
    if (m_preload && snapshot.token.generation == m_preload->generation &&
        snapshot.token.session == m_token.session && m_backend.playback().token == snapshot.token &&
        m_backend.state() == BackendState::Ready && m_backend.snapshot().token == m_token.session) {
        adoptAdvance(); // played on into the preloaded song
        return;
    }
    if (!current()) {
        reset();
        return;
    } // external facade intent/session change disowns this queue
    if (snapshot.token != m_token || snapshot.song != m_playback.song)
        return;
    const auto revision = m_revision;
    if (snapshot.observed && snapshot.observed->playedMs >= m_lastObservedPlayedMs &&
        snapshot.observed->playedMs <= ObservedPlayback::MaximumMs &&
        (!snapshot.observed->capped ||
         snapshot.observed->playedMs == ObservedPlayback::MaximumMs)) {
        m_lastObservedPlayedMs = snapshot.observed->playedMs;
        if (!m_heard && m_lastObservedPlayedMs >= 1000) {
            m_heard = true;
            m_failureStreak = 0;
            m_failureStopNotified = false;
            m_pendingFailureStop.reset();
            m_autoVisited.clear();
        }
    }
    if (snapshot.positionRevision != m_playback.positionRevision) {
        m_positionObservedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count(); // owner arrival, not decoder/acoustic time
    }
    const bool end = snapshot.state == PlaybackState::Ended;
    const bool terminal = !m_terminalHandled && (end || snapshot.state == PlaybackState::Failed);
    if (terminal) {
        m_terminalHandled = true;
        if (!end) {
            m_failureStreak = qMin(m_failureStreak + 1, 10);
            m_autoVisited.insert(m_queue.snapshot().active);
        }
        if (m_lyricsState == LyricsState::Loading && !m_lyricSource)
            m_lyricsState = LyricsState::Failed;
    }
    m_playback = snapshot;
    m_playback.intent = m_intent;
    if (m_history.observe(snapshot)) {
        if (!notifyHistory())
            return;
        if (revision != m_revision)
            return;
    }
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return;
    if (terminal && m_lyricsState == LyricsState::Failed)
        emit lyricsChanged(m_lyricsState);
    if (revision != m_revision)
        return;
    refreshCursors();
    if (revision != m_revision)
        return;
    if (!m_preloadChecked && snapshot.state == PlaybackState::Playing) {
        m_preloadChecked = true; // once playing, so it doesn't compete with this one's start
        refreshPreload();
    }
    if (revision != m_revision || !terminal)
        return;
    if (!end && m_failureStreak >= 10) {
        QTimer::singleShot(0, this, [this, revision] { stopForFailures(revision); });
        return;
    }
    QTimer::singleShot(0, this, [this, revision, end] {
        if (revision == m_revision && current()) {
            if (m_mode == ListeningMode::PersonalFm) {
                // After going back, on through the songs so far first.
                if (const QUuid after = fmNeighbour(1); after.isNull() || !playFm(after, true))
                    enterFmWait(true, false);
            } else
                advance(end);
        }
    });
}
void ListeningSession::stopForFailures(quint64 revision)
{
    if (revision != m_revision || !current() || m_failureStreak < 10 ||
        m_playback.state != PlaybackState::Failed)
        return;
    if (!stopPlayback(true)) {
        if (revision == m_revision)
            m_pendingFailureStop = revision;
        return;
    }
    if (m_revision == revision + 1 && m_intent == TransportIntent::Stop &&
        m_playback.state == PlaybackState::Empty && !m_failureStopNotified) {
        m_failureStopNotified = true;
        emit failureStreakExceeded();
    }
}

void ListeningSession::refreshPreload()
{
    if (m_preloadChecked && current() && m_mode == ListeningMode::PersonalFm) {
        refreshFmPreload();
        return;
    }
    if (!m_preloadChecked || m_mode != ListeningMode::Queue || !current() || !queueContextMatches())
        return;
    const auto state = m_playback.state;
    if (state != PlaybackState::Playing && state != PlaybackState::Paused &&
        state != PlaybackState::Ready)
        return;
    // What advance() would play at the natural end.
    auto candidate = m_queue;
    std::optional<QueueEntry> entry;
    if (candidate.next(true))
        entry = candidate.current();
    if (!entry) {
        if (m_preload) {
            m_preload.reset();
            cancelPreloadLyrics();
            static_cast<void>(m_backend.preloadSong({}, m_quality));
        }
        return;
    }
    requestPreload(entry->song, entry->id, std::move(candidate), false);
}
void ListeningSession::requestPreload(SongId song, QUuid entry, PlaybackQueue queue, bool fromFm)
{
    if (m_preload && m_preload->entry == entry && m_preload->song == song &&
        m_preload->fromFm == fromFm) {
        m_preload->queue = std::move(queue); // same song; the rest of the queue changed
        return;
    }
    const auto nextEntry = queue.current();
    const auto id = m_backend.preloadSong(
        song, m_quality,
        m_permitBiliSource && (fromFm || (nextEntry && nextEntry->program.value.isEmpty())));
    if (!id) {
        m_preload.reset();
        return;
    }
    m_preload = Preload{id, entry, song, std::move(queue), fromFm};
    // Its lyrics too: where it's sung, for the transition into it.
    cancelPreloadLyrics();
    if (const auto selection = m_backend.playbackSource(id))
        sourceSelected(*selection);
}
void ListeningSession::adoptAdvance()
{
    auto preload = *std::exchange(m_preload, std::nullopt);
    const bool fm = m_mode == ListeningMode::PersonalFm;
    if (preload.fromFm)
        preload.queue = fmQueueWith(preload.song); // after the songs so far
    const auto revision = ++m_revision;
    m_history.finish(ListeningEnd::Ended); // the last song played to its end
    m_autoVisited.clear();                 // as advance() after a natural end
    commit(std::move(preload.queue), m_intent, m_quality, true, fm, m_backend.snapshot(), revision);
    // Playing now, so taken from the supply: only after the commit, as the supply's
    // observers (this session too) see the new load. Unless its first changed meanwhile:
    // that one hasn't played.
    if (preload.fromFm && revision == m_revision && m_mode == ListeningMode::PersonalFm && m_fm) {
        const auto supply = m_fm->snapshot();
        if (!supply.upcoming.isEmpty() && supply.upcoming.first().id == preload.song)
            static_cast<void>(m_fm->takeNext());
        else
            log::warn(lcPlayback, "fm.preload_mismatch");
    }
}
void ListeningSession::advance(bool naturalEnd)
{
    log::debug(lcPlayback, "queue.advance",
               {{"natural", naturalEnd}, {"skipped", m_autoVisited.size()}});
    if (naturalEnd)
        m_autoVisited.clear();
    const auto count = m_queue.snapshot().entries.size();
    if (m_autoVisited.size() >= count) {
        emit advanceStopped(true);
        return;
    }
    auto candidate = m_queue;
    for (qsizetype i = 0; i < count; ++i) {
        if (!candidate.next(naturalEnd, !naturalEnd)) {
            emit advanceStopped(!naturalEnd);
            return;
        }
        naturalEnd = false;
        if (m_autoVisited.contains(candidate.snapshot().active))
            continue;
        if (!begin(std::move(candidate),
                   m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                   true))
            emit advanceStopped(true);
        return;
    }
    emit advanceStopped(true);
}
} // namespace cirrus::application

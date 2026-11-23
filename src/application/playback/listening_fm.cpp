#include "application/music/personal_fm_source.h"
#include "listening_session.h"
#include "logging/log.h"
#include <QThread>
#include <QTimer>
#include <algorithm>

namespace cirrus::application
{
using namespace domain;
namespace
{
// Personal FM keeps this many of its songs so far, to go back to (and to restore).
constexpr qsizetype kFmSongsKept = 100;
} // namespace
ListeningSession::ListeningSession(BackendFacade& backend, PersonalFmSource& fm, QObject* parent)
    : ListeningSession(backend, parent)
{
    Q_ASSERT(fm.thread() == thread());
    if (!fm.isBoundTo(backend))
        return; // never consume another backend/account's supply
    m_fm = &fm;
    connect(&fm, &PersonalFmSource::changed, this, [this] {
        if (m_mode == ListeningMode::PersonalFm) {
            if (!current())
                reset();
            else {
                scheduleFm();
                refreshPreload(); // its first may have come or changed
            }
        }
    });
}
void ListeningSession::clearFmState()
{
    m_mode = ListeningMode::Queue;
    m_fmWaiting = m_fmBlocked = false;
    m_fmPending.reset();
    m_fmScheduledRevision = 0;
    m_restoredFm = false;
}
QUuid ListeningSession::fmNeighbour(int step) const
{
    const auto snapshot = m_queue.snapshot();
    const auto it =
        std::find_if(snapshot.entries.cbegin(), snapshot.entries.cend(),
                     [&](const QueueEntry& entry) { return entry.id == snapshot.active; });
    if (it == snapshot.entries.cend())
        return {};
    const qsizetype index = (it - snapshot.entries.cbegin()) + step;
    return index >= 0 && index < snapshot.entries.size() ? snapshot.entries[index].id : QUuid{};
}
bool ListeningSession::playFm(QUuid entry, bool automatic)
{
    if (m_mode != ListeningMode::PersonalFm || !current())
        return false;
    auto candidate = m_queue;
    if (!candidate.select(entry))
        return false;
    log::info(lcPlayback, "fm.replay", {"automatic", automatic});
    return begin(std::move(candidate),
                 m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                 automatic, true);
}
bool ListeningSession::resumeFm(QUuid entry, AudioQuality quality)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!restoredFm() || !m_fm || m_backend.snapshot().phase != SessionPhase::Authenticated ||
        qualityName(quality).isEmpty())
        return false;
    log::info(lcPlayback, "fm.resume", {"from_new", entry.isNull()});
    auto songs = m_queue;
    if (entry.isNull()) {
        // A new song after them: Personal FM from the start, its songs so far kept.
        if (!startFm(quality, TransportIntent::Play))
            return false;
        if (m_mode == ListeningMode::PersonalFm && m_fmWaiting &&
            m_queue.snapshot().entries.isEmpty()) {
            m_queue = std::move(songs);
            emit fmChanged();
        }
        return true;
    }
    auto candidate = songs;
    if (!candidate.select(entry))
        return false;
    // Personal FM before the load, so that commit() keeps it.
    clearFmState();
    m_mode = ListeningMode::PersonalFm;
    if (!begin(std::move(candidate), TransportIntent::Play, quality, false, true)) {
        if (!current())
            m_mode = ListeningMode::Queue;
        return false;
    }
    // The supply for the songs after this one.
    if (m_mode == ListeningMode::PersonalFm && current()) {
        m_fm->stop();
        static_cast<void>(m_fm->start());
    }
    return true;
}
bool ListeningSession::startFm(AudioQuality quality, TransportIntent intent)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_fm || m_backend.state() != BackendState::Ready ||
        m_backend.snapshot().phase != SessionPhase::Authenticated ||
        !m_backend.snapshot().token.isValid() || qualityName(quality).isEmpty() ||
        (intent != TransportIntent::Play && intent != TransportIntent::Pause))
        return false;
    const auto revision = ++m_revision;
    m_starting = true;
    const auto id = m_backend.stopPlayback();
    m_starting = false;
    if (!acceptLoadCommand(id, revision))
        return false;
    m_history.finish(ListeningEnd::Replaced);
    clearFmState();
    m_mode = ListeningMode::PersonalFm;
    m_fmWaiting = true;
    m_queue.clear();
    m_token = m_backend.playback().token;
    m_playback = m_backend.playback();
    m_queueContext = m_token.session;
    m_quality = quality;
    m_intent = intent;
    m_playback.intent = intent;
    m_auto = false;
    m_autoVisited.clear();
    m_terminalHandled = true;
    cancelLyrics();
    if (revision != m_revision)
        return true;
    m_fm->stop();
    if (revision != m_revision)
        return true;
    if (!m_fm->start())
        blockFm();
    if (revision != m_revision)
        return true;
    emit historyChanged();
    if (revision != m_revision)
        return true;
    emit queueChanged(queue());
    if (revision != m_revision)
        return true;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return true;
    emit lyricsChanged(m_lyricsState);
    if (revision != m_revision)
        return true;
    refreshCursors();
    if (revision != m_revision)
        return true;
    emit fmChanged();
    if (revision == m_revision)
        scheduleFm();
    return true;
}
bool ListeningSession::enterFmWait(bool automatic, bool stopMedia)
{
    if (!m_fm || m_mode != ListeningMode::PersonalFm || !current())
        return false;
    const auto revision = ++m_revision;
    if (stopMedia) {
        m_starting = true;
        const auto id = m_backend.stopPlayback();
        m_starting = false;
        if (!acceptLoadCommand(id, revision))
            return false;
        m_history.finish(ListeningEnd::Replaced);
        m_token = m_backend.playback().token;
        m_playback = m_backend.playback();
        m_playback.intent = m_intent;
    }
    m_fmWaiting = true;
    m_fmBlocked = false;
    m_fmPending.reset();
    m_auto = automatic;
    m_terminalHandled = true;
    cancelLyrics();
    if (revision != m_revision)
        return true;
    emit historyChanged();
    if (revision != m_revision)
        return true;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return true;
    emit lyricsChanged(m_lyricsState);
    if (revision != m_revision)
        return true;
    refreshCursors();
    if (revision != m_revision)
        return true;
    emit fmChanged();
    if (revision == m_revision)
        scheduleFm();
    return true;
}
void ListeningSession::scheduleFm()
{
    if (m_mode != ListeningMode::PersonalFm || !current() || !m_fmWaiting || m_fmBlocked ||
        m_fmScheduledRevision == m_revision)
        return;
    const auto revision = m_revision;
    m_fmScheduledRevision = revision;
    QTimer::singleShot(0, this, [this, revision] {
        if (m_fmScheduledRevision == revision)
            m_fmScheduledRevision = 0;
        if (revision == m_revision)
            consumeFm();
    });
}
void ListeningSession::blockFm()
{
    if (m_fmBlocked)
        return;
    m_fmBlocked = true;
    const auto revision = m_revision;
    emit fmChanged();
    if (revision == m_revision)
        emit advanceStopped(true);
}
void ListeningSession::consumeFm()
{
    if (!m_fm || m_mode != ListeningMode::PersonalFm || !current() || !m_fmWaiting || m_fmBlocked)
        return;
    auto revision = m_revision;
    if (!m_fmPending) {
        auto song = m_fm->takeNext();
        if (revision != m_revision || !current())
            return;
        if (!song) {
            if (!m_fm->snapshot().fetching)
                blockFm();
            return;
        }
        m_fmPending = std::move(song);
    }
    auto candidate = fmQueueWith(m_fmPending->id);
    const auto intent = m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play;
    // Keep the taken item until local Play acceptance. Busy must not silently lose a song.
    if (!begin(std::move(candidate), intent, m_quality, m_auto, true) && m_revision == revision &&
        current() && m_mode == ListeningMode::PersonalFm)
        blockFm();
}
PlaybackQueue ListeningSession::fmQueueWith(const SongId& song) const
{
    // After the songs so far, which stay to go back to (at most kFmSongsKept).
    auto candidate = m_queue;
    if (!candidate.append({song})) {
        candidate.clear();
        candidate.append({song});
    }
    candidate.select(candidate.snapshot().entries.last().id);
    while (candidate.snapshot().entries.size() > kFmSongsKept)
        candidate.remove(candidate.snapshot().entries.first().id);
    return candidate;
}
void ListeningSession::refreshFmPreload()
{
    if (!m_fm || m_fmWaiting || m_fmBlocked)
        return;
    const auto state = m_playback.state;
    if (state != PlaybackState::Playing && state != PlaybackState::Paused &&
        state != PlaybackState::Ready)
        return;
    if (const QUuid after = fmNeighbour(1); !after.isNull()) {
        auto candidate = m_queue;
        if (candidate.select(after)) {
            const auto entry = candidate.current();
            requestPreload(entry->song, after, std::move(candidate), false);
            return;
        }
    }
    const auto supply = m_fm->snapshot();
    if (supply.upcoming.isEmpty()) {
        if (m_preload) {
            m_preload.reset();
            static_cast<void>(m_backend.preloadSong({}, m_quality));
        }
        return; // asked again when the supply refills
    }
    requestPreload(supply.upcoming.first().id, {}, PlaybackQueue{}, true); // queued once taken
}
bool ListeningSession::retryFm()
{
    if (!m_fm || m_mode != ListeningMode::PersonalFm || !current() || !m_fmWaiting)
        return false;
    const auto revision = ++m_revision;
    m_fmBlocked = false;
    m_auto = false; // explicit user retry starts a manual chain, never an endless failure loop
    const auto supply = m_fm->snapshot();
    if (!m_fmPending && supply.upcoming.isEmpty() && !supply.fetching) {
        if (!m_fm->retry())
            blockFm();
    }
    if (revision != m_revision)
        return true;
    emit fmChanged();
    if (revision == m_revision)
        scheduleFm();
    return true;
}
} // namespace cirrus::application

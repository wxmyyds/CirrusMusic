#include "player.h"
#include "notice.h"

#include "app_settings.h"
#include "song_detail_loader.h"
#include "song_list_model.h"
#include "sources.h"

#include <algorithm>

namespace cirrus::ui
{

Player::Player(PlaybackSource* source, AppSettings* settings, QObject* parent)
    : QObject(parent), m_source(source), m_settings(settings), m_queue(new SongListModel(this)),
      m_queueDetails(new SongDetailLoader(m_queue, this))
{
    connect(m_queueDetails, &SongDetailLoader::filled, this, [this] {
        updateCurrent();
        updateUpcoming();
    });
    if (!m_source)
        return;
    m_source->setBiliAccountHandler([this] { emit biliAccountChanged(); });
    m_source->setChangedHandler([this]() { update(); });
    m_source->setNoticeHandler([this](const QString& text) {
        emit notice(noticed("playback.failure_streak", text));
    });
    if (m_settings) {
        m_source->setVolume(std::clamp(m_settings->volume(), 0.0, 1.0));
        m_source->setBiliSourceEnabled(m_settings->biliFallback());
        connect(m_settings, &AppSettings::biliFallbackChanged, this,
                [this] { m_source->setBiliSourceEnabled(m_settings->biliFallback()); });
        m_source->setNormalization(m_settings->normalizeVolume());
        connect(m_settings, &AppSettings::normalizeVolumeChanged, this,
                [this] { m_source->setNormalization(m_settings->normalizeVolume()); });
        const auto transition = [this] {
            m_source->setTransition(m_settings->trackTransition(), m_settings->crossfadeSeconds());
        };
        transition();
        connect(m_settings, &AppSettings::trackTransitionChanged, this, transition);
        connect(m_settings, &AppSettings::crossfadeSecondsChanged, this, transition);
    }
    update();
}

int Player::biliStatus() const { return m_source ? m_source->biliAccount().status : 0; }

QString Player::biliAccountName() const
{
    return m_source ? m_source->biliAccount().name : QString();
}

void Player::saveBiliCookie(const QString& cookie)
{
    if (m_source)
        m_source->saveBiliCookie(cookie);
}

void Player::clearBiliCookie()
{
    if (m_source)
        m_source->clearBiliCookie();
}

void Player::applyModes()
{
    if (!m_settings || m_snapshot.mode != playback::Queue)
        return;
    const int order = m_settings->playOrder();
    const int repeat = m_settings->playRepeat();
    if (m_snapshot.order != order && (order == playback::Sequential || order == playback::Shuffle))
        m_source->setOrder(playback::Order(order));
    if (m_snapshot.repeat != repeat && repeat >= playback::RepeatOff &&
        repeat <= playback::RepeatOne)
        m_source->setRepeat(playback::Repeat(repeat));
}

void Player::update()
{
    const PlaybackSnapshot& next = m_source->snapshot();
    const bool queueChanged = next.queue != m_snapshot.queue;
    const bool moved = queueChanged || next.queueIndex != m_snapshot.queueIndex;
    m_snapshot = next;
    const SongInfo& current = m_snapshot.current;
    const bool detailed = !current.key.isEmpty() && !current.title.isEmpty();
    if (queueChanged) {
        // Details come as views ask; those known already stay (a reorder keeps them).
        // Voices come with the snapshot: they are never asked for as songs.
        QList<SongInfo> known = m_queue->loadedSongs();
        if (detailed)
            known.append(current);
        known += m_snapshot.voices;
        m_queueDetails->clear();
        m_queue->reset(m_snapshot.queue, known);
    } else if (detailed && !m_queue->songAt(m_snapshot.queueIndex)) {
        m_queue->fill({current.key}, {current});
    }
    // The songs either side, ahead of a switch: their titles and covers show at once.
    if (moved && m_snapshot.queueIndex >= 0)
        ensureQueueLoaded(m_snapshot.queueIndex - 1, m_snapshot.queueIndex + 1);
    updateCurrent();
    emit changed();
    updateUpcoming();
    noticePreview();
    applyModes();
}

void Player::updateCurrent()
{
    SongInfo info = m_snapshot.current;
    if (!info.key.isEmpty() && info.title.isEmpty()) {
        // The source is still getting its details; the queue may have them.
        if (const auto row = m_queue->songAt(m_snapshot.queueIndex); row && row->key == info.key)
            info = *row;
    }
    SongItem current(info);
    if (current != m_current) {
        m_current = std::move(current);
        emit currentChanged();
    }
}

void Player::updateUpcoming()
{
    const int count = int(m_snapshot.queue.size());
    int row = m_snapshot.queueIndex + 1;
    if (row >= count && m_snapshot.repeat == playback::RepeatAll)
        row = 0;
    QString key;
    if (m_snapshot.queueIndex >= 0 && row != m_snapshot.queueIndex)
        if (const auto song = m_queue->songAt(row))
            key = song->coverKey;
    if (key != m_upcomingCoverKey) {
        m_upcomingCoverKey = key;
        emit upcomingChanged();
    }
}

void Player::noticePreview()
{
    // Whether it is a preview is known once it plays. Said once for a run of previews,
    // not for every song of a queue a guest plays.
    const QString& key = m_snapshot.current.key;
    if (key.isEmpty() || key == m_previewKnownKey ||
        (m_snapshot.status != playback::Playing && m_snapshot.status != playback::Paused))
        return;
    const bool preview = m_snapshot.previewStartMs >= 0;
    const bool before = m_previewKnown;
    m_previewKnownKey = key;
    m_previewKnown = preview;
    if (preview && !before)
        emit notice(noticed("player.preview_only", tr("这首歌只能试听片段")));
}

QString Player::failureMessage() const
{
    if (m_snapshot.status != playback::Failed)
        return {};
    if (m_snapshot.current.unavailable)
        return tr("这首歌暂时无法播放");
    switch (m_snapshot.failure ? m_snapshot.failure->kind : failure::Unknown) {
    case failure::Network:
    case failure::Timeout:
        return tr("网络连接失败，无法播放");
    case failure::SignedOut:
        return tr("登录后才能播放");
    case failure::Rejected:
        return tr("这首歌暂时无法播放");
    default:
        return tr("播放失败");
    }
}

void Player::playSongs(const QStringList& keys, int index, int origin, const QString& originId,
                       const QString& originName)
{
    if (m_source && index >= 0 && index < keys.size())
        m_source->playSongs(keys, index, PlayOrigin{origin, originId, originName});
}

void Player::addSongs(const QStringList& keys, bool next, int origin, const QString& originId,
                      const QString& originName)
{
    if (!m_source || keys.isEmpty())
        return;
    if (m_snapshot.mode == playback::PersonalFm) {
        emit notice(noticed("player.fm_no_queue", tr("私人 FM 不能加入队列")));
        return;
    }
    const bool playing = !m_snapshot.queue.isEmpty();
    if (m_source->addSongs(keys, next, PlayOrigin{origin, originId, originName}) && playing)
        emit notice(
            next ? (keys.size() > 1 ? noticed("player.play_next",
                                              tr("%1 首歌将在这首之后播放").arg(keys.size()))
                                    : noticed("player.play_next", tr("将在这首之后播放")))
                 : (keys.size() > 1
                        ? noticed("player.queued", tr("已把 %1 首歌加到播放队列").arg(keys.size()))
                        : noticed("player.queued", tr("已加到播放队列"))));
}

void Player::clearQueue()
{
    if (m_source && m_snapshot.mode == playback::Queue)
        m_source->clearQueue();
}

void Player::playVoices(const QStringList& keys, int index)
{
    if (canPlayVoices() && index >= 0 && index < keys.size())
        m_source->playVoices(keys, index);
}

bool Player::canPlayVoices() const { return m_source && m_source->canPlayVoices(); }

void Player::playPause()
{
    if (m_source)
        m_source->playPause();
}

void Player::next()
{
    if (m_source)
        m_source->next();
}

void Player::previous()
{
    if (m_source)
        m_source->previous();
}

void Player::seek(qint64 positionMs)
{
    if (m_source && m_snapshot.seekable)
        m_source->seek(std::max<qint64>(positionMs, 0));
}

void Player::playAt(int index)
{
    if (m_source && index >= 0 && index < m_snapshot.queue.size())
        m_source->playAt(index);
}

void Player::removeAt(int index)
{
    if (!m_source || m_snapshot.mode != playback::Queue || index < 0 ||
        index >= m_snapshot.queue.size())
        return;
    // The rows change now; the snapshot that follows then matches them (no reset).
    m_queue->removeAt(index);
    m_snapshot.queue.removeAt(index);
    if (index < m_snapshot.queueIndex)
        --m_snapshot.queueIndex;
    m_source->removeAt(index);
    emit changed();
}

void Player::moveQueue(int from, int to)
{
    const int count = int(m_snapshot.queue.size());
    if (!m_source || m_snapshot.mode != playback::Queue || from < 0 || from >= count || to < 0 ||
        to >= count || from == to)
        return;
    m_queue->moveRow(from, to);
    m_snapshot.queue.move(from, to);
    // The current song moves with its row.
    int& current = m_snapshot.queueIndex;
    if (from == current)
        current = to;
    else if (from < current && to >= current)
        --current;
    else if (from > current && to <= current)
        ++current;
    m_source->move(from, to);
    emit changed();
}

void Player::ensureQueueLoaded(int first, int last)
{
    const int count = m_queue->rowCount();
    if (count == 0)
        return;
    m_queueDetails->ensureLoaded(std::clamp(first, 0, count - 1), std::clamp(last, 0, count - 1));
}

void Player::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (m_settings)
        m_settings->setVolume(volume);
    if (m_source)
        m_source->setVolume(volume);
}

void Player::setOrder(int order)
{
    if (order != playback::Sequential && order != playback::Shuffle)
        return;
    if (m_settings)
        m_settings->setPlayOrder(order);
    if (m_source)
        m_source->setOrder(playback::Order(order));
}

void Player::setRepeat(int repeat)
{
    if (repeat < playback::RepeatOff || repeat > playback::RepeatOne)
        return;
    if (m_settings)
        m_settings->setPlayRepeat(repeat);
    if (m_source)
        m_source->setRepeat(playback::Repeat(repeat));
}

void Player::startFm()
{
    if (m_source)
        m_source->startFm();
}

} // namespace cirrus::ui

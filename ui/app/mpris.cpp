#include "mpris.h"
#include "logging/log.h"

#include "desktop.h"
#include "media_state.h"
#include "player.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QGuiApplication>

namespace cirrus::ui
{
namespace
{
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.cirrus");
const QString kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
// A position that moved more than this against the clock was a seek.
constexpr qint64 kSeekedMs = 1000;

QString loopOf(int repeat)
{
    switch (repeat) {
    case playback::RepeatAll:
        return QStringLiteral("Playlist");
    case playback::RepeatOne:
        return QStringLiteral("Track");
    default:
        return QStringLiteral("None");
    }
}
} // namespace

Mpris::Mpris(Player* player, Desktop* desktop, QObject* parent)
    : QObject(parent), m_player(player), m_desktop(desktop)
{
    new MprisRoot(this);
    new MprisPlayer(this);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    if (!bus.registerObject(kPath, this, QDBusConnection::ExportAdaptors) ||
        !bus.registerService(kService)) {
        log::warn(lcPlatform, "mpris.register_failed", {"err", bus.lastError().name()});
        bus.unregisterObject(kPath);
        return;
    }
    m_registered = true;
    connect(m_player, &Player::changed, this, &Mpris::playerChanged);
    connect(m_player, &Player::currentChanged, this, &Mpris::playerChanged);
    m_sent = playerProperties();
    m_sent.remove(QStringLiteral("Position"));
}

Mpris::~Mpris()
{
    if (!m_registered)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterService(kService);
    bus.unregisterObject(kPath);
}

QString Mpris::trackId() const
{
    const QString key = m_player->current().key;
    if (key.isEmpty())
        return QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");
    QString part;
    for (const QChar c : key)
        part += c.isLetterOrNumber() && c.unicode() < 128 ? c : QLatin1Char('_');
    return QStringLiteral("/com/guang233/Cirrus/track/s") + part;
}

qint64 Mpris::positionUs() const { return media::positionNow(*m_player) * 1000; }

QVariantMap Mpris::playerProperties() const
{
    const MprisPlayer* adaptor = findChild<MprisPlayer*>();
    QVariantMap values{
        {QStringLiteral("PlaybackStatus"), adaptor->playbackStatus()},
        {QStringLiteral("LoopStatus"), adaptor->loopStatus()},
        {QStringLiteral("Rate"), 1.0},
        {QStringLiteral("Shuffle"), adaptor->shuffle()},
        {QStringLiteral("Metadata"), adaptor->metadata()},
        {QStringLiteral("Volume"), adaptor->volume()},
        {QStringLiteral("Position"), qlonglong(positionUs())},
        {QStringLiteral("MinimumRate"), 1.0},
        {QStringLiteral("MaximumRate"), 1.0},
        {QStringLiteral("CanGoNext"), adaptor->canGoNext()},
        {QStringLiteral("CanGoPrevious"), adaptor->canGoPrevious()},
        {QStringLiteral("CanPlay"), adaptor->canPlay()},
        {QStringLiteral("CanPause"), adaptor->canPause()},
        {QStringLiteral("CanSeek"), adaptor->canSeek()},
        {QStringLiteral("CanControl"), true},
    };
    return values;
}

void Mpris::playerChanged()
{
    // Announce what changed (Position never is: clients interpolate it).
    QVariantMap now = playerProperties();
    now.remove(QStringLiteral("Position"));
    QVariantMap changed;
    for (auto it = now.cbegin(); it != now.cend(); ++it)
        if (m_sent.value(it.key()) != it.value())
            changed.insert(it.key(), it.value());
    m_sent = now;
    if (!changed.isEmpty()) {
        QDBusMessage signal =
            QDBusMessage::createSignal(kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                       QStringLiteral("PropertiesChanged"));
        signal << kPlayerInterface << changed << QStringList();
        QDBusConnection::sessionBus().send(signal);
    }

    // A jump on the same song that the clock doesn't explain was a seek.
    const QString key = m_player->current().key;
    const qint64 expected =
        m_lastPositionMs + qint64(double(m_player->positionAt() - m_lastPositionAt) * m_lastRate);
    if (!key.isEmpty() && key == m_lastKey &&
        std::abs(m_player->positionMs() - expected) > kSeekedMs)
        emit findChild<MprisPlayer*>() -> Seeked(positionUs());
    m_lastKey = key;
    m_lastPositionMs = m_player->positionMs();
    m_lastPositionAt = m_player->positionAt();
    m_lastRate = m_player->rate();
}

// --- org.mpris.MediaPlayer2 -------------------------------------------------------

MprisRoot::MprisRoot(Mpris* owner) : QDBusAbstractAdaptor(owner), m_owner(owner) {}

QString MprisRoot::identity() const { return QGuiApplication::applicationDisplayName(); }

QString MprisRoot::desktopEntry() const { return QGuiApplication::desktopFileName(); }

void MprisRoot::Raise()
{
    log::info(lcPlatform, "mpris.command", {"cmd", "raise"});
    emit m_owner->desktop()->showRequested();
}

void MprisRoot::Quit()
{
    log::info(lcPlatform, "mpris.command", {"cmd", "quit"});
    m_owner->desktop()->quit();
}

// --- org.mpris.MediaPlayer2.Player ------------------------------------------------

MprisPlayer::MprisPlayer(Mpris* owner) : QDBusAbstractAdaptor(owner), m_owner(owner) {}

QString MprisPlayer::playbackStatus() const
{
    switch (m_owner->player()->status()) {
    case playback::Playing:
    case playback::Loading:
        return QStringLiteral("Playing");
    case playback::Paused:
    case playback::Failed:
        return QStringLiteral("Paused");
    default:
        return QStringLiteral("Stopped");
    }
}

QString MprisPlayer::loopStatus() const { return loopOf(m_owner->player()->repeat()); }

void MprisPlayer::setLoopStatus(const QString& value)
{
    if (value == u"Playlist")
        m_owner->player()->setRepeat(playback::RepeatAll);
    else if (value == u"Track")
        m_owner->player()->setRepeat(playback::RepeatOne);
    else if (value == u"None")
        m_owner->player()->setRepeat(playback::RepeatOff);
}

void MprisPlayer::setRate(double value)
{
    // Only 1 is supported; 0 means pause (the specification).
    if (value == 0.0)
        Pause();
}

bool MprisPlayer::shuffle() const { return m_owner->player()->order() == playback::Shuffle; }

void MprisPlayer::setShuffle(bool value)
{
    m_owner->player()->setOrder(value ? playback::Shuffle : playback::Sequential);
}

QVariantMap MprisPlayer::metadata() const
{
    const SongItem song = m_owner->player()->current();
    QVariantMap values{{QStringLiteral("mpris:trackid"),
                        QVariant::fromValue(QDBusObjectPath(m_owner->trackId()))}};
    if (song.key.isEmpty())
        return values;
    if (const qint64 length = m_owner->player()->durationMs(); length > 0)
        values.insert(QStringLiteral("mpris:length"), qlonglong(length * 1000));
    if (!song.title.isEmpty())
        values.insert(QStringLiteral("xesam:title"), song.title);
    if (!song.artistText.isEmpty())
        values.insert(QStringLiteral("xesam:artist"), QStringList{song.artistText});
    if (!song.albumTitle.isEmpty())
        values.insert(QStringLiteral("xesam:album"), song.albumTitle);
    if (const QString art = media::coverUrl(song.coverKey, 512); !art.isEmpty())
        values.insert(QStringLiteral("mpris:artUrl"), art);
    return values;
}

double MprisPlayer::volume() const { return m_owner->player()->volume(); }

void MprisPlayer::setVolume(double value) { m_owner->player()->setVolume(value); }

qlonglong MprisPlayer::position() const { return m_owner->positionUs(); }

bool MprisPlayer::canGoNext() const { return m_owner->player()->canNext(); }

bool MprisPlayer::canGoPrevious() const { return m_owner->player()->canPrevious(); }

bool MprisPlayer::canPlay() const { return !m_owner->player()->current().key.isEmpty(); }

bool MprisPlayer::canPause() const { return canPlay(); }

bool MprisPlayer::canSeek() const
{
    return m_owner->player()->seekable() && m_owner->player()->durationMs() > 0;
}

void MprisPlayer::Next()
{
    log::info(lcPlatform, "mpris.command", {"cmd", "next"});
    m_owner->player()->next();
}

void MprisPlayer::Previous()
{
    log::info(lcPlatform, "mpris.command", {"cmd", "previous"});
    m_owner->player()->previous();
}

void MprisPlayer::Pause()
{
    log::info(lcPlatform, "mpris.command", {"cmd", "pause"});
    media::pause(*m_owner->player());
}

void MprisPlayer::PlayPause()
{
    log::info(lcPlatform, "mpris.command", {"cmd", "play_pause"});
    m_owner->player()->playPause();
}

void MprisPlayer::Stop() { Pause(); }

void MprisPlayer::Play()
{
    log::info(lcPlatform, "mpris.command", {"cmd", "play"});
    media::play(*m_owner->player());
}

void MprisPlayer::Seek(qlonglong offset)
{
    if (!canSeek())
        return;
    const qint64 target = m_owner->positionUs() / 1000 + offset / 1000;
    if (target >= m_owner->player()->durationMs()) {
        Next(); // past the end: the next song (the specification)
        return;
    }
    m_owner->player()->seek(std::max<qint64>(target, 0));
}

void MprisPlayer::SetPosition(const QDBusObjectPath& trackId, qlonglong position)
{
    // Only for the song playing now; a stale id is ignored.
    if (!canSeek() || trackId.path() != m_owner->trackId() || position < 0 ||
        position / 1000 > m_owner->player()->durationMs())
        return;
    m_owner->player()->seek(position / 1000);
}

} // namespace cirrus::ui

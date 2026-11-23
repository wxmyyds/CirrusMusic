#pragma once

#include "request.h"
#include "types.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <optional>

// Playback state as the playback source reports it (docs/ui/presentation.md §5.1).
namespace cirrus::ui::playback
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Playback)

enum Status { Empty, Loading, Playing, Paused, Ended, Failed };
Q_ENUM_NS(Status)
enum Order { Sequential, Shuffle };
Q_ENUM_NS(Order)
enum Repeat { RepeatOff, RepeatAll, RepeatOne };
Q_ENUM_NS(Repeat)
enum Mode { Queue, PersonalFm };
Q_ENUM_NS(Mode)
enum Provider { Netease, Bilibili };
Q_ENUM_NS(Provider)
// Where songs were played from, for the listening reports (integration.md §6.1) and the
// local statistics (pages/stats.md): the page's playlist, album, artist, chart, the daily
// recommendations, a search (the id is the keywords), a song's own page, the recently
// played songs or a listening ranking (the id is its user's key). NoOrigin: none of these.
// Stored with the queue: append only.
enum Origin {
    NoOrigin,
    FromPlaylist,
    FromAlbum,
    FromArtist,
    FromChart,
    FromDaily,
    FromSearch,
    FromSong,
    FromRecent,
    FromRanking
};
Q_ENUM_NS(Origin)

// Milliseconds on the monotonic clock that `positionAt` refers to.
[[nodiscard]] inline qint64 now()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace cirrus::ui::playback

namespace cirrus::ui
{

// playback::Origin, its id (a playlist's key, the search's keywords…) and its name (the
// playlist's title…, shown in the statistics; may be empty).
struct BiliAccountState {
    int status = 0;
    QString name;
};

struct PlayOrigin {
    int kind = playback::NoOrigin;
    QString id;
    QString name;
    friend bool operator==(const PlayOrigin&, const PlayOrigin&) = default;
};

// The whole playback state; sources replace it as a unit. The position is a base
// value: at `positionAt` (playback::now()) it was `positionMs`, and it advances by
// `rate` ms per ms (1 while playing, 0 otherwise).
struct PlaybackSnapshot {
    playback::Status status = playback::Empty;
    playback::Provider provider = playback::Netease;
    quint64 generation = 0;
    SongInfo current; // empty key when Empty
    // Why the current song stopped (Failed); `unavailable` songs fail as Rejected.
    std::optional<Failure> failure;
    qint64 positionMs = 0;
    qint64 positionAt = 0;
    double rate = 0;
    qint64 durationMs = -1; // -1 when unknown
    // A preview clip of the song (all the service plays without a membership): where
    // it starts in the song, -1 for the whole song. Positions and the duration are the
    // clip's; lyrics are on the song's clock.
    qint64 previewStartMs = -1;
    bool seekable = false;
    double volume = 1;
    playback::Order order = playback::Sequential;
    playback::Repeat repeat = playback::RepeatOff;
    bool canNext = false;
    bool canPrevious = false;
    playback::Mode mode = playback::Queue;
    // Personal FM is fetching its next song.
    bool fmWaiting = false;
    // The queue in play order (the shuffled order while shuffling; Personal FM: the
    // songs so far and the next one), and the current song's place in it (-1: Empty).
    QStringList queue;
    int queueIndex = -1;
};

} // namespace cirrus::ui

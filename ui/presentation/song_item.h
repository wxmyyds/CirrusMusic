#pragma once

#include "formatter.h"
#include "types.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// One song for QML outside list models, e.g. `App.player.current`. Same names as the
// SongListModel roles (docs/ui/presentation.md §5.3).
class SongItem
{
    Q_GADGET
    QML_VALUE_TYPE(songItem)
    Q_PROPERTY(QString key MEMBER key CONSTANT) // empty for no song
    Q_PROPERTY(QString title MEMBER title CONSTANT)
    Q_PROPERTY(QString secondaryTitle MEMBER secondaryTitle CONSTANT)
    Q_PROPERTY(QString artistText MEMBER artistText CONSTANT)
    // Keys of the artists with one, in order (the player's "go to artist").
    Q_PROPERTY(QStringList artistKeys MEMBER artistKeys CONSTANT)
    // Every artist, in order: {key (may be empty), name}.
    Q_PROPERTY(QVariantList artists MEMBER artists CONSTANT)
    Q_PROPERTY(QString albumKey MEMBER albumKey CONSTANT)
    Q_PROPERTY(QString albumTitle MEMBER albumTitle CONSTANT)
    Q_PROPERTY(QString coverKey MEMBER coverKey CONSTANT)
    Q_PROPERTY(qint64 durationMs MEMBER durationMs CONSTANT) // -1 when unknown
    Q_PROPERTY(bool unavailable MEMBER unavailable CONSTANT)
    Q_PROPERTY(QString reason MEMBER reason CONSTANT) // recommendation reason, may be empty
    Q_PROPERTY(bool voice MEMBER voice CONSTANT)      // a podcast voice, not a song

  public:
    SongItem() = default;
    explicit SongItem(const SongInfo& song)
        : key(song.key), title(song.title), secondaryTitle(song.secondaryTitle),
          artistText(song.voice ? song.albumTitle : Formatter::artists(song.artists)),
          artistKeys(keysOf(song.artists)),
          artists(listOf(song.artists)), albumKey(song.albumKey), albumTitle(song.albumTitle),
          coverKey(song.coverKey), durationMs(song.durationMs), unavailable(song.unavailable),
          reason(song.reason), voice(song.voice)
    {
    }

    QString key;
    QString title;
    QString secondaryTitle;
    QString artistText;
    QStringList artistKeys;
    QVariantList artists;
    QString albumKey;
    QString albumTitle;
    QString coverKey;
    qint64 durationMs = -1;
    bool unavailable = false;
    QString reason;
    bool voice = false;
    friend bool operator==(const SongItem&, const SongItem&) = default;

  private:
    static QVariantList listOf(const QList<ArtistRef>& artists)
    {
        QVariantList list;
        for (const ArtistRef& artist : artists) {
            QVariantMap item;
            item.insert(QStringLiteral("key"), artist.key);
            item.insert(QStringLiteral("name"), artist.name);
            list.append(item);
        }
        return list;
    }
    static QStringList keysOf(const QList<ArtistRef>& artists)
    {
        QStringList keys;
        for (const ArtistRef& artist : artists) {
            if (!artist.key.isEmpty())
                keys << artist.key;
        }
        return keys;
    }
};

} // namespace cirrus::ui

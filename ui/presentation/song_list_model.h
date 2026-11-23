#pragma once

#include "types.h"

#include <QAbstractListModel>

#include <QtQml/qqmlregistration.h>
#include <functional>

namespace cirrus::ui
{

// Songs of any list: playlist, album, search results, daily songs, queue
// (docs/ui/presentation.md §5.3). Rows exist for every key; `loaded` is false while a
// row only has its key and waits for details.
class SongListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a page model")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // App.player's current song is one of the rows.
    Q_PROPERTY(bool containsCurrent READ containsCurrent NOTIFY containsCurrentChanged)

  public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        SecondaryTitleRole,
        ArtistsRole, // [{key, name}]
        ArtistTextRole,
        AlbumKeyRole,
        AlbumTitleRole,
        CoverKeyRole,
        DurationMsRole,   // -1 when unknown
        DurationTextRole, // "--:--" when unknown
        LoadedRole,
        UnavailableRole,
        CurrentRole,  // the song App.player is on
        ReasonRole,   // recommendation reason, may be empty
        LikedRole,    // follows App.library
        MissingRole,  // the service has no details for it
        PositionRole, // 1-based place in the list (an album's track number)
        SerialRole,   // a podcast voice's episode number, -1 when unknown or a song
        VoiceRole,    // a podcast voice (its album is the podcast), not a song
    };
    Q_ENUM(Role)

    explicit SongListModel(QObject* parent = nullptr);

    // One row per key; details fill the rows whose keys they match.
    void reset(const QStringList& keys, const QList<SongInfo>& details);
    void clear();
    // More loaded songs at the end (paged lists).
    void append(const QList<SongInfo>& songs);
    void remove(const QStringList& keys);
    // One row out, or moved to `to` (where it ends up), by place (keys may repeat).
    void removeAt(int row);
    void moveRow(int from, int to);
    // Moves `rows` (in their current order) into the gap before row `gap` (rowCount()
    // for the end).
    void moveRows(QList<int> rows, int gap);
    // Stable sort by `lessThan` over rows with details; the others keep their order
    // after them.
    void sortRows(const std::function<bool(const SongInfo&, const SongInfo&)>& lessThan);
    void reverse();
    // Details for rows that only had a key; `requested` keys missing from `details`
    // are marked missing.
    void fill(const QStringList& requested, const QList<SongInfo>& details);
    // Keys of the rows in [first, last] still waiting for details.
    [[nodiscard]] QStringList keysToLoad(int first, int last) const;
    [[nodiscard]] int missingCount() const;
    [[nodiscard]] bool allLoaded() const;
    // The rows with details, e.g. to put the list back after a failed change.
    [[nodiscard]] QList<SongInfo> loadedSongs() const;
    // The row's song when it has details.
    [[nodiscard]] std::optional<SongInfo> songAt(int row) const;

    // Every key, e.g. for "play all".
    Q_INVOKABLE QStringList keys() const;
    [[nodiscard]] bool containsCurrent() const { return m_containsCurrent; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void countChanged();
    void containsCurrentChanged();

  private:
    struct Row;
    // Replaces the rows' order, keeping views' selections and positions.
    void applyOrder(QList<Row> rows);
    void setCurrentKey(const QString& key);
    void updateContainsCurrent();

    struct Row {
        SongInfo song;
        bool loaded = false;
        bool missing = false;
    };
    QList<Row> m_rows;
    QString m_currentKey;
    bool m_containsCurrent = false;
};

} // namespace cirrus::ui

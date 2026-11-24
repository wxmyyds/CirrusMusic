#include "song_list_model.h"

#include "app.h"
#include "formatter.h"
#include "library.h"
#include "player.h"

#include <QHash>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

namespace cirrus::ui
{

SongListModel::SongListModel(QObject* parent) : QAbstractListModel(parent)
{
    if (const App* app = App::instance()) {
        Player* player = app->player();
        m_currentKey = player->current().key;
        connect(player, &Player::currentChanged, this,
                [this, player]() { setCurrentKey(player->current().key); });
        connect(app->library(), &Library::likedChanged, this, [this]() {
            if (!m_rows.isEmpty())
                emit dataChanged(index(0), index(rowCount() - 1), {LikedRole});
        });
    }
}

void SongListModel::setCurrentKey(const QString& key)
{
    if (key == m_currentKey)
        return;
    const QString previous = std::exchange(m_currentKey, key);
    for (int row = 0; row < m_rows.size(); ++row) {
        const QString& rowKey = m_rows.at(row).song.key;
        if (rowKey == previous || rowKey == key)
            emit dataChanged(index(row), index(row), {CurrentRole});
    }
    updateContainsCurrent();
}

void SongListModel::updateContainsCurrent()
{
    const bool contains = !m_currentKey.isEmpty() &&
                          std::any_of(m_rows.cbegin(), m_rows.cend(), [this](const Row& row) {
                              return row.song.key == m_currentKey;
                          });
    if (contains == m_containsCurrent)
        return;
    m_containsCurrent = contains;
    emit containsCurrentChanged();
}

void SongListModel::reset(const QStringList& keys, const QList<SongInfo>& details)
{
    QHash<QString, const SongInfo*> byKey;
    for (const SongInfo& song : details)
        byKey.insert(song.key, &song);

    const int oldCount = rowCount();
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(keys.size());
    for (const QString& key : keys) {
        if (const SongInfo* song = byKey.value(key))
            m_rows.append({*song, true});
        else
            m_rows.append({SongInfo{.key = key}, false});
    }
    endResetModel();
    if (rowCount() != oldCount)
        emit countChanged();
    updateContainsCurrent();
}

void SongListModel::clear() { reset({}, {}); }

void SongListModel::append(const QList<SongInfo>& songs)
{
    if (songs.isEmpty())
        return;
    const int first = rowCount();
    beginInsertRows({}, first, first + int(songs.size()) - 1);
    for (const SongInfo& song : songs)
        m_rows.append({song, true});
    endInsertRows();
    emit countChanged();
    updateContainsCurrent();
}

void SongListModel::remove(const QStringList& keys)
{
    bool removed = false;
    for (int row = rowCount() - 1; row >= 0; --row) {
        if (!keys.contains(m_rows.at(row).song.key))
            continue;
        beginRemoveRows({}, row, row);
        m_rows.removeAt(row);
        endRemoveRows();
        removed = true;
    }
    if (!removed)
        return;
    emit countChanged();
    updateContainsCurrent();
}

void SongListModel::removeAt(int row)
{
    if (row < 0 || row >= rowCount())
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit countChanged();
    updateContainsCurrent();
}

void SongListModel::moveRow(int from, int to)
{
    if (from < 0 || from >= rowCount() || to < 0 || to >= rowCount() || from == to)
        return;
    // beginMoveRows takes the gap before which the row goes, in the old numbering.
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_rows.move(from, to);
    endMoveRows();
}

void SongListModel::applyOrder(QList<Row> rows)
{
    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    const QModelIndexList before = persistentIndexList();
    QList<QString> keys;
    for (const QModelIndex& index : before)
        keys << m_rows.at(index.row()).song.key;
    m_rows = std::move(rows);
    QHash<QString, int> rowOf;
    for (int row = 0; row < m_rows.size(); ++row)
        rowOf.insert(m_rows.at(row).song.key, row);
    QModelIndexList after;
    for (const QString& key : std::as_const(keys))
        after << index(rowOf.value(key));
    changePersistentIndexList(before, after);
    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
}

void SongListModel::moveRows(QList<int> rows, int gap)
{
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    rows.removeIf([this](int row) { return row < 0 || row >= rowCount(); });
    if (rows.isEmpty())
        return;
    QList<Row> moved;
    QList<Row> rest;
    int insertAt = 0;
    for (int row = 0; row < rowCount(); ++row) {
        if (row == gap)
            insertAt = int(rest.size());
        if (std::binary_search(rows.cbegin(), rows.cend(), row))
            moved << m_rows.at(row);
        else
            rest << m_rows.at(row);
    }
    if (gap >= rowCount())
        insertAt = int(rest.size());
    for (int i = 0; i < moved.size(); ++i)
        rest.insert(insertAt + i, moved.at(i));
    applyOrder(std::move(rest));
}

void SongListModel::sortRows(const std::function<bool(const SongInfo&, const SongInfo&)>& lessThan)
{
    QList<Row> rows = m_rows;
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        if (a.loaded != b.loaded)
            return a.loaded;
        return a.loaded && lessThan(a.song, b.song);
    });
    applyOrder(std::move(rows));
}

void SongListModel::reverse()
{
    QList<Row> rows = m_rows;
    std::reverse(rows.begin(), rows.end());
    applyOrder(std::move(rows));
}

void SongListModel::fill(const QStringList& requested, const QList<SongInfo>& details)
{
    QHash<QString, const SongInfo*> byKey;
    for (const SongInfo& song : details)
        byKey.insert(song.key, &song);
    const QSet<QString> wanted(requested.cbegin(), requested.cend());
    for (int row = 0; row < rowCount(); ++row) {
        Row& entry = m_rows[row];
        if (entry.loaded || !wanted.contains(entry.song.key))
            continue;
        if (const SongInfo* song = byKey.value(entry.song.key)) {
            entry.song = *song;
            entry.loaded = true;
            entry.missing = false;
        } else {
            entry.missing = true;
        }
        emit dataChanged(index(row), index(row));
    }
}

QStringList SongListModel::keysToLoad(int first, int last) const
{
    QStringList keys;
    for (int row = std::max(0, first); row <= std::min(last, rowCount() - 1); ++row) {
        const Row& entry = m_rows.at(row);
        if (!entry.loaded && !entry.missing)
            keys << entry.song.key;
    }
    return keys;
}

int SongListModel::missingCount() const
{
    return int(
        std::count_if(m_rows.cbegin(), m_rows.cend(), [](const Row& row) { return row.missing; }));
}

bool SongListModel::allLoaded() const
{
    return std::all_of(m_rows.cbegin(), m_rows.cend(),
                       [](const Row& row) { return row.loaded || row.missing; });
}

QList<SongInfo> SongListModel::loadedSongs() const
{
    QList<SongInfo> songs;
    for (const Row& row : m_rows)
        if (row.loaded)
            songs << row.song;
    return songs;
}

std::optional<SongInfo> SongListModel::songAt(int row) const
{
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).loaded)
        return std::nullopt;
    return m_rows.at(row).song;
}

QStringList SongListModel::keys() const
{
    QStringList keys;
    keys.reserve(m_rows.size());
    for (const Row& row : m_rows)
        keys << row.song.key;
    return keys;
}

int SongListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant SongListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row& row = m_rows.at(index.row());
    const SongInfo& song = row.song;
    switch (role) {
    case KeyRole:
        return song.key;
    case TitleRole:
        return song.title;
    case SecondaryTitleRole:
        return song.secondaryTitle;
    case ArtistsRole: {
        QVariantList artists;
        for (const ArtistRef& artist : song.artists)
            artists << QVariantMap{{QStringLiteral("key"), artist.key},
                                   {QStringLiteral("name"), artist.name}};
        return artists;
    }
    case ArtistTextRole:
        return Formatter::artists(song.artists);
    case AlbumKeyRole:
        return song.albumKey;
    case AlbumTitleRole:
        return song.albumTitle;
    case CoverKeyRole:
        return song.coverKey;
    case DurationMsRole:
        return song.durationMs;
    case DurationTextRole:
        return Formatter::duration(song.durationMs);
    case LoadedRole:
        return row.loaded;
    case UnavailableRole:
        return song.unavailable;
    case CurrentRole:
        return !song.key.isEmpty() && song.key == m_currentKey;
    case ReasonRole:
        return song.reason;
    case MissingRole:
        return row.missing;
    case PositionRole:
        return index.row() + 1;
    case SerialRole:
        return song.serial;
    case VoiceRole:
        return song.voice;
    case LikedRole: {
        const App* app = App::instance();
        return app && app->library()->isLiked(song.key);
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> SongListModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {TitleRole, "title"},
        {SecondaryTitleRole, "secondaryTitle"},
        {ArtistsRole, "artists"},
        {ArtistTextRole, "artistText"},
        {AlbumKeyRole, "albumKey"},
        {AlbumTitleRole, "albumTitle"},
        {CoverKeyRole, "coverKey"},
        {DurationMsRole, "durationMs"},
        {DurationTextRole, "durationText"},
        {LoadedRole, "loaded"},
        {UnavailableRole, "unavailable"},
        {CurrentRole, "current"},
        {ReasonRole, "reason"},
        {LikedRole, "liked"},
        {MissingRole, "missing"},
        {PositionRole, "position"},
        {SerialRole, "serial"},
        {VoiceRole, "voice"},
    };
}

} // namespace cirrus::ui

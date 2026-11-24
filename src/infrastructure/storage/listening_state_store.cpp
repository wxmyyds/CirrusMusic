#include "listening_state_store.h"
#include "installation_store.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <climits>

namespace cirrus::storage
{
namespace
{
QString uuidText(QUuid id)
{
    return id.isNull() ? QStringLiteral("") : id.toString(QUuid::WithoutBraces);
}
std::optional<QUuid> storedUuid(const QString& text, bool nullable)
{
    if (nullable && text.isEmpty())
        return QUuid{};
    const QUuid id(text);
    return !id.isNull() && uuidText(id) == text ? std::optional<QUuid>(id) : std::nullopt;
}
std::optional<qint64> storedInteger(const QVariant& value)
{
    if (value.metaType().id() != QMetaType::LongLong && value.metaType().id() != QMetaType::Int)
        return std::nullopt;
    return value.toLongLong();
}
std::optional<domain::PlayOrigin> storedOrigin(const QVariant& kind, const QVariant& id,
                                               const QVariant& name)
{
    const auto value = storedInteger(kind);
    if (!value || *value < 0 || *value > static_cast<int>(domain::PlayOriginKind::Podcast))
        return std::nullopt;
    domain::PlayOrigin origin{static_cast<domain::PlayOriginKind>(*value), id.toString(),
                              name.toString()};
    return domain::validPlayOrigin(origin) ? std::optional(std::move(origin)) : std::nullopt;
}
StorageError sqlError(const QSqlError& error)
{
    bool ok = false;
    const auto code = error.nativeErrorCode().toInt(&ok);
    return ok && (code & 255) == 13 ? StorageError::InsufficientSpace : StorageError::Query;
}
StorageResult<ListeningStateResult> executeQueue(QSqlDatabase& db, const ListeningStateJob& job)
{
    const auto fail = [&](StorageError error) {
        return StorageResult<ListeningStateResult>{error, {job.id, error, {}}};
    };
    if (!job.id || !domain::validListeningAccount(job.accountId))
        return fail(StorageError::Query);
    if (job.operation == ListeningStateOperation::LoadQueue) {
        QSqlQuery header(db);
        header.prepare(
            QStringLiteral("SELECT active, queue_order, queue_repeat, mode FROM playback_queue "
                           "WHERE account_id = ?"));
        header.addBindValue(job.accountId);
        if (!header.exec())
            return fail(StorageError::Query);
        if (!header.next())
            return {StorageError::None, {job.id, StorageError::None, {}}};
        const auto active = storedUuid(header.value(0).toString(), true);
        const auto order = storedInteger(header.value(1));
        const auto repeat = storedInteger(header.value(2));
        const auto mode = storedInteger(header.value(3));
        if (!active || !order || *order < 0 || *order > 1 || !repeat || *repeat < 0 ||
            *repeat > 2 || !mode || *mode < 0 || *mode > 1)
            return fail(StorageError::Corrupt);
        domain::QueueSnapshot queue{{},
                                    *active,
                                    static_cast<domain::QueueOrder>(*order),
                                    static_cast<domain::QueueRepeat>(*repeat)};
        QSqlQuery entries(db);
        entries.prepare(QStringLiteral(
            "SELECT ordinal, entry_id, song_id, origin_kind, origin_id, origin_name, "
            "program_id FROM playback_queue_entry WHERE account_id = ? ORDER BY "
            "ordinal LIMIT 10001"));
        entries.addBindValue(job.accountId);
        if (!entries.exec())
            return fail(StorageError::Query);
        while (entries.next()) {
            const auto id = storedUuid(entries.value(1).toString(), false);
            const auto ordinal = storedInteger(entries.value(0));
            auto origin = storedOrigin(entries.value(3), entries.value(4), entries.value(5));
            if (!id || !ordinal || !origin || *ordinal != queue.entries.size() ||
                queue.entries.size() == domain::MaximumSavedQueueEntries)
                return fail(StorageError::Corrupt);
            queue.entries.append({*id,
                                  {entries.value(2).toString()},
                                  std::move(*origin),
                                  {entries.value(6).toString()}});
        }
        if (!domain::validSavedQueue(queue))
            return fail(StorageError::Corrupt);
        ListeningStateResult result{job.id, StorageError::None, std::move(queue)};
        result.mode = static_cast<domain::ListeningMode>(*mode);
        return {StorageError::None, std::move(result)};
    }
    if (job.operation != ListeningStateOperation::SaveQueue || !domain::validSavedQueue(job.queue))
        return fail(StorageError::Query);
    if (!db.transaction())
        return fail(StorageError::Query);
    const auto rollback = [&] {
        db.rollback();
        return fail(StorageError::Query);
    };
    QSqlQuery erase(db);
    erase.prepare(QStringLiteral("DELETE FROM playback_queue_entry WHERE account_id = ?"));
    erase.addBindValue(job.accountId);
    if (!erase.exec())
        return rollback();
    QSqlQuery header(db);
    header.prepare(
        QStringLiteral("INSERT INTO playback_queue(account_id, active, queue_order, "
                       "queue_repeat, mode) VALUES (?, ?, ?, ?, ?) ON CONFLICT(account_id) DO "
                       "UPDATE SET active=excluded.active, queue_order=excluded.queue_order, "
                       "queue_repeat=excluded.queue_repeat, mode=excluded.mode"));
    header.addBindValue(job.accountId);
    header.addBindValue(uuidText(job.queue.active));
    header.addBindValue(static_cast<int>(job.queue.order));
    header.addBindValue(static_cast<int>(job.queue.repeat));
    header.addBindValue(job.mode == domain::ListeningMode::PersonalFm ? 1 : 0);
    if (!header.exec())
        return rollback();
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO playback_queue_entry(account_id, ordinal, entry_id, "
                                  "song_id, origin_kind, origin_id, origin_name, program_id) "
                                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    for (qsizetype index = 0; index < job.queue.entries.size(); ++index) {
        const auto& entry = job.queue.entries[index];
        insert.bindValue(0, job.accountId);
        insert.bindValue(1, index);
        insert.bindValue(2, uuidText(entry.id));
        insert.bindValue(3, entry.song.value);
        insert.bindValue(4, static_cast<int>(entry.origin.kind));
        insert.bindValue(5, entry.origin.id.isEmpty() ? QStringLiteral("") : entry.origin.id);
        insert.bindValue(6, entry.origin.name.isEmpty() ? QStringLiteral("") : entry.origin.name);
        insert.bindValue(7,
                         entry.program.value.isEmpty() ? QStringLiteral("") : entry.program.value);
        if (!insert.exec())
            return rollback();
    }
    if (!db.commit())
        return rollback();
    return {StorageError::None, {job.id, StorageError::None, {}}};
}
StorageResult<ListeningStateResult> executeHistory(QSqlDatabase& db, const ListeningStateJob& job)
{
    const auto fail = [&](StorageError error) {
        return StorageResult<ListeningStateResult>{error, {job.id, error, {}}};
    };
    if (!job.id || !domain::validListeningAccount(job.accountId))
        return fail(StorageError::Query);
    if (job.operation == ListeningStateOperation::LoadHistory) {
        if (job.pageSize < 1 || job.pageSize > 100 ||
            (job.cursor && (job.cursor->id.isNull() || job.cursor->startedAtMs <= 0 ||
                            job.cursor->startedAtMs > 253402300799999LL)))
            return fail(StorageError::Query);
        QString statement = QStringLiteral(
            "SELECT record_id,song_id,source,entry_id,started_at_ms,ended_at_ms,played_ms,"
            "capped,end_reason,preview_start_ms,preview_end_ms,actual_quality,origin_kind,"
            "origin_id,origin_name FROM listening_history WHERE account_id=?");
        if (job.cursor)
            statement +=
                QStringLiteral(" AND (started_at_ms<? OR (started_at_ms=? AND record_id<?))");
        statement += QStringLiteral(" ORDER BY started_at_ms DESC,record_id DESC LIMIT ?");
        QSqlQuery query(db);
        query.prepare(statement);
        query.addBindValue(job.accountId);
        if (job.cursor) {
            query.addBindValue(job.cursor->startedAtMs);
            query.addBindValue(job.cursor->startedAtMs);
            query.addBindValue(uuidText(job.cursor->id));
        }
        query.addBindValue(job.pageSize + 1);
        if (!query.exec())
            return fail(sqlError(query.lastError()));
        domain::ListeningHistoryPage page;
        while (query.next()) {
            domain::StoredListeningRecord row;
            const auto id = storedUuid(query.value(0).toString(), false);
            const auto entry = storedUuid(query.value(3).toString(), true);
            const auto source = storedInteger(query.value(2));
            const auto start = storedInteger(query.value(4));
            const auto played = storedInteger(query.value(6));
            const auto capped = storedInteger(query.value(7));
            const auto end = storedInteger(query.value(8));
            if (!id || !entry || !source || *source < 0 || *source > 1 || !start || !played ||
                !capped || *capped < 0 || *capped > 1 || !end || *end < 0 || *end > 5)
                return fail(StorageError::Corrupt);
            row.id = *id;
            row.accountId = job.accountId;
            row.song = {query.value(1).toString()};
            row.source = static_cast<domain::ListeningMode>(*source);
            row.queueEntry = *entry;
            row.startedAtMs = *start;
            row.playedMs = *played;
            row.capped = *capped != 0;
            row.end = static_cast<domain::ListeningEnd>(*end);
            if (!query.value(5).isNull()) {
                row.endedAtMs = storedInteger(query.value(5));
                if (!row.endedAtMs)
                    return fail(StorageError::Corrupt);
            }
            if (query.value(9).isNull() != query.value(10).isNull())
                return fail(StorageError::Corrupt);
            if (!query.value(9).isNull()) {
                const auto a = storedInteger(query.value(9)), b = storedInteger(query.value(10));
                if (!a || !b)
                    return fail(StorageError::Corrupt);
                row.preview = domain::PreviewRange{*a, *b};
            }
            row.actualQuality = query.value(11).toString();
            const auto origin = storedOrigin(query.value(12), query.value(13), query.value(14));
            if (!origin)
                return fail(StorageError::Corrupt);
            row.origin = *origin;
            if (!domain::validStoredListeningRecord(row))
                return fail(StorageError::Corrupt);
            page.records.append(std::move(row));
        }
        if (page.records.size() > job.pageSize) {
            page.records.removeLast();
            const auto& last = page.records.last();
            page.next = domain::ListeningHistoryCursor{last.startedAtMs, last.id};
        }
        return {StorageError::None, {job.id, StorageError::None, {}, std::move(page)}};
    }
    if (job.operation != ListeningStateOperation::ClearHistory &&
        job.operation != ListeningStateOperation::SaveHistory)
        return fail(StorageError::Query);
    if (job.operation == ListeningStateOperation::SaveHistory &&
        (job.records.isEmpty() || job.records.size() > 64))
        return fail(StorageError::Query);
    for (const auto& row : job.records)
        if (row.accountId != job.accountId || !domain::validStoredListeningRecord(row))
            return fail(StorageError::Query);
    if (!db.transaction())
        return fail(StorageError::Query);
    const auto rollback = [&](StorageError error = StorageError::Query) {
        db.rollback();
        return fail(error);
    };
    QSqlQuery query(db);
    if (job.operation == ListeningStateOperation::ClearHistory) {
        query.prepare(QStringLiteral("DELETE FROM listening_history WHERE account_id=?"));
        query.addBindValue(job.accountId);
        if (!query.exec())
            return rollback(sqlError(query.lastError()));
        QSqlQuery songs(db); // their artists go with them (foreign key cascade)
        songs.prepare(QStringLiteral("DELETE FROM listening_song WHERE account_id=?"));
        songs.addBindValue(job.accountId);
        if (!songs.exec())
            return rollback(sqlError(songs.lastError()));
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO listening_history(account_id,record_id,song_id,source,entry_id,"
            "started_at_ms,ended_at_ms,played_ms,capped,end_reason,preview_start_ms,preview_end_ms,"
            "actual_quality,origin_kind,origin_id,origin_name) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
            "?) ON CONFLICT(account_id,record_id) "
            "DO UPDATE SET "
            "played_ms=MAX(played_ms,excluded.played_ms),capped=MAX(capped,excluded.capped),"
            "ended_at_ms=COALESCE(ended_at_ms,excluded.ended_at_ms),"
            "end_reason=CASE WHEN end_reason=0 THEN excluded.end_reason ELSE end_reason END,"
            "preview_start_ms=CASE WHEN excluded.played_ms>=played_ms THEN "
            "excluded.preview_start_ms ELSE preview_start_ms END,"
            "preview_end_ms=CASE WHEN excluded.played_ms>=played_ms THEN excluded.preview_end_ms "
            "ELSE preview_end_ms END,"
            "actual_quality=CASE WHEN excluded.played_ms>=played_ms THEN excluded.actual_quality "
            "ELSE actual_quality END "
            "WHERE song_id=excluded.song_id AND source=excluded.source AND "
            "entry_id=excluded.entry_id "
            "AND started_at_ms=excluded.started_at_ms AND origin_kind=excluded.origin_kind AND "
            "origin_id=excluded.origin_id AND origin_name=excluded.origin_name"));
        for (const auto& row : job.records) {
            query.bindValue(0, job.accountId); // canonical non-null empty TEXT for Guest
            query.bindValue(1, uuidText(row.id));
            query.bindValue(2, row.song.value);
            query.bindValue(3, static_cast<int>(row.source));
            query.bindValue(4, uuidText(row.queueEntry));
            query.bindValue(5, row.startedAtMs);
            query.bindValue(6, row.endedAtMs ? QVariant(*row.endedAtMs) : QVariant());
            query.bindValue(7, row.playedMs);
            query.bindValue(8, row.capped ? 1 : 0);
            query.bindValue(9, static_cast<int>(row.end));
            query.bindValue(10, row.preview ? QVariant(row.preview->startMs) : QVariant());
            query.bindValue(11, row.preview ? QVariant(row.preview->endMs) : QVariant());
            query.bindValue(12,
                            row.actualQuality.isEmpty() ? QStringLiteral("") : row.actualQuality);
            query.bindValue(13, static_cast<int>(row.origin.kind));
            query.bindValue(14, row.origin.id.isEmpty() ? QStringLiteral("") : row.origin.id);
            query.bindValue(15, row.origin.name.isEmpty() ? QStringLiteral("") : row.origin.name);
            if (!query.exec())
                return rollback(sqlError(query.lastError()));
            if (query.numRowsAffected() != 1)
                return rollback(StorageError::Corrupt); // ID reuse cannot overwrite another fact
        }
    }
    if (!db.commit())
        return rollback(sqlError(db.lastError()));
    return {StorageError::None, {job.id, StorageError::None, {}}};
}
// ---- song details (statistics) ----------------------------------------------------

bool storableText(const QString& text, qsizetype maximum)
{
    return text.size() <= maximum && text.isValidUtf16() && !text.contains(QChar::Null);
}
QString text(const QString& value) { return value.isEmpty() ? QStringLiteral("") : value; }
// What is kept of a song's details: its id must be valid; anything else that is not
// storable is dropped (not the song).
domain::Song storableSong(domain::Song song)
{
    if (!storableText(song.title, 1024))
        song.title.clear();
    QList<domain::ArtistCredit> artists;
    for (auto& artist : song.artists) {
        if (artists.size() == 64)
            break;
        if (artist.id && !domain::validMusicId(artist.id->value))
            artist.id.reset();
        if (!storableText(artist.name, 512))
            artist.name.clear();
        if (artist.id || !artist.name.isEmpty())
            artists.append(std::move(artist));
    }
    song.artists = std::move(artists);
    if (song.album) {
        if (song.album->id && !domain::validMusicId(song.album->id->value))
            song.album->id.reset();
        if (!storableText(song.album->title, 1024))
            song.album->title.clear();
        const auto scheme = song.album->cover.scheme();
        if (!song.album->cover.isValid() || (scheme != u"https" && scheme != u"http") ||
            song.album->cover.toString(QUrl::FullyEncoded).size() > 2048)
            song.album->cover.clear();
    }
    if (song.durationMs &&
        (*song.durationMs < 0 || *song.durationMs > domain::ObservedPlayback::MaximumMs))
        song.durationMs.reset();
    return song;
}

StorageResult<ListeningStateResult> executeSongs(QSqlDatabase& db, const ListeningStateJob& job)
{
    const auto fail = [&](StorageError error) {
        return StorageResult<ListeningStateResult>{error, {job.id, error, {}}};
    };
    if (!job.id || !domain::validListeningAccount(job.accountId))
        return fail(StorageError::Query);
    if (job.operation == ListeningStateOperation::MissingSongs) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT h.song_id FROM listening_history h WHERE h.account_id=? AND NOT EXISTS("
            "SELECT 1 FROM listening_song s WHERE s.account_id=h.account_id AND "
            "s.song_id=h.song_id) GROUP BY h.song_id ORDER BY MAX(h.started_at_ms) DESC LIMIT "
            "100"));
        query.addBindValue(job.accountId);
        if (!query.exec())
            return fail(sqlError(query.lastError()));
        ListeningStateResult result{job.id, StorageError::None, {}};
        while (query.next()) {
            const auto id = query.value(0).toString();
            if (!domain::validMusicId(id))
                return fail(StorageError::Corrupt);
            result.missingSongs.append({id});
        }
        return {StorageError::None, std::move(result)};
    }
    if (job.operation != ListeningStateOperation::SaveSongs ||
        (job.songs.isEmpty() && job.unavailable.isEmpty()) || job.songs.size() > 100 ||
        job.unavailable.size() > 100)
        return fail(StorageError::Query);
    for (const auto& song : job.songs)
        if (!domain::validMusicId(song.id.value))
            return fail(StorageError::Query);
    for (const auto& song : job.unavailable)
        if (!domain::validMusicId(song.value))
            return fail(StorageError::Query);
    if (!db.transaction())
        return fail(StorageError::Query);
    const auto rollback = [&](const QSqlError& error) {
        db.rollback();
        return fail(sqlError(error));
    };
    // Only songs still in this account's history (a clear may have run since they were
    // asked for); a song the service has no details for never replaces details it had.
    QSqlQuery upsert(db);
    upsert.prepare(QStringLiteral(
        "INSERT INTO listening_song(account_id,song_id,available,title,album_id,album_title,"
        "cover,duration_ms,updated_at_ms) SELECT ?,?,?,?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM "
        "listening_history WHERE account_id=? AND song_id=?) ON CONFLICT(account_id,song_id) DO "
        "UPDATE SET available=excluded.available,title=excluded.title,album_id=excluded.album_id,"
        "album_title=excluded.album_title,cover=excluded.cover,duration_ms=excluded.duration_ms,"
        "updated_at_ms=excluded.updated_at_ms WHERE excluded.available=1"));
    QSqlQuery eraseArtists(db);
    eraseArtists.prepare(
        QStringLiteral("DELETE FROM listening_song_artist WHERE account_id=? AND song_id=?"));
    QSqlQuery artist(db);
    artist.prepare(QStringLiteral(
        "INSERT INTO listening_song_artist(account_id,song_id,position,artist_id,name) SELECT "
        "?,?,?,?,? WHERE EXISTS(SELECT 1 FROM listening_song WHERE account_id=? AND song_id=? "
        "AND available=1)"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto save = [&](const domain::Song& song, bool available) {
        upsert.bindValue(0, job.accountId);
        upsert.bindValue(1, song.id.value);
        upsert.bindValue(2, available ? 1 : 0);
        upsert.bindValue(3, text(song.title));
        upsert.bindValue(4,
                         song.album && song.album->id ? song.album->id->value : QStringLiteral(""));
        upsert.bindValue(5, song.album ? text(song.album->title) : QStringLiteral(""));
        upsert.bindValue(6, song.album ? text(song.album->cover.toString(QUrl::FullyEncoded))
                                       : QStringLiteral(""));
        upsert.bindValue(7, song.durationMs ? QVariant(*song.durationMs) : QVariant());
        upsert.bindValue(8, now);
        upsert.bindValue(9, job.accountId);
        upsert.bindValue(10, song.id.value);
        if (!upsert.exec())
            return false;
        if (!available)
            return true;
        eraseArtists.bindValue(0, job.accountId);
        eraseArtists.bindValue(1, song.id.value);
        if (!eraseArtists.exec())
            return false;
        for (qsizetype i = 0; i < song.artists.size(); ++i) {
            artist.bindValue(0, job.accountId);
            artist.bindValue(1, song.id.value);
            artist.bindValue(2, i);
            artist.bindValue(3,
                             song.artists[i].id ? song.artists[i].id->value : QStringLiteral(""));
            artist.bindValue(4, text(song.artists[i].name));
            artist.bindValue(5, job.accountId);
            artist.bindValue(6, song.id.value);
            if (!artist.exec())
                return false;
        }
        return true;
    };
    for (const auto& song : job.songs)
        if (!save(storableSong(song), true))
            return rollback(upsert.lastError().isValid() ? upsert.lastError() : artist.lastError());
    for (const auto& id : job.unavailable)
        if (!save(domain::Song{id, {}, {}, {}, {}, {}, {}, {}}, false))
            return rollback(upsert.lastError());
    if (!db.commit())
        return rollback(db.lastError());
    return {StorageError::None, {job.id, StorageError::None, {}}};
}

// ---- statistics ---------------------------------------------------------------------

// The play rule (listening_stats_types.h) over a history row `h` and its song row `s`.
QString counted(QLatin1StringView h, QLatin1StringView s)
{
    return QStringLiteral("(%1.preview_start_ms IS NULL AND (%1.played_ms>=%3 OR "
                          "(%2.duration_ms IS NOT NULL AND %2.duration_ms<%3 AND "
                          "(%1.end_reason=1 OR %1.played_ms*10>=%2.duration_ms*9))))")
        .arg(h, s, QString::number(domain::CountedPlayMinimumMs));
}

class StatsReader
{
  public:
    StatsReader(QSqlDatabase& db, const ListeningStateJob& job) : m_db(db), m_job(job)
    {
        const auto& query = *job.stats;
        m_from = query.fromMs;
        m_to = query.toMs ? query.toMs : 253402300800000LL;
        // The records in the range: c is 1 for a counted play, lt the local start time.
        m_range = QStringLiteral(
                      "WITH r AS (SELECT h.song_id AS song,h.source AS source,h.origin_kind AS "
                      "ok,h.origin_id AS oid,h.origin_name AS oname,h.started_at_ms AS t,"
                      "h.played_ms AS ms,%1 AS c,datetime(h.started_at_ms/1000,'unixepoch',"
                      "'localtime') AS lt FROM listening_history h LEFT JOIN listening_song s ON "
                      "s.account_id=h.account_id AND s.song_id=h.song_id WHERE h.account_id=:a "
                      "AND h.started_at_ms>=:f AND h.started_at_ms<:t) ")
                      .arg(counted(QLatin1StringView("h"), QLatin1StringView("s")));
        const bool byPlays = query.order == domain::ListeningRankOrder::Plays;
        m_having = byPlays ? QStringLiteral(" HAVING p>0") : QStringLiteral(" HAVING m>0");
        m_order = byPlays ? QStringLiteral(" ORDER BY p DESC,m DESC")
                          : QStringLiteral(" ORDER BY m DESC,p DESC");
    }

    StorageError read(domain::ListeningStats& stats)
    {
        stats.query = *m_job.stats;
        if (!totals(stats.totals, stats.songsWithoutDetails) || !periods(stats.periods) ||
            !timeSlots(stats) || !songs(stats.songs) || !artists(stats.artists) ||
            !albums(stats.albums) || !origins(stats.origins) || !streak(stats.streak) ||
            !first(stats.firstRecordAtMs))
            return m_error;
        return StorageError::None;
    }

  private:
    bool run(QSqlQuery& query, const QString& statement)
    {
        query.setForwardOnly(true);
        if (!query.prepare(statement)) {
            m_error = sqlError(query.lastError());
            return false;
        }
        const auto bind = [&](QLatin1StringView name, const QVariant& value) {
            if (statement.contains(name))
                query.bindValue(QString(name), value);
        };
        bind(QLatin1StringView(":a"), m_job.accountId);
        bind(QLatin1StringView(":f"), m_from);
        bind(QLatin1StringView(":t"), m_to);
        bind(QLatin1StringView(":n"), m_job.stats->limit);
        if (!query.exec()) {
            m_error = sqlError(query.lastError());
            return false;
        }
        return true;
    }
    bool corrupt()
    {
        m_error = StorageError::Corrupt;
        return false;
    }
    static int count(const QVariant& value)
    {
        return int(std::min<qint64>(value.toLongLong(), INT_MAX));
    }

    bool totals(domain::ListeningTotals& totals, int& withoutDetails)
    {
        QSqlQuery query(m_db);
        if (!run(query,
                 m_range +
                     QStringLiteral(
                         "SELECT COALESCE(SUM(ms),0),COALESCE(SUM(c),0),COUNT(*),COUNT("
                         "DISTINCT CASE WHEN c=1 THEN song END),COUNT(DISTINCT date(lt)),"
                         "COUNT(DISTINCT CASE WHEN NOT EXISTS(SELECT 1 FROM listening_song "
                         "s WHERE s.account_id=:a AND s.song_id=r.song) THEN song END) FROM r")) ||
            !query.next())
            return false;
        totals.playedMs = query.value(0).toLongLong();
        totals.plays = count(query.value(1));
        totals.records = count(query.value(2));
        totals.songs = count(query.value(3));
        totals.days = count(query.value(4));
        withoutDetails = count(query.value(5));
        const QString artistKey =
            QStringLiteral("CASE a.artist_id WHEN '' THEN 'n:'||a.name ELSE a.artist_id END");
        QSqlQuery more(m_db);
        if (!run(
                more,
                m_range +
                    QStringLiteral(
                        "SELECT (SELECT COUNT(DISTINCT %1) FROM r JOIN listening_song_artist a "
                        "ON a.account_id=:a AND a.song_id=r.song WHERE r.c=1),(SELECT COUNT("
                        "DISTINCT s.album_id) FROM r JOIN listening_song s ON s.account_id=:a "
                        "AND s.song_id=r.song WHERE r.c=1 AND s.album_id<>''),(SELECT COUNT(*) "
                        "FROM (SELECT DISTINCT song FROM r WHERE c=1) x WHERE NOT EXISTS("
                        "SELECT 1 FROM listening_history h LEFT JOIN listening_song s ON "
                        "s.account_id=h.account_id AND s.song_id=h.song_id WHERE "
                        "h.account_id=:a AND h.song_id=x.song AND h.started_at_ms<:f AND %2))")
                        .arg(artistKey, counted(QLatin1StringView("h"), QLatin1StringView("s")))) ||
            !more.next())
            return false;
        totals.artists = count(more.value(0));
        totals.albums = count(more.value(1));
        totals.newSongs = count(more.value(2));
        return true;
    }

    bool periods(QList<domain::ListeningPeriod>& periods)
    {
        QString start;
        switch (m_job.stats->periodSize) {
        case domain::ListeningPeriodSize::Day:
            start = QStringLiteral("date(lt)");
            break;
        case domain::ListeningPeriodSize::Week: // the Monday on or before
            start = QStringLiteral("date(lt,'weekday 0','-6 days')");
            break;
        case domain::ListeningPeriodSize::Month:
            start = QStringLiteral("date(lt,'start of month')");
            break;
        }
        QSqlQuery query(m_db);
        if (!run(query, m_range + QStringLiteral("SELECT %1 AS b,SUM(ms),SUM(c) FROM r GROUP BY "
                                                 "b ORDER BY b")
                                      .arg(start)))
            return false;
        while (query.next()) {
            const auto day = QDate::fromString(query.value(0).toString(), Qt::ISODate);
            if (!day.isValid())
                return corrupt();
            periods.append({day, query.value(1).toLongLong(), count(query.value(2))});
        }
        return true;
    }

    bool timeSlots(domain::ListeningStats& stats)
    {
        QSqlQuery query(m_db);
        if (!run(query, m_range + QStringLiteral("SELECT CAST(strftime('%w',lt) AS INTEGER) AS d,"
                                                 "CAST(strftime('%H',lt) AS INTEGER) AS h,"
                                                 "SUM(ms),SUM(c) FROM r GROUP BY d,h")))
            return false;
        while (query.next()) {
            const auto day = query.value(0).toInt(); // 0 is Sunday
            const auto hour = query.value(1).toInt();
            if (day < 0 || day > 6 || hour < 0 || hour > 23)
                return corrupt();
            const domain::ListeningSlot slot{query.value(2).toLongLong(), count(query.value(3))};
            const auto weekday = (day + 6) % 7;
            stats.weekHours[weekday * 24 + hour] = slot;
            stats.hours[hour].playedMs += slot.playedMs;
            stats.hours[hour].plays += slot.plays;
            stats.weekdays[weekday].playedMs += slot.playedMs;
            stats.weekdays[weekday].plays += slot.plays;
        }
        return true;
    }

    bool songs(QList<domain::StatsSong>& songs)
    {
        QSqlQuery query(m_db);
        if (!run(query,
                 m_range +
                     QStringLiteral(
                         "SELECT r.song,SUM(r.c) AS p,SUM(r.ms) AS m,MAX(r.t),s.available,"
                         "s.title,s.album_id,s.album_title,s.cover,s.duration_ms FROM r LEFT "
                         "JOIN listening_song s ON s.account_id=:a AND s.song_id=r.song GROUP "
                         "BY r.song") +
                     m_having + m_order + QStringLiteral(",MAX(r.t) DESC LIMIT :n")))
            return false;
        QHash<QString, qsizetype> index;
        while (query.next()) {
            domain::StatsSong row;
            row.song.id = {query.value(0).toString()};
            if (!domain::validMusicId(row.song.id.value))
                return corrupt();
            row.plays = count(query.value(1));
            row.playedMs = query.value(2).toLongLong();
            row.lastPlayedAtMs = query.value(3).toLongLong();
            row.known = query.value(4).toInt() == 1;
            if (row.known) {
                row.song.title = query.value(5).toString();
                const auto albumId = query.value(6).toString();
                if (!albumId.isEmpty() || !query.value(7).toString().isEmpty())
                    row.song.album = domain::AlbumReference{
                        albumId.isEmpty()
                            ? std::nullopt
                            : std::optional<domain::AlbumId>(domain::AlbumId{albumId}),
                        query.value(7).toString(), QUrl(query.value(8).toString())};
                if (!query.value(9).isNull())
                    row.song.durationMs = query.value(9).toLongLong();
            }
            index.insert(row.song.id.value, songs.size());
            songs.append(std::move(row));
        }
        if (songs.isEmpty())
            return true;
        QStringList marks;
        for (qsizetype i = 0; i < songs.size(); ++i)
            marks.append(QStringLiteral("?"));
        QSqlQuery artists(m_db);
        artists.setForwardOnly(true);
        artists.prepare(QStringLiteral("SELECT song_id,artist_id,name FROM listening_song_artist "
                                       "WHERE account_id=? AND song_id IN (%1) ORDER BY "
                                       "song_id,position")
                            .arg(marks.join(u',')));
        artists.addBindValue(m_job.accountId);
        for (const auto& row : std::as_const(songs))
            artists.addBindValue(row.song.id.value);
        if (!artists.exec()) {
            m_error = sqlError(artists.lastError());
            return false;
        }
        while (artists.next()) {
            const auto at = index.value(artists.value(0).toString(), -1);
            if (at < 0)
                return corrupt();
            const auto id = artists.value(1).toString();
            songs[at].song.artists.append(
                {id.isEmpty() ? std::nullopt
                              : std::optional<domain::ArtistId>(domain::ArtistId{id}),
                 artists.value(2).toString()});
        }
        return true;
    }

    bool artists(QList<domain::StatsArtist>& artists)
    {
        QSqlQuery query(m_db);
        if (!run(query,
                 m_range +
                     QStringLiteral(
                         "SELECT CASE a.artist_id WHEN '' THEN 'n:'||a.name ELSE a.artist_id END "
                         "AS k,MAX(a.artist_id),MAX(a.name),SUM(r.c) AS p,SUM(r.ms) AS m,COUNT("
                         "DISTINCT CASE WHEN r.c=1 THEN r.song END) FROM r JOIN (SELECT DISTINCT "
                         "song_id,artist_id,name FROM listening_song_artist WHERE "
                         "account_id=:a) a ON a.song_id=r.song GROUP BY k") +
                     m_having + m_order + QStringLiteral(",k LIMIT :n")))
            return false;
        while (query.next()) {
            const auto id = query.value(1).toString();
            artists.append({{id.isEmpty() ? std::nullopt
                                          : std::optional<domain::ArtistId>(domain::ArtistId{id}),
                             query.value(2).toString()},
                            count(query.value(3)),
                            query.value(4).toLongLong(),
                            count(query.value(5))});
        }
        return true;
    }

    bool albums(QList<domain::StatsAlbum>& albums)
    {
        QSqlQuery query(m_db);
        if (!run(query,
                 m_range +
                     QStringLiteral(
                         "SELECT s.album_id AS k,MAX(s.album_title),MAX(s.cover),SUM(r.c) AS p,"
                         "SUM(r.ms) AS m,COUNT(DISTINCT CASE WHEN r.c=1 THEN r.song END) FROM r "
                         "JOIN listening_song s ON s.account_id=:a AND s.song_id=r.song WHERE "
                         "s.album_id<>'' GROUP BY k") +
                     m_having + m_order + QStringLiteral(",k LIMIT :n")))
            return false;
        while (query.next())
            albums.append({{domain::AlbumId{query.value(0).toString()}, query.value(1).toString(),
                            QUrl(query.value(2).toString())},
                           count(query.value(3)),
                           query.value(4).toLongLong(),
                           count(query.value(5))});
        return true;
    }

    bool origins(QList<domain::StatsOrigin>& origins)
    {
        QSqlQuery query(m_db);
        // The name it had last; FM is one row; queue songs without an origin are left out.
        if (!run(query,
                 m_range +
                     QStringLiteral(
                         "SELECT source,ok,oid,(SELECT r2.oname FROM r r2 WHERE "
                         "r2.source=r.source AND r2.ok=r.ok AND r2.oid=r.oid ORDER BY r2.t DESC "
                         "LIMIT 1),SUM(c) AS p,SUM(ms) AS m FROM r WHERE source=1 OR ok<>0 "
                         "GROUP BY source,ok,oid") +
                     m_having + m_order + QStringLiteral(" LIMIT :n")))
            return false;
        while (query.next()) {
            const auto source = storedInteger(query.value(0));
            const auto origin = storedOrigin(query.value(1), query.value(2), query.value(3));
            if (!source || *source < 0 || *source > 1 || !origin)
                return corrupt();
            origins.append({static_cast<domain::ListeningMode>(*source), *origin,
                            count(query.value(4)), query.value(5).toLongLong()});
        }
        return true;
    }

    bool streak(domain::ListeningStreak& streak)
    {
        QSqlQuery query(m_db);
        if (!run(query,
                 QStringLiteral("SELECT DISTINCT date(h.started_at_ms/1000,'unixepoch',"
                                "'localtime') AS d FROM listening_history h LEFT JOIN "
                                "listening_song s ON s.account_id=h.account_id AND "
                                "s.song_id=h.song_id WHERE h.account_id=:a AND %1 ORDER BY d")
                     .arg(counted(QLatin1StringView("h"), QLatin1StringView("s")))))
            return false;
        int length = 0;
        QDate previous;
        while (query.next()) {
            const auto day = QDate::fromString(query.value(0).toString(), Qt::ISODate);
            if (!day.isValid())
                return corrupt();
            length = previous.isValid() && previous.addDays(1) == day ? length + 1 : 1;
            if (length > streak.longest) {
                streak.longest = length;
                streak.longestEnd = day;
            }
            previous = day;
        }
        streak.lastDay = previous;
        const qint64 now =
            m_job.stats->nowMs ? m_job.stats->nowMs : QDateTime::currentMSecsSinceEpoch();
        const QDate today = QDateTime::fromMSecsSinceEpoch(now).date(); // local, as SQLite's
        streak.current =
            previous.isValid() && (previous == today || previous.addDays(1) == today) ? length : 0;
        return true;
    }

    bool first(std::optional<qint64>& first)
    {
        QSqlQuery query(m_db);
        if (!run(query, QStringLiteral("SELECT MIN(started_at_ms) FROM listening_history WHERE "
                                       "account_id=:a")) ||
            !query.next())
            return false;
        if (!query.value(0).isNull())
            first = query.value(0).toLongLong();
        return true;
    }

    QSqlDatabase& m_db;
    const ListeningStateJob& m_job;
    qint64 m_from = 0, m_to = 0;
    QString m_range, m_having, m_order;
    StorageError m_error = StorageError::Query;
};

StorageResult<ListeningStateResult> executeStats(QSqlDatabase& db, const ListeningStateJob& job)
{
    const auto fail = [&](StorageError error) {
        return StorageResult<ListeningStateResult>{error, {job.id, error, {}}};
    };
    if (!job.id || !domain::validListeningAccount(job.accountId) || !job.stats ||
        !job.stats->isValid())
        return fail(StorageError::Query);
    if (!db.transaction()) // one consistent view across the queries
        return fail(StorageError::Query);
    domain::ListeningStats stats;
    const auto error = StatsReader(db, job).read(stats);
    db.commit();
    if (error != StorageError::None)
        return fail(error);
    ListeningStateResult result{job.id, StorageError::None, {}};
    result.stats = std::move(stats);
    return {StorageError::None, std::move(result)};
}
} // namespace

QList<Migration> ListeningStateStore::migrations()
{
    auto migrations = InstallationStore::migrations();
    migrations.append(
        {2,
         {QStringLiteral(
              "CREATE TABLE playback_queue (account_id TEXT PRIMARY KEY NOT NULL, "
              "active TEXT NOT NULL, queue_order INTEGER NOT NULL CHECK(queue_order IN (0,1)), "
              "queue_repeat INTEGER NOT NULL CHECK(queue_repeat IN (0,1,2)))"),
          QStringLiteral(
              "CREATE TABLE playback_queue_entry (account_id TEXT NOT NULL REFERENCES "
              "playback_queue(account_id) ON DELETE CASCADE, ordinal INTEGER NOT NULL "
              "CHECK(ordinal BETWEEN 0 AND 9999), entry_id TEXT NOT NULL, song_id TEXT NOT NULL, "
              "PRIMARY KEY(account_id, ordinal), UNIQUE(account_id, entry_id))")}});
    migrations.append(
        {3,
         {QStringLiteral(
              "CREATE TABLE listening_history (account_id TEXT NOT NULL, record_id TEXT NOT NULL, "
              "song_id TEXT NOT NULL, source INTEGER NOT NULL CHECK(source IN (0,1)), entry_id "
              "TEXT NOT NULL, "
              "started_at_ms INTEGER NOT NULL CHECK(typeof(started_at_ms)='integer' AND "
              "started_at_ms>0), "
              "ended_at_ms INTEGER, played_ms INTEGER NOT NULL CHECK(typeof(played_ms)='integer' "
              "AND played_ms BETWEEN 1 AND 604800000), capped INTEGER NOT NULL CHECK(capped IN "
              "(0,1)), "
              "end_reason INTEGER NOT NULL CHECK(end_reason BETWEEN 0 AND 5), "
              "preview_start_ms INTEGER, preview_end_ms INTEGER, actual_quality TEXT NOT NULL, "
              "CHECK((end_reason=0 AND ended_at_ms IS NULL) OR (end_reason<>0 AND ended_at_ms IS "
              "NOT NULL AND ended_at_ms>=started_at_ms)), "
              "PRIMARY KEY(account_id,record_id))"),
          QStringLiteral("CREATE INDEX listening_history_page ON "
                         "listening_history(account_id,started_at_ms DESC,record_id DESC)")}});
    // v4: local statistics (docs/api/local-listening-history.md §6). Where each queued song
    // and each record came from, and a snapshot of every heard song's details per account.
    migrations.append(
        {4,
         {QStringLiteral("ALTER TABLE playback_queue_entry ADD COLUMN origin_kind INTEGER NOT NULL "
                         "DEFAULT 0 CHECK(origin_kind BETWEEN 0 AND 255)"),
          QStringLiteral("ALTER TABLE playback_queue_entry ADD COLUMN origin_id TEXT NOT NULL "
                         "DEFAULT ''"),
          QStringLiteral("ALTER TABLE playback_queue_entry ADD COLUMN origin_name TEXT NOT NULL "
                         "DEFAULT ''"),
          QStringLiteral("ALTER TABLE listening_history ADD COLUMN origin_kind INTEGER NOT NULL "
                         "DEFAULT 0 CHECK(origin_kind BETWEEN 0 AND 255)"),
          QStringLiteral("ALTER TABLE listening_history ADD COLUMN origin_id TEXT NOT NULL "
                         "DEFAULT ''"),
          QStringLiteral("ALTER TABLE listening_history ADD COLUMN origin_name TEXT NOT NULL "
                         "DEFAULT ''"),
          QStringLiteral("CREATE INDEX listening_history_song ON "
                         "listening_history(account_id,song_id,started_at_ms)"),
          QStringLiteral(
              "CREATE TABLE listening_song (account_id TEXT NOT NULL, song_id TEXT NOT NULL, "
              "available INTEGER NOT NULL CHECK(available IN (0,1)), title TEXT NOT NULL, "
              "album_id TEXT NOT NULL, album_title TEXT NOT NULL, cover TEXT NOT NULL, "
              "duration_ms INTEGER CHECK(duration_ms IS NULL OR (typeof(duration_ms)='integer' "
              "AND duration_ms BETWEEN 0 AND 604800000)), updated_at_ms INTEGER NOT NULL, "
              "PRIMARY KEY(account_id,song_id))"),
          QStringLiteral(
              "CREATE TABLE listening_song_artist (account_id TEXT NOT NULL, song_id TEXT NOT "
              "NULL, position INTEGER NOT NULL CHECK(position BETWEEN 0 AND 63), artist_id TEXT "
              "NOT NULL, name TEXT NOT NULL, PRIMARY KEY(account_id,song_id,position), FOREIGN "
              "KEY(account_id,song_id) REFERENCES listening_song(account_id,song_id) ON DELETE "
              "CASCADE)"),
          QStringLiteral("CREATE INDEX listening_song_artist_by_artist ON "
                         "listening_song_artist(account_id,artist_id)")}});
    // v5: the saved queue may be Personal FM's songs so far (docs/api/fm-continuous-playback.md).
    migrations.append(
        {5,
         {QStringLiteral("ALTER TABLE playback_queue ADD COLUMN mode INTEGER NOT NULL DEFAULT 0 "
                         "CHECK(mode IN (0,1))")}});
    // v6: a queue entry may be a podcast program played through its track
    // (docs/api/podcast-playback.md).
    migrations.append(
        {6,
         {QStringLiteral("ALTER TABLE playback_queue_entry ADD COLUMN program_id TEXT "
                         "NOT NULL DEFAULT ''")}});
    return migrations;
}
ListeningStateStore::ListeningStateStore(DatabaseWorker& database, QObject* parent)
    : QObject(parent), m_database(database)
{
    Q_ASSERT(database.thread() == thread());
}
void ListeningStateStore::submit(ListeningStateJob job)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (job.accountId.isEmpty())
        job.accountId = QStringLiteral(""); // empty Guest bucket is TEXT, not driver-dependent NULL
    const bool write = job.operation == ListeningStateOperation::SaveQueue ||
                       job.operation == ListeningStateOperation::SaveHistory ||
                       job.operation == ListeningStateOperation::ClearHistory ||
                       job.operation == ListeningStateOperation::SaveSongs;
    if (!job.id || m_pending >= 8) {
        m_writeFailed = m_writeFailed || write;
        emit completed({job.id, StorageError::Query, {}});
        return;
    }
    ++m_pending;
    const auto id = job.id;
    m_database.run<ListeningStateResult>(
        this,
        [job = std::move(job)](QSqlDatabase& db) {
            switch (job.operation) {
            case ListeningStateOperation::LoadQueue:
            case ListeningStateOperation::SaveQueue:
                return executeQueue(db, job);
            case ListeningStateOperation::LoadStats:
                return executeStats(db, job);
            case ListeningStateOperation::MissingSongs:
            case ListeningStateOperation::SaveSongs:
                return executeSongs(db, job);
            default:
                return executeHistory(db, job);
            }
        },
        [this, id, write](StorageResult<ListeningStateResult> result) {
            --m_pending;
            m_writeFailed = m_writeFailed || (write && !result.ok());
            if (!result.ok())
                result.value = {id, result.error, {}};
            const QPointer<ListeningStateStore> guard(this);
            emit completed(std::move(result.value));
            if (guard && !m_pending)
                emit idle();
        });
}
} // namespace cirrus::storage

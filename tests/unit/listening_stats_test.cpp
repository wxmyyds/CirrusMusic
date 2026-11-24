// Local listening statistics and the v4 history schema (docs/api/local-listening-history.md
// §6), against the store and a real SQLite file. Local time is fixed to UTC+8 without DST
// (a POSIX TZ string: no tz database needed) so day, hour and week buckets are known.
#include "infrastructure/storage/database_worker.h"
#include "infrastructure/storage/listening_state_store.h"

#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <time.h> // tzset

using namespace cirrus;
using namespace cirrus::storage;
using domain::ListeningEnd;
using domain::ListeningMode;
using domain::PlayOrigin;
using domain::PlayOriginKind;

namespace
{
const bool kZone = [] {
    qputenv("TZ", "CST-8");
    tzset();
    return true;
}();
const QString kAccount = QStringLiteral("42");

qint64 at(int day, int hour, int minute = 0) // local time, day 2 is Monday 2026-11-02
{
    return QDateTime(QDate(2026, 11, day), QTime(hour, minute)).toMSecsSinceEpoch();
}

domain::StoredListeningRecord record(const QString& song, qint64 start, qint64 played,
                                     ListeningEnd end = ListeningEnd::Replaced,
                                     PlayOrigin origin = {},
                                     ListeningMode source = ListeningMode::Queue)
{
    domain::StoredListeningRecord row;
    row.id = QUuid::createUuid();
    row.accountId = kAccount;
    row.song = {song};
    row.source = source;
    row.queueEntry = source == ListeningMode::Queue ? QUuid::createUuid() : QUuid{};
    row.startedAtMs = start;
    if (end != ListeningEnd::Active)
        row.endedAtMs = start + played + 500;
    row.playedMs = played;
    row.end = end;
    row.actualQuality = QStringLiteral("exhigh");
    row.origin = std::move(origin);
    return row;
}

domain::Song song(const QString& id, const QString& title, QList<domain::ArtistCredit> artists,
                  std::optional<QString> album, qint64 durationMs)
{
    domain::Song song;
    song.id = {id};
    song.title = title;
    song.artists = std::move(artists);
    if (album)
        song.album = domain::AlbumReference{domain::AlbumId{*album}, *album + QStringLiteral(" LP"),
                                            QUrl(QStringLiteral("https://p1.music.126.net/a.jpg"))};
    song.durationMs = durationMs;
    return song;
}
domain::ArtistCredit artist(const QString& id, const QString& name)
{
    return {id.isEmpty() ? std::nullopt : std::optional<domain::ArtistId>(domain::ArtistId{id}),
            name};
}

class Fixture
{
  public:
    explicit Fixture(const QString& path, QList<Migration> migrations = {})
        : worker(path, migrations.isEmpty() ? ListeningStateStore::migrations() : migrations),
          store(worker)
    {
        QSignalSpy opened(&worker, &DatabaseWorker::opened);
        worker.open();
        ok = opened.wait(5000) &&
             qvariant_cast<StorageError>(opened.first().first()) == StorageError::None;
    }
    ListeningStateResult run(ListeningStateJob job)
    {
        job.id = ++next;
        if (job.accountId.isEmpty() && job.operation != ListeningStateOperation::SaveHistory)
            job.accountId = kAccount;
        QSignalSpy done(&store, &ListeningStateStore::completed);
        store.submit(std::move(job));
        if (!done.wait(5000))
            return {0, StorageError::Unavailable, {}};
        return qvariant_cast<ListeningStateResult>(done.first().first());
    }
    StorageError save(QList<domain::StoredListeningRecord> rows)
    {
        ListeningStateJob job;
        job.operation = ListeningStateOperation::SaveHistory;
        job.accountId = kAccount;
        job.records = std::move(rows);
        return run(std::move(job)).error;
    }
    StorageError saveSongs(QList<domain::Song> songs, QList<domain::SongId> unavailable = {})
    {
        ListeningStateJob job;
        job.operation = ListeningStateOperation::SaveSongs;
        job.songs = std::move(songs);
        job.unavailable = std::move(unavailable);
        return run(std::move(job)).error;
    }
    std::optional<domain::ListeningStats> stats(domain::ListeningStatsQuery query)
    {
        ListeningStateJob job;
        job.operation = ListeningStateOperation::LoadStats;
        job.stats = query;
        auto result = run(std::move(job));
        return result.error == StorageError::None ? result.stats : std::nullopt;
    }
    QString scalar(const QString& sql)
    {
        std::optional<QString> value;
        QObject context;
        worker.run<QString>(
            &context,
            [sql](QSqlDatabase& db) -> StorageResult<QString> {
                QSqlQuery q(db);
                if (!q.exec(sql))
                    return {StorageError::Query, {}};
                return {StorageError::None, q.next() ? q.value(0).toString() : QString()};
            },
            [&value](StorageResult<QString> r) { value = r.ok() ? r.value : QStringLiteral("!"); });
        if (!QTest::qWaitFor([&value] { return value.has_value(); }, 5000))
            return QStringLiteral("!");
        return *value;
    }
    DatabaseWorker worker;
    ListeningStateStore store;
    bool ok = false;
    quint64 next = 0;
};
} // namespace

class ListeningStatsTest : public QObject
{
    Q_OBJECT
  private slots:
    void upgradeKeepsVersion3Rows();
    void queueAndRecordsKeepTheirOrigin();
    void countedPlays();
    void rankingsAndShares();
    void periodsSlotsAndStreak();
    void songDetails();
    void rejectsBadQueries();
};

void ListeningStatsTest::upgradeKeepsVersion3Rows()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("state.sqlite"));
    auto v3 = ListeningStateStore::migrations();
    while (!v3.isEmpty() && v3.last().version > 3)
        v3.removeLast();
    {
        Fixture old(path, v3);
        QVERIFY(old.ok);
        QCOMPARE(old.scalar(QStringLiteral(
                     "INSERT INTO listening_history(account_id,record_id,song_id,source,entry_id,"
                     "started_at_ms,ended_at_ms,played_ms,capped,end_reason,actual_quality) VALUES"
                     "('42','6f1d1b8e-58b6-4d39-9a3e-2b1f5c3a7d10','100',0,'',1762000000000,"
                     "1762000040000,40000,0,1,'')")),
                 QString());
    }
    Fixture fixture(path);
    QVERIFY(fixture.ok);
    // Up to the newest migration (v5 added the saved queue's mode).
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT max(version) FROM schema_migrations")),
             QString::number(ListeningStateStore::migrations().last().version));
    ListeningStateJob page;
    page.operation = ListeningStateOperation::LoadHistory;
    const auto loaded = fixture.run(page);
    QCOMPARE(loaded.error, StorageError::None);
    QVERIFY(loaded.history);
    QCOMPARE(loaded.history->records.size(), 1);
    QCOMPARE(loaded.history->records.first().origin, PlayOrigin{});
    const auto stats = fixture.stats({});
    QVERIFY(stats);
    QCOMPARE(stats->totals.plays, 1);
    QCOMPARE(stats->totals.playedMs, 40000);
    QCOMPARE(stats->songsWithoutDetails, 1);
}

void ListeningStatsTest::queueAndRecordsKeepTheirOrigin()
{
    QTemporaryDir dir;
    Fixture fixture(dir.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(fixture.ok);
    const PlayOrigin playlist{PlayOriginKind::Playlist, QStringLiteral("777"),
                              QStringLiteral("深夜通勤")};
    domain::QueueSnapshot queue;
    queue.entries = {{QUuid::createUuid(), {QStringLiteral("1")}, playlist},
                     {QUuid::createUuid(), {QStringLiteral("2")}, {PlayOriginKind::Search, {}, {}}},
                     {QUuid::createUuid(),
                      {QStringLiteral("99")},
                      {PlayOriginKind::Podcast, QStringLiteral("4"), QStringLiteral("播客")},
                      {QStringLiteral("5")}}};
    queue.active = queue.entries.first().id;
    ListeningStateJob saveQueue;
    saveQueue.operation = ListeningStateOperation::SaveQueue;
    saveQueue.queue = queue;
    QCOMPARE(fixture.run(saveQueue).error, StorageError::None);
    ListeningStateJob loadQueue;
    loadQueue.operation = ListeningStateOperation::LoadQueue;
    const auto restored = fixture.run(loadQueue);
    QVERIFY(restored.queue);
    QCOMPARE(restored.queue->entries.size(), 3);
    QCOMPARE(restored.queue->entries[0].origin, playlist);
    QVERIFY(restored.queue->entries[0].program.value.isEmpty());
    QCOMPARE(restored.queue->entries[1].origin.kind, PlayOriginKind::Search);
    QCOMPARE(restored.queue->entries[2].origin.kind, PlayOriginKind::Podcast);
    QCOMPARE(restored.queue->entries[2].program.value, QStringLiteral("5")); // the program
    QCOMPARE(restored.queue->entries[2].song.value, QStringLiteral("99"));   // its track
    QCOMPARE(restored.mode, ListeningMode::Queue);
    saveQueue.mode = ListeningMode::PersonalFm; // its songs so far, saved as the queue
    QCOMPARE(fixture.run(saveQueue).error, StorageError::None);
    QCOMPARE(fixture.run(loadQueue).mode, ListeningMode::PersonalFm);
    saveQueue.mode = ListeningMode::Queue;
    QCOMPARE(fixture.run(saveQueue).error, StorageError::None);
    QCOMPARE(fixture.run(loadQueue).mode, ListeningMode::Queue);

    auto row = record(QStringLiteral("1"), at(3, 9), 45000, ListeningEnd::Active, playlist);
    QCOMPARE(fixture.save({row}), StorageError::None);
    row.playedMs = 60000; // the same record growing
    row.end = ListeningEnd::Ended;
    row.endedAtMs = row.startedAtMs + 61000;
    QCOMPARE(fixture.save({row}), StorageError::None);
    auto rewritten = row;
    rewritten.origin.name = QStringLiteral("别的歌单"); // a stored fact is not rewritten
    QCOMPARE(fixture.save({rewritten}), StorageError::Corrupt);
    ListeningStateJob page;
    page.operation = ListeningStateOperation::LoadHistory;
    const auto loaded = fixture.run(page);
    QVERIFY(loaded.history);
    QCOMPARE(loaded.history->records.size(), 1);
    QCOMPARE(loaded.history->records.first().origin, playlist);
    QCOMPARE(loaded.history->records.first().playedMs, 60000);

    auto fm = record(QStringLiteral("3"), at(3, 10), 40000, ListeningEnd::Ended, playlist,
                     ListeningMode::PersonalFm);
    QCOMPARE(fixture.save({fm}), StorageError::Query); // FM records have no origin
}

void ListeningStatsTest::countedPlays()
{
    QTemporaryDir dir;
    Fixture fixture(dir.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(fixture.ok);
    auto preview = record(QStringLiteral("3"), at(3, 9, 3), 35000);
    preview.preview = domain::PreviewRange{60000, 90000};
    QCOMPARE(fixture.save({
                 record(QStringLiteral("1"), at(3, 9, 0), 30000),                      // counts
                 record(QStringLiteral("2"), at(3, 9, 1), 29999),                      // too short
                 preview,                                                              // time only
                 record(QStringLiteral("4"), at(3, 9, 4), 19000, ListeningEnd::Ended), // to end
                 record(QStringLiteral("5"), at(3, 9, 5), 18000),                      // 90 %
                 record(QStringLiteral("6"), at(3, 9, 6), 10000, ListeningEnd::Ended), // ended?
                 record(QStringLiteral("7"), at(3, 9, 7), 25000, ListeningEnd::Ended), // unknown
             }),
             StorageError::None);
    // 4, 5 and 6 are 20 s songs; 6 "ended" after 10 s of 20 is a seek to the end: still
    // counted, as the end is what the rule asks for. 7 has no details: no duration known.
    QCOMPARE(fixture.saveSongs({song(QStringLiteral("4"), QStringLiteral("Four"), {}, {}, 20000),
                                song(QStringLiteral("5"), QStringLiteral("Five"), {}, {}, 20000),
                                song(QStringLiteral("6"), QStringLiteral("Six"), {}, {}, 20000)}),
             StorageError::None);
    const auto stats = fixture.stats({});
    QVERIFY(stats);
    QCOMPARE(stats->totals.records, 7);
    QCOMPARE(stats->totals.plays, 4); // 1, 4, 5, 6
    QCOMPARE(stats->totals.songs, 4);
    QCOMPARE(stats->totals.playedMs, 30000 + 29999 + 35000 + 19000 + 18000 + 10000 + 25000);
    QCOMPARE(stats->songsWithoutDetails, 4); // 1, 2, 3, 7
}

void ListeningStatsTest::rankingsAndShares()
{
    QTemporaryDir dir;
    Fixture fixture(dir.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(fixture.ok);
    const PlayOrigin commute{PlayOriginKind::Playlist, QStringLiteral("777"),
                             QStringLiteral("通勤")};
    auto renamed = commute;
    renamed.name = QStringLiteral("深夜通勤");
    QCOMPARE(fixture.save({
                 record(QStringLiteral("1"), at(3, 9), 200000, ListeningEnd::Ended, commute),
                 record(QStringLiteral("1"), at(3, 10), 200000, ListeningEnd::Ended, renamed),
                 record(QStringLiteral("1"), at(4, 9), 200000, ListeningEnd::Ended, renamed),
                 record(QStringLiteral("2"), at(4, 10), 600000, ListeningEnd::Ended),
                 record(QStringLiteral("3"), at(4, 11), 50000, ListeningEnd::Ended, {},
                        ListeningMode::PersonalFm),
                 record(QStringLiteral("4"), at(4, 12), 5000), // skipped
             }),
             StorageError::None);
    QCOMPARE(fixture.saveSongs({song(QStringLiteral("1"), QStringLiteral("One"),
                                     {artist(QStringLiteral("10"), QStringLiteral("A")),
                                      artist(QStringLiteral("11"), QStringLiteral("B"))},
                                     QStringLiteral("100"), 200000),
                                song(QStringLiteral("2"), QStringLiteral("Two"),
                                     {artist(QStringLiteral("10"), QStringLiteral("A"))},
                                     QStringLiteral("100"), 600000),
                                song(QStringLiteral("3"), QStringLiteral("Three"),
                                     {artist({}, QStringLiteral("Nameless"))}, {}, 50000)}),
             StorageError::None);

    auto stats = fixture.stats({});
    QVERIFY(stats);
    QCOMPARE(stats->totals.artists, 3);
    QCOMPARE(stats->totals.albums, 1);
    QCOMPARE(stats->songs.size(), 3); // 4 was never a counted play
    QCOMPARE(stats->songs[0].song.id.value, QStringLiteral("1"));
    QCOMPARE(stats->songs[0].plays, 3);
    QVERIFY(stats->songs[0].known);
    QCOMPARE(stats->songs[0].song.title, QStringLiteral("One"));
    QCOMPARE(stats->songs[0].song.artists.size(), 2);
    QCOMPARE(stats->songs[0].song.artists[1].name, QStringLiteral("B"));
    QVERIFY(stats->songs[0].song.album);
    QCOMPARE(stats->songs[0].song.album->title, QStringLiteral("100 LP"));
    QCOMPARE(stats->songs[0].lastPlayedAtMs, at(4, 9));

    QCOMPARE(stats->artists.size(), 3);
    QCOMPARE(stats->artists[0].artist.name, QStringLiteral("A")); // 4 plays: One ×3, Two
    QCOMPARE(stats->artists[0].plays, 4);
    QCOMPARE(stats->artists[0].songs, 2);
    QCOMPARE(stats->artists[0].playedMs, 1200000);
    QCOMPARE(stats->artists[1].artist.name, QStringLiteral("B"));
    QVERIFY(!stats->artists[2].artist.id);
    QCOMPARE(stats->artists[2].artist.name, QStringLiteral("Nameless"));

    QCOMPARE(stats->albums.size(), 1);
    QCOMPARE(stats->albums[0].plays, 4);
    QCOMPARE(stats->albums[0].songs, 2);

    QCOMPARE(stats->origins.size(), 2);          // queue songs without an origin are left out
    QCOMPARE(stats->origins[0].origin, renamed); // its latest name
    QCOMPARE(stats->origins[0].plays, 3);
    QCOMPARE(stats->origins[1].mode, ListeningMode::PersonalFm);

    domain::ListeningStatsQuery byTime;
    byTime.order = domain::ListeningRankOrder::Time;
    byTime.limit = 1;
    stats = fixture.stats(byTime);
    QVERIFY(stats);
    QCOMPARE(stats->songs.size(), 1);
    QCOMPARE(stats->songs[0].song.id.value, QStringLiteral("1")); // 600 s, then Two's 600 s
    QCOMPARE(stats->artists.size(), 1);

    domain::ListeningStatsQuery secondDay;
    secondDay.fromMs = at(4, 0);
    secondDay.toMs = at(5, 0);
    stats = fixture.stats(secondDay);
    QVERIFY(stats);
    QCOMPARE(stats->totals.plays, 3);    // One, Two, Three
    QCOMPARE(stats->totals.newSongs, 2); // One was first heard the day before
    QCOMPARE(stats->totals.days, 1);
}

void ListeningStatsTest::periodsSlotsAndStreak()
{
    QTemporaryDir dir;
    Fixture fixture(dir.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(fixture.ok);
    // 23:30 local on Sunday the 1st is 15:30 UTC: it belongs to the 1st, a Sunday.
    QCOMPARE(fixture.save({
                 record(QStringLiteral("1"), at(1, 23, 30), 60000),
                 record(QStringLiteral("1"), at(2, 0, 30), 60000),
                 record(QStringLiteral("2"), at(3, 8), 60000),
                 record(QStringLiteral("2"), at(3, 8, 30), 10000), // no play: no streak day
                 record(QStringLiteral("3"), at(5, 21), 60000),
                 record(QStringLiteral("3"), at(6, 21), 60000),
                 record(QStringLiteral("3"), at(9, 21), 60000), // Monday of the next week
             }),
             StorageError::None);
    domain::ListeningStatsQuery query;
    query.nowMs = at(10, 12);
    auto stats = fixture.stats(query);
    QVERIFY(stats);
    QCOMPARE(stats->periods.size(), 6);
    QCOMPARE(stats->periods[0].start, QDate(2026, 11, 1));
    QCOMPARE(stats->periods[2].start, QDate(2026, 11, 3));
    QCOMPARE(stats->periods[2].playedMs, 70000);
    QCOMPARE(stats->periods[2].plays, 1);
    QCOMPARE(stats->hours[23].plays, 1);
    QCOMPARE(stats->hours[0].plays, 1);
    QCOMPARE(stats->hours[21].plays, 3);
    QCOMPARE(stats->weekdays[6].plays, 1);                  // Sunday
    QCOMPARE(stats->weekdays[0].plays, 2);                  // Mondays the 2nd and the 9th
    QCOMPARE(stats->weekHours[6 * 24 + 23].plays, 1);       // Sunday 23:30
    QCOMPARE(stats->weekHours[0 * 24 + 0].plays, 1);        // Monday 00:30
    QCOMPARE(stats->weekHours[1 * 24 + 8].playedMs, 70000); // Tuesday 8:00 and 8:30
    QCOMPARE(stats->weekHours[1 * 24 + 8].plays, 1);
    QCOMPARE(stats->streak.longest, 3); // 1st to 3rd
    QCOMPARE(stats->streak.longestEnd, QDate(2026, 11, 3));
    QCOMPARE(stats->streak.current, 1); // the 9th, and today is the 10th
    QCOMPARE(stats->streak.lastDay, QDate(2026, 11, 9));
    QCOMPARE(stats->firstRecordAtMs, at(1, 23, 30));

    query.periodSize = domain::ListeningPeriodSize::Week;
    stats = fixture.stats(query);
    QVERIFY(stats);
    QCOMPARE(stats->periods.size(), 3);
    QCOMPARE(stats->periods[0].start, QDate(2026, 10, 26)); // the week of Sunday the 1st
    QCOMPARE(stats->periods[1].start, QDate(2026, 11, 2));
    QCOMPARE(stats->periods[1].plays, 4);
    QCOMPARE(stats->periods[2].start, QDate(2026, 11, 9));

    query.periodSize = domain::ListeningPeriodSize::Month;
    query.nowMs = at(12, 12); // two days after the last: no current streak
    stats = fixture.stats(query);
    QVERIFY(stats);
    QCOMPARE(stats->periods.size(), 1);
    QCOMPARE(stats->periods[0].start, QDate(2026, 11, 1));
    QCOMPARE(stats->streak.current, 0);
}

void ListeningStatsTest::songDetails()
{
    QTemporaryDir dir;
    Fixture fixture(dir.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(fixture.ok);
    QCOMPARE(fixture.save({record(QStringLiteral("1"), at(3, 9), 40000),
                           record(QStringLiteral("2"), at(3, 10), 40000),
                           record(QStringLiteral("3"), at(3, 11), 40000)}),
             StorageError::None);
    ListeningStateJob missing;
    missing.operation = ListeningStateOperation::MissingSongs;
    auto result = fixture.run(missing);
    QCOMPARE(result.error, StorageError::None);
    QCOMPARE(result.missingSongs,
             (QList<domain::SongId>{
                 {QStringLiteral("3")}, {QStringLiteral("2")}, {QStringLiteral("1")}}));

    // A song nobody heard is not stored; one the service lacks is marked, and never
    // replaces details already there.
    QCOMPARE(
        fixture.saveSongs({song(QStringLiteral("3"), QStringLiteral("Three"),
                                {artist(QStringLiteral("10"), QStringLiteral("A"))}, {}, 200000),
                           song(QStringLiteral("9"), QStringLiteral("Nine"), {}, {}, 1000)},
                          {{QStringLiteral("2")}}),
        StorageError::None);
    QCOMPARE(fixture.saveSongs({}, {{QStringLiteral("3")}}), StorageError::None);
    result = fixture.run(missing);
    QCOMPARE(result.missingSongs, (QList<domain::SongId>{{QStringLiteral("1")}}));
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT count(*) FROM listening_song")),
             QStringLiteral("2"));
    const auto stats = fixture.stats({});
    QVERIFY(stats);
    QCOMPARE(stats->songsWithoutDetails, 1);
    for (const auto& row : stats->songs) {
        QCOMPARE(row.known, row.song.id.value == QStringLiteral("3"));
        if (row.known)
            QCOMPARE(row.song.title, QStringLiteral("Three"));
    }

    ListeningStateJob clear;
    clear.operation = ListeningStateOperation::ClearHistory;
    QCOMPARE(fixture.run(clear).error, StorageError::None);
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT count(*) FROM listening_song")),
             QStringLiteral("0"));
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT count(*) FROM listening_song_artist")),
             QStringLiteral("0"));
}

void ListeningStatsTest::rejectsBadQueries()
{
    QTemporaryDir dir;
    Fixture fixture(dir.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(fixture.ok);
    domain::ListeningStatsQuery backwards;
    backwards.fromMs = at(5, 0);
    backwards.toMs = at(4, 0);
    QVERIFY(!fixture.stats(backwards));
    domain::ListeningStatsQuery tooMany;
    tooMany.limit = 101;
    QVERIFY(!fixture.stats(tooMany));
    const auto empty = fixture.stats({});
    QVERIFY(empty);
    QCOMPARE(empty->totals.records, 0);
    QVERIFY(!empty->firstRecordAtMs);
    QCOMPARE(empty->streak.current, 0);
}

QTEST_GUILESS_MAIN(ListeningStatsTest)
#include "listening_stats_test.moc"

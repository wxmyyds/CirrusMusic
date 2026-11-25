#include "fake_sources.h"

#include "fake_controls.h"
#include "formatter.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <random>

namespace cirrus::ui
{

namespace
{
// Embedded by the Cirrus.Samples module.
const QString kSamples = QStringLiteral(":/qt/qml/Cirrus/Samples/");

// The fake users: commenters ("u-N"), repliers ("r-N") and fans ("fan-N"), each with a
// user page (FakeCatalog::user).
const char* const kUserNames[] = {"海边的旧仓库", "不睡觉的猫", "Mori",       "通勤三号线",
                                  "晚风收藏家",   "阿柚",       "Echo_42",    "听歌的土豆",
                                  "雨天备忘录",   "北纬三十度", "一颗小行星", "Lumen 的歌迷",
                                  "夜班列车长",   "Kai",        "半杯冰美式"};
const char* const kReplyNames[] = {"晚风收藏家", "Mori", "阿柚", "Kai", "半杯冰美式"};
const char* const kUserAvatars[] = {"sunset", "ocean", "forest", "neon", ""};
const QString kFakeAccountKey = QStringLiteral("fake-account");

// Replies after the controlled delay, unless canceled or `context` is gone first.
// The failure to inject is decided when the request starts; requests that pages don't
// show as states (search suggestions, trending keywords) leave it for the next one.
template <class T>
Request replyLater(QObject* context, FakeControls* controls, Reply<T> reply,
                   std::function<Outcome<T>()> produce, bool injectFailures = true)
{
    auto alive = std::make_shared<bool>(true);
    const int failure = injectFailures ? controls->takeFailure() : -1;
    QTimer::singleShot(
        controls->nextDelay(), context,
        [alive, failure, controls, reply = std::move(reply), produce = std::move(produce)]() {
            if (!*alive)
                return;
            *alive = false;
            Outcome<T> outcome;
            if (failure >= 0) {
                outcome.failure = Failure{failure::Kind(failure), std::nullopt};
                if (failure == failure::RateLimited)
                    outcome.failure->retryAfterMs = 3000;
            } else {
                outcome = produce();
                outcome.partial = outcome.partial || controls->partial();
            }
            reply(std::move(outcome));
        });
    return Request([alive]() { *alive = false; });
}

// "covers/sunset.jpg" -> "sunset"; the fake cover key is the sample cover name.
QString coverKey(const QJsonValue& path)
{
    return path.isString() ? QFileInfo(path.toString()).baseName() : QString();
}

QStringList strings(const QJsonValue& array)
{
    QStringList list;
    for (const QJsonValue& value : array.toArray())
        list << value.toString();
    return list;
}

// What the samples leave out of an album (type, release day, label, notes,
// comments), made up from its place in the catalog so it stays the same.
void describe(AlbumInfo& info, int index, int songCount)
{
    static const QStringList labels = {QStringLiteral("潮汐唱片"), QStringLiteral("Cirrus Records"),
                                       QStringLiteral("独立发行")};
    info.type = songCount <= 2 ? QStringLiteral("EP") : QStringLiteral("专辑");
    if (info.year > 0)
        info.publishedAtMs = QDate(info.year, 1 + index * 5 % 12, 1 + index * 7 % 28)
                                 .startOfDay()
                                 .toMSecsSinceEpoch();
    // The last sample (the long-titled demo album) has no label and no comments.
    if (info.key == u"al-13")
        return;
    info.company = labels.at(index % labels.size());
    info.commentCount = 12 + index * 37;
    if (info.key == u"al-08")
        info.aliases = {QStringLiteral("Caution")};
    if (index % 3 == 0)
        info.description =
            QObject::tr("「%1」是一张虚构的示例专辑，用来检查专辑页的介绍：一行放不下时出现"
                        "「更多」，点开在对话框里读全文。录音在一间靠海的旧仓库完成，"
                        "混音保留了房间本身的回声。")
                .arg(info.title);
}

// The sample catalog, read once and shared by the fakes.
struct Samples {
    QHash<QString, SongInfo> songs;
    QHash<QString, PlaylistPage> playlists;
    QHash<QString, AlbumPage> albums;
    QHash<QString, ArtistInfo> artists;
    QHash<QString, PodcastInfo> podcasts;
    // A podcast's voices, newest first; and every voice by key.
    QHash<QString, QStringList> podcastVoices;
    QHash<QString, SongInfo> voices;
    // In catalog order.
    QStringList songOrder;
    QStringList playlistOrder;
    QStringList albumOrder;
    QStringList artistOrder;
    QStringList podcastOrder;
    // By artist key, in catalog order.
    QHash<QString, QStringList> artistSongs;
    QHash<QString, QStringList> artistAlbums;

    // A sample song by key. The long sample playlist repeats the samples under keys
    // like "so-001~123"; those resolve to the sample with the key kept.
    [[nodiscard]] SongInfo song(const QString& key) const
    {
        if (voices.contains(key))
            return voices[key];
        SongInfo info =
            songs.value(key.section(QLatin1Char('~'), 0, 0), SongInfo{.key = key, .title = key});
        info.key = key;
        return info;
    }

    [[nodiscard]] CollectionItem playlistItem(const QString& key) const
    {
        const PlaylistPage& page = playlists[key];
        return {page.info.key,
                collection::Playlist,
                page.info.title,
                page.info.creator,
                0,
                int(page.songKeys.size()),
                page.info.coverKey};
    }
    [[nodiscard]] CollectionItem albumItem(const QString& key) const
    {
        const AlbumPage& page = albums[key];
        QStringList names;
        for (const ArtistRef& artist : page.info.artists)
            names << artist.name;
        return {page.info.key,     collection::Album,
                page.info.title,   names.join(QStringLiteral(" / ")),
                page.info.year,    int(page.songKeys.size()),
                page.info.coverKey};
    }
    [[nodiscard]] CollectionItem artistItem(const QString& key) const
    {
        const ArtistInfo& info = artists[key];
        return {info.key, collection::Artist, info.name, {}, 0, info.albumCount, info.avatarKey};
    }
    [[nodiscard]] CollectionItem podcastItem(const QString& key) const
    {
        const PodcastInfo& info = podcasts[key];
        return {info.key, collection::Podcast, info.title, {}, 0, info.voiceCount, info.coverKey};
    }
};

// What the signed-in account likes, created and saved, and its edits to its own
// playlists; the catalog and the library share it, so a change shows up everywhere.
struct FakeAccount {
    // The account's "我喜欢的音乐": its songs are `liked`.
    static inline const QString kLikedPlaylist = QStringLiteral("pl-liked");
    static inline const QString kNickname = QStringLiteral("雾港听众");

    QStringList created{kLikedPlaylist, QStringLiteral("pl-01"), QStringLiteral("pl-05")};
    QStringList saved{QStringLiteral("pl-02"), QStringLiteral("pl-03"), QStringLiteral("pl-04"),
                      QStringLiteral("pl-long")};
    QStringList savedAlbums{QStringLiteral("al-01"), QStringLiteral("al-03"),
                            QStringLiteral("al-06"), QStringLiteral("al-13")};
    QStringList savedPodcasts{QStringLiteral("pc-02")};
    QStringList followedArtists{QStringLiteral("ar-01"), QStringLiteral("ar-04"),
                                QStringLiteral("ar-11")};
    QStringList liked{QStringLiteral("so-001"), QStringLiteral("so-005"), QStringLiteral("so-011"),
                      QStringLiteral("so-017"), QStringLiteral("so-023"), QStringLiteral("so-031"),
                      QStringLiteral("so-034"), QStringLiteral("so-008")};

    struct Edit {
        std::optional<QString> title;
        std::optional<QString> description;
        std::optional<QStringList> songKeys;
    };
    QHash<QString, Edit> edits;
    // Playlists created in this run, by key: their titles.
    QHash<QString, QString> made;
    // Users followed (true) or unfollowed in this run.
    QHash<QString, bool> userFollows;

    // Playlists that only the account has (not in the samples).
    [[nodiscard]] bool accountOnly(const QString& key) const
    {
        return key == kLikedPlaylist || made.contains(key);
    }
    [[nodiscard]] QStringList songKeys(const QString& key, const Samples& samples) const
    {
        if (key == kLikedPlaylist)
            return liked;
        const Edit edit = edits.value(key);
        return edit.songKeys ? *edit.songKeys : samples.playlists.value(key).songKeys;
    }
    void setSongKeys(const QString& key, const QStringList& keys)
    {
        if (key == kLikedPlaylist)
            liked = keys;
        else
            edits[key].songKeys = keys;
    }
    // A playlist only the account has, before edits.
    [[nodiscard]] PlaylistPage accountPlaylist(const QString& key, const Samples& samples) const
    {
        PlaylistPage page;
        page.info.key = key;
        page.info.liked = key == kLikedPlaylist;
        page.info.title =
            page.info.liked ? kNickname + QStringLiteral("喜欢的音乐") : made.value(key);
        page.info.creator = kNickname;
        page.info.creatorKey = kFakeAccountKey;
        page.info.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
        // The service counts plays and saves for every playlist, the account's own too.
        page.info.playCount = page.info.liked ? 1'286 : 0;
        page.info.subscriberCount = 0;
        page.info.commentCount = 0;
        page.songKeys = songKeys(key, samples);
        for (const QString& song : std::as_const(page.songKeys))
            page.songs << samples.song(song);
        if (!page.songs.isEmpty())
            page.info.coverKey = page.songs.constFirst().coverKey;
        return page;
    }
    void apply(PlaylistPage& page, const Samples& samples) const
    {
        const auto edit = edits.constFind(page.info.key);
        if (edit == edits.cend())
            return;
        if (edit->title)
            page.info.title = *edit->title;
        if (edit->description)
            page.info.description = *edit->description;
        if (edit->songKeys) {
            page.songKeys = *edit->songKeys;
            page.songs.clear();
            for (const QString& key : std::as_const(page.songKeys))
                page.songs << samples.song(key);
        }
    }
    [[nodiscard]] CollectionItem playlistItem(const QString& key, const Samples& samples) const
    {
        CollectionItem item;
        if (accountOnly(key)) {
            const PlaylistPage page = accountPlaylist(key, samples);
            item = {key, collection::Playlist,      page.info.title,   page.info.creator,
                    0,   int(page.songKeys.size()), page.info.coverKey};
        } else {
            item = samples.playlistItem(key);
        }
        const Edit edit = edits.value(key);
        if (edit.title)
            item.title = *edit.title;
        if (edit.songKeys)
            item.count = int(edit.songKeys->size());
        return item;
    }
};

// Header details the sample catalog leaves out: tags, counts and dates made up from
// the key, so a playlist always shows the same ones.
void fillPlaylistDetails(PlaylistInfo& info)
{
    static const QHash<QString, QStringList> tags{
        {QStringLiteral("pl-01"),
         {QStringLiteral("华语"), QStringLiteral("夜晚"), QStringLiteral("安静")}},
        {QStringLiteral("pl-02"), {QStringLiteral("轻音乐"), QStringLiteral("放松")}},
        {QStringLiteral("pl-03"), {QStringLiteral("华语"), QStringLiteral("独立")}},
        {QStringLiteral("pl-04"), {QStringLiteral("运动"), QStringLiteral("欧美")}},
    };
    const qint64 seed = std::accumulate(info.key.cbegin(), info.key.cend(), qint64(7),
                                        [](qint64 sum, QChar c) { return sum * 31 + c.unicode(); });
    // Mixed, so keys that differ in one letter get far apart numbers.
    const qint64 n = qint64((quint64(seed) * 0x9E37'79B9'7F4A'7C15ull) >> 24);
    info.tags = tags.value(info.key);
    info.playCount = 2'000 + n % 1'500'000;
    info.subscriberCount = 40 + n % 9'000;
    info.commentCount = n % 400;
    info.updatedAtMs = QDateTime::currentDateTime().addDays(-(n % 40)).toMSecsSinceEpoch();
}

std::shared_ptr<const Samples> loadSamples()
{
    QFile file(kSamples + QStringLiteral("catalog.json"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("fake catalog: %s is missing; link Cirrus.Samples", qPrintable(file.fileName()));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    auto samples = std::make_shared<Samples>();

    QHash<QString, QString> artistNames;
    for (const QJsonValue& artist : root.value(QLatin1StringView("artists")).toArray())
        artistNames.insert(artist[QLatin1StringView("id")].toString(),
                           artist[QLatin1StringView("name")].toString());
    QHash<QString, QJsonObject> albums;
    for (const QJsonValue& album : root.value(QLatin1StringView("albums")).toArray())
        albums.insert(album[QLatin1StringView("id")].toString(), album.toObject());
    for (const QJsonValue& artist : root.value(QLatin1StringView("artists")).toArray()) {
        const QString key = artist[QLatin1StringView("id")].toString();
        samples->artists.insert(key, ArtistInfo{.key = key, .name = artistNames.value(key)});
        samples->artistOrder << key;
    }

    for (const QJsonValue& value : root.value(QLatin1StringView("songs")).toArray()) {
        const QJsonObject song = value.toObject();
        SongInfo info;
        info.key = song.value(QLatin1StringView("id")).toString();
        info.title = song.value(QLatin1StringView("title")).toString();
        for (const QString& id : strings(song.value(QLatin1StringView("artistIds"))))
            info.artists << ArtistRef{id, artistNames.value(id)};
        info.albumKey = song.value(QLatin1StringView("albumId")).toString();
        const QJsonObject album = albums.value(info.albumKey);
        info.albumTitle = album.value(QLatin1StringView("title")).toString();
        info.coverKey = coverKey(album.value(QLatin1StringView("cover")));
        const QJsonValue duration = song.value(QLatin1StringView("durationMs"));
        info.durationMs = duration.isDouble() ? qint64(duration.toDouble()) : -1;
        info.unavailable = !song.value(QLatin1StringView("playable")).toBool(true);
        samples->songs.insert(info.key, info);
        samples->songOrder << info.key;
        for (const ArtistRef& artist : std::as_const(info.artists))
            samples->artistSongs[artist.key] << info.key;
    }

    for (const QJsonValue& value : root.value(QLatin1StringView("albums")).toArray()) {
        const QJsonObject album = value.toObject();
        AlbumPage page;
        page.info.key = album.value(QLatin1StringView("id")).toString();
        page.info.title = album.value(QLatin1StringView("title")).toString();
        for (const QString& id : strings(album.value(QLatin1StringView("artistIds")))) {
            page.info.artists << ArtistRef{id, artistNames.value(id)};
            samples->artistAlbums[id] << page.info.key;
        }
        page.info.year = album.value(QLatin1StringView("year")).toInt();
        page.info.coverKey = coverKey(album.value(QLatin1StringView("cover")));
        page.songKeys = strings(album.value(QLatin1StringView("songIds")));
        for (const QString& key : std::as_const(page.songKeys))
            page.songs << samples->songs.value(key);
        describe(page.info, int(samples->albumOrder.size()), int(page.songKeys.size()));
        samples->albums.insert(page.info.key, page);
        samples->albumOrder << page.info.key;
    }

    // The samples have no artist pictures or bios: avatars stay empty (placeholder).
    for (ArtistInfo& artist : samples->artists) {
        artist.songCount = int(samples->artistSongs.value(artist.key).size());
        artist.albumCount = int(samples->artistAlbums.value(artist.key).size());
    }

    for (const QJsonValue& value : root.value(QLatin1StringView("playlists")).toArray()) {
        const QJsonObject playlist = value.toObject();
        PlaylistPage page;
        page.info.key = playlist.value(QLatin1StringView("id")).toString();
        page.info.title = playlist.value(QLatin1StringView("title")).toString();
        page.info.description = playlist.value(QLatin1StringView("description")).toString();
        page.info.creator = playlist.value(QLatin1StringView("creator")).toString();
        // A commenter's page stands in for the creator's.
        page.info.creatorKey =
            QStringLiteral("u-%1").arg(qHash(page.info.key) % std::size(kUserNames));
        page.info.coverKey = coverKey(playlist.value(QLatin1StringView("cover")));
        fillPlaylistDetails(page.info);
        page.songKeys = strings(playlist.value(QLatin1StringView("songIds")));
        for (const QString& key : std::as_const(page.songKeys))
            page.songs << samples->songs.value(key);
        samples->playlists.insert(page.info.key, page);
        samples->playlistOrder << page.info.key;
    }

    // A long playlist for paged loading: the samples over and over, details for the
    // first batch only; the rest are fetched as they scroll into view.
    {
        PlaylistPage page;
        page.info.key = QStringLiteral("pl-long");
        page.info.title = QStringLiteral("很长的歌单");
        page.info.description = QStringLiteral("600 首，用来检查长歌单的按页加载。");
        page.info.creator = QStringLiteral("Cirrus 示例");
        page.info.coverKey = QStringLiteral("ocean");
        fillPlaylistDetails(page.info);
        for (int i = 0; i < 600; ++i)
            page.songKeys << samples->songOrder.at(i % samples->songOrder.size()) +
                                 QLatin1Char('~') + QString::number(i);
        for (const QString& key : page.songKeys.mid(0, kSongDetailsBatch))
            page.songs << samples->song(key);
        samples->playlists.insert(page.info.key, page);
        samples->playlistOrder << page.info.key;
    }

    // Podcasts are not in the sample catalog: a few made up here, the first one long
    // enough for paged loading. Voices are numbered, newest first, 18 to 58 minutes.
    {
        const struct {
            const char* title;
            const char* cover;
            int voices;
            const char* topic;
        } made[] = {
            {"夜航电台", "galaxy", 120, "深夜的歌和故事"},
            {"雾港来信", "ocean", 18, "听众来信与回信"},
            {"慢慢说唱片", "crimson", 12, "一张唱片的前前后后"},
            {"森林录音笔记", "forest", 9, "野外录音和声音设计"},
            {"城市漫步", "neon", 6, "边走边聊城市里的声音"},
        };
        for (int p = 0; p < int(std::size(made)); ++p) {
            PodcastInfo info;
            info.key = QStringLiteral("pc-%1").arg(p + 1, 2, 10, QLatin1Char('0'));
            info.title = QString::fromUtf8(made[p].title);
            info.coverKey = QString::fromLatin1(made[p].cover);
            info.voiceCount = made[p].voices;
            info.description = QObject::tr("一档虚构的示例播客：%1。每周更新，用来检查播客页和"
                                           "声音的列表。")
                                   .arg(QString::fromUtf8(made[p].topic));
            QStringList keys;
            for (int n = made[p].voices; n >= 1; --n) {
                SongInfo voice;
                voice.key = QStringLiteral("vo-%1-%2")
                                .arg(p + 1, 2, 10, QLatin1Char('0'))
                                .arg(n, 3, 10, QLatin1Char('0'));
                voice.title =
                    QObject::tr("第 %1 期：%2").arg(n).arg(QString::fromUtf8(made[p].topic));
                voice.albumKey = info.key;
                voice.albumTitle = info.title;
                voice.coverKey = info.coverKey;
                voice.voice = true;
                voice.serial = n;
                voice.durationMs = qint64(18 + (n * 7 + p * 5) % 41) * 60'000;
                samples->voices.insert(voice.key, voice);
                keys << voice.key;
            }
            samples->podcasts.insert(info.key, info);
            samples->podcastVoices.insert(info.key, keys);
            samples->podcastOrder << info.key;
        }
    }
    return samples;
}

// One page of `all`, with its total.
template <class T> ResultPage<T> slice(const QList<T>& all, PageRequest page)
{
    ResultPage<T> result;
    result.items = all.mid(page.offset, page.limit);
    result.hasMore = page.offset + page.limit < all.size();
    result.total = all.size();
    return result;
}

class FakeCatalog : public QObject, public CatalogSource
{
  public:
    FakeCatalog(FakeControls* controls, std::shared_ptr<const Samples> samples,
                std::shared_ptr<const FakeAccount> account)
        : m_controls(controls), m_samples(std::move(samples)), m_account(std::move(account))
    {
        // Charts are playlists of their own, made from the sample songs.
        const struct {
            const char* key;
            const char* title;
            const char* update;
            const char* cover;
        } charts[] = {{"ch-soar", QT_TR_NOOP("飙升榜"), QT_TR_NOOP("每天更新"), "crimson"},
                      {"ch-new", QT_TR_NOOP("新歌榜"), QT_TR_NOOP("每天更新"), "ocean"},
                      {"ch-original", QT_TR_NOOP("原创榜"), QT_TR_NOOP("每周四更新"), "forest"},
                      {"ch-hot", QT_TR_NOOP("热歌榜"), QT_TR_NOOP("每周四更新"), "sunset"},
                      // Without previews, as the service gives the rest.
                      {"ch-rap", QT_TR_NOOP("说唱榜"), QT_TR_NOOP("每周五更新"), "mono"},
                      {"ch-electronic", QT_TR_NOOP("电音榜"), QT_TR_NOOP("每周五更新"), "neon"},
                      {"ch-classical", QT_TR_NOOP("古典榜"), QT_TR_NOOP("每周一更新"), "minimal"},
                      {"ch-acg", QT_TR_NOOP("ACG 榜"), QT_TR_NOOP("每周三更新"), "pastel"},
                      {"ch-western", QT_TR_NOOP("欧美热歌榜"), QT_TR_NOOP("每周日更新"), "galaxy"},
                      {"ch-japanese", QT_TR_NOOP("日语榜"), QT_TR_NOOP("每周二更新"), "stripes"},
                      {"ch-korean", QT_TR_NOOP("韩语榜"), QT_TR_NOOP("每周二更新"), "hazard"},
                      {"ch-folk", QT_TR_NOOP("国风榜"), "", "earth"}};
        std::mt19937 random(7);
        for (const auto& chart : charts) {
            QStringList keys = m_samples->songOrder;
            std::shuffle(keys.begin(), keys.end(), random);
            keys = keys.mid(0, kChartSongs);
            PlaylistPage page;
            page.info = {QString::fromLatin1(chart.key), tr(chart.title), tr(chart.update),
                         tr("Cirrus 示例"), QString::fromLatin1(chart.cover)};
            fillPlaylistDetails(page.info);
            page.info.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
            page.songKeys = keys;
            for (const QString& key : std::as_const(keys))
                page.songs << m_samples->songs[key];
            m_charts << page;
        }
    }

    Request playlist(const QString& key, Reply<PlaylistPage> reply) override
    {
        return replyLater<PlaylistPage>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<PlaylistPage> outcome;
            const auto chart =
                std::find_if(m_charts.cbegin(), m_charts.cend(),
                             [&](const PlaylistPage& c) { return c.info.key == key; });
            const auto found = m_samples->playlists.constFind(key);
            const bool accountOnly = m_account->accountOnly(key) && m_controls->signedIn();
            if (chart == m_charts.cend() && found == m_samples->playlists.cend() && !accountOnly) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            PlaylistPage page = accountOnly ? m_account->accountPlaylist(key, *m_samples)
                                : chart != m_charts.cend() ? *chart
                                                           : *found;
            m_account->apply(page, *m_samples);
            if (m_controls->signedIn()) {
                page.info.own = m_account->created.contains(key);
                page.info.subscribed = m_account->saved.contains(key);
            }
            if (m_controls->empty()) {
                page.songKeys.clear();
                page.songs.clear();
            }
            outcome.value = page;
            return outcome;
        });
    }

    Request songDetails(const QStringList& keys, Reply<QList<SongInfo>> reply) override
    {
        return replyLater<QList<SongInfo>>(this, m_controls, std::move(reply), [this, keys]() {
            Outcome<QList<SongInfo>> outcome;
            QList<SongInfo> songs;
            for (const QString& key : keys.mid(0, kSongDetailsBatch)) {
                // Every 97th song of the long playlist has no details (removed by the
                // service), for the "missing" rows.
                if (key.section(QLatin1Char('~'), 1).toInt() % 97 == 96)
                    continue;
                songs << m_samples->song(key);
            }
            outcome.value = songs;
            return outcome;
        });
    }

    Request album(const QString& key, Reply<AlbumPage> reply) override
    {
        return replyLater<AlbumPage>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<AlbumPage> outcome;
            const auto found = m_samples->albums.constFind(key);
            if (found == m_samples->albums.cend()) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            AlbumPage page = *found;
            page.info.subscribed = m_controls->signedIn() && m_account->savedAlbums.contains(key);
            if (m_controls->empty()) {
                page.songKeys.clear();
                page.songs.clear();
            }
            outcome.value = page;
            return outcome;
        });
    }

    Request artist(const QString& key, Reply<ArtistPage> reply) override
    {
        return replyLater<ArtistPage>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<ArtistPage> outcome;
            const auto found = m_samples->artists.constFind(key);
            if (found == m_samples->artists.cend()) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            ArtistPage page;
            page.info = *found;
            page.info.description = introductionOf(key).brief;
            page.info.aliases = aliasesOf(key);
            page.info.followed = m_controls->signedIn() && m_account->followedArtists.contains(key);
            if (!m_controls->empty()) {
                const QList<SongInfo> songs = songsOf(key);
                page.info.songCount = int(songs.size());
                page.topSongs = songs.mid(0, kTopSongs);
            }
            outcome.value = page;
            return outcome;
        });
    }

    Request artistSongs(const QString& key, bool byTime, PageRequest page,
                        Reply<ResultPage<SongInfo>> reply) override
    {
        return replyLater<ResultPage<SongInfo>>(
            this, m_controls, std::move(reply), [this, key, byTime, page]() {
                QList<SongInfo> songs = m_controls->empty() ? QList<SongInfo>() : songsOf(key);
                // The samples have no release dates: newest first is the list reversed.
                if (byTime)
                    std::reverse(songs.begin(), songs.end());
                return Outcome<ResultPage<SongInfo>>{slice(songs, page)};
            });
    }

    Request artistAlbums(const QString& key, PageRequest page,
                         Reply<ResultPage<CollectionItem>> reply) override
    {
        return replyLater<ResultPage<CollectionItem>>(
            this, m_controls, std::move(reply), [this, key, page]() {
                QList<CollectionItem> albums;
                if (!m_controls->empty()) {
                    for (const QString& album : m_samples->artistAlbums.value(key))
                        albums << m_samples->albumItem(album);
                    // Newest first.
                    std::stable_sort(albums.begin(), albums.end(),
                                     [](const CollectionItem& a, const CollectionItem& b) {
                                         return a.year > b.year;
                                     });
                }
                return Outcome<ResultPage<CollectionItem>>{slice(albums, page)};
            });
    }

    Request artistIntroduction(const QString& key, Reply<ArtistIntroduction> reply) override
    {
        return replyLater<ArtistIntroduction>(this, m_controls, std::move(reply), [this, key]() {
            return Outcome<ArtistIntroduction>{introductionOf(key)};
        });
    }

    // The song page has no fake data (the gallery shows design factors, not features).
    Request songWiki(const SongInfo&, Reply<SongWiki> reply) override
    {
        return replyLater<SongWiki>(this, m_controls, std::move(reply),
                                    []() { return Outcome<SongWiki>{SongWiki{}}; });
    }
    Request similarSongs(const QString&, Reply<QList<SongInfo>> reply) override
    {
        return replyLater<QList<SongInfo>>(this, m_controls, std::move(reply), []() {
            return Outcome<QList<SongInfo>>{QList<SongInfo>{}};
        });
    }
    Request similarPlaylists(const QString&, Reply<QList<CollectionItem>> reply) override
    {
        return replyLater<QList<CollectionItem>>(this, m_controls, std::move(reply), []() {
            return Outcome<QList<CollectionItem>>{QList<CollectionItem>{}};
        });
    }

    Request similarArtists(const QString& key, Reply<QList<CollectionItem>> reply) override
    {
        return replyLater<QList<CollectionItem>>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<QList<CollectionItem>> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            QList<CollectionItem> artists;
            if (!m_controls->empty()) {
                const QStringList& all = m_samples->artistOrder;
                const qsizetype start = all.indexOf(key) + 1;
                for (qsizetype i = 0; i < all.size() && artists.size() < kSimilarArtists; ++i) {
                    const QString& other = all.at((start + i) % all.size());
                    if (other != key)
                        artists << m_samples->artistItem(other);
                }
            }
            outcome.value = artists;
            return outcome;
        });
    }

    Request suggestions(const QString& text, Reply<QList<SearchSuggestion>> reply) override
    {
        return replyLater<QList<SearchSuggestion>>(
            this, m_controls, std::move(reply),
            [this, text]() {
                // Grouped like the service's: songs, artists, albums, playlists.
                QList<SearchSuggestion> found;
                const auto offer = [&](const QStringList& keys, auto title, bool song,
                                       collection::Kind kind) {
                    int taken = 0;
                    for (const QString& key : keys) {
                        const QString name = title(key);
                        if (taken < kSuggestions && matches(name, text)) {
                            found << SearchSuggestion{song, kind, key, name};
                            ++taken;
                        }
                    }
                };
                offer(
                    m_samples->songOrder,
                    [this](const QString& key) { return m_samples->songs[key].title; }, true,
                    collection::Playlist);
                offer(
                    m_samples->artistOrder,
                    [this](const QString& key) { return m_samples->artists[key].name; }, false,
                    collection::Artist);
                offer(
                    m_samples->albumOrder,
                    [this](const QString& key) { return m_samples->albums[key].info.title; }, false,
                    collection::Album);
                offer(
                    m_samples->playlistOrder,
                    [this](const QString& key) { return m_samples->playlists[key].info.title; },
                    false, collection::Playlist);
                return Outcome<QList<SearchSuggestion>>{found};
            },
            false);
    }

    Request hotKeywords(Reply<QList<HotKeyword>> reply) override
    {
        return replyLater<QList<HotKeyword>>(
            this, m_controls, std::move(reply),
            [this]() {
                // A fixed mix of songs and artists from the samples; some with a note.
                static const char* const notes[] = {"最近很多人在听",  "", "新歌上线", "", "",
                                                    "被收进了热门歌单"};
                QList<HotKeyword> keywords;
                int i = 0;
                for (const char* key : {"so-001", "so-003", "so-031", "so-014", "so-017", "so-019"})
                    keywords << HotKeyword{m_samples->songs[QLatin1StringView(key)].title,
                                           QString::fromUtf8(notes[i++])};
                for (const char* key : {"ar-01", "ar-04"})
                    keywords << HotKeyword{m_samples->artists[QLatin1StringView(key)].name,
                                           QObject::tr("歌手")};
                for (const char* key : {"al-02", "al-03"})
                    keywords << HotKeyword{m_samples->albums[QLatin1StringView(key)].info.title,
                                           {}};
                keywords << HotKeyword{m_samples->podcasts[QStringLiteral("pc-01")].title,
                                       QObject::tr("播客")};
                return Outcome<QList<HotKeyword>>{keywords};
            },
            false);
    }

    Request searchSongs(const QString& text, PageRequest page,
                        Reply<ResultPage<SongInfo>> reply) override
    {
        return replyLater<ResultPage<SongInfo>>(this, m_controls, std::move(reply), [=, this]() {
            QList<SongInfo> found;
            if (!m_controls->empty()) {
                for (const QString& key : m_samples->songOrder) {
                    const SongInfo& song = m_samples->songs[key];
                    bool hit = matches(song.title, text) || matches(song.albumTitle, text);
                    for (const ArtistRef& artist : song.artists)
                        hit = hit || matches(artist.name, text);
                    if (hit)
                        found << song;
                }
            }
            return Outcome<ResultPage<SongInfo>>{slice(found, page)};
        });
    }

    Request searchCollections(collection::Kind kind, const QString& text, PageRequest page,
                              Reply<ResultPage<CollectionItem>> reply) override
    {
        return replyLater<ResultPage<CollectionItem>>(
            this, m_controls, std::move(reply), [=, this]() {
                QList<CollectionItem> found;
                if (!m_controls->empty()) {
                    switch (kind) {
                    case collection::Album:
                        for (const QString& key : m_samples->albumOrder) {
                            const CollectionItem item = m_samples->albumItem(key);
                            if (matches(item.title, text) || matches(item.byline, text))
                                found << item;
                        }
                        break;
                    case collection::Artist:
                        for (const QString& key : m_samples->artistOrder)
                            if (matches(m_samples->artists[key].name, text))
                                found << m_samples->artistItem(key);
                        break;
                    case collection::Playlist:
                        for (const QString& key : m_samples->playlistOrder) {
                            const PlaylistInfo& info = m_samples->playlists[key].info;
                            if (matches(info.title, text) || matches(info.description, text) ||
                                matches(info.creator, text))
                                found << m_samples->playlistItem(key);
                        }
                        break;
                    case collection::Podcast:
                        for (const QString& key : m_samples->podcastOrder) {
                            const PodcastInfo& info = m_samples->podcasts[key];
                            if (matches(info.title, text) || matches(info.description, text))
                                found << m_samples->podcastItem(key);
                        }
                        break;
                    }
                }
                return Outcome<ResultPage<CollectionItem>>{slice(found, page)};
            });
    }

    Request searchVoices(const QString& text, PageRequest page,
                         Reply<ResultPage<SongInfo>> reply) override
    {
        return replyLater<ResultPage<SongInfo>>(this, m_controls, std::move(reply), [=, this]() {
            QList<SongInfo> found;
            if (!m_controls->empty())
                for (const QString& podcast : m_samples->podcastOrder)
                    for (const QString& key : m_samples->podcastVoices[podcast]) {
                        const SongInfo& voice = m_samples->voices[key];
                        if (matches(voice.title, text) || matches(voice.albumTitle, text))
                            found << voice;
                    }
            return Outcome<ResultPage<SongInfo>>{slice(found, page)};
        });
    }

    Request voice(const QString& key, Reply<SongInfo> reply) override
    {
        return replyLater<SongInfo>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<SongInfo> outcome;
            if (m_samples->voices.contains(key))
                outcome.value = m_samples->voices[key];
            else
                outcome.failure = Failure{failure::Rejected, std::nullopt};
            return outcome;
        });
    }
    Request podcast(const QString& key, Reply<PodcastInfo> reply) override
    {
        return replyLater<PodcastInfo>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<PodcastInfo> outcome;
            if (!m_samples->podcasts.contains(key)) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            PodcastInfo info = m_samples->podcasts[key];
            info.subscribed = m_controls->signedIn() && m_account->savedPodcasts.contains(key);
            outcome.value = info;
            return outcome;
        });
    }

    Request podcastVoices(const QString& key, bool oldestFirst, PageRequest page,
                          Reply<ResultPage<SongInfo>> reply) override
    {
        return replyLater<ResultPage<SongInfo>>(this, m_controls, std::move(reply), [=, this]() {
            QStringList keys = m_samples->podcastVoices.value(key);
            if (oldestFirst)
                std::reverse(keys.begin(), keys.end());
            QList<SongInfo> voices;
            for (const QString& voice : std::as_const(keys))
                voices << m_samples->voices[voice];
            return Outcome<ResultPage<SongInfo>>{slice(voices, page)};
        });
    }

    Request feed(Feed which, Reply<QList<FeedSection>> reply) override
    {
        return replyLater<QList<FeedSection>>(this, m_controls, std::move(reply), [=, this]() {
            QList<FeedSection> sections;
            if (m_controls->empty())
                return Outcome<QList<FeedSection>>{sections};
            const auto collections = [](const QString& title, const QList<CollectionItem>& items) {
                return FeedSection{feed::Collections, title, items, {}};
            };
            QList<CollectionItem> playlists;
            for (const QString& key : m_samples->playlistOrder)
                if (!m_samples->playlists[key].songKeys.isEmpty())
                    playlists << m_samples->playlistItem(key);
            if (which == feed::Home) {
                sections << FeedSection{feed::Shortcuts, {}, {}, {}};
                sections << collections(tr("推荐歌单"), playlists);
                FeedSection songs{feed::Songs, tr("猜你喜欢"), {}, {}};
                for (const char* key : {"so-005", "so-017", "so-031", "so-022", "so-009"})
                    songs.songs << m_samples->songs[QLatin1StringView(key)];
                sections << songs;
            } else {
                QList<CollectionItem> albums;
                for (const QString& key : m_samples->albumOrder)
                    albums << m_samples->albumItem(key);
                std::stable_sort(albums.begin(), albums.end(),
                                 [](const CollectionItem& a, const CollectionItem& b) {
                                     return a.year > b.year;
                                 });
                QList<CollectionItem> artists;
                for (const QString& key : m_samples->artistOrder)
                    artists << m_samples->artistItem(key);
                sections << collections(tr("新碟上架"), albums);
                sections << collections(tr("热门歌单"), playlists);
                sections << collections(tr("推荐歌手"), artists);
            }
            return Outcome<QList<FeedSection>>{sections};
        });
    }

    Request dailySongs(Reply<QList<SongInfo>> reply) override
    {
        return replyLater<QList<SongInfo>>(this, m_controls, std::move(reply), [this]() {
            Outcome<QList<SongInfo>> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            QList<SongInfo> songs;
            if (!m_controls->empty()) {
                songs = dailyPick(QDate::currentDate());
                // Reasons for some songs, worded like the service's.
                for (qsizetype i = 0; i < songs.size(); i += 2) {
                    SongInfo& song = songs[i];
                    if (song.artists.isEmpty())
                        continue;
                    song.reason = i % 4 == 0
                                      ? tr("根据你喜欢的%1推荐").arg(song.artists.first().name)
                                      : tr("与你最近常听的歌风格相似");
                }
            }
            outcome.value = songs;
            return outcome;
        });
    }

    // The last week, skipping one day (as if it were not kept).
    Request dailyHistoryDates(Reply<QStringList> reply) override
    {
        return replyLater<QStringList>(this, m_controls, std::move(reply), [this]() {
            Outcome<QStringList> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            QStringList dates;
            if (!m_controls->empty()) {
                const QDate today = QDate::currentDate();
                for (int days = 1; days <= kHistoryDays; ++days) {
                    if (days != 4)
                        dates << today.addDays(-days).toString(Qt::ISODate);
                }
            }
            outcome.value = dates;
            return outcome;
        });
    }

    Request dailyHistorySongs(const QString& date, Reply<QList<SongInfo>> reply) override
    {
        return replyLater<QList<SongInfo>>(this, m_controls, std::move(reply), [this, date]() {
            Outcome<QList<SongInfo>> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            const QDate day = QDate::fromString(date, Qt::ISODate);
            const qint64 age = day.daysTo(QDate::currentDate());
            if (!day.isValid() || age < 1 || age > kHistoryDays) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            outcome.value = m_controls->empty() ? QList<SongInfo>() : dailyPick(day);
            return outcome;
        });
    }

    Request shelf(feed::Shelf which, Reply<QList<CollectionItem>> reply) override
    {
        return replyLater<QList<CollectionItem>>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<QList<CollectionItem>> outcome;
            if (which == feed::DailyPlaylists && !m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            QList<CollectionItem> items;
            if (!m_controls->empty()) {
                if (which == feed::PopularArtists) {
                    for (const QString& key : m_samples->artistOrder)
                        items << m_samples->artistItem(key);
                } else if (which == feed::NewAlbums) {
                    for (const QString& key : m_samples->albumOrder)
                        items << m_samples->albumItem(key);
                    std::stable_sort(items.begin(), items.end(),
                                     [](const CollectionItem& a, const CollectionItem& b) {
                                         return a.year > b.year;
                                     });
                } else {
                    QStringList keys;
                    for (const QString& key : m_samples->playlistOrder)
                        if (!m_samples->playlists[key].songKeys.isEmpty())
                            keys << key;
                    if (which == feed::DailyPlaylists) {
                        // A different but stable order each day.
                        std::mt19937 random(QDate::currentDate().toJulianDay() + 1);
                        std::shuffle(keys.begin(), keys.end(), random);
                    }
                    for (const QString& key : std::as_const(keys))
                        items << m_samples->playlistItem(key);
                }
            }
            outcome.value = items;
            return outcome;
        });
    }

    Request charts(Reply<QList<ChartInfo>> reply) override
    {
        return replyLater<QList<ChartInfo>>(this, m_controls, std::move(reply), [this]() {
            QList<ChartInfo> charts;
            if (!m_controls->empty()) {
                for (const PlaylistPage& page : std::as_const(m_charts)) {
                    ChartInfo chart{page.info.key,
                                    page.info.title,
                                    page.info.description,
                                    page.info.coverKey,
                                    {}};
                    if (charts.size() < kOfficialCharts)
                        for (const SongInfo& song : page.songs.mid(0, kChartPreviews))
                            chart.previews
                                << ChartPreview{song.title, Formatter::artists(song.artists)};
                    charts << chart;
                }
            }
            return Outcome<QList<ChartInfo>>{charts};
        });
    }

    Request playlistCategories(Reply<PlaylistCategories> reply) override
    {
        return replyLater<PlaylistCategories>(this, m_controls, std::move(reply), []() {
            const auto list = [](std::initializer_list<const char*> names) {
                QStringList list;
                for (const char* name : names)
                    list << QString::fromUtf8(name);
                return list;
            };
            PlaylistCategories categories;
            categories.groups = {
                {tr("语种"), list({"华语", "欧美", "日语", "韩语", "粤语"})},
                {tr("风格"),
                 list({"流行", "摇滚",     "民谣", "电子",    "舞曲", "说唱", "轻音乐",    "爵士",
                       "乡村", "R&B/Soul", "古典", "民族",    "英伦", "金属", "朋克",      "蓝调",
                       "雷鬼", "世界音乐", "拉丁", "New Age", "古风", "后摇", "Bossa Nova"})},
                {tr("场景"), list({"清晨", "夜晚", "学习", "工作", "午休", "下午茶", "地铁", "驾车",
                                   "运动", "旅行", "散步", "酒吧"})},
                {tr("情感"), list({"怀旧", "清新", "浪漫", "伤感", "治愈", "放松", "孤独", "感动",
                                   "兴奋", "快乐", "安静", "思念"})},
                {tr("主题"),
                 list({"综艺", "影视原声", "ACG", "儿童", "校园", "游戏", "70后", "80后", "90后",
                       "00后", "网络歌曲", "KTV", "经典", "翻唱", "吉他", "钢琴", "器乐"})},
            };
            categories.hot = list(
                {"华语", "流行", "摇滚", "民谣", "电子", "轻音乐", "夜晚", "治愈", "欧美", "学习"});
            // Not every category has premium playlists ("夜晚" has none).
            categories.premium = list({"华语", "欧美", "流行", "摇滚", "电子", "轻音乐", "古典",
                                       "爵士", "民谣", "影视原声", "ACG"});
            return Outcome<PlaylistCategories>{categories};
        });
    }

    Request categoryPlaylists(const QString& category, bool newest, PageRequest page,
                              Reply<ResultPage<CollectionItem>> reply) override
    {
        return replyLater<ResultPage<CollectionItem>>(
            this, m_controls, std::move(reply), [this, category, newest, page]() {
                QList<CollectionItem> items = categoryItems(category);
                // The samples have no creation dates: newest first is the list reversed.
                if (newest)
                    std::reverse(items.begin(), items.end());
                return Outcome<ResultPage<CollectionItem>>{slice(items, page)};
            });
    }

    // Cursor paged like the service's: `before` is where the last page ended (0 for
    // the first); the cursor counts down, one per playlist.
    Request premiumPlaylists(const QString& category, qint64 before, int limit,
                             Reply<ResultPage<CollectionItem>> reply) override
    {
        return replyLater<ResultPage<CollectionItem>>(
            this, m_controls, std::move(reply), [this, category, before, limit]() {
                QList<CollectionItem> items = categoryItems(category);
                items = items.mid(0, int(items.size() * 2 / 3));
                const int offset = before == 0 ? 0 : int(kPremiumCursor - before);
                ResultPage<CollectionItem> result;
                result.items = items.mid(offset, limit);
                if (offset + limit < items.size())
                    result.next = kPremiumCursor - (offset + limit);
                return Outcome<ResultPage<CollectionItem>>{result};
            });
    }

    Request browseArtists(int kind, int area, const QString& initial, PageRequest page,
                          Reply<ResultPage<CollectionItem>> reply) override
    {
        return replyLater<ResultPage<CollectionItem>>(
            this, m_controls, std::move(reply), [this, kind, area, initial, page]() {
                QList<CollectionItem> items;
                if (!m_controls->empty()) {
                    for (const QString& key : m_samples->artistOrder) {
                        const ArtistTraits traits = traitsOf(key);
                        if ((kind == 0 || traits.kind == kind) &&
                            (area == 0 || traits.area == area) &&
                            (initial.isEmpty() || initial == QLatin1Char(traits.initial)))
                            items << m_samples->artistItem(key);
                    }
                }
                // No total: the service doesn't tell.
                ResultPage<CollectionItem> result = slice(items, page);
                result.total.reset();
                return Outcome<ResultPage<CollectionItem>>{result};
            });
    }

    Request newAlbums(int area, PageRequest page, Reply<ResultPage<CollectionItem>> reply) override
    {
        return replyLater<ResultPage<CollectionItem>>(
            this, m_controls, std::move(reply), [this, area, page]() {
                QList<CollectionItem> items = albumsIn(area);
                std::stable_sort(items.begin(), items.end(),
                                 [](const CollectionItem& a, const CollectionItem& b) {
                                     return a.year > b.year;
                                 });
                return Outcome<ResultPage<CollectionItem>>{slice(items, page)};
            });
    }

    Request albumChart(int area, bool monthly, Reply<QList<CollectionItem>> reply) override
    {
        return replyLater<QList<CollectionItem>>(
            this, m_controls, std::move(reply), [this, area, monthly]() {
                QList<CollectionItem> items = albumsIn(area);
                // A different but stable order for the week and the month.
                std::mt19937 random(monthly ? 31 : 7);
                std::shuffle(items.begin(), items.end(), random);
                return Outcome<QList<CollectionItem>>{items};
            });
    }

    // Any key is a user: the account, a commenter, replier or fan. "u-13" (Kai) is a
    // musician; "u-4" hides the ranking, "u-6" the follows and fans.
    Request user(const QString& key, Reply<UserInfo> reply) override
    {
        return replyLater<UserInfo>(this, m_controls, std::move(reply),
                                    [this, key]() { return Outcome<UserInfo>{userOf(key)}; });
    }

    Request userPlaylists(const QString& key, Reply<QList<CollectionItem>> reply) override
    {
        return replyLater<QList<CollectionItem>>(this, m_controls, std::move(reply), [this, key]() {
            QList<CollectionItem> items;
            if (m_controls->empty())
                return Outcome<QList<CollectionItem>>{items};
            if (key == kFakeAccountKey) {
                for (const QString& playlist : m_account->created) {
                    items << m_account->playlistItem(playlist, *m_samples);
                    items.last().created = true;
                }
                for (const QString& playlist : m_account->saved) {
                    items << m_samples->playlistItem(playlist);
                    items.last().created = false;
                }
                return Outcome<QList<CollectionItem>>{items};
            }
            const QStringList& all = m_samples->playlistOrder;
            const auto seed = userSeed(key);
            const int created = int(seed % 4), saved = int((seed >> 8) % 6);
            for (int i = 0; i < created + saved && !all.isEmpty(); ++i) {
                const auto pick = ((seed >> 16) + size_t(i)) % size_t(all.size());
                items << m_samples->playlistItem(all.at(qsizetype(pick)));
                items.last().created = i < created;
            }
            return Outcome<QList<CollectionItem>>{items};
        });
    }

    Request userRanking(const QString& key, bool allTime, Reply<UserRanking> reply) override
    {
        return replyLater<UserRanking>(this, m_controls, std::move(reply), [=, this]() {
            UserRanking ranking;
            ranking.hidden = !userOf(key).rankingOpen;
            if (ranking.hidden || m_controls->empty())
                return Outcome<UserRanking>{ranking};
            QStringList keys = m_samples->songOrder;
            std::mt19937 random(quint32(userSeed(key)) + (allTime ? 1 : 0));
            std::shuffle(keys.begin(), keys.end(), random);
            for (const QString& song : keys.mid(0, allTime ? 30 : 10))
                ranking.songs << m_samples->songs[song];
            return Outcome<UserRanking>{ranking};
        });
    }

    Request userFollows(const QString& key, PageRequest page, Reply<UserList> reply) override
    {
        return replyLater<UserList>(this, m_controls, std::move(reply), [=, this]() {
            UserList list;
            list.hidden = key == QStringLiteral("u-6");
            if (list.hidden)
                return Outcome<UserList>{list};
            QList<UserItem> all;
            if (!m_controls->empty())
                for (int i = 0; i < int(std::size(kUserNames)); ++i)
                    if (QStringLiteral("u-%1").arg(i) != key)
                        all << itemOf(userOf(QStringLiteral("u-%1").arg(i)));
            list.page = slice(all, page);
            return Outcome<UserList>{list};
        });
    }

    Request userFans(const QString& key, qint64 before, int limit, Reply<UserList> reply) override
    {
        return replyLater<UserList>(this, m_controls, std::move(reply), [=, this]() {
            UserList list;
            list.hidden = key == QStringLiteral("u-6");
            if (list.hidden)
                return Outcome<UserList>{list};
            // Followed a day apart, newest first; the cursor is the last one's time.
            const int count = m_controls->empty() ? 0 : int(userOf(key).fans);
            const int offset = before == 0 ? 0 : int((kFanEpoch - before) / kFanStep) + 1;
            for (int i = offset; i < std::min(count, offset + limit); ++i)
                list.page.items << itemOf(userOf(QStringLiteral("fan-%1").arg(i)));
            list.page.total = count;
            list.page.hasMore = offset + limit < count;
            if (!list.page.items.isEmpty() && *list.page.hasMore)
                list.page.next = kFanEpoch - qint64(offset + list.page.items.size() - 1) * kFanStep;
            return Outcome<UserList>{list};
        });
    }

  private:
    static constexpr qint64 kFanEpoch = 1'780'000'000'000;
    static constexpr qint64 kFanStep = 86'400'000;

    [[nodiscard]] static quint64 userSeed(const QString& key)
    {
        quint64 seed = 11;
        for (QChar c : key)
            seed = seed * 131 + c.unicode();
        return seed * 0x9E37'79B9'7F4A'7C15ull >> 8;
    }

    [[nodiscard]] UserInfo userOf(const QString& key) const
    {
        UserInfo info;
        info.key = key;
        const auto seed = userSeed(key);
        const int index = key.section(QLatin1Char('-'), 1).toInt();
        if (key == kFakeAccountKey) {
            info.nickname = FakeAccount::kNickname;
            info.avatarKey = QStringLiteral("pastel");
        } else if (key.startsWith(QLatin1String("u-"))) {
            info.nickname = QString::fromUtf8(kUserNames[index % std::size(kUserNames)]);
            info.avatarKey = QString::fromLatin1(kUserAvatars[index % std::size(kUserAvatars)]);
        } else if (key.startsWith(QLatin1String("r-"))) {
            info.nickname = QString::fromUtf8(kReplyNames[index % std::size(kReplyNames)]);
        } else {
            info.nickname = QString::fromUtf8(kUserNames[seed % std::size(kUserNames)]) +
                            QString::number(index);
            info.avatarKey = QString::fromLatin1(kUserAvatars[seed % std::size(kUserAvatars)]);
        }
        static const char* const signatures[] = {"", "耳机是另一个世界的入口。", "只听喜欢的歌。",
                                                 "",
                                                 "白天上班，晚上写歌，周末去看演出。"
                                                 "最近在学吉他，进度很慢，但很开心。"};
        info.signature = QString::fromUtf8(signatures[seed % std::size(signatures)]);
        info.level = int(seed % 10) + 1;
        info.follows = qint64((seed >> 4) % 300);
        info.fans = key == QStringLiteral("u-13") ? 34'210 : qint64((seed >> 12) % 90);
        info.playlists = qint64((seed % 4) + ((seed >> 8) % 6));
        info.artistKey = key == QStringLiteral("u-13") && !m_samples->artistOrder.isEmpty()
                             ? m_samples->artistOrder.first()
                             : QString();
        info.rankingOpen = key != QStringLiteral("u-4");
        info.followed = m_controls->signedIn() && key != kFakeAccountKey &&
                        m_account->userFollows.value(key, index % 3 == 0);
        return info;
    }

    [[nodiscard]] static UserItem itemOf(const UserInfo& info)
    {
        return {info.key, info.nickname, info.avatarKey, info.signature, info.followed};
    }

    static constexpr int kTopSongs = 50;
    static constexpr int kChartSongs = 20;
    static constexpr int kChartPreviews = 3;
    // The first charts have previews (the official ones).
    static constexpr int kOfficialCharts = 4;
    // The "全部" playlists, for paging; a category has fewer.
    static constexpr int kCategoryPlaylists = 95;
    static constexpr int kSuggestions = 8;
    static constexpr int kDailySongs = 15;
    static constexpr int kHistoryDays = 7;
    static constexpr int kSimilarArtists = 6;
    // The first sample artist's song list is this long, for paging.
    static constexpr int kLongArtistSongs = 230;

    // Where the premium cursor starts counting down.
    static constexpr qint64 kPremiumCursor = 1'000'000;

    // A category's playlists: the samples tagged with it, or (for a category the
    // samples don't use) all of them in an order of its own, repeated for paging.
    // "儿童" has none, for the empty state.
    [[nodiscard]] QList<CollectionItem> categoryItems(const QString& category) const
    {
        if (m_controls->empty() || category == u"儿童")
            return {};
        QStringList keys;
        for (const QString& key : m_samples->playlistOrder)
            if (!m_samples->playlists[key].songKeys.isEmpty() && key != u"pl-long")
                keys << key;
        QStringList tagged;
        for (const QString& key : std::as_const(keys))
            if (m_samples->playlists[key].info.tags.contains(category))
                tagged << key;
        int count = kCategoryPlaylists;
        if (!tagged.isEmpty()) {
            keys = tagged;
            count = int(tagged.size()) * 6;
        } else if (!category.isEmpty()) {
            std::mt19937 random(qHash(category));
            std::shuffle(keys.begin(), keys.end(), random);
            count = 24 + int(qHash(category) % 50);
        }
        QList<CollectionItem> items;
        for (int i = 0; i < count; ++i) {
            const QString& key = keys.at(i % keys.size());
            CollectionItem item = m_samples->playlistItem(key);
            item.playCount = m_samples->playlists[key].info.playCount;
            items << item;
        }
        return items;
    }

    // What browsing artists filters on, made up for the samples.
    struct ArtistTraits {
        int kind; // 1 male, 2 female, 3 group
        int area; // 1 Chinese, 2 Western, 3 Japanese, 4 Korean, 5 other
        char initial;
    };
    [[nodiscard]] static ArtistTraits traitsOf(const QString& key)
    {
        static const QHash<QString, ArtistTraits> traits{
            {QStringLiteral("ar-01"), {3, 1, 'W'}}, {QStringLiteral("ar-02"), {3, 2, 'L'}},
            {QStringLiteral("ar-03"), {3, 1, 'L'}}, {QStringLiteral("ar-04"), {3, 2, 'N'}},
            {QStringLiteral("ar-05"), {1, 1, 'S'}}, {QStringLiteral("ar-06"), {3, 5, 'P'}},
            {QStringLiteral("ar-07"), {3, 1, 'C'}}, {QStringLiteral("ar-08"), {2, 3, 'K'}},
            {QStringLiteral("ar-09"), {3, 1, 'T'}}, {QStringLiteral("ar-10"), {2, 2, 'Q'}},
            {QStringLiteral("ar-11"), {3, 1, 'X'}}, {QStringLiteral("ar-12"), {1, 4, 'P'}},
        };
        return traits.value(key, {3, 5, '#'});
    }

    // The sample albums of an area, as newAlbums() numbers them (0 all, 1 Chinese,
    // 2 Western, 3 Korean, 4 Japanese), by their first artist.
    [[nodiscard]] QList<CollectionItem> albumsIn(int area) const
    {
        static const int artistArea[] = {0, 1, 2, 4, 3};
        QList<CollectionItem> items;
        if (m_controls->empty())
            return items;
        for (const QString& key : m_samples->albumOrder) {
            const AlbumPage& album = m_samples->albums[key];
            const int own =
                album.info.artists.isEmpty() ? 5 : traitsOf(album.info.artists.first().key).area;
            if (area <= 0 || area > 4 || own == artistArea[area])
                items << m_samples->albumItem(key);
        }
        return items;
    }

    // All of an artist's songs. The first sample artist's songs repeat under keys like
    // "so-001~2", so the list pages.
    [[nodiscard]] QList<SongInfo> songsOf(const QString& key) const
    {
        const QStringList own = m_samples->artistSongs.value(key);
        QList<SongInfo> songs;
        for (const QString& song : own)
            songs << m_samples->songs.value(song);
        if (key == u"ar-01" && !own.isEmpty()) {
            for (int i = 0; songs.size() < kLongArtistSongs; ++i)
                songs << m_samples->song(own.at(i % own.size()) + QLatin1Char('~') +
                                         QString::number(i / own.size() + 1));
        }
        return songs;
    }

    [[nodiscard]] static QStringList aliasesOf(const QString& key)
    {
        static const QHash<QString, QStringList> aliases = {
            {QStringLiteral("ar-01"), {QStringLiteral("Fog Harbor")}},
            {QStringLiteral("ar-08"), {QStringLiteral("きいろ"), QStringLiteral("Kiiro")}},
            {QStringLiteral("ar-11"), {QStringLiteral("Stardust Choir")}},
        };
        return aliases.value(key);
    }

    // Made up for every sample artist but the last, which has none.
    [[nodiscard]] ArtistIntroduction introductionOf(const QString& key) const
    {
        ArtistIntroduction intro;
        if (key == m_samples->artistOrder.last())
            return intro;
        const QString name = m_samples->artists.value(key).name;
        intro.brief = tr("%1是虚构的示例歌手，用来检查歌手页的简介：一行放不下时出现「更多」，"
                         "点开切到「详情」标签读全文。")
                          .arg(name);
        intro.sections = {
            {tr("早年经历"),
             tr("%1在一座海边小城开始写歌，最早的录音是在旧仓库里用一支麦克风完成的。"
                "那几年的作品大多没有发行，只在朋友之间流传。")
                 .arg(name)},
            {tr("音乐风格"), tr("作品以安静的编曲和清晰的人声为主，常用环境录音作为过渡。"
                                "现场演出时会把每首歌重新编排，很少照原样演奏。")},
        };
        return intro;
    }

    // A different but stable pick each day.
    [[nodiscard]] QList<SongInfo> dailyPick(QDate day) const
    {
        QStringList keys = m_samples->songOrder;
        std::mt19937 random(day.toJulianDay());
        std::shuffle(keys.begin(), keys.end(), random);
        QList<SongInfo> songs;
        for (const QString& key : keys.mid(0, kDailySongs))
            songs << m_samples->songs[key];
        return songs;
    }

    static bool matches(const QString& candidate, const QString& text)
    {
        return candidate.contains(text, Qt::CaseInsensitive);
    }

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
    std::shared_ptr<const FakeAccount> m_account;
    QList<PlaylistPage> m_charts;
};

// The signed-in account's library: a fixed selection of the samples.
class FakeLibrary : public QObject, public LibrarySource
{
  public:
    FakeLibrary(FakeControls* controls, std::shared_ptr<const Samples> samples,
                std::shared_ptr<FakeAccount> account)
        : m_controls(controls), m_samples(std::move(samples)), m_account(std::move(account))
    {
    }

    Request library(Reply<LibraryPage> reply) override
    {
        return replyLater<LibraryPage>(this, m_controls, std::move(reply), [this]() {
            Outcome<LibraryPage> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            LibraryPage page;
            if (!m_controls->empty()) {
                for (const QString& key : std::as_const(m_account->liked))
                    page.likedSongs << m_samples->song(key);
                for (const QString& key : recentKeys())
                    page.recentSongs << m_samples->songs[key];
                for (const QString& key : std::as_const(m_account->created))
                    page.createdPlaylists << m_account->playlistItem(key, *m_samples);
                for (const QString& key : std::as_const(m_account->saved))
                    if (m_samples->playlists.contains(key))
                        page.savedPlaylists << m_samples->playlistItem(key);
                for (const QString& key : std::as_const(m_account->savedAlbums))
                    page.albums << m_samples->albumItem(key);
                for (const QString& key : std::as_const(m_account->followedArtists))
                    page.artists << m_samples->artistItem(key);
            }
            outcome.value = page;
            return outcome;
        });
    }

    Request recentSongs(int limit, Reply<QList<SongInfo>> reply) override
    {
        return replyLater<QList<SongInfo>>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<QList<SongInfo>> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            QList<SongInfo> songs;
            if (!m_controls->empty()) {
                // The few the home page shows, then the rest of the samples.
                QStringList keys = recentKeys();
                for (const QString& key : m_samples->songOrder)
                    if (!keys.contains(key))
                        keys << key;
                for (const QString& key : keys.mid(0, limit))
                    songs << m_samples->songs[key];
            }
            outcome.value = songs;
            return outcome;
        });
    }

    Request setPlaylistSubscribed(const QString& key, bool subscribed, Reply<bool> reply) override
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<bool> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            m_account->saved.removeAll(key);
            if (subscribed)
                m_account->saved.prepend(key);
            outcome.value = subscribed;
            return outcome;
        });
    }

    Request setAlbumSubscribed(const QString& key, bool subscribed, Reply<bool> reply) override
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<bool> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            m_account->savedAlbums.removeAll(key);
            if (subscribed)
                m_account->savedAlbums.prepend(key);
            outcome.value = subscribed;
            return outcome;
        });
    }

    Request setPodcastSubscribed(const QString& key, bool subscribed, Reply<bool> reply) override
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<bool> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            m_account->savedPodcasts.removeAll(key);
            if (subscribed)
                m_account->savedPodcasts.prepend(key);
            outcome.value = subscribed;
            return outcome;
        });
    }

    Request setArtistFollowed(const QString& key, bool followed, Reply<bool> reply) override
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<bool> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            m_account->followedArtists.removeAll(key);
            if (followed)
                m_account->followedArtists.prepend(key);
            outcome.value = followed;
            return outcome;
        });
    }

    Request setUserFollowed(const QString& key, bool followed, Reply<bool> reply) override
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<bool> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            m_account->userFollows.insert(key, followed);
            outcome.value = followed;
            return outcome;
        });
    }

    Request likedSongKeys(Reply<QStringList> reply) override
    {
        return replyLater<QStringList>(
            this, m_controls, std::move(reply),
            [this]() {
                Outcome<QStringList> outcome;
                if (!m_controls->signedIn())
                    outcome.failure = Failure{failure::SignedOut, std::nullopt};
                else
                    outcome.value = m_account->liked;
                return outcome;
            },
            false);
    }

    Request likedPlaylist(Reply<CollectionItem> reply) override
    {
        return read<CollectionItem>(std::move(reply), [this]() {
            CollectionItem item = m_account->playlistItem(FakeAccount::kLikedPlaylist, *m_samples);
            item.created = true;
            return item;
        });
    }

    Request libraryCollections(collection::Kind kind, PageRequest page,
                               Reply<ResultPage<CollectionItem>> reply) override
    {
        return read<ResultPage<CollectionItem>>(std::move(reply), [=, this]() {
            QList<CollectionItem> items;
            if (m_controls->empty())
                return slice(items, page);
            switch (kind) {
            case collection::Playlist:
                // Like the service: the account's own first, then the saved ones.
                for (const QString& key : std::as_const(m_account->created)) {
                    CollectionItem item = m_account->playlistItem(key, *m_samples);
                    item.created = true;
                    items << item;
                }
                for (const QString& key : std::as_const(m_account->saved)) {
                    CollectionItem item = m_samples->playlistItem(key);
                    item.created = false;
                    items << item;
                }
                break;
            case collection::Album:
                for (const QString& key : std::as_const(m_account->savedAlbums))
                    items << m_samples->albumItem(key);
                break;
            case collection::Artist:
                for (const QString& key : std::as_const(m_account->followedArtists))
                    items << m_samples->artistItem(key);
                break;
            case collection::Podcast:
                for (const QString& key : std::as_const(m_account->savedPodcasts))
                    items << m_samples->podcastItem(key);
                break;
            }
            return slice(items, page);
        });
    }

    Request recentCollections(collection::Kind kind, int limit,
                              Reply<QList<CollectionItem>> reply) override
    {
        return read<QList<CollectionItem>>(std::move(reply), [=, this]() {
            QList<CollectionItem> items;
            if (m_controls->empty())
                return items;
            if (kind == collection::Playlist)
                for (const char* key : {"pl-03", "pl-01", "pl-long", "pl-04"})
                    items << m_samples->playlistItem(QString::fromLatin1(key));
            else if (kind == collection::Album)
                for (const char* key : {"al-02", "al-05", "al-01"})
                    items << m_samples->albumItem(QString::fromLatin1(key));
            else if (kind == collection::Podcast)
                for (const char* key : {"pc-01", "pc-03"})
                    items << m_samples->podcastItem(QString::fromLatin1(key));
            return items.mid(0, limit);
        });
    }

    Request recentVoices(int limit, Reply<QList<SongInfo>> reply) override
    {
        return read<QList<SongInfo>>(std::move(reply), [=, this]() {
            QList<SongInfo> voices;
            if (!m_controls->empty())
                for (const char* key : {"vo-01-120", "vo-01-119", "vo-03-012", "vo-01-087"})
                    voices << m_samples->song(QString::fromLatin1(key));
            return voices.mid(0, limit);
        });
    }

    Request listeningRanking(bool allTime, Reply<QList<SongInfo>> reply) override
    {
        return read<QList<SongInfo>>(std::move(reply), [=, this]() {
            QList<SongInfo> songs;
            if (m_controls->empty())
                return songs;
            // A fixed order from the samples: a short week, a long all-time list.
            QStringList keys = m_samples->songOrder;
            std::mt19937 random(allTime ? 7 : 3);
            std::shuffle(keys.begin(), keys.end(), random);
            for (const QString& key : keys.mid(0, allTime ? 40 : 12))
                songs << m_samples->songs[key];
            return songs;
        });
    }

    Request createPlaylist(const QString& title, Reply<QString> reply) override
    {
        return read<QString>(std::move(reply), [=, this]() {
            const QString key = QStringLiteral("pl-new-%1").arg(m_account->made.size() + 1);
            m_account->made.insert(key, title);
            // After "我喜欢的音乐", like the service.
            m_account->created.insert(qMin<qsizetype>(1, m_account->created.size()), key);
            return key;
        });
    }

    Request setSongLiked(const QString& key, bool liked, Reply<bool> reply) override
    {
        return write(std::move(reply), {}, [=, this]() {
            m_account->liked.removeAll(key);
            if (liked)
                m_account->liked.prepend(key);
        });
    }

    Request addToPlaylist(const QString& playlistKey, const QStringList& songKeys,
                          Reply<bool> reply) override
    {
        return write(std::move(reply), playlistKey, [=, this]() {
            QStringList keys = m_account->songKeys(playlistKey, *m_samples);
            for (auto it = songKeys.crbegin(); it != songKeys.crend(); ++it)
                if (!keys.contains(*it))
                    keys.prepend(*it);
            m_account->setSongKeys(playlistKey, keys);
        });
    }

    Request removeFromPlaylist(const QString& playlistKey, const QStringList& songKeys,
                               Reply<bool> reply) override
    {
        return write(std::move(reply), playlistKey, [=, this]() {
            QStringList keys = m_account->songKeys(playlistKey, *m_samples);
            keys.removeIf([&](const QString& key) { return songKeys.contains(key); });
            m_account->setSongKeys(playlistKey, keys);
        });
    }

    Request reorderPlaylist(const QString& playlistKey, const QStringList& songKeys,
                            Reply<bool> reply) override
    {
        return write(std::move(reply), playlistKey,
                     [=, this]() { m_account->setSongKeys(playlistKey, songKeys); });
    }

    Request updatePlaylist(const QString& playlistKey, const QString& title,
                           const QString& description, Reply<bool> reply) override
    {
        if (playlistKey == FakeAccount::kLikedPlaylist)
            return write(std::move(reply), {}, {});
        return write(std::move(reply), playlistKey, [=, this]() {
            FakeAccount::Edit& edit = m_account->edits[playlistKey];
            edit.title = title;
            edit.description = description;
        });
    }

    [[nodiscard]] bool canSavePlaylists() const override { return true; }

    Request deletePlaylist(const QString& playlistKey, Reply<bool> reply) override
    {
        if (playlistKey == FakeAccount::kLikedPlaylist)
            return write(std::move(reply), {}, {});
        return write(std::move(reply), playlistKey,
                     [=, this]() { m_account->created.removeAll(playlistKey); });
    }

  private:
    // A read of the signed-in account's library.
    template <class T> Request read(Reply<T> reply, std::function<T()> produce)
    {
        return replyLater<T>(this, m_controls, std::move(reply), [this, produce]() {
            Outcome<T> outcome;
            if (!m_controls->signedIn())
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
            else
                outcome.value = produce();
            return outcome;
        });
    }

    // A write for the signed-in account; with `playlistKey`, only to its own playlist.
    Request write(Reply<bool> reply, const QString& playlistKey, std::function<void()> apply)
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<bool> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
            } else if (!apply ||
                       (!playlistKey.isEmpty() && !m_account->created.contains(playlistKey))) {
                // No change to make: the service refuses it.
                outcome.failure = Failure{failure::Rejected, std::nullopt};
            } else {
                apply();
                outcome.value = true;
            }
            return outcome;
        });
    }

    static QStringList recentKeys()
    {
        return {"so-031", "so-001", "so-019", "so-026", "so-012", "so-007", "so-022", "so-014"};
    }

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
    std::shared_ptr<FakeAccount> m_account;
};

// The account follows App.debug.account. QR sign-in plays itself out: scanned after
// 4 s, confirming 2 s later, signed in 1 s after that; an unscanned code expires
// after 60 s. The code is a made-up pattern, not a scannable QR code.
class FakeSession : public QObject, public SessionSource
{
  public:
    explicit FakeSession(FakeControls* controls) : m_controls(controls)
    {
        m_step.setSingleShot(true);
        connect(&m_step, &QTimer::timeout, this, [this]() { advance(); });
        connect(controls, &FakeControls::accountChanged, this, [this]() {
            m_step.stop();
            m_state.qrPhase = account::NoQr;
            m_state.qrImage = {};
            applyAccount();
        });
        applyAccount();
    }

    const SessionSnapshot& snapshot() const override { return m_state; }
    void setChangedHandler(std::function<void()> changed) override
    {
        m_changed = std::move(changed);
    }

    void beginQrLogin(bool) override
    {
        if (m_state.status == account::SignedIn)
            return;
        m_state.status = account::SigningIn;
        newCode();
    }

    void refreshQr() override
    {
        if (m_state.status == account::SigningIn)
            newCode();
    }

    void cancelLogin() override
    {
        if (m_state.status != account::SigningIn)
            return;
        m_step.stop();
        m_state.qrPhase = account::NoQr;
        m_state.qrImage = {};
        applyAccount();
    }

    void signOut() override { m_controls->setAccount(account::Guest); }

    // Code "000000" and password "wrong" fail; anything else signs in after a moment.
    void beginFormLogin(account::FormMethod method, const QString&, const QString&,
                        const QString& secret, bool) override
    {
        if (m_state.status == account::SignedIn || m_state.formBusy)
            return;
        m_step.stop(); // like the backend: the QR attempt ends
        m_state.status = account::SigningIn;
        m_state.qrPhase = account::NoQr;
        m_state.qrImage = {};
        m_state.formBusy = true;
        m_state.formError.clear();
        const bool wrong = method == account::PhoneCode ? secret == QStringLiteral("000000")
                                                        : secret == QStringLiteral("wrong");
        QTimer::singleShot(kSignInMs, this, [this, method, wrong]() {
            if (!m_state.formBusy)
                return;
            m_state.formBusy = false;
            if (wrong) {
                m_state.formError = method == account::PhoneCode
                                        ? QCoreApplication::translate("Session", "验证码错误")
                                        : QCoreApplication::translate("Session", "密码错误");
                applyAccount();
                return;
            }
            m_controls->setAccount(account::SignedIn);
        });
        notify();
    }

    void sendSmsCode(const QString&, const QString&) override
    {
        if (m_state.smsState == account::SmsSending)
            return;
        m_state.smsState = account::SmsSending;
        m_state.smsError.clear();
        QTimer::singleShot(800, this, [this]() {
            m_state.smsState = account::SmsSent;
            m_state.smsResendAt = QDateTime::currentMSecsSinceEpoch() + 60000;
            notify();
        });
        notify();
    }

    void clearFormError() override
    {
        if (m_state.formError.isEmpty() && m_state.smsError.isEmpty())
            return;
        m_state.formError.clear();
        m_state.smsError.clear();
        notify();
    }

  private:
    void applyAccount()
    {
        const bool signedIn = m_controls->account() == account::SignedIn;
        m_state.status = account::Status(m_controls->account());
        m_state.accountKey = signedIn ? kFakeAccountKey : QString();
        m_state.nickname = signedIn ? FakeAccount::kNickname : QString();
        m_state.avatarKey = signedIn ? QStringLiteral("pastel") : QString();
        notify();
    }

    void newCode()
    {
        m_state.qrPhase = account::WaitingForScan;
        m_state.qrImage = pattern(++m_state.qrSerial);
        m_step.start(kScanMs);
        notify();
    }

    void advance()
    {
        switch (m_state.qrPhase) {
        case account::WaitingForScan:
            m_state.qrPhase = account::WaitingForConfirmation;
            m_step.start(kConfirmMs);
            break;
        case account::WaitingForConfirmation:
            m_state.qrPhase = account::Confirming;
            m_step.start(kSignInMs);
            break;
        case account::Confirming:
            m_state.qrPhase = account::NoQr;
            m_state.qrImage = {};
            // applyAccount() follows through accountChanged.
            m_controls->setAccount(account::SignedIn);
            return;
        default:
            return;
        }
        notify();
    }

    // Finder squares in three corners and seeded noise: looks like a code, scans as
    // nothing.
    static QImage pattern(int seed)
    {
        constexpr int modules = 29;
        QImage image(modules + 2, modules + 2, QImage::Format_Mono);
        image.fill(0);
        QRandomGenerator random{quint32(seed)};
        const auto finder = [&](int fx, int fy, int x, int y) {
            const int dx = x - fx, dy = y - fy;
            if (dx < 0 || dy < 0 || dx > 6 || dy > 6)
                return -1;
            const int ring = std::min({dx, dy, 6 - dx, 6 - dy});
            return ring == 1 ? 0 : 1;
        };
        for (int y = 0; y < modules; ++y) {
            for (int x = 0; x < modules; ++x) {
                int on = -1;
                for (const auto& [fx, fy] : {std::pair{0, 0}, {modules - 7, 0}, {0, modules - 7}})
                    on = std::max(on, finder(fx, fy, x, y));
                if (on < 0)
                    on = random.bounded(2);
                image.setPixel(x + 1, y + 1, on ? 1 : 0);
            }
        }
        // Index 0 is light (the one-module quiet zone from fill(0)), 1 is dark.
        image.setColor(0, qRgb(255, 255, 255));
        image.setColor(1, qRgb(0, 0, 0));
        return image.convertToFormat(QImage::Format_RGB32);
    }

    void notify()
    {
        if (m_notifyPending)
            return;
        m_notifyPending = true;
        QMetaObject::invokeMethod(
            this,
            [this]() {
                m_notifyPending = false;
                if (m_changed)
                    m_changed();
            },
            Qt::QueuedConnection);
    }

    static constexpr int kScanMs = 4000;
    static constexpr int kConfirmMs = 2000;
    static constexpr int kSignInMs = 1000;

    FakeControls* m_controls;
    std::function<void()> m_changed;
    SessionSnapshot m_state;
    QTimer m_step;
    bool m_notifyPending = false;
};

// Sample covers by name. Not subject to the delay and failure knobs, which are for
// page states; covers always arrive on the next event loop turn.
class FakeImages : public QObject, public ImageSource
{
  public:
    Request image(const QString& key, int /*size*/, Reply<QByteArray> reply) override
    {
        auto alive = std::make_shared<bool>(true);
        QTimer::singleShot(0, this, [alive, key, reply = std::move(reply)]() {
            if (!*alive)
                return;
            *alive = false;
            Outcome<QByteArray> outcome;
            QFile file(kSamples + QStringLiteral("covers/") + key + QStringLiteral(".jpg"));
            if (file.open(QIODevice::ReadOnly))
                outcome.value = file.readAll();
            else
                outcome.failure = Failure{failure::Rejected, std::nullopt};
            reply(std::move(outcome));
        });
        return Request([alive]() { *alive = false; });
    }
};

// Simulated playback on the monotonic clock (docs/ui/presentation.md §7): loading takes
// the controlled delay, songs end by themselves, the queue follows order and repeat,
// unavailable songs fail and so does everything while offline. Songs of unknown
// duration play for kUnknownLengthMs.
class FakePlayback : public QObject, public PlaybackSource
{
  public:
    FakePlayback(FakeControls* controls, std::shared_ptr<const Samples> samples)
        : m_controls(controls), m_samples(std::move(samples))
    {
        m_load.setSingleShot(true);
        m_end.setSingleShot(true);
        m_end.setTimerType(Qt::PreciseTimer);
        connect(&m_load, &QTimer::timeout, this, [this]() { finishLoading(); });
        connect(&m_end, &QTimer::timeout, this, [this]() { songEnded(); });
    }

    const PlaybackSnapshot& snapshot() const override { return m_state; }
    void setChangedHandler(std::function<void()> changed) override
    {
        m_changed = std::move(changed);
    }

    void playSongs(const QStringList& keys, int index, const PlayOrigin& = {}) override
    {
        if (index < 0 || index >= keys.size())
            return;
        m_state.mode = playback::Queue;
        m_queue = keys;
        arrange(index);
        start();
    }

    // Voices play like songs here; the real source waits for the backend (search.md §3.4).
    void playVoices(const QStringList& keys, int index) override { playSongs(keys, index); }
    [[nodiscard]] bool canPlayVoices() const override { return true; }

    void startFm() override
    {
        m_state.mode = playback::PersonalFm;
        m_queue.clear();
        m_sequence.clear();
        m_pos = 0;
        extendFm();
        start();
    }

    void playPause() override
    {
        switch (m_state.status) {
        case playback::Playing:
            hold(position());
            break;
        case playback::Paused:
            play(m_state.positionMs);
            break;
        case playback::Ended:
            m_pos = 0;
            start();
            break;
        case playback::Failed:
            start();
            break;
        default:
            return;
        }
        notify();
    }

    void next() override
    {
        if (m_sequence.isEmpty())
            return;
        if (m_state.mode == playback::PersonalFm) {
            ++m_pos;
            extendFm();
            start();
            return;
        }
        if (m_pos + 1 < m_sequence.size())
            ++m_pos;
        else if (m_state.repeat != playback::RepeatOff)
            m_pos = 0;
        else
            return;
        start();
    }

    void previous() override
    {
        if (m_sequence.isEmpty() || m_state.mode == playback::PersonalFm)
            return;
        if (m_pos > 0)
            --m_pos;
        else if (m_state.repeat != playback::RepeatOff && m_sequence.size() > 1)
            m_pos = int(m_sequence.size()) - 1;
        else
            return;
        start();
    }

    void seek(qint64 positionMs) override
    {
        if (!m_state.seekable)
            return;
        positionMs = std::clamp<qint64>(positionMs, 0, length());
        if (m_state.status == playback::Playing)
            play(positionMs);
        else
            hold(positionMs);
        notify();
    }

    void playAt(int index) override
    {
        if (index < 0 || index >= m_sequence.size())
            return;
        m_pos = index;
        start();
    }

    void removeAt(int index) override
    {
        if (m_state.mode != playback::Queue || index < 0 || index >= m_sequence.size())
            return;
        const int entry = m_sequence.at(index);
        m_queue.removeAt(entry);
        m_sequence.removeAt(index);
        for (int& other : m_sequence)
            if (other > entry)
                --other;
        if (m_sequence.isEmpty()) {
            m_end.stop();
            m_load.stop();
            m_pos = 0;
            m_state = PlaybackSnapshot{
                .volume = m_state.volume, .order = m_state.order, .repeat = m_state.repeat};
            notify();
            return;
        }
        if (index < m_pos) {
            --m_pos;
        } else if (index == m_pos) {
            // The next one, or the first when the playing song was the last.
            if (m_pos >= m_sequence.size())
                m_pos = 0;
            start();
            return;
        }
        updateNavigation();
        notify();
    }

    bool addSongs(const QStringList& keys, bool next, const PlayOrigin& origin = {}) override
    {
        if (m_state.mode == playback::PersonalFm || keys.isEmpty())
            return false;
        if (m_sequence.isEmpty()) {
            playSongs(keys, 0, origin);
            return true;
        }
        int at = next ? m_pos + 1 : int(m_sequence.size());
        for (const QString& key : keys) {
            m_queue << key;
            m_sequence.insert(at++, int(m_queue.size()) - 1);
        }
        updateNavigation();
        notify();
        return true;
    }

    void playNow(const QString& key, bool /*voice*/, const PlayOrigin& origin = {}) override
    {
        if (key.isEmpty())
            return;
        if (m_state.mode == playback::PersonalFm || m_sequence.isEmpty()) {
            playSongs({key}, 0, origin);
            return;
        }
        m_queue << key;
        m_sequence.insert(++m_pos, int(m_queue.size()) - 1);
        start();
    }

    void clearQueue() override
    {
        if (m_state.mode != playback::Queue)
            return;
        m_end.stop();
        m_load.stop();
        m_queue.clear();
        m_sequence.clear();
        m_pos = 0;
        m_state = PlaybackSnapshot{
            .volume = m_state.volume, .order = m_state.order, .repeat = m_state.repeat};
        notify();
    }

    void move(int from, int to) override
    {
        if (m_state.mode != playback::Queue || from < 0 || from >= m_sequence.size() || to < 0 ||
            to >= m_sequence.size() || from == to)
            return;
        m_sequence.move(from, to);
        if (from == m_pos)
            m_pos = to;
        else if (from < m_pos && to >= m_pos)
            --m_pos;
        else if (from > m_pos && to <= m_pos)
            ++m_pos;
        // In order, the queue itself is the play order: keep them the same, so turning
        // shuffle off later doesn't undo the move.
        if (m_state.order == playback::Sequential) {
            QStringList queue;
            for (int entry : std::as_const(m_sequence))
                queue << m_queue.at(entry);
            m_queue = queue;
            std::iota(m_sequence.begin(), m_sequence.end(), 0);
        }
        updateNavigation();
        notify();
    }

    void setVolume(double volume) override
    {
        m_state.volume = volume;
        notify();
    }

    void setOrder(playback::Order order) override
    {
        m_state.order = order;
        if (!m_sequence.isEmpty())
            arrange(m_sequence.at(m_pos));
        notify();
    }

    void setRepeat(playback::Repeat repeat) override
    {
        m_state.repeat = repeat;
        updateNavigation();
        notify();
    }

  private:
    static constexpr qint64 kUnknownLengthMs = 200'000;

    // Play order over the queue, starting at the queue entry `index`: in queue order,
    // or `index` followed by the rest shuffled.
    void arrange(int index)
    {
        m_sequence.resize(m_queue.size());
        std::iota(m_sequence.begin(), m_sequence.end(), 0);
        if (m_state.order == playback::Shuffle) {
            m_sequence.removeAt(index);
            std::shuffle(m_sequence.begin(), m_sequence.end(), *QRandomGenerator::global());
            m_sequence.prepend(index);
            m_pos = 0;
        } else {
            m_pos = index;
        }
        updateNavigation();
    }

    void start()
    {
        m_end.stop();
        const QString key = m_queue.at(m_sequence.at(m_pos));
        m_state.current = m_samples->song(key);
        m_state.status = playback::Loading;
        m_state.failure.reset();
        m_state.positionMs = 0;
        m_state.positionAt = playback::now();
        m_state.rate = 0;
        m_state.durationMs = m_state.current.durationMs;
        m_state.seekable = false;
        updateNavigation();
        m_load.start(m_controls->nextDelay());
        notify();
    }

    void finishLoading()
    {
        std::optional<failure::Kind> failure;
        if (m_controls->offline())
            failure = failure::Network;
        else if (m_state.current.unavailable)
            failure = failure::Rejected;
        if (failure) {
            m_state.status = playback::Failed;
            m_state.failure = Failure{*failure, std::nullopt};
        } else {
            m_state.seekable = m_state.durationMs >= 0;
            play(0);
        }
        notify();
    }

    void songEnded()
    {
        if (m_state.mode == playback::PersonalFm) {
            next();
            return;
        }
        if (m_state.repeat == playback::RepeatOne) {
            play(0);
            notify();
        } else if (m_pos + 1 < m_sequence.size() || m_state.repeat == playback::RepeatAll) {
            m_pos = (m_pos + 1) % int(m_sequence.size());
            start();
        } else {
            hold(length());
            m_state.status = playback::Ended;
            notify();
        }
    }

    void play(qint64 from)
    {
        m_state.status = playback::Playing;
        m_state.positionMs = from;
        m_state.positionAt = playback::now();
        m_state.rate = 1;
        m_end.start(int(std::max<qint64>(length() - from, 0)));
    }

    void hold(qint64 at)
    {
        m_end.stop();
        if (m_state.status == playback::Playing)
            m_state.status = playback::Paused;
        m_state.positionMs = at;
        m_state.positionAt = playback::now();
        m_state.rate = 0;
    }

    [[nodiscard]] qint64 length() const
    {
        return m_state.durationMs >= 0 ? m_state.durationMs : kUnknownLengthMs;
    }

    [[nodiscard]] qint64 position() const
    {
        const auto elapsed = qint64(double(playback::now() - m_state.positionAt) * m_state.rate);
        return std::min(m_state.positionMs + elapsed, length());
    }

    void updateNavigation()
    {
        m_state.queue.clear();
        for (int entry : std::as_const(m_sequence))
            m_state.queue << m_queue.at(entry);
        m_state.queueIndex = m_sequence.isEmpty() ? -1 : m_pos;
        if (m_state.mode == playback::PersonalFm) {
            // FM only goes forward.
            m_state.canNext = true;
            m_state.canPrevious = false;
            return;
        }
        m_state.canNext = m_pos + 1 < m_sequence.size() ||
                          (!m_sequence.isEmpty() && m_state.repeat != playback::RepeatOff);
        m_state.canPrevious =
            m_pos > 0 || (m_sequence.size() > 1 && m_state.repeat != playback::RepeatOff);
    }

    // Personal FM: makes sure a song waits at m_pos, picked at random from the
    // playable samples (not the one just played).
    void extendFm()
    {
        while (m_pos >= m_sequence.size()) {
            QStringList playable;
            for (const QString& key : m_samples->songOrder)
                if (!m_samples->songs[key].unavailable &&
                    (m_queue.isEmpty() || key != m_queue.last()))
                    playable << key;
            m_queue << playable.at(QRandomGenerator::global()->bounded(int(playable.size())));
            m_sequence << int(m_queue.size()) - 1;
        }
    }

    // Reports on the next event loop turn, once for any number of changes.
    void notify()
    {
        if (m_notifyPending)
            return;
        m_notifyPending = true;
        QMetaObject::invokeMethod(
            this,
            [this]() {
                m_notifyPending = false;
                if (m_changed)
                    m_changed();
            },
            Qt::QueuedConnection);
    }

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
    std::function<void()> m_changed;
    PlaybackSnapshot m_state;
    QStringList m_queue;
    QList<int> m_sequence; // queue indices in play order
    int m_pos = 0;         // into m_sequence
    QTimer m_load;
    QTimer m_end;
    bool m_notifyPending = false;
};
} // namespace

// Comments made up for each song, playlist and album, the same every run; what the
// account likes, publishes and deletes is kept while the app runs (comments.md).
class FakeComments : public QObject, public CommentSource
{
  public:
    FakeComments(FakeControls* controls, std::shared_ptr<const Samples> samples,
                 std::shared_ptr<const FakeAccount> account)
        : m_controls(controls), m_samples(std::move(samples)), m_account(std::move(account))
    {
    }

    Request comments(const CommentTarget& target, comment::Sort sort, const QString& cursor,
                     int limit, Reply<CommentPage> reply) override
    {
        return replyLater<CommentPage>(
            this, m_controls, std::move(reply), [this, target, sort, cursor, limit]() {
                QList<CommentInfo> all =
                    m_controls->empty() ? QList<CommentInfo>() : shown(generated(target));
                if (sort == comment::Hot) {
                    std::stable_sort(all.begin(), all.end(),
                                     [](const CommentInfo& a, const CommentInfo& b) {
                                         return a.likedCount > b.likedCount;
                                     });
                } else if (sort == comment::Recommended && all.size() > kRecommendedHot) {
                    // A few popular ones first, then the newest.
                    QList<CommentInfo> hot = all;
                    std::stable_sort(hot.begin(), hot.end(),
                                     [](const CommentInfo& a, const CommentInfo& b) {
                                         return a.likedCount > b.likedCount;
                                     });
                    hot = hot.mid(0, kRecommendedHot);
                    QList<CommentInfo> ordered = hot;
                    for (const CommentInfo& info : std::as_const(all))
                        if (std::none_of(hot.cbegin(), hot.cend(),
                                         [&](const CommentInfo& h) { return h.key == info.key; }))
                            ordered << info;
                    all = ordered;
                }
                // Newly published ones come first, by any order, as the newest.
                all = m_published.value(id(target)) + all;
                return Outcome<CommentPage>{page(all, cursor, limit)};
            });
    }

    Request replies(const CommentTarget& target, const QString& commentKey, const QString& cursor,
                    int limit, Reply<CommentPage> reply) override
    {
        return replyLater<CommentPage>(
            this, m_controls, std::move(reply), [this, target, commentKey, cursor, limit]() {
                Outcome<CommentPage> outcome;
                const QList<CommentInfo> all = generated(target);
                const auto parent =
                    std::find_if(all.cbegin(), all.cend(),
                                 [&](const CommentInfo& c) { return c.key == commentKey; });
                const bool published = m_published.value(id(target)).size() > 0 &&
                                       commentKey.startsWith(QStringLiteral("cm-mine-"));
                if (parent == all.cend() && !published) {
                    outcome.failure = Failure{failure::Rejected, std::nullopt};
                    return outcome;
                }
                QList<CommentInfo> replies;
                if (parent != all.cend())
                    replies = repliesOf(*parent);
                // The account's replies are the newest, at the end.
                replies = shown(replies + m_publishedReplies.value(commentKey));
                CommentPage result = page(replies, cursor, limit);
                outcome.value = result;
                return outcome;
            });
    }

    Request setCommentLiked(const CommentTarget& target, const QString& commentKey, bool liked,
                            Reply<bool> reply) override
    {
        Q_UNUSED(target);
        return write(std::move(reply), [this, commentKey, liked]() {
            m_liked.insert(commentKey, liked);
            return true;
        });
    }

    Request publishComment(const CommentTarget& target, const QString& text,
                           Reply<bool> reply) override
    {
        return write(std::move(reply), [this, target, text]() {
            m_published[id(target)].prepend(mine(text));
            return true;
        });
    }

    Request replyToComment(const CommentTarget& target, const QString& commentKey,
                           const QString& text, Reply<bool> reply) override
    {
        Q_UNUSED(target);
        return write(std::move(reply), [this, commentKey, text]() {
            // A reply to a reply goes into the same floor.
            const QString floor = commentKey.section(QStringLiteral("-r"), 0, 0);
            m_publishedReplies[floor] << mine(text);
            return true;
        });
    }

    Request deleteComment(const CommentTarget& target, const QString& commentKey,
                          Reply<bool> reply) override
    {
        return write(std::move(reply), [this, target, commentKey]() {
            // Only the account's own; the service refuses the rest.
            const QList<CommentInfo> all = generated(target) + m_published.value(id(target));
            const auto found = std::find_if(all.cbegin(), all.cend(), [&](const CommentInfo& c) {
                return c.key == commentKey;
            });
            bool own = found != all.cend() && found->authorKey == kAccountKey;
            for (const QList<CommentInfo>& replies : std::as_const(m_publishedReplies))
                for (const CommentInfo& c : replies)
                    own = own || c.key == commentKey;
            if (!own)
                return false;
            m_deleted.insert(commentKey);
            return true;
        });
    }

  private:
    static constexpr int kRecommendedHot = 5;
    static inline const QString kAccountKey = kFakeAccountKey;

    [[nodiscard]] static QString id(const CommentTarget& target)
    {
        return QString::number(int(target.kind)) + QLatin1Char(':') + target.key;
    }

    [[nodiscard]] static quint64 seedOf(const QString& text)
    {
        quint64 seed = 7;
        for (QChar c : text)
            seed = seed * 31 + c.unicode();
        return seed * 0x9E37'79B9'7F4A'7C15ull;
    }

    // As many as the playlist or album page says; songs get a number of their own,
    // and songs whose key ends in 7 have none (the empty state).
    [[nodiscard]] int countOf(const CommentTarget& target) const
    {
        if (target.kind == comment::Album)
            return int(qMax<qint64>(0, m_samples->albums.value(target.key).info.commentCount));
        if (target.kind == comment::Playlist) {
            if (m_account->accountOnly(target.key))
                return 0;
            const auto found = m_samples->playlists.constFind(target.key);
            if (found != m_samples->playlists.cend())
                return int(qMax<qint64>(0, found->info.commentCount));
            PlaylistInfo info; // a chart: made up the same way as its page
            info.key = target.key;
            fillPlaylistDetails(info);
            return int(qMax<qint64>(0, info.commentCount));
        }
        if (target.key.endsWith(QLatin1Char('7')))
            return 0;
        return int(seedOf(target.key) >> 40) % 260 + 3;
    }

    // Every comment of a target, newest first, before the account's changes.
    [[nodiscard]] QList<CommentInfo> generated(const CommentTarget& target) const
    {
        const auto& names = kUserNames;
        static const char* const lines[] = {"这首歌陪我走过了很多个下班的夜晚。",
                                            "前奏一出来就想起那年夏天。",
                                            "第二段副歌的和声太好听了，循环了一整天。",
                                            "有没有人和我一样，是从歌单里偶然听到的？",
                                            "耳机里放着这首，地铁好像也没那么挤了。",
                                            "编曲很克制，越听越有味道。",
                                            "歌词写得像日记一样。",
                                            "推荐给了室友，现在宿舍天天放。",
                                            "希望有一天能去现场听。",
                                            "凌晨三点，还在单曲循环。",
                                            "这个声音太治愈了。",
                                            "桥段那里的鼓点处理得真妙。"};
        const int count = countOf(target);
        const QString prefix = QStringLiteral("cm-%1-").arg(seedOf(id(target)) >> 44, 0, 16);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        std::mt19937 random(quint32(seedOf(id(target)) >> 32));
        QList<CommentInfo> all;
        qint64 time = now - 5 * 60'000;
        for (int i = 0; i < count; ++i) {
            CommentInfo c;
            c.key = prefix + QString::number(i);
            // A few are the account's own, for deleting.
            if (i % 37 == 5) {
                c.authorKey = kAccountKey;
                c.authorName = FakeAccount::kNickname;
                c.avatarKey = QStringLiteral("pastel");
            } else {
                const int author = int(random() % std::size(names));
                c.authorKey = QStringLiteral("u-%1").arg(author);
                c.authorName = QString::fromUtf8(names[author]);
                c.avatarKey = QString::fromLatin1(kUserAvatars[author % std::size(kUserAvatars)]);
            }
            if (i % 23 != 11) {
                QString text;
                const int sentences = i % 17 == 3 ? 36 : 1 + int(random() % 3);
                for (int n = 0; n < sentences; ++n)
                    text += QString::fromUtf8(lines[random() % std::size(lines)]);
                if (i % 17 == 3)
                    text = tr("很长的一段：") + text;
                c.text = text;
            }
            c.timeMs = time;
            time -= qint64(random() % (i < 20 ? 90 : 2000)) * 60'000 + 60'000;
            c.likedCount = qint64(random() % (i % 9 == 0 ? 30'000 : 400));
            c.liked = i % 29 == 7;
            if (i % 7 == 2 && i + 3 < count) {
                c.replyTo =
                    CommentExcerpt{QString::fromUtf8(names[(i + 3) % std::size(names)]),
                                   i % 31 == 2 ? QString() : QString::fromUtf8(lines[i % 12])};
            }
            c.replyCount = i % 5 == 0 ? qint64(random() % 45 + 1) : 0;
            all << c;
        }
        return all;
    }

    // A comment's replies, oldest first; some reply to an earlier one.
    [[nodiscard]] static QList<CommentInfo> repliesOf(const CommentInfo& parent)
    {
        const auto& names = kReplyNames;
        static const char* const lines[] = {"同感！",       "我也是从这个歌单来的。",
                                            "哈哈哈真的",   "说得太好了",
                                            "加一，循环中", "第一次听就喜欢上了"};
        QList<CommentInfo> replies;
        std::mt19937 random(quint32(seedOf(parent.key) >> 32));
        for (qint64 j = 0; j < qMax<qint64>(0, parent.replyCount); ++j) {
            CommentInfo r;
            r.key = parent.key + QStringLiteral("-r") + QString::number(j);
            const int author = int(random() % std::size(names));
            r.authorKey = QStringLiteral("r-%1").arg(author);
            r.authorName = QString::fromUtf8(names[author]);
            r.text = QString::fromUtf8(lines[random() % std::size(lines)]);
            r.timeMs = parent.timeMs + (j + 1) * 7 * 60'000;
            r.likedCount = qint64(random() % 60);
            if (j % 4 == 3)
                r.replyTo = CommentExcerpt{replies.last().authorName, *replies.last().text};
            r.replyCount = 0;
            replies << r;
        }
        return replies;
    }

    // The account's changes applied: likes and deletions.
    [[nodiscard]] QList<CommentInfo> shown(QList<CommentInfo> all) const
    {
        all.removeIf([this](const CommentInfo& c) { return m_deleted.contains(c.key); });
        for (CommentInfo& c : all) {
            // Likes are the account's: none for guests.
            if (!m_controls->signedIn() && c.liked) {
                c.liked = false;
                --c.likedCount;
            }
            const auto liked = m_liked.constFind(c.key);
            if (liked != m_liked.cend() && *liked != c.liked) {
                c.likedCount += *liked ? 1 : -1;
                c.liked = *liked;
            }
            c.replyCount += m_publishedReplies.value(c.key).size();
        }
        return all;
    }

    [[nodiscard]] CommentInfo mine(const QString& text)
    {
        CommentInfo c;
        c.key = QStringLiteral("cm-mine-%1").arg(++m_serial);
        c.authorKey = kAccountKey;
        c.authorName = FakeAccount::kNickname;
        c.avatarKey = QStringLiteral("pastel");
        c.text = text;
        c.timeMs = QDateTime::currentMSecsSinceEpoch();
        c.likedCount = 0;
        c.replyCount = 0;
        return c;
    }

    // Offset pages; the cursor is the offset as text.
    [[nodiscard]] static CommentPage page(const QList<CommentInfo>& all, const QString& cursor,
                                          int limit)
    {
        const int offset = cursor.toInt();
        CommentPage result;
        result.items = all.mid(offset, limit);
        result.total = all.size();
        result.hasMore = offset + limit < all.size();
        if (*result.hasMore)
            result.next = QString::number(offset + limit);
        return result;
    }

    Request write(Reply<bool> reply, std::function<bool()> apply)
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [this, apply]() {
            Outcome<bool> outcome;
            if (!m_controls->signedIn())
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
            else if (!apply())
                outcome.failure = Failure{failure::Rejected, std::nullopt};
            else
                outcome.value = true;
            return outcome;
        });
    }

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
    std::shared_ptr<const FakeAccount> m_account;
    QHash<QString, bool> m_liked;
    QSet<QString> m_deleted;
    // By target id, newest first; replies by comment key, oldest first.
    QHash<QString, QList<CommentInfo>> m_published;
    QHash<QString, QList<CommentInfo>> m_publishedReplies;
    int m_serial = 0;
};

// The resource cache: a fixed amount until cleared; the backend's budgets (128 MiB of
// details and lyrics, 512 MiB of covers) as the limit.
class FakeCache : public QObject, public CacheSource
{
  public:
    explicit FakeCache(FakeControls* controls) : m_controls(controls) {}

    Request usage(Reply<CacheUsage> reply) override
    {
        return replyLater<CacheUsage>(
            this, m_controls, std::move(reply),
            [this]() { return Outcome<CacheUsage>{CacheUsage{m_bytes, 640ll << 20}}; }, false);
    }

    Request clear(Reply<bool> reply) override
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [this]() {
            m_bytes = 0;
            return Outcome<bool>{true};
        });
    }

    Request audioUsage(Reply<qint64> reply) override
    {
        return replyLater<qint64>(
            this, m_controls, std::move(reply),
            [this]() { return Outcome<qint64>{std::min(m_audioBytes, m_audioCapacity)}; }, false);
    }

    Request clearAudio(Reply<qint64> reply) override
    {
        return replyLater<qint64>(this, m_controls, std::move(reply), [this]() {
            m_audioBytes = 0;
            return Outcome<qint64>{qint64(0)};
        });
    }

    void setAudioCapacity(qint64 bytes) override { m_audioCapacity = bytes; }

  private:
    FakeControls* m_controls;
    qint64 m_bytes = 193'462'272;        // 184.5 MiB
    qint64 m_audioBytes = 1'395'864'371; // 1.3 GiB
    qint64 m_audioCapacity = qint64(2) << 30;
};

// Sample lyrics (ui/samples/lyrics/<song>.json) split into the provider's tracks:
// line-timed original, translation and romanization, the original by word as the
// enhanced version, and syllable-timed romaji where a line has it. "so-033" is
// instrumental; songs without a file have none.
class FakeLyrics : public QObject, public LyricsSource
{
  public:
    explicit FakeLyrics(FakeControls* controls) : m_controls(controls) {}

    Request lyrics(const QString& songKey, Reply<SongLyrics> reply) override
    {
        const QString id = songKey.section(u'~', 0, 0);
        return replyLater<SongLyrics>(this, m_controls, std::move(reply),
                                      [id]() { return Outcome<SongLyrics>{load(id)}; });
    }

  private:
    static QList<TimedText> timed(const QJsonArray& array)
    {
        QList<TimedText> result;
        for (const QJsonValue& value : array) {
            const QJsonObject object = value.toObject();
            result << TimedText{object["startMs"].toInteger(), object["endMs"].toInteger(),
                                object["text"].toString()};
        }
        return result;
    }

    static SongLyrics load(const QString& id)
    {
        SongLyrics lyrics;
        if (id == QLatin1String("so-033")) {
            lyrics.instrumental = true;
            return lyrics;
        }
        QFile file(kSamples + QStringLiteral("lyrics/") + id + QStringLiteral(".json"));
        if (!file.open(QIODevice::ReadOnly)) {
            lyrics.noLyric = true;
            return lyrics;
        }
        LyricTrack original, words, translation, romanization, syllables;
        original.timing = translation.timing = romanization.timing = LyricTrack::Lines;
        words.timing = syllables.timing = LyricTrack::Words;
        const QJsonArray lines = QJsonDocument::fromJson(file.readAll())["lines"].toArray();
        for (const QJsonValue& value : lines) {
            const QJsonObject object = value.toObject();
            const qint64 start = object["startMs"].toInteger();
            const qint64 end = object["endMs"].toInteger();
            const QString text = object["text"].toString();
            original.lines << LyricLine{start, end, text, {}};
            translation.lines << LyricLine{start, end, object["translation"].toString(), {}};
            romanization.lines << LyricLine{start, end, object["romanization"].toString(), {}};
            if (!text.isEmpty())
                words.lines << LyricLine{start, end, text, timed(object["words"].toArray())};
            if (object.contains("romanizationWords"))
                syllables.lines << LyricLine{start, end, object["romanization"].toString(),
                                             timed(object["romanizationWords"].toArray())};
        }
        lyrics.original.standard = original;
        lyrics.original.enhanced = words;
        lyrics.translation.standard = translation;
        lyrics.romanization.standard = romanization;
        if (!syllables.lines.isEmpty())
            lyrics.romanization.enhanced = syllables;
        return lyrics;
    }

    FakeControls* m_controls;
};

// Local listening statistics over a made-up history: 75 days up to now, more in the
// evenings and at weekends, some skips, a few origins. The same every run (seeded),
// aggregated per query like the backend does.
class FakeStats : public QObject, public StatsSource
{
  public:
    FakeStats(FakeControls* controls, std::shared_ptr<const Samples> samples)
        : m_controls(controls), m_samples(std::move(samples))
    {
        build();
    }

    Request stats(const StatsQuery& query, Reply<ListeningStats> reply) override
    {
        return replyLater<ListeningStats>(this, m_controls, std::move(reply), [this, query]() {
            return Outcome<ListeningStats>{aggregate(query)};
        });
    }

    Request clear(Reply<bool> reply) override
    {
        return replyLater<bool>(this, m_controls, std::move(reply), [this]() {
            m_records.clear();
            if (m_changed)
                QTimer::singleShot(0, this, [this] { m_changed(); });
            return Outcome<bool>{true};
        });
    }

    void setChangedHandler(std::function<void()> changed) override
    {
        m_changed = std::move(changed);
    }

  private:
    struct Record {
        qint64 startedAtMs = 0;
        qint64 playedMs = 0;
        QString song;
        StatsOrigin origin; // amount unused
        bool counted = false;
    };

    void build()
    {
        const QStringList& songs = m_samples->songOrder;
        if (songs.isEmpty())
            return;
        std::mt19937 random(20261112);
        QList<StatsOrigin> origins;
        for (const QString& key : m_samples->playlistOrder.mid(0, 3))
            origins.append({StatsOrigin::Playlist, key, m_samples->playlists[key].info.title, {}});
        origins.append({StatsOrigin::Fm, {}, {}, {}});
        origins.append({StatsOrigin::Daily, {}, {}, {}});
        if (!m_samples->albumOrder.isEmpty()) {
            const QString album = m_samples->albumOrder.first();
            origins.append({StatsOrigin::Album, album, m_samples->albums[album].info.title, {}});
        }
        origins.append({StatsOrigin::Search, {}, {}, {}});
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QDate today = QDate::currentDate();
        for (int back = 74; back >= 0; --back) {
            const QDate day = today.addDays(-back);
            if (back > 2 && random() % 7 == 0)
                continue; // some days without music
            const bool weekend = day.dayOfWeek() >= 6;
            // Listening sessions: a commute in the morning on weekdays, evenings, and
            // weekend afternoons.
            QList<int> hours;
            if (!weekend && random() % 3 != 0)
                hours.append(8);
            if (random() % 4 != 0)
                hours.append(20 + int(random() % 3));
            if (weekend)
                hours.append(14 + int(random() % 3));
            for (const int hour : hours) {
                qint64 at = day.startOfDay()
                                .addSecs(hour * 3600 + int(random() % 1200))
                                .toMSecsSinceEpoch();
                const auto& origin = origins[random() % origins.size()];
                const int count = 3 + int(random() % 6);
                for (int i = 0; i < count && at < now; ++i) {
                    // Favourites come back: the first songs are picked far more often.
                    const auto pick = std::min<qsizetype>(
                        qsizetype(std::pow(double(random() % 1000) / 1000.0, 2.2) *
                                  double(songs.size())),
                        songs.size() - 1);
                    const SongInfo song = m_samples->song(songs[pick]);
                    const qint64 length = song.durationMs > 0 ? song.durationMs : 210000;
                    const bool skipped = random() % 9 == 0;
                    const qint64 played = skipped ? 4000 + qint64(random() % 20000) : length;
                    m_records.append({at, played, song.key, origin, played >= 30000});
                    at += played + 2000;
                }
            }
        }
    }

    static QDate periodStart(const QDate& day, StatsQuery::PeriodSize size)
    {
        switch (size) {
        case StatsQuery::Week:
            return day.addDays(1 - day.dayOfWeek());
        case StatsQuery::Month:
            return QDate(day.year(), day.month(), 1);
        default:
            return day;
        }
    }

    ListeningStats aggregate(const StatsQuery& query) const
    {
        ListeningStats stats;
        const qint64 to = query.toMs > 0 ? query.toMs : std::numeric_limits<qint64>::max();
        QMap<QDate, StatsAmount> periods;
        QHash<QString, StatsSong> songs;
        QHash<QString, StatsItem> artists, albums;
        QHash<QString, QSet<QString>> artistSongs, albumSongs;
        QHash<QString, StatsOrigin> origins;
        QSet<QString> countedSongs, days, seenBefore;
        QSet<QDate> playDays;
        const auto add = [](StatsAmount& amount, const Record& r) {
            amount.playedMs += r.playedMs;
            amount.plays += r.counted ? 1 : 0;
        };
        for (const Record& r : m_records) {
            const QDateTime local = QDateTime::fromMSecsSinceEpoch(r.startedAtMs);
            if (r.counted)
                playDays.insert(local.date());
            if (r.startedAtMs < query.fromMs) {
                if (r.counted)
                    seenBefore.insert(r.song);
                continue;
            }
            if (r.startedAtMs >= to)
                continue;
            ++stats.records;
            add(stats.total, r);
            days.insert(local.date().toString(Qt::ISODate));
            add(periods[periodStart(local.date(), query.periodSize)], r);
            add(stats.weekHours[(local.date().dayOfWeek() - 1) * 24 + local.time().hour()], r);
            const SongInfo song = m_samples->song(r.song);
            StatsSong& row = songs[r.song];
            row.song = song;
            row.known = true;
            add(row.amount, r);
            for (const ArtistRef& artist : song.artists) {
                const QString id =
                    artist.key.isEmpty() ? QStringLiteral("n:") + artist.name : artist.key;
                StatsItem& item = artists[id];
                item.key = artist.key;
                item.title = artist.name;
                add(item.amount, r);
                if (r.counted)
                    artistSongs[id].insert(r.song);
            }
            if (!song.albumKey.isEmpty()) {
                StatsItem& item = albums[song.albumKey];
                item.key = song.albumKey;
                item.title = song.albumTitle;
                item.coverKey = song.coverKey;
                add(item.amount, r);
                if (r.counted)
                    albumSongs[song.albumKey].insert(r.song);
            }
            const QString originId = QString::number(r.origin.kind) + u':' + r.origin.key;
            StatsOrigin& origin = origins[originId];
            origin.kind = r.origin.kind;
            origin.key = r.origin.key;
            origin.name = r.origin.name;
            add(origin.amount, r);
            if (r.counted)
                countedSongs.insert(r.song);
        }
        stats.songs = int(countedSongs.size());
        stats.artists = int(std::count_if(artistSongs.cbegin(), artistSongs.cend(),
                                          [](const auto& s) { return !s.isEmpty(); }));
        stats.albums = int(std::count_if(albumSongs.cbegin(), albumSongs.cend(),
                                         [](const auto& s) { return !s.isEmpty(); }));
        for (const QString& song : countedSongs)
            stats.newSongs += seenBefore.contains(song) ? 0 : 1;
        stats.days = int(days.size());
        for (auto it = periods.cbegin(); it != periods.cend(); ++it)
            stats.periods.append({it.key(), it.value()});

        const auto rank = [&query](auto list) {
            const auto value = [&query](const StatsAmount& a) {
                return query.byTime ? a.playedMs : qint64(a.plays);
            };
            list.removeIf([&](const auto& row) { return value(row.amount) <= 0; });
            std::stable_sort(list.begin(), list.end(), [&](const auto& a, const auto& b) {
                if (value(a.amount) != value(b.amount))
                    return value(a.amount) > value(b.amount);
                return (query.byTime ? a.amount.plays : a.amount.playedMs) >
                       (query.byTime ? b.amount.plays : b.amount.playedMs);
            });
            return list.mid(0, query.limit);
        };
        stats.topSongs = rank(songs.values());
        for (auto it = artists.begin(); it != artists.end(); ++it)
            it->songs = int(artistSongs.value(it.key()).size());
        for (auto it = albums.begin(); it != albums.end(); ++it)
            it->songs = int(albumSongs.value(it.key()).size());
        stats.topArtists = rank(artists.values());
        stats.topAlbums = rank(albums.values());
        stats.topOrigins = rank(origins.values());

        QList<QDate> sorted(playDays.cbegin(), playDays.cend());
        std::sort(sorted.begin(), sorted.end());
        int run = 0;
        for (qsizetype i = 0; i < sorted.size(); ++i) {
            run = i > 0 && sorted[i - 1].addDays(1) == sorted[i] ? run + 1 : 1;
            stats.longestStreak = std::max(stats.longestStreak, run);
        }
        const QDate today = QDate::currentDate();
        if (!sorted.isEmpty() && sorted.last().daysTo(today) <= 1)
            stats.streak = run;
        if (!m_records.isEmpty())
            stats.firstRecordAtMs = m_records.first().startedAtMs;
        return stats;
    }

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
    QList<Record> m_records; // oldest first
    std::function<void()> m_changed;
};

Sources makeFakeSources()
{
    Sources sources;
    sources.fakeControls = std::make_unique<FakeControls>();
    const std::shared_ptr<const Samples> samples = loadSamples();
    const auto account = std::make_shared<FakeAccount>();
    sources.catalog = std::make_unique<FakeCatalog>(sources.fakeControls.get(), samples, account);
    sources.images = std::make_unique<FakeImages>();
    sources.playback = std::make_unique<FakePlayback>(sources.fakeControls.get(), samples);
    sources.session = std::make_unique<FakeSession>(sources.fakeControls.get());
    sources.library = std::make_unique<FakeLibrary>(sources.fakeControls.get(), samples, account);
    sources.comments = std::make_unique<FakeComments>(sources.fakeControls.get(), samples, account);
    sources.cache = std::make_unique<FakeCache>(sources.fakeControls.get());
    sources.lyrics = std::make_unique<FakeLyrics>(sources.fakeControls.get());
    sources.stats = std::make_unique<FakeStats>(sources.fakeControls.get(), samples);
    return sources;
}

} // namespace cirrus::ui

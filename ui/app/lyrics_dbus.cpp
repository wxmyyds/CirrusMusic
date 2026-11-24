#include "lyrics_dbus.h"
#include "app_settings.h"
#include "lyrics.h"
#include "media_state.h"
#include "mpris.h"
#include "player.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <utility>

namespace cirrus::ui
{
namespace
{
const QString kPath = QStringLiteral("/org/cirrus/Lyrics");
const QString kInterface = QStringLiteral("org.cirrus.Lyrics1");

QString statusName(int status)
{
    switch (status) {
    case lyrics::Loading: return QStringLiteral("loading");
    case lyrics::Ready: return QStringLiteral("ready");
    case lyrics::Failed: return QStringLiteral("error");
    case lyrics::NoLyrics: return QStringLiteral("empty");
    case lyrics::Instrumental: return QStringLiteral("instrumental");
    default: return QStringLiteral("idle");
    }
}
QString timingName(int timing)
{
    return timing == lyrics::Words ? QStringLiteral("words") :
           timing == lyrics::Lines ? QStringLiteral("lines") : QStringLiteral("plain");
}
}

LyricsDBus::LyricsDBus(Mpris* mpris, CurrentLyrics* lyrics, AppSettings* settings)
    : QObject(mpris), m_mpris(mpris), m_lyrics(lyrics), m_settings(settings)
{
    new LyricsDBusAdaptor(this);
    if (!mpris->registered())
        return;
    m_registered = QDBusConnection::sessionBus().registerObject(
        kPath, this, QDBusConnection::ExportAdaptors);
    if (!m_registered)
        return;
    if (m_settings)
        connect(m_settings, &AppSettings::externalLyricsChanged, this, &LyricsDBus::refreshDocument);
    if (m_lyrics) {
        connect(m_lyrics, &CurrentLyrics::changed, this, &LyricsDBus::refreshDocument);
        connect(m_lyrics, &CurrentLyrics::shiftChanged, this, &LyricsDBus::refreshDocument);
    }
    connect(mpris->player(), &Player::currentChanged, this, &LyricsDBus::refreshDocument);
    connect(mpris->player(), &Player::changed, this, [this] {
        const Player* player = m_mpris->player();
        // Position/volume snapshots are frequent; do not rebuild or resend the
        // entire word-timed document for each one.
        if (enabled() && (m_document.value("songKey").toString() != player->current().key ||
            m_document.value("generation").toULongLong() != player->generation() ||
            m_document.value("playbackProvider").toString() !=
                (player->provider() == playback::Bilibili ? "bilibili" : "netease") ||
            m_document.value("previewStartMs").toLongLong() != player->previewStartMs()))
            refreshDocument();
        updateTimer();
        refreshState(true);
    });
    m_timer.setInterval(25);
    connect(&m_timer, &QTimer::timeout, this, [this] { refreshState(); });
    refreshDocument();
}

LyricsDBus::~LyricsDBus()
{
    if (m_registered)
        QDBusConnection::sessionBus().unregisterObject(kPath);
}

bool LyricsDBus::enabled() const { return m_settings && m_settings->externalLyrics(); }

QVariantMap LyricsDBus::buildDocument() const
{
    if (!enabled())
        return {};
    const Player* player = m_mpris->player();
    const SongItem song = player->current();
    const bool matches = m_lyrics && m_lyrics->matchesPlayback();
    const int status = matches ? m_lyrics->status() :
                       song.key.isEmpty() ? lyrics::None : lyrics::Loading;
    QVariantMap doc{
        {"trackId", QVariant::fromValue(QDBusObjectPath(m_mpris->trackId()))},
        {"songKey", song.key},
        {"generation", QVariant::fromValue(player->generation())},
        {"playbackProvider", player->provider() == playback::Bilibili ? "bilibili" : "netease"},
        {"title", song.title}, {"artist", song.artistText}, {"album", song.albumTitle},
        {"status", statusName(status)},
        {"timing", matches ? timingName(m_lyrics->timing()) : QStringLiteral("plain")},
        {"shiftMs", qlonglong(matches ? m_lyrics->shiftMs() : 0)},
        {"userOffsetMs", qlonglong(matches ? m_lyrics->offsetMs() : 0)},
        {"previewStartMs", qlonglong(player->previewStartMs())},
        {"lines", QVariantList{}}, {"plainText", QString{}},
    };
    if (status != lyrics::Ready)
        return doc;

    // Reject the entire document rather than truncating a timed or UTF-16 sequence.
    qsizetype units = 0;
    qsizetype wordCount = 0;
    bool valid = m_lyrics->lines()->rowCount() <= 4096;
    const auto check = [&](const QString& text) {
        units += text.size();
        valid = valid && units <= 512 * 1024 && text.isValidUtf16() && !text.contains(QChar::Null);
    };
    QVariantList lines;
    for (const ShownLine& line : m_lyrics->lines()->lines()) {
        check(line.text);
        check(line.translation);
        check(line.romanization);
        QVariantList words;
        for (qsizetype i = 0; i < line.words.size(); ++i) {
            const TimedText& word = line.words[i];
            check(word.text);
            valid = valid && ++wordCount <= 32768 && word.startMs >= 0 &&
                    word.endMs >= word.startMs;
            const int from = line.wordOffsets.value(i, -1);
            valid = valid && from >= 0 && from + word.text.size() <= line.text.size();
            words.append(QVariantMap{{"text", word.text},
                {"startMs", qlonglong(word.startMs)}, {"endMs", qlonglong(word.endMs)},
                {"durationMs", qlonglong(word.endMs - word.startMs)},
                {"from", from}, {"to", from + int(word.text.size())}});
        }
        valid = valid && (m_lyrics->timing() == lyrics::PlainText ||
                (line.startMs >= 0 && (line.endMs == -1 || line.endMs >= line.startMs)));
        lines.append(QVariantMap{{"text", line.text}, {"translation", line.translation},
            {"romanization", line.romanization},
            {"startMs", qlonglong(m_lyrics->timing() == lyrics::PlainText ? -1 : line.startMs)},
            {"endMs", qlonglong(m_lyrics->timing() == lyrics::PlainText ? -1 : line.endMs)},
            {"words", words}});
        if (!valid)
            break;
    }
    if (!valid) {
        doc["status"] = QStringLiteral("error");
        doc["errorCode"] = QStringLiteral("invalid-document");
        return doc;
    }
    doc["lines"] = lines;
    doc["plainText"] = m_lyrics->playbackText();
    return doc;
}

QVariantMap LyricsDBus::state() const
{
    if (!enabled() || !m_lyrics)
        return {};
    const Player* player = m_mpris->player();
    const qint64 position = media::positionNow(*player);
    const qint64 time = position - m_document.value("shiftMs").toLongLong();
    int current = -1;
    QVariantList activeLines, activeWords;
    const auto& lines = m_lyrics->lines()->lines();
    if (m_document.value("status") == "ready" && m_lyrics->matchesPlayback()) {
        if (m_lyrics->timing() == lyrics::PlainText) {
            current = lines.isEmpty() ? -1 : 0;
        } else {
            for (qsizetype i = 0; i < lines.size(); ++i) {
                const ShownLine& line = lines[i];
                if (line.startMs > time)
                    break;
                current = int(i);
                if (line.endMs >= 0 && time >= line.endMs)
                    continue;
                activeLines.append(int(i));
                for (qsizetype j = 0; j < line.words.size(); ++j) {
                    const TimedText& word = line.words[j];
                    if (time >= word.startMs && time < word.endMs)
                        activeWords.append(QVariantMap{{"line", int(i)}, {"word", int(j)}});
                }
            }
        }
    }
    return {{"revision", QVariant::fromValue(m_revision)},
        {"trackId", QVariant::fromValue(QDBusObjectPath(m_mpris->trackId()))},
        {"positionMs", qlonglong(position)}, {"sampledAtMs", qlonglong(playback::now())},
        {"durationMs", qlonglong(player->durationMs())}, {"rate", player->rate()},
        {"playbackStatus", player->status() == playback::Playing ? "playing" :
                           player->status() == playback::Loading ? "loading" :
                           player->status() == playback::Empty ? "stopped" : "paused"},
        {"currentLine", current}, {"activeLines", activeLines}, {"activeWords", activeWords}};
}

QVariantMap LyricsDBus::snapshot() const
{
    return {{"version", uint(1)}, {"enabled", enabled()},
            {"document", document()}, {"state", state()}};
}

void LyricsDBus::notify(const QVariantMap& changed)
{
    if (!m_registered || changed.isEmpty())
        return;
    auto signal = QDBusMessage::createSignal(kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal << kInterface << changed << QStringList{};
    QDBusConnection::sessionBus().send(signal);
}

void LyricsDBus::refreshDocument()
{
    QVariantMap next = buildDocument();
    auto previous = m_document;
    previous.remove("revision");
    const bool changed = previous != next;
    const bool switchChanged = m_wasEnabled != enabled();
    m_wasEnabled = enabled();
    if (changed || switchChanged) {
        ++m_revision;
        if (enabled())
            next["revision"] = QVariant::fromValue(m_revision);
        m_document = std::move(next);
        const auto now = state();
        m_sentState = now;
        m_sentState.remove("positionMs");
        m_sentState.remove("sampledAtMs");
        notify({{"Enabled", enabled()}, {"Document", m_document}, {"State", now}});
    }
    updateTimer();
}

void LyricsDBus::updateTimer()
{
    const bool running = enabled() && m_mpris->player()->rate() > 0 &&
                         m_document.value("status") == "ready";
    if (running && !m_timer.isActive())
        m_timer.start();
    else if (!running)
        m_timer.stop();
}

void LyricsDBus::refreshState(bool force)
{
    const auto now = state();
    auto comparable = now;
    comparable.remove("positionMs");
    comparable.remove("sampledAtMs");
    if (force || comparable != m_sentState) {
        m_sentState = comparable;
        notify({{"State", now}});
    }
}
} // namespace cirrus::ui

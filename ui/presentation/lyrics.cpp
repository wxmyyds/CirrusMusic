#include "lyrics.h"

#include "player.h"
#include "sources.h"
#include "logging/log.h"

#include <QSettings>

#include <algorithm>
#include <limits>

namespace cirrus::ui
{
namespace
{

constexpr qint64 kOpenEnd = std::numeric_limits<qint64>::max() / 4;

bool hasLines(const std::optional<LyricTrack>& track) { return track && !track->lines.isEmpty(); }

// The track to use for a language: the enhanced one first when `preferEnhanced`
// (the original: it may have word timing), else the standard one first.
const LyricTrack* pick(const LanguageLyrics& language, bool preferEnhanced)
{
    if (preferEnhanced && hasLines(language.enhanced))
        return &*language.enhanced;
    if (hasLines(language.standard))
        return &*language.standard;
    if (hasLines(language.enhanced))
        return &*language.enhanced;
    return nullptr;
}

struct Span {
    qint64 start;
    qint64 end;
};

// A line's span on the display clock (its track's offset applied); an unknown end
// runs to the next line, or stays open for the last one.
Span spanOf(const LyricTrack& track, int index)
{
    const LyricLine& line = track.lines[index];
    qint64 end = line.endMs;
    if (end < 0)
        end = index + 1 < track.lines.size() ? track.lines[index + 1].startMs : kOpenEnd;
    return {line.startMs + track.offsetMs, end == kOpenEnd ? kOpenEnd : end + track.offsetMs};
}

// The line of `track` that goes with an original line: the one overlapping it most
// in time, or the one at the same position (among non-empty lines) when either side
// has no times (docs/ui/pages/player.md §5.6). -1 when none.
int matchLine(const LyricTrack& track, Span span, int position, bool timed)
{
    if (!timed || track.timing == LyricTrack::PlainText) {
        int seen = 0;
        for (int i = 0; i < track.lines.size(); ++i) {
            if (track.lines[i].text.trimmed().isEmpty())
                continue;
            if (seen++ == position)
                return i;
        }
        return -1;
    }
    int best = -1;
    qint64 bestOverlap = 0;
    for (int i = 0; i < track.lines.size(); ++i) {
        if (track.lines[i].text.trimmed().isEmpty() && track.lines[i].words.isEmpty())
            continue;
        const Span other = spanOf(track, i);
        const qint64 overlap = std::min(span.end, other.end) - std::max(span.start, other.start);
        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            best = i;
        }
    }
    return best;
}

QVariantList wordsVariant(const ShownLine& line)
{
    QVariantList words;
    for (int i = 0; i < line.words.size(); ++i) {
        const TimedText& word = line.words[i];
        const int from = line.wordOffsets.value(i, -1);
        words << QVariantMap{{QStringLiteral("startMs"), word.startMs},
                             {QStringLiteral("endMs"), word.endMs},
                             {QStringLiteral("text"), word.text},
                             {QStringLiteral("from"), from},
                             {QStringLiteral("to"), from + int(word.text.size())}};
    }
    return words;
}

QVariantList rubyVariant(const ShownLine& line)
{
    QVariantList units;
    for (const RubyUnit& unit : line.ruby)
        units << QVariantMap{{QStringLiteral("text"), unit.text},
                             {QStringLiteral("ruby"), unit.ruby},
                             {QStringLiteral("from"), unit.from},
                             {QStringLiteral("to"), unit.to}};
    return units;
}

} // namespace

// --- LyricLinesModel ----------------------------------------------------------------

int LyricLinesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_lines.size());
}

QVariant LyricLinesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_lines.size())
        return {};
    const ShownLine& line = m_lines[index.row()];
    switch (role) {
    case StartMsRole:
        return line.startMs;
    case EndMsRole:
        return line.endMs;
    case TextRole:
        return line.text;
    case TranslationRole:
        return line.translation;
    case RomanizationRole:
        return line.romanization;
    case WordsRole:
        return wordsVariant(line);
    case RubyRole:
        return rubyVariant(line);
    case GapMsRole:
        return line.gapMs;
    default:
        return {};
    }
}

QHash<int, QByteArray> LyricLinesModel::roleNames() const
{
    return {{StartMsRole, "startMs"},
            {EndMsRole, "endMs"},
            {TextRole, "text"},
            {TranslationRole, "translation"},
            {RomanizationRole, "romanization"},
            {WordsRole, "words"},
            {RubyRole, "ruby"},
            {GapMsRole, "gapMs"}};
}

void LyricLinesModel::reset(QList<ShownLine> lines)
{
    const bool countChanges = lines.size() != m_lines.size();
    beginResetModel();
    m_lines = std::move(lines);
    endResetModel();
    if (countChanges)
        emit countChanged();
}

// --- CurrentLyrics ------------------------------------------------------------------

CurrentLyrics::CurrentLyrics(LyricsSource* source, Player* player, QObject* parent)
    : QObject(parent), m_source(source), m_player(player), m_lines(new LyricLinesModel(this))
{
    loadOffsets();
    connect(this, &CurrentLyrics::changed, this, &CurrentLyrics::shiftChanged);
    connect(this, &CurrentLyrics::offsetChanged, this, &CurrentLyrics::shiftChanged);
    connect(m_player, &Player::currentChanged, this, [this] {
        if (m_player->current().key != m_key)
            load();
    });
    connect(m_player, &Player::changed, this, [this] {
        if (m_generation != m_player->generation() || m_provider != m_player->provider())
            load();
        if (m_player->previewStartMs() == m_previewStartMs)
            return;
        m_previewStartMs = m_player->previewStartMs();
        emit shiftChanged();
        updateCurrent();
    });
    load();
}

qint64 CurrentLyrics::offsetMs() const { return m_offsets.value(m_key, 0); }

namespace
{
constexpr auto kOffsetsKey = "lyrics/offsets";
constexpr qsizetype kOffsetsKept = 500;
} // namespace

void CurrentLyrics::loadOffsets()
{
    // "key=ms", oldest first.
    const QStringList saved = QSettings().value(QLatin1StringView(kOffsetsKey)).toStringList();
    for (const QString& entry : saved) {
        const QString key = entry.section(QLatin1Char('='), 0, 0);
        const qint64 ms = entry.section(QLatin1Char('='), 1).toLongLong();
        if (!key.isEmpty() && ms != 0 && !m_offsets.contains(key)) {
            m_offsets.insert(key, ms);
            m_offsetOrder.append(key);
        }
    }
}

void CurrentLyrics::saveOffsets()
{
    m_offsetOrder.removeAll(m_key);
    if (m_offsets.contains(m_key))
        m_offsetOrder.append(m_key);
    while (m_offsetOrder.size() > kOffsetsKept)
        m_offsets.remove(m_offsetOrder.takeFirst());
    QStringList saved;
    saved.reserve(m_offsetOrder.size());
    for (const QString& key : std::as_const(m_offsetOrder))
        saved.append(key + QLatin1Char('=') + QString::number(m_offsets.value(key)));
    QSettings().setValue(QLatin1StringView(kOffsetsKey), saved);
}

QString CurrentLyrics::plainText(bool romanization, bool translation) const
{
    QStringList out;
    for (const ShownLine& line : m_lines->lines()) {
        out << line.text;
        if (romanization && !line.romanization.isEmpty())
            out << line.romanization;
        if (translation && !line.translation.isEmpty())
            out << line.translation;
    }
    return out.join(QLatin1Char('\n'));
}

bool CurrentLyrics::matchesPlayback() const
{
    return m_key == m_player->current().key && m_generation == m_player->generation() &&
           m_provider == m_player->provider();
}

QString CurrentLyrics::playbackText() const
{
    if (m_status != lyrics::Ready || !matchesPlayback())
        return {};
    const QString text = plainText(false, false);
    // Bound the D-Bus payload; do not truncate lyrics or publish invalid text.
    if (text.size() > 512 * 1024 || !text.isValidUtf16() || text.contains(QChar::Null))
        return {};
    return text;
}

void CurrentLyrics::setOffset(qint64 offsetMs)
{
    if (m_key.isEmpty() || offsetMs == this->offsetMs())
        return;
    if (offsetMs == 0)
        m_offsets.remove(m_key);
    else
        m_offsets.insert(m_key, offsetMs);
    saveOffsets();
    emit offsetChanged();
    updateCurrent();
}

void CurrentLyrics::retry()
{
    if (m_status == lyrics::Failed)
        load();
}

void CurrentLyrics::setPositionMs(qint64 positionMs)
{
    if (positionMs == m_positionMs)
        return;
    m_positionMs = positionMs;
    emit positionChanged();
    updateCurrent();
}

void CurrentLyrics::load()
{
    m_request.cancel();
    m_key = m_player->current().key;
    m_generation = m_player->generation();
    m_provider = m_player->provider();
    log::info(lcUi, "lyrics.load", {{"song", m_key}, {"token", m_generation},
                                   {"provider", m_provider}, {"voice", m_player->current().voice}});
    m_lines->reset({});
    m_hasTranslation = m_hasRomanization = m_japanese = m_hasRuby = false;
    m_trackOffsetMs = 0;
    m_previewStartMs = m_player->previewStartMs();
    m_timing = lyrics::Lines;
    if (!m_key.isEmpty() && m_player->current().voice) {
        setStatus(lyrics::NoLyrics); // a podcast voice has none
    } else if (m_key.isEmpty() || !m_source ||
               (m_generation == 0 && m_provider == playback::Bilibili)) {
        setStatus(lyrics::None);
    } else {
        setStatus(lyrics::Loading);
        m_request = m_source->lyrics(m_key, [this](Outcome<SongLyrics> outcome) {
            m_request.release();
            apply(outcome);
        });
    }
    // Notify only after retiring the old document; a subscriber must never see
    // the previous video's lines under the new playback identity.
    emit offsetChanged();
    updateCurrent();
}

void CurrentLyrics::apply(const Outcome<SongLyrics>& outcome)
{
    if (!outcome.value) {
        log::warn(lcUi, "lyrics.failed", {{"song", m_key}, {"token", m_generation},
                                         {"provider", m_provider}});
        setStatus(lyrics::Failed);
        return;
    }
    const SongLyrics& lyrics = *outcome.value;
    lyrics::Timing timing = lyrics::Lines;
    QList<ShownLine> lines = buildLines(lyrics, &timing);
    log::info(lcUi, "lyrics.received",
              {{"song", m_key}, {"token", m_generation}, {"provider", m_provider},
               {"standard_lines", lyrics.original.standard ? lyrics.original.standard->lines.size() : 0},
               {"enhanced_lines", lyrics.original.enhanced ? lyrics.original.enhanced->lines.size() : 0},
               {"shown_lines", lines.size()}, {"no_lyric", lyrics.noLyric},
               {"instrumental", lyrics.instrumental}});
    if (const LyricTrack* original = pick(lyrics.original, true))
        m_trackOffsetMs = original->offsetMs;
    m_timing = timing;
    for (const ShownLine& line : lines) {
        m_hasTranslation = m_hasTranslation || !line.translation.isEmpty();
        m_hasRomanization = m_hasRomanization || !line.romanization.isEmpty();
        m_japanese = m_japanese || containsKana(line.text);
        m_hasRuby = m_hasRuby || !line.ruby.isEmpty();
    }
    m_lines->reset(std::move(lines));
    if (lyrics.instrumental)
        setStatus(lyrics::Instrumental);
    else if (m_lines->rowCount() == 0)
        setStatus(lyrics::NoLyrics);
    else
        setStatus(lyrics::Ready);
    emit changed();
    updateCurrent();
}

void CurrentLyrics::setStatus(lyrics::Status status)
{
    m_status = status;
    emit changed();
}

QList<ShownLine> CurrentLyrics::buildLines(const SongLyrics& lyrics, lyrics::Timing* timing)
{
    QList<ShownLine> lines;
    const LyricTrack* original = pick(lyrics.original, true);
    if (!original)
        return lines;
    const bool timed = original->timing != LyricTrack::PlainText;
    bool words = original->timing == LyricTrack::Words;
    const LyricTrack* translation = pick(lyrics.translation, false);
    const LyricTrack* romanization = pick(lyrics.romanization, false);
    const LyricTrack* syllables = hasLines(lyrics.romanization.enhanced) &&
                                          lyrics.romanization.enhanced->timing == LyricTrack::Words
                                      ? &*lyrics.romanization.enhanced
                                      : nullptr;
    bool japanese = false;
    for (const LyricLine& line : original->lines)
        japanese = japanese || containsKana(line.text);

    qint64 previousEnd = 0;
    for (int i = 0; i < original->lines.size(); ++i) {
        const LyricLine& source = original->lines[i];
        if (source.text.trimmed().isEmpty())
            continue; // an empty line only marks where the one before ends
        const Span span = timed ? spanOf(*original, i) : Span{-1, -1};
        ShownLine line;
        line.text = source.text;
        line.startMs = source.startMs;
        line.endMs = timed ? (span.end == kOpenEnd ? -1 : span.end - original->offsetMs) : -1;
        line.gapMs = timed ? std::max<qint64>(0, line.startMs - previousEnd) : 0;
        if (timed)
            previousEnd = line.endMs >= 0 ? line.endMs : line.startMs;

        // Words, located in the text; a line whose words don't fit is timed by line.
        int cursor = 0;
        for (const TimedText& word : source.words) {
            const int at = line.text.indexOf(word.text, cursor);
            if (at < 0) {
                line.words.clear();
                line.wordOffsets.clear();
                break;
            }
            line.words << word;
            line.wordOffsets << at;
            cursor = at + int(word.text.size());
        }

        const int position = int(lines.size());
        if (translation) {
            const int match = matchLine(*translation, span, position, timed);
            if (match >= 0)
                line.translation = translation->lines[match].text.trimmed();
        }
        if (romanization) {
            const int match = matchLine(*romanization, span, position, timed);
            if (match >= 0) {
                const LyricLine& roman = romanization->lines[match];
                line.romanization = roman.text.trimmed();
                if (line.romanization.isEmpty()) {
                    for (const TimedText& syllable : roman.words)
                        line.romanization += syllable.text;
                }
            }
        }

        if (japanese) {
            std::optional<QList<RubyUnit>> ruby;
            if (syllables && !line.words.isEmpty()) {
                const int match = matchLine(*syllables, span, position, timed);
                if (match >= 0) {
                    QList<TimedText> timedSyllables = syllables->lines[match].words;
                    const qint64 shift = syllables->offsetMs - original->offsetMs;
                    for (TimedText& syllable : timedSyllables)
                        syllable.startMs += shift, syllable.endMs += shift;
                    ruby = alignRubyByTime(line.text, line.words, timedSyllables);
                }
            }
            if (!ruby && !line.romanization.isEmpty())
                ruby = alignRuby(line.text, line.romanization);
            if (ruby)
                line.ruby = *ruby;
        }
        lines << line;
    }
    std::stable_sort(lines.begin(), lines.end(),
                     [](const ShownLine& a, const ShownLine& b) { return a.startMs < b.startMs; });

    if (words) {
        words = std::any_of(lines.begin(), lines.end(),
                            [](const ShownLine& line) { return !line.words.isEmpty(); });
    }
    if (timing)
        *timing = !timed ? lyrics::PlainText : words ? lyrics::Words : lyrics::Lines;
    return lines;
}

void CurrentLyrics::updateCurrent()
{
    int current = -1;
    QList<int> active;
    const QList<ShownLine>& lines = m_lines->lines();
    if (m_status == lyrics::Ready && m_timing != lyrics::PlainText && !lines.isEmpty()) {
        const qint64 t = m_positionMs - shiftMs();
        const auto after = std::upper_bound(
            lines.begin(), lines.end(), t,
            [](qint64 value, const ShownLine& line) { return value < line.startMs; });
        current = int(after - lines.begin()) - 1;
        for (int i = current; i >= 0; --i) {
            const qint64 end = lines[i].endMs >= 0 ? lines[i].endMs : kOpenEnd;
            if (t < end)
                active.prepend(i);
            else if (i < current)
                break; // earlier lines overlapping this far back are rare; stop here
        }
    }
    if (current == m_current && active == m_active)
        return;
    m_current = current;
    m_active = active;
    emit currentChanged();
}

} // namespace cirrus::ui

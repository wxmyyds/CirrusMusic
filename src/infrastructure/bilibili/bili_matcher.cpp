#include "bili_matcher.h"

#include <QRegularExpression>
#include <cstdlib>

namespace cirrus::bilibili
{
namespace
{
// Letters/numbers only (any script); everything else collapses to single spaces.
QString normalizeText(const QString& value)
{
    static const thread_local QRegularExpression nonText(
        QStringLiteral("[^\\p{L}\\p{N}]+"));
    static const thread_local QRegularExpression spaces(QStringLiteral("\\s+"));
    auto normalized = value.toLower();
    return normalized.replace(nonText, QStringLiteral(" "))
        .replace(spaces, QStringLiteral(" "))
        .trimmed();
}

bool containsText(const QString& normalizedText, const QString& rawNeedle)
{
    const auto needle = normalizeText(rawNeedle).remove(u' ');
    if (needle.size() < 2)
        return false;
    return QString(normalizedText).remove(u' ').contains(needle);
}

int tokenOverlap(const QString& normalizedText, const QString& rawNeedle)
{
    const auto tokens = normalizeText(rawNeedle).split(u' ', Qt::SkipEmptyParts);
    QStringList usable;
    for (const auto& token : tokens)
        if (token.size() >= 2)
            usable.append(token);
    if (usable.isEmpty())
        return 0;
    qsizetype hits = 0;
    for (const auto& token : usable)
        if (normalizedText.contains(token))
            ++hits;
    if (hits == usable.size())
        return 35;
    return hits > 0 ? 18 : 0;
}

int durationScore(qint64 originalMs, int candidateSec)
{
    if (originalMs <= 0 || candidateSec <= 0)
        return 0;
    const auto candidateMs = qint64(candidateSec) * 1000;
    const auto diff = std::llabs(candidateMs - originalMs);
    if (diff <= 8000)
        return 30;
    if (diff <= 20000)
        return 22;
    if (diff <= 45000)
        return 12;
    if (candidateMs > originalMs * 2)
        return -15;
    return 0;
}
} // namespace

QStringList buildQueries(const SongTargetInfo& song)
{
    static const thread_local QRegularExpression spaces(QStringLiteral("\\s+"));
    const auto collapse = [](QString value) {
        return value.replace(spaces, QStringLiteral(" ")).trimmed();
    };
    const auto title = collapse(song.title);
    const auto artist = collapse(song.artist);
    QStringList candidates;
    if (title.isEmpty())
        return {};
    candidates << (title + u' ' + artist) << (artist + u' ' + title) << title
               << (title + u' ' + artist + QStringLiteral(" 无损"));
    QStringList out;
    for (const auto& query : candidates) {
        const auto normalized = collapse(query);
        if (!normalized.isEmpty() && !out.contains(normalized))
            out.append(normalized);
    }
    return out;
}

int durationFilter(qint64 durationMs)
{
    const auto seconds = durationMs / 1000;
    if (seconds <= 0)
        return 0;
    if (seconds < 10 * 60)
        return 1;
    if (seconds < 30 * 60)
        return 2;
    if (seconds < 60 * 60)
        return 3;
    return 4;
}

int score(const SongTargetInfo& song, const QString& title, const QString& author,
          int durationSec)
{
    const auto normalizedTitle = normalizeText(title);
    const auto normalizedAuthor = normalizeText(author);
    const bool titleHit = containsText(normalizedTitle, song.title);
    const bool artistHit = !song.artist.isEmpty() && (containsText(normalizedTitle, song.artist) ||
                                                      containsText(normalizedAuthor, song.artist));
    int total = titleHit ? 55 : tokenOverlap(normalizedTitle, song.title);
    if (artistHit)
        total += 25;
    total += durationScore(song.durationMs, durationSec);
    return total;
}

std::optional<PageMatch> bestPage(const SongTargetInfo& song, const SearchItem& item,
                                  const VideoInfo& video)
{
    if (song.durationMs > 0 && qint64(item.durationSec) * 1000 > song.durationMs * 2)
        return std::nullopt;
    const auto itemScore = score(song, item.title, item.author, item.durationSec);
    std::optional<PageMatch> best;
    for (const auto& page : video.pages) {
        if (page.cid <= 0 ||
            (song.durationMs > 0 && qint64(page.durationSec) * 1000 > song.durationMs * 2))
            continue;
        const auto pageScore =
            score(song, page.part + u' ' + video.title, video.ownerName, page.durationSec);
        const auto total = itemScore + pageScore;
        if (!best || total > best->score)
            best = PageMatch{page, total};
    }
    return best;
}

} // namespace cirrus::bilibili

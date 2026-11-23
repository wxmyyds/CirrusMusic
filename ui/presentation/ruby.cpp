#include "ruby.h"

#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace cirrus::ui
{
namespace
{

// --- Characters ---------------------------------------------------------------------

bool isHiragana(char32_t c) { return c >= 0x3041 && c <= 0x3096; }
bool isKatakana(char32_t c) { return (c >= 0x30a1 && c <= 0x30fa) || c == 0x30fc; }
bool isKana(char32_t c) { return isHiragana(c) || isKatakana(c); }
bool isKanji(char32_t c)
{
    return (c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf) ||
           (c >= 0x20000 && c <= 0x2ffff) || c == 0x3005 /* 々 */ || c == 0x3006 /* 〆 */ ||
           c == 0x30f6 /* ヶ */ || c == 0x30f5 /* ヵ */;
}
bool isLatin(char32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           (c >= 0xff10 && c <= 0xff19) || (c >= 0xff21 && c <= 0xff3a) ||
           (c >= 0xff41 && c <= 0xff5a);
}
bool isOpenBracket(char32_t c) { return c == '(' || c == 0xff08; }
bool isCloseBracket(char32_t c) { return c == ')' || c == 0xff09; }
// What may stand between the kana of a bracketed reading (ろっかん・とっかん).
bool isReadingGap(char32_t c)
{
    return c == ' ' || c == 0x3000 || c == 0x30fb /* ・ */ || c == 0xff65 /* ･ */;
}

// Katakana to hiragana (ー stays).
char32_t toHiragana(char32_t c) { return c >= 0x30a1 && c <= 0x30f6 ? c - 0x60 : c; }

bool isSmallYa(char32_t h) { return h == U'ゃ' || h == U'ゅ' || h == U'ょ'; }
bool isSmallVowel(char32_t h)
{
    return h == U'ぁ' || h == U'ぃ' || h == U'ぅ' || h == U'ぇ' || h == U'ぉ' || h == U'ゎ';
}
bool isSmall(char32_t h) { return isSmallYa(h) || isSmallVowel(h); }
bool isVowel(QChar c) { return QStringLiteral("aiueo").contains(c); }

// --- Readings -----------------------------------------------------------------------

// Hepburn first, then the other ways romanizations write it (particles both ways).
const QHash<char32_t, QStringList>& kanaTable()
{
    static const QHash<char32_t, QStringList> table = [] {
        QHash<char32_t, QStringList> t;
        const auto add = [&t](char32_t kana, const char* readings) {
            t.insert(kana, QString::fromLatin1(readings).split(u'|'));
        };
        add(U'あ', "a"), add(U'い', "i"), add(U'う', "u"), add(U'え', "e"), add(U'お', "o");
        add(U'か', "ka"), add(U'き', "ki"), add(U'く', "ku"), add(U'け', "ke"), add(U'こ', "ko");
        add(U'が', "ga"), add(U'ぎ', "gi"), add(U'ぐ', "gu"), add(U'げ', "ge"), add(U'ご', "go");
        add(U'さ', "sa"), add(U'し', "shi|si"), add(U'す', "su"), add(U'せ', "se");
        add(U'そ', "so"), add(U'ざ', "za"), add(U'じ', "ji|zi"), add(U'ず', "zu");
        add(U'ぜ', "ze"), add(U'ぞ', "zo"), add(U'た', "ta"), add(U'ち', "chi|ti");
        add(U'つ', "tsu|tu"), add(U'て', "te"), add(U'と', "to"), add(U'だ', "da");
        add(U'ぢ', "ji|di|zi"), add(U'づ', "zu|du"), add(U'で', "de"), add(U'ど', "do");
        add(U'な', "na"), add(U'に', "ni"), add(U'ぬ', "nu"), add(U'ね', "ne"), add(U'の', "no");
        add(U'は', "ha|wa"), add(U'ひ', "hi"), add(U'ふ', "fu|hu"), add(U'へ', "he|e");
        add(U'ほ', "ho"), add(U'ば', "ba"), add(U'び', "bi"), add(U'ぶ', "bu"), add(U'べ', "be");
        add(U'ぼ', "bo"), add(U'ぱ', "pa"), add(U'ぴ', "pi"), add(U'ぷ', "pu"), add(U'ぺ', "pe");
        add(U'ぽ', "po"), add(U'ま', "ma"), add(U'み', "mi"), add(U'む', "mu"), add(U'め', "me");
        add(U'も', "mo"), add(U'や', "ya"), add(U'ゆ', "yu"), add(U'よ', "yo"), add(U'ら', "ra");
        add(U'り', "ri"), add(U'る', "ru"), add(U'れ', "re"), add(U'ろ', "ro"), add(U'わ', "wa");
        add(U'ゐ', "wi|i"), add(U'ゑ', "we|e"), add(U'を', "wo|o"), add(U'ゔ', "vu|bu");
        add(U'ん', "n|nn|m");
        add(U'ぁ', "a"), add(U'ぃ', "i"), add(U'ぅ', "u"), add(U'ぇ', "e"), add(U'ぉ', "o");
        add(U'ゃ', "ya"), add(U'ゅ', "yu"), add(U'ょ', "yo"), add(U'ゎ', "wa");
        return t;
    }();
    return table;
}

QString stripVowel(const QString& reading)
{
    return !reading.isEmpty() && isVowel(reading.back()) ? reading.chopped(1) : reading;
}

void addUnique(QStringList& list, const QString& item)
{
    if (!list.contains(item))
        list << item;
}

// Readings of a base kana with an optional small one after it (きょ, ティ).
QStringList syllableReadings(char32_t base, char32_t small)
{
    const QStringList baseReadings = kanaTable().value(base);
    if (!small)
        return baseReadings;
    const QString vowel = kanaTable().value(small).constFirst().right(1);
    QStringList readings;
    if (isSmallYa(small)) {
        if (base == U'し') {
            readings << "sh" + vowel << "sy" + vowel;
        } else if (base == U'じ') {
            readings << "j" + vowel << "jy" + vowel << "zy" + vowel;
        } else if (base == U'ち') {
            readings << "ch" + vowel << "ty" + vowel << "cy" + vowel;
        } else if (base == U'ぢ') {
            readings << "j" + vowel << "dy" + vowel;
        } else {
            for (const QString& reading : baseReadings)
                addUnique(readings, stripVowel(reading) + "y" + vowel);
        }
        // Romanizations that spell the small kana on its own: ぎゃく gi ya ku.
        for (const QString& reading : baseReadings)
            addUnique(readings, reading + "y" + vowel);
        return readings;
    }
    // Small vowels make foreign sounds: ティ ti/thi, ファ fa, ウィ wi.
    if (base == U'て' || base == U'で')
        readings << stripVowel(baseReadings.constFirst()) + vowel
                 << stripVowel(baseReadings.constFirst()) + "h" + vowel;
    if (base == U'う')
        readings << "w" + vowel << "u" + vowel;
    for (const QString& reading : baseReadings)
        addUnique(readings, stripVowel(reading) + vowel);
    for (const QString& reading : baseReadings)
        addUnique(readings, reading + vowel);
    return readings;
}

// Doubles the first consonant for a preceding っ (kitte, matcha / maccha).
QStringList geminated(const QStringList& readings)
{
    QStringList result;
    for (const QString& reading : readings) {
        if (reading.isEmpty() || isVowel(reading.front()) || reading.front() == u'n')
            continue;
        if (reading.startsWith(u"ch"))
            addUnique(result, "t" + reading);
        addUnique(result, reading.front() + reading);
    }
    return result;
}

// --- Units --------------------------------------------------------------------------

enum class Kind { Kana, Kanji, Latin };

// A unit the romanization is matched against.
struct MatchUnit {
    Kind kind;
    QStringList readings;  // Kana: possible readings; Latin: the exact text
    int kanji = 0;         // Kanji: number of characters
    int display = 0;       // index of the display unit it belongs to
    bool geminate = false; // a っ before a kanji: may double its first consonant
};

struct DisplayUnit {
    QString text;
    int from = 0;
    int to = 0;
    bool annotated = true; // false: no ruby (punctuation, spaces, English)
    bool bracket = false;  // a bracket, a unit of its own
    bool gap = false;      // only isReadingGap characters
};

// A pair of brackets (§6.3). Romanizations leave out what is in brackets unless it is
// sung: a reading (溢(こぼ)れた: kobore), a chorus or an echo (パッと（パッと）), English
// (夜に (I'll spend…)). So the matching may read it as sung, leave it out, or (kana
// only) take it as the reading of the text right before it.
struct Group {
    int first = 0, end = 0;  // its match units [first, end)
    int open = 0, close = 0; // the display units of its brackets
    QList<int> covers;       // a reading: the match units [s, first) it may stand for
};

struct Tokens {
    QList<MatchUnit> match;
    QList<DisplayUnit> display;
    QList<Group> groups;
};

// Code points of `text` with their UTF-16 offsets.
struct CodePoint {
    char32_t c;
    int at;
};
std::vector<CodePoint> codePoints(const QString& text)
{
    std::vector<CodePoint> points;
    for (int i = 0; i < text.size();) {
        char32_t c = text.at(i).unicode();
        int length = 1;
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() &&
            text.at(i + 1).isLowSurrogate()) {
            c = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            length = 2;
        }
        points.push_back({c, i});
        i += length;
    }
    return points;
}

// The last vowel a kana unit's first reading ends with, for long vowels after it.
QChar lastVowel(const QList<MatchUnit>& units)
{
    if (units.isEmpty() || units.back().kind != Kind::Kana || units.back().readings.isEmpty())
        return {};
    const QString& reading = units.back().readings.constFirst();
    return !reading.isEmpty() && isVowel(reading.back()) ? reading.back() : QChar();
}

// Splits kana from position `p` into a match unit with its display unit and returns the
// position after it.
size_t addKana(const std::vector<CodePoint>& points, size_t p, const QString& line, Tokens& tokens)
{
    const auto at = [&](size_t i) { return i < points.size() ? toHiragana(points[i].c) : 0; };
    const auto end = [&](size_t i) { return i < points.size() ? points[i].at : int(line.size()); };
    const size_t start = p;
    const char32_t c = at(p);
    QStringList readings;
    bool geminate = false;
    const QChar previous = lastVowel(tokens.match);

    if (c == U'っ') {
        const char32_t next = at(p + 1);
        if (isKana(next) && next != U'っ' && next != U'ー' && !isSmall(next)) {
            const char32_t small = isSmall(at(p + 2)) ? at(p + 2) : 0;
            readings = geminated(syllableReadings(next, small));
            p += small ? 3 : 2;
        } else {
            readings = {QString(), QStringLiteral("t"), QStringLiteral("tsu")};
            p += 1;
            geminate = p < points.size() && isKanji(points[p].c);
        }
    } else if (c == U'ー') {
        if (!previous.isNull()) {
            readings = {previous, QString()};
            if (previous == u'o')
                readings << QStringLiteral("u");
            else if (previous == u'e')
                readings << QStringLiteral("i");
        } else {
            readings = {QString(), "a", "i", "u", "e", "o"};
        }
        p += 1;
    } else {
        const char32_t small = isSmall(at(p + 1)) && !isSmall(c) ? at(p + 1) : 0;
        readings = syllableReadings(c, small);
        p += small ? 2 : 1;
        // Long vowels: とう tou/to, とお too/tou/to, せい sei/see/se.
        if (!small && !previous.isNull()) {
            if (c == U'う' && (previous == u'o' || previous == u'u'))
                addUnique(readings, QString());
            if (c == U'お' && previous == u'o')
                addUnique(readings, "u"), addUnique(readings, QString());
            if (c == U'い' && previous == u'e')
                addUnique(readings, "e"), addUnique(readings, QString());
            if (c == U'え' && previous == u'e')
                addUnique(readings, QString());
            if (c == U'あ' && previous == u'a')
                addUnique(readings, QString());
        }
    }
    if (readings.isEmpty())
        readings = {QString()};

    tokens.display.push_back(
        {line.mid(points[start].at, end(p) - points[start].at), points[start].at, end(p), true});
    tokens.match.push_back({Kind::Kana, readings, 0, int(tokens.display.size()) - 1, geminate});
    return p;
}

// Where a reading in `group` may begin: kana only inside (and isReadingGap), right after
// kana and kanji with no gap, covering at least one kanji (抉り(えぐり); 記憶の彼方(きおくの
// かなた) as the romanization has it: kioku no + the reading of 彼方); empty when it is no
// reading.
QList<int> readingCovers(const Tokens& tokens, const Group& group)
{
    for (int i = group.first; i < group.end; ++i) {
        if (tokens.match[i].kind != Kind::Kana)
            return {};
    }
    for (int d = group.open + 1; d < group.close; ++d) {
        if (!tokens.display[d].annotated && !tokens.display[d].gap)
            return {};
    }
    // It begins at a kanji, or at kana that begin the text (お嬢(おじょう)).
    QList<int> covers;
    bool kanji = false;
    int s = group.first - 1;
    for (; s >= 0; --s) {
        const MatchUnit& unit = tokens.match[s];
        if (unit.kind == Kind::Latin || unit.display != group.open - (group.first - s))
            break;
        kanji = kanji || unit.kind == Kind::Kanji;
        if (unit.kind == Kind::Kanji)
            covers << s;
    }
    if (kanji && s + 1 < group.first && tokens.match[s + 1].kind == Kind::Kana)
        covers << s + 1;
    return covers;
}

Tokens tokenize(const QString& line)
{
    Tokens tokens;
    const std::vector<CodePoint> points = codePoints(line);
    const auto end = [&](size_t i) { return i < points.size() ? points[i].at : int(line.size()); };
    std::vector<std::pair<int, int>> open; // brackets not closed yet: display unit, match units
    size_t p = 0;
    while (p < points.size()) {
        const char32_t c = points[p].c;
        if (isKanji(c)) {
            size_t q = p;
            while (q < points.size() && isKanji(points[q].c))
                ++q;
            const int from = points[p].at;
            const QString kanji = line.mid(from, end(q) - from);
            tokens.display.push_back({kanji, from, end(q), true});
            tokens.match.push_back({Kind::Kanji, {}, int(q - p), int(tokens.display.size()) - 1});
            p = q;
        } else if (isKana(c)) {
            p = addKana(points, p, line, tokens);
        } else if (isLatin(c)) {
            size_t q = p;
            while (q < points.size() && isLatin(points[q].c))
                ++q;
            const int from = points[p].at;
            const QString text = line.mid(from, end(q) - from);
            tokens.display.push_back({text, from, end(q), false});
            tokens.match.push_back({Kind::Latin,
                                    {text.normalized(QString::NormalizationForm_KC).toLower()},
                                    0,
                                    int(tokens.display.size()) - 1});
            p = q;
        } else if (isOpenBracket(c) || isCloseBracket(c)) {
            const int from = points[p].at;
            tokens.display.push_back(
                {line.mid(from, end(p + 1) - from), from, end(p + 1), false, true});
            if (isOpenBracket(c)) {
                open.push_back({int(tokens.display.size()) - 1, int(tokens.match.size())});
            } else if (!open.empty()) {
                const auto [at, first] = open.back();
                open.pop_back();
                if (first < tokens.match.size())
                    tokens.groups.push_back(
                        {first, int(tokens.match.size()), at, int(tokens.display.size()) - 1, {}});
            }
            ++p;
        } else {
            // Punctuation and spaces: shown, not matched; consecutive ones together.
            const int from = points[p].at;
            const bool gap = isReadingGap(c);
            if (!tokens.display.isEmpty() && !tokens.display.back().annotated &&
                !tokens.display.back().bracket && tokens.display.back().to == from &&
                (tokens.match.isEmpty() ||
                 tokens.match.back().display != int(tokens.display.size()) - 1)) {
                tokens.display.back().to = end(p + 1);
                tokens.display.back().text =
                    line.mid(tokens.display.back().from, end(p + 1) - tokens.display.back().from);
                tokens.display.back().gap = tokens.display.back().gap && gap;
            } else {
                tokens.display.push_back(
                    {line.mid(from, end(p + 1) - from), from, end(p + 1), false, false, gap});
            }
            ++p;
        }
    }
    for (Group& group : tokens.groups)
        group.covers = readingCovers(tokens, group);
    return tokens;
}

// --- Romanization -------------------------------------------------------------------

// Lowercase letters and digits only; vowels with a macron or circumflex doubled
// (ō → ou, ā → aa). `map` gives each character's index in the original.
struct Normalized {
    QString text;
    QList<int> map;
};
Normalized normalizeRomaji(const QString& romanization)
{
    Normalized result;
    for (int i = 0; i < romanization.size(); ++i) {
        const QString decomposed =
            QString(romanization.at(i)).normalized(QString::NormalizationForm_KD);
        if (decomposed.isEmpty())
            continue;
        const QChar base = decomposed.at(0).toLower();
        if (!((base >= u'a' && base <= u'z') || (base >= u'0' && base <= u'9')))
            continue;
        result.text += base;
        result.map << i;
        const bool longMark = decomposed.size() > 1 && (decomposed.at(1).unicode() == 0x0304 ||
                                                        decomposed.at(1).unicode() == 0x0302);
        if (longMark && isVowel(base)) {
            result.text += base == u'o' ? QChar(u'u') : base;
            result.map << i;
        }
    }
    return result;
}

// The ranges [open, after close) of the line's bracket pairs.
QList<std::pair<int, int>> bracketSpans(const QString& line)
{
    QList<std::pair<int, int>> spans;
    std::vector<int> open;
    for (const CodePoint& point : codePoints(line)) {
        if (isOpenBracket(point.c)) {
            open.push_back(point.at);
        } else if (isCloseBracket(point.c) && !open.empty()) {
            spans.push_back({open.back(), point.at + 1});
            open.pop_back();
        }
    }
    return spans;
}

} // namespace

bool containsKana(const QString& text)
{
    for (const CodePoint& point : codePoints(text)) {
        if (isKana(point.c) && point.c != 0x30fc)
            return true;
    }
    return false;
}

std::optional<QList<RubyUnit>> alignRuby(const QString& line, const QString& romanization)
{
    const Tokens tokens = tokenize(line);
    const Normalized romaji = normalizeRomaji(romanization);
    const int n = int(tokens.match.size());
    const int m = int(romaji.text.size());
    if (n == 0 || m == 0)
        return std::nullopt;

    // Between two letters of the romanization: a space or a mark (apart; NetEase writes
    // syllables apart, ro ku ka n, others words), a mark that is no space, apostrophe or
    // hyphen (marked: the romanization's ・ or 、).
    const QString& text = romaji.text;
    std::vector<bool> apart(m + 1, true), marked(m + 1, false);
    for (int k = 1; k < m; ++k) {
        apart[k] = romaji.map[k] - romaji.map[k - 1] > 1;
        for (int c = romaji.map[k - 1] + 1; c < romaji.map[k] && !marked[k]; ++c) {
            const QChar x = romanization.at(c);
            marked[k] = !x.isSpace() && !QStringView(u"'’-").contains(x);
        }
    }
    std::vector<int> vowels(m + 1, 0);
    for (int k = 0; k < m; ++k)
        vowels[k + 1] = vowels[k] + (isVowel(text.at(k)) ? 1 : 0);
    // No reading begins with ん or っ: not at a syllabic n (n before a consonant or a gap)
    // or the first of a doubled consonant.
    const auto canBegin = [&](int j) {
        if (j + 1 >= m)
            return text.at(j) != u'n';
        const QChar c = text.at(j), next = text.at(j + 1);
        if (c == u'n')
            return !apart[j + 1] && (isVowel(next) || next == u'y');
        return !(c == next && !isVowel(c) && !apart[j + 1]);
    };
    constexpr double kInside = 0.15;

    // The ways unit `i` may take letters from `j` on, with their cost. A kanji run of k
    // characters takes from where a reading may begin a vowel or more, up to 12k letters
    // (唇 kuchibiru) and not across a mark; it costs its distance from about 3.5k letters
    // (two morae each) and ends inside a word or syllable. A kana's later readings cost a
    // little more than its first, reading nothing (a long vowel left out) much more; a っ
    // before a kanji may take the consonant the kanji's reading doubles (真っ白 ma s shiro).
    const auto steps = [&](int i, int j, const auto& take) {
        const MatchUnit& unit = tokens.match[i];
        if (unit.kind == Kind::Kanji) {
            if (j < m && !canBegin(j))
                return;
            for (int length = 1; length <= 12 * unit.kanji && j + length <= m; ++length) {
                if (length > 1 && marked[j + length - 1])
                    break;
                if (vowels[j + length] == vowels[j])
                    continue;
                take(length, std::abs(length - 3.5 * unit.kanji) * 0.1 + (apart[j] ? 0 : kInside) +
                                 (apart[j + length] ? 0 : kInside));
            }
            return;
        }
        if (unit.geminate && j + 1 < m && text.at(j) == text.at(j + 1) && !isVowel(text.at(j)) &&
            text.at(j) != u'n')
            take(1, 0.01);
        for (int r = 0; r < unit.readings.size(); ++r) {
            const QString& reading = unit.readings[r];
            // A dropped long vowel only when nothing else fits.
            if (QStringView(romaji.text).mid(j).startsWith(reading))
                take(int(reading.size()), reading.isEmpty() ? 0.3 : r * 0.01);
        }
    };
    // Brackets left out cost more than sung or read ones, so they are left out only when
    // the romanization does not have them; a reading over more units costs a little more.
    constexpr double kLeftOut = 1.0;
    constexpr double kWiderReading = 0.3;

    // cost[i][j]: the best way to give the first i units the first j letters; back[i][j]
    // the step that got there: unit i - 1 (group -1), or a group left out or read.
    constexpr double kInf = std::numeric_limits<double>::infinity();
    struct Step {
        int i = -1, j = -1;
        int group = -1;
        bool reading = false;
    };
    std::vector<std::vector<double>> cost(n + 1, std::vector<double>(m + 1, kInf));
    std::vector<std::vector<Step>> back(n + 1, std::vector<Step>(m + 1));
    const auto relax = [&](int i, int j, int to, int letters, double total, Step step) {
        if (letters <= m && total < cost[to][letters]) {
            cost[to][letters] = total;
            step.i = i, step.j = j;
            back[to][letters] = step;
        }
    };
    cost[0][0] = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j <= m; ++j) {
            if (cost[i][j] == kInf)
                continue;
            steps(i, j, [&](int length, double extra) {
                relax(i, j, i + 1, j + length, cost[i][j] + extra, {});
            });
            for (int g = 0; g < tokens.groups.size(); ++g) {
                const Group& group = tokens.groups[g];
                if (group.first == i)
                    relax(i, j, group.end, j, cost[i][j] + kLeftOut, {-1, -1, g, false});
                if (!group.covers.contains(i))
                    continue;
                // The group's kana from j on, for the units [i, first) it stands for.
                std::vector<double> inner(m + 1, kInf);
                inner[j] = cost[i][j] + kWiderReading * (group.first - i - 1);
                for (int k = group.first; k < group.end; ++k) {
                    std::vector<double> next(m + 1, kInf);
                    for (int at = j; at <= m; ++at) {
                        if (inner[at] == kInf)
                            continue;
                        steps(k, at, [&](int length, double extra) {
                            next[at + length] = std::min(next[at + length], inner[at] + extra);
                        });
                    }
                    inner = std::move(next);
                }
                for (int at = j; at <= m; ++at) {
                    if (inner[at] < kInf)
                        relax(i, j, group.end, at, inner[at], {-1, -1, g, true});
                }
            }
        }
    }
    if (cost[n][m] == kInf)
        return std::nullopt;

    // Letters [start, end) of the romaji per match unit (none: -1), and the readings taken.
    struct Reading {
        int group = 0;
        int begin = 0, end = 0;
    };
    std::vector<std::pair<int, int>> letters(n, {-1, -1});
    QHash<int, Reading> readings; // by the display unit it begins at
    for (int i = n, j = m; i > 0;) {
        const Step step = back[i][j];
        if (step.group < 0)
            letters[step.i] = {step.j, j};
        else if (step.reading)
            readings.insert(tokens.match[step.i].display, {step.group, step.j, j});
        i = step.i, j = step.j;
    }
    QHash<int, int> unitOf; // display unit to match unit
    for (int i = 0; i < n; ++i)
        unitOf.insert(tokens.match[i].display, i);

    QList<RubyUnit> result;
    int used = 0; // original characters already given to a unit
    const auto rubyOf = [&](int begin, int end) {
        if (end <= begin)
            return QString();
        const int from = std::max(romaji.map[begin], used);
        const int to = romaji.map[end - 1] + 1;
        used = std::max(used, to);
        return to > from ? romanization.mid(from, to - from).trimmed() : QString();
    };
    for (int d = 0; d < tokens.display.size(); ++d) {
        const DisplayUnit& unit = tokens.display[d];
        if (const auto reading = readings.find(d); reading != readings.end()) {
            // The reading's romaji over the text it stands for; the brackets and their kana
            // stay as the lyrics have them, without ruby.
            const Group& group = tokens.groups[reading->group];
            const int base = tokens.display[group.open].from;
            result << RubyUnit{line.mid(unit.from, base - unit.from),
                               rubyOf(reading->begin, reading->end), unit.from, base};
            for (d = group.open; d <= group.close; ++d) {
                const DisplayUnit& shown = tokens.display[d];
                result << RubyUnit{shown.text, QString(), shown.from, shown.to};
            }
            d = group.close;
            continue;
        }
        RubyUnit ruby{unit.text, QString(), unit.from, unit.to};
        const int i = unitOf.value(d, -1);
        if (i >= 0 && letters[i].first >= 0) {
            const QString text = rubyOf(letters[i].first, letters[i].second);
            if (unit.annotated)
                ruby.ruby = text;
        }
        result << ruby;
    }
    return result;
}

std::optional<QList<RubyUnit>> alignRubyByTime(const QString& line, const QList<TimedText>& words,
                                               const QList<TimedText>& romaji)
{
    if (words.isEmpty() || romaji.isEmpty())
        return std::nullopt;
    // Where each word is in the line.
    QList<int> offsets;
    int cursor = 0;
    for (const TimedText& word : words) {
        const int at = line.indexOf(word.text, cursor);
        if (at < 0)
            return std::nullopt;
        offsets << at;
        cursor = at + int(word.text.size());
    }
    // Words joined by a syllable sung over several of them form one group.
    QList<int> group(words.size());
    for (int i = 0; i < group.size(); ++i)
        group[i] = i;
    QList<QList<int>> sungOver; // per syllable, the words it overlaps
    for (const TimedText& syllable : romaji) {
        QList<int> over;
        for (int i = 0; i < words.size(); ++i) {
            const qint64 overlap = std::min(syllable.endMs, words[i].endMs) -
                                   std::max(syllable.startMs, words[i].startMs);
            if (overlap > 0)
                over << i;
        }
        for (int i : over)
            group[i] = group[over.constFirst()];
        sungOver << over;
    }
    // Words in brackets may have no romaji (a chorus, an echo: §6.3).
    const auto spans = bracketSpans(line);
    const auto bracketed = [&](int from, int to) {
        return std::any_of(spans.cbegin(), spans.cend(), [&](const std::pair<int, int>& span) {
            return span.first <= from && to <= span.second;
        });
    };
    QList<RubyUnit> result;
    int position = 0;
    for (int i = 0; i < words.size();) {
        int j = i;
        while (j + 1 < words.size() && group[j + 1] == group[i])
            ++j;
        if (offsets[i] > position)
            result << RubyUnit{line.mid(position, offsets[i] - position), QString(), position,
                               offsets[i]};
        const int end = offsets[j] + int(words[j].text.size());
        QString ruby;
        for (int s = 0; s < romaji.size(); ++s) {
            if (!sungOver[s].isEmpty() && sungOver[s].constFirst() >= i &&
                sungOver[s].constFirst() <= j)
                ruby += romaji[s].text.trimmed();
        }
        const QString text = line.mid(offsets[i], end - offsets[i]);
        bool needsRuby = false;
        for (const CodePoint& point : codePoints(text))
            needsRuby = needsRuby || isKana(point.c) || isKanji(point.c);
        if (needsRuby && ruby.isEmpty() && !bracketed(offsets[i], end))
            return std::nullopt;
        result << RubyUnit{text, ruby, offsets[i], end};
        position = end;
        i = j + 1;
    }
    if (position < line.size())
        result << RubyUnit{line.mid(position), QString(), position, int(line.size())};
    return result;
}

} // namespace cirrus::ui

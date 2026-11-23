#pragma once

#include "request.h"
#include "playback.h"
#include "ruby.h"
#include "types.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <algorithm>

namespace cirrus::ui
{

class LyricsSource;
class Player;

namespace lyrics
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Lyrics)

enum Status { None, Loading, Ready, Failed, NoLyrics, Instrumental };
Q_ENUM_NS(Status)
enum Timing { PlainText, Lines, Words };
Q_ENUM_NS(Timing)
} // namespace lyrics

// One shown line: the original with its translation, romanization and romaji ruby.
struct ShownLine {
    qint64 startMs = -1;
    qint64 endMs = -1;
    QString text;
    QString translation;
    QString romanization;
    QList<TimedText> words;
    QList<int> wordOffsets; // where each word starts in `text`
    QList<RubyUnit> ruby;   // empty when the line has no ruby
    qint64 gapMs = 0;       // since the previous line ended (the start for the first)
};

// The lines of App.lyrics (docs/ui/pages/player.md §5). Roles: startMs, endMs, text,
// translation, romanization (empty when none), words ([{startMs, endMs, text, from,
// to}], from/to in `text`), ruby ([{text, ruby, from, to}], empty when not aligned),
// gapMs (time since the previous line ended).
class LyricLinesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.lyrics.lines")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Role {
        StartMsRole = Qt::UserRole + 1,
        EndMsRole,
        TextRole,
        TranslationRole,
        RomanizationRole,
        WordsRole,
        RubyRole,
        GapMsRole,
    };

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void reset(QList<ShownLine> lines);
    [[nodiscard]] const QList<ShownLine>& lines() const { return m_lines; }

  signals:
    void countChanged();

  private:
    QList<ShownLine> m_lines;
};

// The playing song's lyrics (App.lyrics, docs/ui/presentation.md §5.2). Follows
// App.player; the original is the main track (enhanced when it has lines), and the
// translation and romanization are matched to its lines by time (§5.6). Japanese
// lines get romaji ruby where it aligns (ruby.h). QML writes the smooth position
// (PlaybackClock) to `positionMs`; `current` and `activeLines` change only when the
// position crosses a line.
class CurrentLyrics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.lyrics")
    Q_PROPERTY(int status READ status NOTIFY changed) // Lyrics.Status
    Q_PROPERTY(int timing READ timing NOTIFY changed) // Lyrics.Timing
    Q_PROPERTY(bool hasTranslation READ hasTranslation NOTIFY changed)
    Q_PROPERTY(bool hasRomanization READ hasRomanization NOTIFY changed)
    // A Japanese song: its romanization is shown as ruby where lines align.
    Q_PROPERTY(bool japanese READ japanese NOTIFY changed)
    // At least one line has ruby (all lines then keep room for it).
    Q_PROPERTY(bool hasRuby READ hasRuby NOTIFY changed)
    Q_PROPERTY(cirrus::ui::LyricLinesModel* lines READ lines CONSTANT)
    // User offset for this song in ms; positive shows the lyrics later.
    Q_PROPERTY(qint64 offsetMs READ offsetMs NOTIFY offsetChanged)
    // Line and word times are this much behind the playback position (the user offset
    // plus the file's own, less where a preview clip starts): line time = positionMs -
    // shiftMs.
    Q_PROPERTY(qint64 shiftMs READ shiftMs NOTIFY shiftChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs WRITE setPositionMs NOTIFY positionChanged)
    // The last line that has started (-1 before the first); stays through a gap.
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    // Lines being sung now (several when they overlap); empty in a gap.
    Q_PROPERTY(QList<int> activeLines READ activeLines NOTIFY currentChanged)

  public:
    CurrentLyrics(LyricsSource* source, Player* player, QObject* parent = nullptr);

    [[nodiscard]] int status() const { return m_status; }
    [[nodiscard]] int timing() const { return m_timing; }
    [[nodiscard]] bool hasTranslation() const { return m_hasTranslation; }
    [[nodiscard]] bool hasRomanization() const { return m_hasRomanization; }
    [[nodiscard]] bool japanese() const { return m_japanese; }
    [[nodiscard]] bool hasRuby() const { return m_hasRuby; }
    [[nodiscard]] LyricLinesModel* lines() const { return m_lines; }
    [[nodiscard]] qint64 offsetMs() const;
    [[nodiscard]] qint64 shiftMs() const
    {
        return offsetMs() + m_trackOffsetMs - std::max<qint64>(m_previewStartMs, 0);
    }
    [[nodiscard]] qint64 positionMs() const { return m_positionMs; }
    void setPositionMs(qint64 positionMs);
    [[nodiscard]] int current() const { return m_current; }
    [[nodiscard]] QList<int> activeLines() const { return m_active; }

    Q_INVOKABLE void setOffset(qint64 offsetMs);
    // The lines as plain text, one per line, each followed by its romanization and
    // translation when asked (for copying).
    Q_INVOKABLE QString plainText(bool romanization, bool translation) const;
    Q_INVOKABLE void retry();

    // Builds the shown lines from the provider's tracks (public for checking).
    [[nodiscard]] static QList<ShownLine> buildLines(const SongLyrics& lyrics,
                                                     lyrics::Timing* timing);

  signals:
    void changed();
    void offsetChanged();
    void shiftChanged();
    void positionChanged();
    void currentChanged();

  private:
    void load();
    void apply(const Outcome<SongLyrics>& outcome);
    void setStatus(lyrics::Status status);
    void updateCurrent();

    LyricsSource* m_source;
    Player* m_player;
    LyricLinesModel* m_lines;
    Request m_request;
    QString m_key;
    quint64 m_generation = 0;
    int m_provider = playback::Netease;
    lyrics::Status m_status = lyrics::None;
    lyrics::Timing m_timing = lyrics::Lines;
    bool m_hasTranslation = false;
    bool m_hasRomanization = false;
    bool m_japanese = false;
    bool m_hasRuby = false;
    qint64 m_trackOffsetMs = 0;
    qint64 m_previewStartMs = -1; // the player's
    // User offsets by song key, kept across runs (the latest kOffsetsKept songs).
    void loadOffsets();
    void saveOffsets();
    QHash<QString, qint64> m_offsets;
    QStringList m_offsetOrder; // their keys, least recently set first
    qint64 m_positionMs = 0;
    int m_current = -1;
    QList<int> m_active;
};

} // namespace cirrus::ui

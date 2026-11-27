#pragma once

#include "loadable_model.h"
#include "stats_types.h"

#include <QDate>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlparserstatus.h>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class SongListModel;
class StatsSource;

// App.stats: clearing the local listening history (settings) and a revision that grows
// whenever the stored statistics change (pages/stats.md §5, §7).
class LocalStats : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.stats")
    Q_PROPERTY(bool clearing READ clearing NOTIFY clearingChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

  public:
    explicit LocalStats(StatsSource* source, QObject* parent = nullptr);

    [[nodiscard]] bool clearing() const { return m_clearing.pending(); }
    [[nodiscard]] int revision() const { return m_revision; }
    Q_INVOKABLE void clear();

  signals:
    void changed();
    void clearingChanged();
    void cleared(bool ok);

  private:
    StatsSource* m_source;
    Request m_clearing;
    int m_revision = 0;
};

// The listening statistics page (docs/ui/pages/stats.md): one range (a week, month or
// year, paged back from the current one, or all time) scopes everything. Loads the range
// and the same stretch of the period before it (the comparison). Formats the numbers;
// QML only lays them out. Reloads by itself when a play is recorded (§5).
class StatsPageModel : public LoadableModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(int range READ range WRITE setRange NOTIFY rangeChanged)
    Q_PROPERTY(bool byTime READ byTime WRITE setByTime NOTIFY byTimeChanged)
    // Ranking rows: 10 songs (100 on the full ranking page), 5 of the others.
    Q_PROPERTY(int songLimit READ songLimit WRITE setSongLimit NOTIFY songLimitChanged)
    // Periods back from the current one (0); never positive.
    Q_PROPERTY(int offset READ offset WRITE setOffset NOTIFY rangeChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY contentChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY rangeChanged)
    // "11月9日–15日", "2026年11月", "2026年"; empty for all time.
    Q_PROPERTY(QString periodTitle READ periodTitle NOTIFY rangeChanged)
    // "回到本周"…; empty for all time.
    Q_PROPERTY(QString currentText READ currentText NOTIFY rangeChanged)

    // The headline: "本周听了" and the time as [{value, unit}] ("6", "小时", "12", "分").
    Q_PROPERTY(QString headline READ headline NOTIFY contentChanged)
    Q_PROPERTY(QVariantList durationParts READ durationParts NOTIFY contentChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY contentChanged)
    // "比上周同期多 40 分钟"; empty without a comparison. Direction: 1 more, -1 less, 0 same.
    Q_PROPERTY(QString compareText READ compareText NOTIFY compareChanged)
    Q_PROPERTY(int compareDirection READ compareDirection NOTIFY compareChanged)
    Q_PROPERTY(int plays READ plays NOTIFY contentChanged)
    Q_PROPERTY(int songCount READ songCount NOTIFY contentChanged)
    Q_PROPERTY(int artistCount READ artistCount NOTIFY contentChanged)
    Q_PROPERTY(int newSongs READ newSongs NOTIFY contentChanged)
    // The range has no records (the page still shows its header and range).
    Q_PROPERTY(bool periodEmpty READ periodEmpty NOTIFY contentChanged)
    // There are records at all (else the page is one empty state).
    Q_PROPERTY(bool hasRecords READ hasRecords NOTIFY contentChanged)
    // "2026年11月8日起"; the first record's day.
    Q_PROPERTY(QString sinceText READ sinceText NOTIFY contentChanged)
    Q_PROPERTY(int streak READ streak NOTIFY contentChanged)
    Q_PROPERTY(int longestStreak READ longestStreak NOTIFY contentChanged)

    // The trend: [{label, ms, plays, future, clickable, tip}] oldest first, empty periods
    // included; `chartTop` the value at the top of the plot, `grid` [{ms, text}] its lines,
    // `peak` the bar labelled with its value (-1 none) and `peakText` that value.
    Q_PROPERTY(QVariantList bars READ bars NOTIFY contentChanged)
    Q_PROPERTY(double chartTop READ chartTop NOTIFY contentChanged)
    Q_PROPERTY(QVariantList grid READ grid NOTIFY contentChanged)
    Q_PROPERTY(int peak READ peak NOTIFY contentChanged)
    Q_PROPERTY(QString peakText READ peakText NOTIFY contentChanged)

    // Rankings. songs: a song list; songValues [{fraction, text}] by row. The others:
    // [{key, title, coverKey, fraction, text, clickable, kind}] (kind: origins only).
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)
    Q_PROPERTY(QVariantList songValues READ songValues NOTIFY contentChanged)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY contentChanged)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY contentChanged)
    Q_PROPERTY(QVariantList origins READ origins NOTIFY contentChanged)

    // The weekday × hour grid: 168 levels 0–4 (Monday 0:00 first), its tooltips and the
    // line under it ("最常在周六 21 点前后听").
    Q_PROPERTY(QVariantList heat READ heat NOTIFY contentChanged)
    Q_PROPERTY(QStringList heatTips READ heatTips NOTIFY contentChanged)
    Q_PROPERTY(QString heatSummary READ heatSummary NOTIFY contentChanged)

  public:
    enum Range { Week, Month, Year, AllTime };
    Q_ENUM(Range)
    enum OriginKind {
        Fm,
        Playlist,
        Album,
        Artist,
        Daily,
        Search,
        Recent,
        Ranking,
        Other,
        Heartbeat
    };
    Q_ENUM(OriginKind)

    explicit StatsPageModel(QObject* parent = nullptr);

    [[nodiscard]] int range() const { return m_range; }
    void setRange(int range);
    [[nodiscard]] bool byTime() const { return m_byTime; }
    void setByTime(bool byTime);
    [[nodiscard]] int songLimit() const { return m_songLimit; }
    void setSongLimit(int limit);
    [[nodiscard]] int offset() const { return m_offset; }
    void setOffset(int offset);
    [[nodiscard]] bool canGoBack() const;
    [[nodiscard]] bool canGoForward() const { return m_range != AllTime && m_offset < 0; }
    [[nodiscard]] QString periodTitle() const;
    [[nodiscard]] QString currentText() const;

    [[nodiscard]] QString headline() const;
    [[nodiscard]] QVariantList durationParts() const;
    [[nodiscard]] QString durationText() const;
    [[nodiscard]] QString compareText() const { return m_compareText; }
    [[nodiscard]] int compareDirection() const { return m_compareDirection; }
    [[nodiscard]] int plays() const { return m_stats.total.plays; }
    [[nodiscard]] int songCount() const { return m_stats.songs; }
    [[nodiscard]] int artistCount() const { return m_stats.artists; }
    [[nodiscard]] int newSongs() const { return m_stats.newSongs; }
    [[nodiscard]] bool periodEmpty() const { return m_stats.records == 0; }
    [[nodiscard]] bool hasRecords() const { return m_stats.firstRecordAtMs >= 0; }
    [[nodiscard]] QString sinceText() const;
    [[nodiscard]] int streak() const { return m_stats.streak; }
    [[nodiscard]] int longestStreak() const { return m_stats.longestStreak; }

    [[nodiscard]] QVariantList bars() const { return m_bars; }
    [[nodiscard]] double chartTop() const { return m_chartTop; }
    [[nodiscard]] QVariantList grid() const { return m_grid; }
    [[nodiscard]] int peak() const { return m_peak; }
    [[nodiscard]] QString peakText() const { return m_peakText; }

    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] QVariantList songValues() const { return m_songValues; }
    [[nodiscard]] QVariantList artists() const { return m_artists; }
    [[nodiscard]] QVariantList albums() const { return m_albums; }
    [[nodiscard]] QVariantList origins() const { return m_origins; }

    [[nodiscard]] QVariantList heat() const { return m_heat; }
    [[nodiscard]] QStringList heatTips() const { return m_heatTips; }
    [[nodiscard]] QString heatSummary() const { return m_heatSummary; }

    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goToCurrent();
    // A month or year bar of the year and all-time trends: shows that month (year).
    Q_INVOKABLE void openBar(int index);

    // "1 小时 12 分", "48 分钟", "40 秒".
    static QString formatDuration(qint64 ms);

  signals:
    void rangeChanged();
    void byTimeChanged();
    void songLimitChanged();
    void contentChanged();
    void compareChanged();

  protected:
    // Loads once QML has set the range, offset and limit.
    void classBegin() override {}
    void componentComplete() override;
    Request load() override;
    void clear() override;

  private:
    struct Span {
        QDate first; // inclusive
        QDate end;   // exclusive
    };
    [[nodiscard]] std::optional<Span> span(int offset) const;
    [[nodiscard]] StatsQuery queryOf(const std::optional<Span>& span) const;
    void apply(ListeningStats stats);
    void loadComparison();
    void buildBars();
    void buildRankings();
    void buildHeat();
    void navigate(int range, int offset);

    bool m_complete = false;
    int m_range = Week;
    bool m_byTime = false;
    int m_songLimit = 10;
    int m_offset = 0;
    ListeningStats m_stats;
    QVariantList m_bars, m_grid, m_songValues, m_artists, m_albums, m_origins, m_heat;
    QStringList m_heatTips;
    QString m_heatSummary, m_peakText;
    double m_chartTop = 0;
    int m_peak = -1;
    SongListModel* m_songs;
    Request m_comparison;
    QString m_compareText;
    int m_compareDirection = 0;
    QTimer m_changed; // coalesces recorded plays into one reload
};

} // namespace cirrus::ui

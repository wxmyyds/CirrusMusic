#include "stats_model.h"

#include "app.h"
#include "session.h"
#include "song_list_model.h"
#include "sources.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>

namespace cirrus::ui
{

namespace
{
constexpr qint64 kMinute = 60'000;
constexpr qint64 kHour = 60 * kMinute;

QString weekdayName(int weekday) // 1 is Monday
{
    static const char* const names[] = {
        QT_TRANSLATE_NOOP("StatsPageModel", "周一"), QT_TRANSLATE_NOOP("StatsPageModel", "周二"),
        QT_TRANSLATE_NOOP("StatsPageModel", "周三"), QT_TRANSLATE_NOOP("StatsPageModel", "周四"),
        QT_TRANSLATE_NOOP("StatsPageModel", "周五"), QT_TRANSLATE_NOOP("StatsPageModel", "周六"),
        QT_TRANSLATE_NOOP("StatsPageModel", "周日")};
    return QCoreApplication::translate("StatsPageModel", names[std::clamp(weekday, 1, 7) - 1]);
}

QString weekdayLetter(int weekday)
{
    static const char* const names[] = {
        QT_TRANSLATE_NOOP("StatsPageModel", "一"), QT_TRANSLATE_NOOP("StatsPageModel", "二"),
        QT_TRANSLATE_NOOP("StatsPageModel", "三"), QT_TRANSLATE_NOOP("StatsPageModel", "四"),
        QT_TRANSLATE_NOOP("StatsPageModel", "五"), QT_TRANSLATE_NOOP("StatsPageModel", "六"),
        QT_TRANSLATE_NOOP("StatsPageModel", "日")};
    return QCoreApplication::translate("StatsPageModel", names[std::clamp(weekday, 1, 7) - 1]);
}

QString monthDay(const QDate& day)
{
    return StatsPageModel::tr("%1月%2日").arg(day.month()).arg(day.day());
}

// The gridline step for a plot reaching `top`: a round time, at most two lines.
qint64 gridStep(qint64 top)
{
    static constexpr std::array<int, 24> minutes{1,   2,   5,    10,   15,   20,   30,   60,
                                                 90,  120, 180,  240,  300,  360,  480,  600,
                                                 720, 900, 1200, 1500, 1800, 2400, 3000, 3600};
    for (const int step : minutes)
        if (top <= 2 * qint64(step) * kMinute)
            return qint64(step) * kMinute;
    const qint64 hours = (top / 2 + kHour - 1) / kHour;
    return hours * kHour;
}

QString axisText(qint64 ms)
{
    if (ms < kHour)
        return StatsPageModel::tr("%1 分钟").arg(ms / kMinute);
    if (ms % kHour == 0)
        return StatsPageModel::tr("%1 小时").arg(ms / kHour);
    return StatsPageModel::tr("%1 小时").arg(double(ms) / double(kHour), 0, 'f', 1);
}

QString amountTip(const QString& when, const StatsAmount& amount)
{
    if (amount.playedMs <= 0)
        return StatsPageModel::tr("%1 · 没有听歌").arg(when);
    return StatsPageModel::tr("%1 · %2 · %3 次")
        .arg(when, StatsPageModel::formatDuration(amount.playedMs))
        .arg(amount.plays);
}

QDate localDate(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).date(); }
} // namespace

// ---- App.stats -----------------------------------------------------------------------

LocalStats::LocalStats(StatsSource* source, QObject* parent) : QObject(parent), m_source(source)
{
    if (m_source)
        m_source->setChangedHandler([this] {
            ++m_revision;
            emit changed();
        });
}

void LocalStats::clear()
{
    if (!m_source || m_clearing.pending())
        return;
    m_clearing = m_source->clear([this](Outcome<bool> outcome) {
        m_clearing.release();
        emit clearingChanged();
        emit cleared(outcome.value.value_or(false));
    });
    emit clearingChanged();
}

// ---- the page --------------------------------------------------------------------------

StatsPageModel::StatsPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
    m_changed.setSingleShot(true);
    m_changed.setInterval(2000);
    connect(&m_changed, &QTimer::timeout, this, [this] {
        if (state() != loadstate::Idle && state() != loadstate::Loading)
            refresh();
    });
    if (App* app = App::instance())
        connect(app->stats(), &LocalStats::changed, this, [this] { m_changed.start(); });
    followAccount();
}

void StatsPageModel::componentComplete()
{
    m_complete = true;
    reload();
}

QString StatsPageModel::formatDuration(qint64 ms)
{
    ms = std::max<qint64>(ms, 0);
    if (ms < kMinute)
        return tr("%1 秒").arg(ms / 1000);
    if (ms < kHour)
        return tr("%1 分钟").arg(ms / kMinute);
    const qint64 hours = ms / kHour, minutes = ms % kHour / kMinute;
    if (hours >= 100 || minutes == 0)
        return tr("%1 小时").arg(hours);
    return tr("%1 小时 %2 分").arg(hours).arg(minutes);
}

void StatsPageModel::setRange(int range)
{
    range = std::clamp(range, int(Week), int(AllTime));
    if (range != m_range)
        navigate(range, 0);
}

void StatsPageModel::setOffset(int offset)
{
    if (std::min(offset, 0) != m_offset)
        navigate(m_range, offset);
}

void StatsPageModel::setByTime(bool byTime)
{
    if (byTime == m_byTime)
        return;
    m_byTime = byTime;
    emit byTimeChanged();
    if (state() != loadstate::Idle)
        refresh();
}

void StatsPageModel::setSongLimit(int limit)
{
    limit = std::clamp(limit, 5, 100);
    if (limit == m_songLimit)
        return;
    m_songLimit = limit;
    emit songLimitChanged();
    if (state() != loadstate::Idle)
        refresh();
}

void StatsPageModel::navigate(int range, int offset)
{
    m_range = range;
    m_offset = std::min(offset, 0);
    m_compareText.clear();
    m_compareDirection = 0;
    emit compareChanged();
    emit rangeChanged();
    // The new range replaces the old one in place: no skeleton (§5).
    if (!m_complete)
        return;
    if (state() == loadstate::Idle)
        reload();
    else
        refresh();
}

void StatsPageModel::goBack()
{
    if (canGoBack())
        navigate(m_range, m_offset - 1);
}

void StatsPageModel::goForward()
{
    if (canGoForward())
        navigate(m_range, m_offset + 1);
}

void StatsPageModel::goToCurrent()
{
    if (m_offset != 0)
        navigate(m_range, 0);
}

std::optional<StatsPageModel::Span> StatsPageModel::span(int offset) const
{
    const QDate today = QDate::currentDate();
    switch (m_range) {
    case Week: {
        const QDate first = today.addDays(1 - today.dayOfWeek()).addDays(7 * qint64(offset));
        return Span{first, first.addDays(7)};
    }
    case Month: {
        const QDate first = QDate(today.year(), today.month(), 1).addMonths(offset);
        return Span{first, first.addMonths(1)};
    }
    case Year: {
        const QDate first(today.year() + offset, 1, 1);
        return Span{first, first.addYears(1)};
    }
    default:
        return std::nullopt;
    }
}

bool StatsPageModel::canGoBack() const
{
    const auto current = span(m_offset);
    return current && hasRecords() && current->first > localDate(m_stats.firstRecordAtMs);
}

StatsQuery StatsPageModel::queryOf(const std::optional<Span>& span) const
{
    StatsQuery query;
    if (span) {
        query.fromMs = span->first.startOfDay().toMSecsSinceEpoch();
        query.toMs = span->end.startOfDay().toMSecsSinceEpoch();
    }
    query.periodSize = m_range == Week || m_range == Month ? StatsQuery::Day : StatsQuery::Month;
    query.byTime = m_byTime;
    query.limit = m_songLimit;
    return query;
}

QString StatsPageModel::periodTitle() const
{
    const auto current = span(m_offset);
    if (!current)
        return {};
    switch (m_range) {
    case Week: {
        const QDate last = current->end.addDays(-1);
        if (current->first.year() != last.year())
            return tr("%1年%2–%3年%4")
                .arg(current->first.year())
                .arg(monthDay(current->first))
                .arg(last.year())
                .arg(monthDay(last));
        if (current->first.month() != last.month())
            return tr("%1–%2").arg(monthDay(current->first), monthDay(last));
        return tr("%1–%2日").arg(monthDay(current->first)).arg(last.day());
    }
    case Month:
        return tr("%1年%2月").arg(current->first.year()).arg(current->first.month());
    default:
        return tr("%1年").arg(current->first.year());
    }
}

QString StatsPageModel::currentText() const
{
    if (m_offset == 0)
        return {};
    switch (m_range) {
    case Week:
        return tr("回到本周");
    case Month:
        return tr("回到本月");
    case Year:
        return tr("回到今年");
    default:
        return {};
    }
}

QString StatsPageModel::headline() const
{
    if (m_range == AllTime)
        return tr("一共听了");
    if (m_offset != 0)
        return tr("%1听了").arg(periodTitle());
    switch (m_range) {
    case Week:
        return tr("本周听了");
    case Month:
        return tr("本月听了");
    default:
        return tr("今年听了");
    }
}

QVariantList StatsPageModel::durationParts() const
{
    const qint64 ms = m_stats.total.playedMs;
    const auto part = [](qint64 value, const QString& unit) {
        return QVariantMap{{QStringLiteral("value"), QString::number(value)},
                           {QStringLiteral("unit"), unit}};
    };
    if (ms < kMinute)
        return {part(ms / 1000, tr("秒"))};
    if (ms < kHour)
        return {part(ms / kMinute, tr("分钟"))};
    const qint64 hours = ms / kHour, minutes = ms % kHour / kMinute;
    if (hours >= 100 || minutes == 0)
        return {part(hours, tr("小时"))};
    return {part(hours, tr("小时")), part(minutes, tr("分"))};
}

QString StatsPageModel::durationText() const { return formatDuration(m_stats.total.playedMs); }

QString StatsPageModel::sinceText() const
{
    if (!hasRecords())
        return {};
    const QDate first = localDate(m_stats.firstRecordAtMs);
    return tr("%1年%2月%3日起").arg(first.year()).arg(first.month()).arg(first.day());
}

Request StatsPageModel::load()
{
    m_comparison.cancel();
    return App::instance()->statsSource()->stats(
        queryOf(span(m_offset)), handle<ListeningStats>([this](ListeningStats stats) {
            const bool empty = stats.firstRecordAtMs < 0;
            apply(std::move(stats));
            loadComparison();
            return empty;
        }));
}

void StatsPageModel::clear()
{
    m_comparison.cancel();
    apply({});
}

void StatsPageModel::apply(ListeningStats stats)
{
    m_stats = std::move(stats);
    buildBars();
    buildRankings();
    buildHeat();
    emit contentChanged();
}

void StatsPageModel::loadComparison()
{
    const auto current = span(m_offset);
    const auto previous = span(m_offset - 1);
    if (!current || !previous || m_stats.records == 0) {
        m_compareText.clear();
        m_compareDirection = 0;
        emit compareChanged();
        return;
    }
    // A period still running is compared with the same stretch of the one before.
    const QDate today = QDate::currentDate();
    const bool running = today >= current->first && today < current->end;
    Span stretch = *previous;
    if (running)
        stretch.end =
            std::min(previous->first.addDays(current->first.daysTo(today) + 1), previous->end);
    StatsQuery query = queryOf(stretch);
    query.limit = 1;
    const qint64 now = m_stats.total.playedMs;
    m_comparison = App::instance()->statsSource()->stats(
        query, [this, running, now](Outcome<ListeningStats> outcome) {
            m_comparison.release();
            m_compareText.clear();
            m_compareDirection = 0;
            if (outcome.value && outcome.value->total.playedMs > 0) {
                const QString before = m_range == Week    ? tr("上周")
                                       : m_range == Month ? tr("上月")
                                                          : tr("去年");
                const QString than = running ? tr("%1同期").arg(before) : before;
                const qint64 diff = now - outcome.value->total.playedMs;
                if (std::abs(diff) < kMinute) {
                    m_compareText = tr("和%1差不多").arg(than);
                } else {
                    m_compareDirection = diff > 0 ? 1 : -1;
                    m_compareText = (diff > 0 ? tr("比%1多 %2") : tr("比%1少 %2"))
                                        .arg(than, formatDuration(std::abs(diff)));
                }
            }
            emit compareChanged();
        });
}

void StatsPageModel::buildBars()
{
    m_bars.clear();
    m_grid.clear();
    m_peak = -1;
    m_peakText.clear();
    QHash<QDate, StatsAmount> byStart;
    for (const auto& period : std::as_const(m_stats.periods))
        byStart.insert(period.start, period.amount);
    const QDate today = QDate::currentDate();
    const QDate thisMonth(today.year(), today.month(), 1);
    struct Bar {
        QString label, when;
        StatsAmount amount;
        bool future = false;
        int range = -1, offset = 0; // what clicking it opens; -1 nothing
    };
    QList<Bar> bars;
    const auto current = span(m_offset);
    if (m_range == Week || m_range == Month) {
        for (QDate day = current->first; day < current->end; day = day.addDays(1)) {
            Bar bar;
            if (m_range == Week)
                bar.label = weekdayLetter(day.dayOfWeek());
            else if (day.day() % 7 == 1)
                bar.label = QString::number(day.day());
            bar.when = m_range == Week ? weekdayName(day.dayOfWeek()) : monthDay(day);
            bar.amount = byStart.value(day);
            bar.future = day > today;
            bars.append(bar);
        }
    } else {
        QDate first = current ? current->first : QDate();
        QDate end = current ? current->end : thisMonth.addMonths(1);
        if (!current) // all time: from the first record's month
            first = hasRecords() ? QDate(localDate(m_stats.firstRecordAtMs).year(),
                                         localDate(m_stats.firstRecordAtMs).month(), 1)
                                 : thisMonth;
        const qint64 months = (end.year() - first.year()) * 12 + end.month() - first.month();
        if (!current && months > 36) { // by year
            for (int year = first.year(); year <= today.year(); ++year) {
                Bar bar;
                bar.label = QString::number(year);
                bar.when = tr("%1年").arg(year);
                for (int month = 1; month <= 12; ++month) {
                    const auto amount = byStart.value(QDate(year, month, 1));
                    bar.amount.playedMs += amount.playedMs;
                    bar.amount.plays += amount.plays;
                }
                bar.range = Year;
                bar.offset = year - today.year();
                bars.append(bar);
            }
        } else {
            for (QDate month = first; month < end; month = month.addMonths(1)) {
                Bar bar;
                if (current || months <= 12)
                    bar.label = tr("%1月").arg(month.month());
                else if (month.month() == 1 || month == first)
                    bar.label = QString::number(month.year());
                bar.when = tr("%1年%2月").arg(month.year()).arg(month.month());
                bar.amount = byStart.value(month);
                bar.future = month > thisMonth;
                if (!bar.future) {
                    bar.range = Month;
                    bar.offset =
                        int((month.year() - today.year()) * 12 + month.month() - today.month());
                }
                bars.append(bar);
            }
        }
    }

    qint64 top = 0;
    for (qsizetype i = 0; i < bars.size(); ++i)
        if (bars[i].amount.playedMs > top) {
            top = bars[i].amount.playedMs;
            m_peak = int(i);
        }
    for (const auto& bar : std::as_const(bars))
        m_bars.append(QVariantMap{{QStringLiteral("label"), bar.label},
                                  {QStringLiteral("ms"), double(bar.amount.playedMs)},
                                  {QStringLiteral("plays"), bar.amount.plays},
                                  {QStringLiteral("future"), bar.future},
                                  {QStringLiteral("clickable"), bar.range >= 0},
                                  {QStringLiteral("range"), bar.range},
                                  {QStringLiteral("offset"), bar.offset},
                                  {QStringLiteral("tip"), amountTip(bar.when, bar.amount)}});
    if (top <= 0) {
        m_chartTop = double(kHour);
        return;
    }
    m_peakText = formatDuration(top);
    const qint64 step = gridStep(top);
    const qint64 lines = (top + step - 1) / step;
    m_chartTop = double(lines * step);
    for (qint64 i = 1; i <= lines; ++i)
        m_grid.append(QVariantMap{{QStringLiteral("ms"), double(i * step)},
                                  {QStringLiteral("text"), axisText(i * step)}});
}

void StatsPageModel::openBar(int index)
{
    if (index < 0 || index >= m_bars.size())
        return;
    const QVariantMap bar = m_bars.at(index).toMap();
    const int range = bar.value(QStringLiteral("range")).toInt();
    if (range >= 0)
        navigate(range, bar.value(QStringLiteral("offset")).toInt());
}

void StatsPageModel::buildRankings()
{
    const auto value = [this](const StatsAmount& amount) {
        return m_byTime ? double(amount.playedMs) : double(amount.plays);
    };
    const auto text = [this](const StatsAmount& amount) {
        return m_byTime ? formatDuration(amount.playedMs) : tr("%1 次").arg(amount.plays);
    };
    const auto top = [&](const auto& rows) {
        double best = 0;
        for (const auto& row : rows)
            best = std::max(best, value(row.amount));
        return best > 0 ? best : 1.0;
    };

    QStringList keys;
    QList<SongInfo> infos;
    m_songValues.clear();
    const double songTop = top(m_stats.topSongs);
    for (const auto& row : std::as_const(m_stats.topSongs)) {
        SongInfo info = row.song;
        if (!row.known) { // still being filled in, or gone from the service
            info.title = tr("未知歌曲");
            info.unavailable = true;
        }
        info.reason = text(row.amount); // the full ranking's value column
        keys.append(info.key);
        infos.append(info);
        m_songValues.append(QVariantMap{{QStringLiteral("fraction"), value(row.amount) / songTop},
                                        {QStringLiteral("text"), text(row.amount)}});
    }
    m_songs->reset(keys, infos);

    const auto items = [&](const QList<StatsItem>& rows) {
        QVariantList list;
        const double best = top(rows);
        for (const auto& row : rows.mid(0, 5))
            list.append(QVariantMap{{QStringLiteral("key"), row.key},
                                    {QStringLiteral("title"), row.title},
                                    {QStringLiteral("coverKey"), row.coverKey},
                                    {QStringLiteral("fraction"), value(row.amount) / best},
                                    {QStringLiteral("text"), text(row.amount)},
                                    {QStringLiteral("clickable"), !row.key.isEmpty()},
                                    {QStringLiteral("kind"), -1}});
        return list;
    };
    m_artists = items(m_stats.topArtists);
    m_albums = items(m_stats.topAlbums);

    m_origins.clear();
    const double originTop = top(m_stats.topOrigins);
    const QString account = App::instance() ? App::instance()->session()->accountKey() : QString();
    for (const auto& row : m_stats.topOrigins.mid(0, 5)) {
        QString title;
        bool clickable = !row.key.isEmpty();
        const auto named = [&](const QString& kind) {
            return row.name.isEmpty() ? kind : tr("%1 · %2").arg(row.name, kind);
        };
        switch (row.kind) {
        case StatsOrigin::Fm:
            title = tr("私人 FM");
            break;
        case StatsOrigin::Playlist:
            title = named(tr("歌单"));
            break;
        case StatsOrigin::Album:
            title = named(tr("专辑"));
            break;
        case StatsOrigin::Artist:
            title = named(tr("歌手"));
            break;
        case StatsOrigin::Daily:
            title = tr("每日推荐");
            break;
        case StatsOrigin::Search:
            title = tr("搜索");
            break;
        case StatsOrigin::Recent:
            title = tr("最近播放");
            break;
        case StatsOrigin::Ranking:
            title = row.key == account || row.name.isEmpty() ? tr("听歌排行")
                                                             : tr("%1 的听歌排行").arg(row.name);
            break;
        case StatsOrigin::Heartbeat:
            title = named(tr("心动推荐"));
            break;
        default:
            title = tr("其他");
            clickable = false;
        }
        m_origins.append(QVariantMap{{QStringLiteral("key"), row.key},
                                     {QStringLiteral("title"), title},
                                     {QStringLiteral("coverKey"), QString()},
                                     {QStringLiteral("fraction"), value(row.amount) / originTop},
                                     {QStringLiteral("text"), text(row.amount)},
                                     {QStringLiteral("clickable"), clickable},
                                     {QStringLiteral("own"), row.key == account},
                                     {QStringLiteral("kind"), int(row.kind)}});
    }
}

void StatsPageModel::buildHeat()
{
    m_heat.clear();
    m_heatTips.clear();
    m_heatSummary.clear();
    qint64 top = 0;
    int best = -1;
    for (int i = 0; i < 7 * 24; ++i)
        if (m_stats.weekHours[i].playedMs > top) {
            top = m_stats.weekHours[i].playedMs;
            best = i;
        }
    for (int i = 0; i < 7 * 24; ++i) {
        const auto& amount = m_stats.weekHours[i];
        int level = 0;
        if (amount.playedMs > 0) // quarters of the busiest hour
            level = amount.playedMs * 4 <= top       ? 1
                    : amount.playedMs * 2 <= top     ? 2
                    : amount.playedMs * 4 <= top * 3 ? 3
                                                     : 4;
        m_heat.append(level);
        const int hour = i % 24;
        m_heatTips.append(amountTip(
            tr("%1 %2–%3 点").arg(weekdayName(i / 24 + 1)).arg(hour).arg(hour + 1), amount));
    }
    if (best >= 0)
        m_heatSummary = tr("最常在%1 %2 点前后听").arg(weekdayName(best / 24 + 1)).arg(best % 24);
}

} // namespace cirrus::ui

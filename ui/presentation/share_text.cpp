#include "share_text.h"

#include <QCoreApplication>
#include <optional>

namespace cirrus::ui::sharing
{
namespace
{
bool isId(const QString& key)
{
    if (key.isEmpty() || key.size() > 20)
        return false;
    for (const QChar c : key)
        if (c < u'0' || c > u'9')
            return false;
    return key != QStringLiteral("0");
}

std::optional<QString> value(QStringView name, const Fields& fields)
{
    if (name == u"链接")
        return fields.link;
    if (name == u"名称")
        return fields.name;
    if (name == u"作者")
        return fields.author;
    if (name == u"类型")
        return fields.type;
    return std::nullopt;
}

struct Part {
    QString text;
    bool missing = false; // a placeholder in it came out empty
};

// From `at` up to the end, or (nested) up to the `]` closing it, which is left at `at`.
Part expandFrom(const QString& templ, qsizetype& at, const Fields& fields, bool nested)
{
    Part part;
    while (at < templ.size()) {
        const QChar c = templ.at(at);
        if (c == u']' && nested)
            return part;
        if (c == u'[') {
            qsizetype end = at + 1;
            const Part inner = expandFrom(templ, end, fields, true);
            if (end < templ.size()) { // closed
                if (!inner.missing)
                    part.text += inner.text;
                at = end + 1;
                continue;
            }
            // Never closed: the bracket is text.
        } else if (c == u'{') {
            const auto close = templ.indexOf(u'}', at + 1);
            if (close > at) {
                if (const auto v = value(QStringView(templ).mid(at + 1, close - at - 1), fields)) {
                    part.missing = part.missing || v->isEmpty();
                    part.text += *v;
                    at = close + 1;
                    continue;
                }
            }
        }
        part.text += c;
        ++at;
    }
    return part;
}
} // namespace

QString link(Kind kind, const QString& key)
{
    if (!isId(key))
        return {};
    const char* path = "song";
    switch (kind) {
    case Kind::Song:
        path = "song";
        break;
    case Kind::Playlist:
        path = "playlist";
        break;
    case Kind::Album:
        path = "album";
        break;
    case Kind::Artist:
        path = "artist";
        break;
    case Kind::Podcast:
        path = "djradio";
        break;
    case Kind::Voice:
        path = "program";
        break;
    case Kind::User:
        path = "user/home";
        break;
    }
    return QStringLiteral("https://music.163.com/%1?id=%2").arg(QLatin1StringView(path), key);
}

QString typeName(Kind kind)
{
    switch (kind) {
    case Kind::Song:
        return QCoreApplication::translate("Share", "单曲");
    case Kind::Playlist:
        return QCoreApplication::translate("Share", "歌单");
    case Kind::Album:
        return QCoreApplication::translate("Share", "专辑");
    case Kind::Artist:
        return QCoreApplication::translate("Share", "歌手");
    case Kind::Podcast:
        return QCoreApplication::translate("Share", "播客");
    case Kind::Voice:
        return QCoreApplication::translate("Share", "声音");
    case Kind::User:
        return QCoreApplication::translate("Share", "用户");
    }
    return {};
}

QString expand(const QString& templ, const Fields& fields)
{
    qsizetype at = 0;
    QString text = expandFrom(templ, at, fields, false).text.trimmed();
    if (!fields.link.isEmpty() && !text.contains(fields.link))
        text = text.isEmpty() ? fields.link : text + u' ' + fields.link;
    return text;
}
} // namespace cirrus::ui::sharing

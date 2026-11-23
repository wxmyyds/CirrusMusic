#pragma once
#include <QString>

// Share text (docs/ui/pages/share.md §1–§2): the web player's public link of an item and
// the user's template filled in with it. Pure functions, no app state.
namespace cirrus::ui::sharing
{
// Append only: Share::Kind (QML) has the same values.
enum class Kind { Song, Playlist, Album, Artist, Podcast, Voice, User };

// The template until the user writes one.
inline QString defaultTemplate()
{
    return QStringLiteral("分享[{作者}的]{类型}《{名称}》：{链接}");
}

// https://music.163.com/<path>?id=<key>; empty when `key` is no id.
QString link(Kind kind, const QString& key);
// 单曲, 歌单…
QString typeName(Kind kind);

struct Fields {
    QString link, name, author, type;
};
// `templ` with {链接} {名称} {作者} {类型} put in; a […] part is left out when a
// placeholder in it came out empty (foobar2000's way), other text stays as written. The
// link is added at the end when the result doesn't have it.
QString expand(const QString& templ, const Fields& fields);
} // namespace cirrus::ui::sharing

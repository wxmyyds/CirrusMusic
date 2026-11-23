#include "bili_wbi.h"

#include <QCryptographicHash>
#include <QList>
#include <QPair>
#include <QUrlQuery>
#include <algorithm>

namespace cirrus::bilibili
{
namespace
{
constexpr int kMixinIndex[] = {46, 47, 18, 2,  53, 8,  23, 32, 15, 50, 10, 31, 58, 3,  45, 35,
                               27, 43, 5,  49, 33, 9,  42, 19, 29, 28, 14, 39, 12, 38, 41, 13,
                               37, 48, 7,  16, 24, 55, 40, 61, 26, 17, 0,  1,  60, 51, 30, 4,
                               22, 25, 54, 21, 56, 62, 6,  63, 57, 20, 34, 52, 59, 11, 36, 44};

QString fileNameStem(const QString& urlOrName)
{
    auto name = urlOrName;
    if (const auto slash = name.lastIndexOf(u'/'); slash >= 0)
        name = name.mid(slash + 1);
    if (const auto dot = name.indexOf(u'.'); dot >= 0)
        name = name.left(dot);
    return name;
}

// The provider's value filter: !'()* are removed before encoding.
QString filterValue(QString value)
{
    for (const auto bad : {u'!', u'\'', u'(', u')', u'*'})
        value.remove(bad);
    return value;
}

QByteArray percentEncode(const QString& value)
{
    QByteArray out;
    const auto utf8 = value.toUtf8();
    for (const char c : utf8) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') ||
            u == '-' || u == '_' || u == '.' || u == '~') {
            out.append(char(u));
        } else {
            out.append('%');
            out.append(QByteArray::number(u, 16).rightJustified(2, '0').toUpper());
        }
    }
    return out;
}
} // namespace

QString WbiSigner::mixinKey(const QString& imgUrl, const QString& subUrl)
{
    const auto img = fileNameStem(imgUrl);
    const auto sub = fileNameStem(subUrl);
    if (img.size() != 32 || sub.size() != 32)
        return {};
    const auto raw = img + sub;
    QString mixed;
    mixed.reserve(std::size(kMixinIndex));
    for (const int index : kMixinIndex)
        if (index < raw.size())
            mixed.append(raw.at(index));
    return mixed.size() >= 32 ? mixed.left(32) : mixed;
}

QUrl WbiSigner::sign(const QUrl& url, const QString& mixinKey, qint64 unixSeconds)
{
    if (mixinKey.size() != 32 || !url.isValid())
        return url;
    // Existing query pairs, decoded; then wts. Sorted by key as the provider requires.
    QList<QPair<QString, QString>> params;
    for (const auto& [key, value] : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
        if (key != QStringLiteral("wts") && key != QStringLiteral("w_rid"))
            params.append({key, filterValue(value)});
    }
    params.append({QStringLiteral("wts"), QString::number(unixSeconds)});
    std::sort(params.begin(), params.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    QByteArray joined;
    for (qsizetype i = 0; i < params.size(); ++i) {
        if (i)
            joined.append('&');
        joined.append(percentEncode(params.at(i).first));
        joined.append('=');
        joined.append(percentEncode(params.at(i).second));
    }
    const auto rid = QCryptographicHash::hash(joined + mixinKey.toUtf8(),
                                              QCryptographicHash::Md5)
                         .toHex();

    QUrl signedUrl = url;
    signedUrl.setQuery(QString::fromLatin1(joined) + QStringLiteral("&w_rid=") +
                       QString::fromLatin1(rid));
    return signedUrl;
}

} // namespace cirrus::bilibili

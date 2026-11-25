#include "app_link.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace cirrus::ui::applink
{
namespace
{
constexpr qsizetype kMaximumLength = 2048;

QString digits(const QJsonValue& value)
{
    QString text;
    if (value.isString())
        text = value.toString();
    else if (value.isDouble() && value.toDouble() >= 1 && value.toDouble() <= 9007199254740991.0 &&
             value.toDouble() == double(qint64(value.toDouble())))
        text = QString::number(qint64(value.toDouble()));
    if (text.isEmpty() || text.size() > 20 || text == QStringLiteral("0"))
        return {};
    for (const QChar c : text)
        if (c < u'0' || c > u'9')
            return {};
    return text;
}

Target targetOf(const QString& type)
{
    if (type == u"song")
        return Target::Song;
    if (type == u"program" || type == u"dj")
        return Target::Voice;
    if (type == u"playlist")
        return Target::Playlist;
    if (type == u"album")
        return Target::Album;
    if (type == u"artist")
        return Target::Artist;
    if (type == u"djradio")
        return Target::Podcast;
    if (type == u"user" || type == u"user/home")
        return Target::User;
    return Target::Unsupported;
}
} // namespace

bool plausible(const QString& url)
{
    static const QRegularExpression shape(
        QStringLiteral("^orpheus:[/\\\\]{2,}[A-Za-z0-9+/=_%?&.:-]{1,%1}$").arg(kMaximumLength),
        QRegularExpression::CaseInsensitiveOption);
    return url.size() <= kMaximumLength + 16 && shape.match(url).hasMatch();
}

std::optional<Request> parse(const QString& url)
{
    if (!plausible(url))
        return std::nullopt;
    // As the client reads it: the scheme, then the payload up to `?`, trailing slashes off.
    static const QRegularExpression scheme(QStringLiteral("^orpheus:[/\\\\]{2,}"),
                                           QRegularExpression::CaseInsensitiveOption);
    QString rest = url.mid(scheme.match(url).capturedLength());
    rest = rest.section(u'?', 0, 0);
    while (rest.endsWith(u'/') || rest.endsWith(u'\\'))
        rest.chop(1);
    const QByteArray payload = QByteArray::fromPercentEncoding(rest.toLatin1());
    const auto decoded = QByteArray::fromBase64Encoding(
        payload, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    QByteArray json = decoded ? *decoded : QByteArray();
    if (!decoded) {
        const auto urlSafe = QByteArray::fromBase64Encoding(
            payload, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!urlSafe)
            return std::nullopt;
        json = *urlSafe;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    Request request;
    request.target = targetOf(object.value(QStringLiteral("type")).toString());
    request.play = object.value(QStringLiteral("cmd")).toString() == u"play";
    if (request.target != Target::Unsupported) {
        request.id = digits(object.value(QStringLiteral("id")));
        if (request.id.isEmpty())
            return std::nullopt;
    }
    return request;
}
} // namespace cirrus::ui::applink

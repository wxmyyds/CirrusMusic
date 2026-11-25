#include "update_release.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace cirrus::ui::updates
{
std::optional<std::array<int, 3>> parseVersion(QStringView text)
{
    if (text.startsWith(u'v'))
        text = text.mid(1);
    const auto parts = text.split(u'.');
    if (parts.size() != 3)
        return std::nullopt;
    std::array<int, 3> version{};
    for (int i = 0; i < 3; ++i) {
        // Digits only: toInt() would take "+1" or " 1".
        if (parts[i].isEmpty() || parts[i].size() > 6)
            return std::nullopt;
        for (QChar c : parts[i])
            if (c < u'0' || c > u'9')
                return std::nullopt;
        version[i] = parts[i].toInt();
    }
    return version;
}

bool isNewer(QStringView latest, QStringView current)
{
    const auto a = parseVersion(latest);
    const auto b = parseVersion(current);
    return a && b && *a > *b;
}

bool isReleasePage(const QUrl& url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") &&
           url.host() == QLatin1String("github.com") && url.port() == -1 &&
           url.userInfo().isEmpty() &&
           url.path().startsWith(QLatin1String("/wxmyyds/CirrusMusic/releases/"));
}

bool isDownloadUrl(const QUrl& url, QStringView version, QStringView name)
{
    return url.isValid() && url.scheme() == QLatin1String("https") &&
           url.host() == QLatin1String("github.com") && url.port() == -1 &&
           url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
           url.path() ==
               QStringLiteral("/wxmyyds/CirrusMusic/releases/download/v%1/%2").arg(version, name);
}

QString installerName(QStringView version)
{
    return QStringLiteral("Cirrus-%1-windows-x64-setup.exe").arg(version);
}

namespace
{
constexpr qint64 kMaximumInstallerBytes = 512LL * 1024 * 1024;

std::optional<Asset> installerOf(const QJsonArray& assets, const QString& version)
{
    const QString name = installerName(version);
    for (const auto& value : assets) {
        const QJsonObject object = value.toObject();
        if (object.value(QLatin1String("name")).toString() != name)
            continue;
        Asset asset;
        asset.name = name;
        asset.url = QUrl(object.value(QLatin1String("browser_download_url")).toString());
        asset.size = qint64(object.value(QLatin1String("size")).toDouble());
        const QString digest = object.value(QLatin1String("digest")).toString();
        static const QRegularExpression sha256(QStringLiteral("^sha256:([0-9a-f]{64})$"));
        const auto match = sha256.match(digest);
        if (!isDownloadUrl(asset.url, version, name) || asset.size <= 0 ||
            asset.size > kMaximumInstallerBytes || !match.hasMatch())
            return std::nullopt;
        asset.sha256 = QByteArray::fromHex(match.captured(1).toLatin1());
        return asset;
    }
    return std::nullopt;
}
} // namespace

QString notesToPlainText(const QString& markdown)
{
    static const QRegularExpression link(QStringLiteral(R"(!?\[([^\]]*)\]\([^)]*\))"));
    static const QRegularExpression marks(QStringLiteral(R"(\*\*|__|`)"));
    static const QRegularExpression heading(QStringLiteral(R"(^#{1,6}\s+)"));
    static const QRegularExpression item(QStringLiteral(R"(^(\s*)[-*+]\s+)"));
    static const QRegularExpression tag(QStringLiteral(R"(<[^>]*>)"));
    QStringList out;
    for (QString line : markdown.split(u'\n')) {
        line.remove(u'\r');
        line.replace(link, QStringLiteral("\\1"));
        line.remove(tag);
        line.remove(marks);
        if (line.contains(heading)) {
            line.remove(heading);
        } else if (const auto match = item.match(line); match.hasMatch()) {
            line = match.captured(1) + QStringLiteral("• ") + line.mid(match.capturedEnd());
        }
        out << (line.trimmed().isEmpty() ? QString() : line);
    }
    // At most one empty line in a row, none at the ends.
    QString text = out.join(u'\n');
    static const QRegularExpression blanks(QStringLiteral("\n{3,}"));
    text.replace(blanks, QStringLiteral("\n\n"));
    return text.trimmed();
}

std::optional<Release> parseRelease(const QByteArray& json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    if (object.value(QLatin1String("draft")).toBool() ||
        object.value(QLatin1String("prerelease")).toBool())
        return std::nullopt;
    const QString tag = object.value(QLatin1String("tag_name")).toString();
    if (!parseVersion(tag))
        return std::nullopt;
    Release release;
    release.version = tag.startsWith(u'v') ? tag.mid(1) : tag;
    release.title = object.value(QLatin1String("name")).toString();
    if (release.title.isEmpty())
        release.title = QStringLiteral("Cirrus %1").arg(release.version);
    release.notes = notesToPlainText(object.value(QLatin1String("body")).toString());
    release.page = QUrl(object.value(QLatin1String("html_url")).toString());
    release.published =
        QDateTime::fromString(object.value(QLatin1String("published_at")).toString(), Qt::ISODate);
    if (!isReleasePage(release.page))
        return std::nullopt;
    release.installer =
        installerOf(object.value(QLatin1String("assets")).toArray(), release.version);
    return release;
}
} // namespace cirrus::ui::updates

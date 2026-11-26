#include "bili_playurl.h"
#include "bili_quality.h"

#include <QJsonArray>
#include <algorithm>

namespace cirrus::bilibili
{
namespace
{
QString text(const QJsonObject& object, const char* key)
{
    const auto value = object.value(QLatin1String(key));
    return value.isString() ? value.toString() : QString{};
}

QJsonObject objectOr(const QJsonObject& object, const char* key)
{
    return object.value(QLatin1String(key)).toObject();
}

QStringList stringList(const QJsonArray& array)
{
    QStringList out;
    for (const auto& value : array)
        if (value.isString() && !value.toString().isEmpty())
            out.append(value.toString());
    return out;
}

std::optional<AudioStream> dashItem(const QJsonObject& object)
{
    const auto baseUrl = object.contains(QStringLiteral("baseUrl"))
                             ? text(object, "baseUrl")
                             : text(object, "base_url");
    if (baseUrl.isEmpty())
        return std::nullopt;
    QStringList backups;
    const auto backupArray = object.contains(QStringLiteral("backupUrl"))
                                 ? object.value(QStringLiteral("backupUrl")).toArray()
                                 : object.value(QStringLiteral("backup_url")).toArray();
    backups = stringList(backupArray);

    AudioStream stream;
    stream.id = object.value(QStringLiteral("id")).toInt();
    stream.mimeType = object.contains(QStringLiteral("mimeType"))
                          ? text(object, "mimeType")
                          : text(object, "mime_type");
    stream.codecs = text(object, "codecs");
    stream.bitrateKbps = int(std::clamp<qint64>(
        object.value(QStringLiteral("bandwidth")).toInteger() / 1000, 0, 2147483647));
    stream.candidates = prioritize(baseUrl, backups);
    stream.url = stream.candidates.isEmpty() ? baseUrl : stream.candidates.first();
    stream.size = std::max<qint64>(0, object.value(QStringLiteral("size")).toInteger());
    const auto md5 = text(object, "md5").toLower().toLatin1();
    if (md5.size() == 32 && std::all_of(md5.begin(), md5.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        stream.md5 = md5;
    return stream;
}

QList<AudioStream> dashArray(const QJsonArray& array, const QString& tag)
{
    QList<AudioStream> out;
    for (const auto& value : array) {
        auto stream = dashItem(value.toObject());
        if (!stream)
            continue;
        if (!tag.isEmpty())
            stream->qualityTag = tag;
        out.append(*stream);
    }
    return out;
}

int boundedInt(const QJsonValue& value)
{
    return value.isDouble() ? std::max(0, int(value.toInteger())) : 0;
}
} // namespace

PlayUrlMapping mapPlayUrl(const QJsonObject& data)
{
    PlayUrlMapping mapping;
    auto& result = mapping.result;
    result.durationMs = qint64(boundedInt(data.value(QStringLiteral("timelength"))));

    const auto dash = objectOr(data, "dash");
    result.streams += dashArray(dash.value(QStringLiteral("audio")).toArray(), {});
    const auto dolby = objectOr(dash, "dolby");
    result.streams += dashArray(dolby.value(QStringLiteral("audio")).toArray(),
                                QStringLiteral("dolby"));
    const auto flac = objectOr(dash, "flac");
    if (const auto single = dashItem(objectOr(flac, "audio")); single && !single->url.isEmpty()) {
        AudioStream stream = *single;
        stream.qualityTag = QStringLiteral("hires");
        result.streams.append(stream);
    }

    const auto durl = data.value(QStringLiteral("durl")).toArray();
    result.videoAvailable =
        !dash.value(QStringLiteral("video")).toArray().isEmpty() || !durl.isEmpty();

    if (result.streams.isEmpty()) {
        // Only the single whole-file mp4 is a real fallback; a multi-part durl is not audio.
        if (durl.size() == 1 && durl.first().isObject()) {
            const auto item = durl.first().toObject();
            const auto url = text(item, "url");
            if (!url.isEmpty()) {
                AudioStream stream;
                const auto lengthMs = item.value(QStringLiteral("length")).toInteger();
                const auto size = item.value(QStringLiteral("size")).toInteger();
                stream.bitrateKbps =
                    lengthMs > 0 ? int(std::max<qint64>(0, size * 8 / lengthMs)) : 0;
                stream.size = std::max<qint64>(0, size);
                stream.mimeType = QStringLiteral("video/mp4");
                const auto backups = item.contains(QStringLiteral("backup_url"))
                                         ? item.value(QStringLiteral("backup_url")).toArray()
                                         : item.value(QStringLiteral("backupUrl")).toArray();
                stream.candidates = prioritize(url, stringList(backups));
                stream.url = stream.candidates.isEmpty() ? url : stream.candidates.first();
                result.streams.append(stream);
            }
        }
    }
    return mapping;
}

} // namespace cirrus::bilibili

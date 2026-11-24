#pragma once
#include "application/ports/music_gateway.h"
#include "bili_http_client.h"

#include <QHash>
#include <QJsonObject>
#include <memory>

namespace cirrus::bilibili
{
std::optional<QUrl> subtitleUrl(const QJsonObject& data);
domain::LyricTrack subtitleTrack(const QJsonObject& data);

class Lyrics final : public QObject
{
    Q_OBJECT
  public:
    explicit Lyrics(QObject* parent = nullptr);
    explicit Lyrics(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent = nullptr);
    void setCookie(QByteArray cookie) { m_cookie = std::move(cookie); }
    quint64 request(domain::PlaybackSourceSelection source);
    void cancel(quint64 id);

  signals:
    void completed(quint64 id, cirrus::application::MusicResult result);

  private:
    struct Pending {
        domain::PlaybackSourceSelection source;
        quint64 httpId = 0;
        bool subtitle = false;
    };
    void received(quint64 httpId, HttpOutcome outcome);
    void finish(quint64 id, application::MusicResult result);
    std::unique_ptr<HttpClient> m_http;
    QByteArray m_cookie;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_httpIds;
    quint64 m_nextId = 0;
};
} // namespace cirrus::bilibili

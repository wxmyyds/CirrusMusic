#pragma once
#include "application/ports/music_gateway.h"
#include "application/ports/playback.h"
#include "bili_api.h"

#include <QHash>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

namespace cirrus::bilibili
{

// The Bilibili provider behind the one resolver interface. A StreamQuery names a NetEase song
// by id only, so this first asks the catalog for that same song's title/artist/duration (a
// detail read, not a provider fallback), then searches Bilibili for it. The answer becomes a
// stream-addressed StreamSource: identity is the provider's name for the stream, never a
// digest; the source carries the two request headers its CDN needs and the bvid/cid for the
// lyrics path. Bilibili needs no NetEase token, so any accepted session may resolve one.
class StreamResolver final : public application::IStreamResolver
{
    Q_OBJECT
  public:
    StreamResolver(application::IMusicGateway& catalog, std::unique_ptr<BiliApi> api,
                   QObject* parent = nullptr);
    ~StreamResolver() override;

    quint64 resolve(domain::SessionToken context, domain::StreamQuery query) override;
    void cancel(quint64 id) override;
    void cancelAll(); // the owning gateway is going away before this resolver answers

    // A configured login cookie ("k=v; k2=v2"); empty stays anonymous.
    void setCookie(QByteArray cookie)
    {
        if (m_api)
            m_api->setCookie(std::move(cookie));
    }

  private:
    struct Pending {
        domain::SessionToken context;
        domain::StreamQuery query;
        quint64 gatewayId = 0; // the detail read in flight
        quint64 apiId = 0;     // the Bilibili resolve in flight
    };
    void detailed(quint64 id, domain::SessionToken context, application::MusicResult result);
    void resolved(quint64 apiId, BiliApi::Result result);
    void finish(quint64 id, application::StreamResult result);
    void fail(quint64 id, domain::PlaybackError error);

    application::IMusicGateway& m_catalog;
    std::unique_ptr<BiliApi> m_api;
    QHash<quint64, Pending> m_pending;    // ours
    QHash<quint64, quint64> m_gatewayIds; // catalog id -> ours
    QHash<quint64, quint64> m_apiIds;     // Bilibili id -> ours
    quint64 m_nextId = 0;
};

} // namespace cirrus::bilibili

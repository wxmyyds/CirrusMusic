#pragma once
#include "application/ports/playback.h"

#include <QHash>

namespace cirrus::application
{

// The one routing table from a StreamQuery to its single provider, implemented as an
// IStreamResolver so PlaybackCoordinator keeps depending on the interface alone. NetEase is
// asked first; only when it says NeedPermission, or hands back a preview-only source, and the
// query allows it, does Bilibili become THE provider for this song. Once chosen, a Bilibili
// failure is that song's failure (no retry, no NetEase preview fallback). Everything else —
// network, timeout, rate limit, decode — stays NetEase's own failure and is not switched.
class ProviderRouter final : public IStreamResolver
{
    Q_OBJECT
  public:
    ProviderRouter(IStreamResolver& netease, IStreamResolver& bilibili, QObject* parent = nullptr);
    ~ProviderRouter() override;

    quint64 resolve(domain::SessionToken context, domain::StreamQuery query) override;
    void cancel(quint64 id) override;

  private:
    struct Pending {
        domain::SessionToken context;
        domain::StreamQuery query;
        quint64 neteaseId = 0;
        quint64 biliId = 0;
    };
    void neteaseDone(quint64 neteaseId, domain::SessionToken context,
                     application::StreamResult result);
    void biliDone(quint64 biliId, domain::SessionToken context, application::StreamResult result);
    void finish(quint64 id, domain::SessionToken context, application::StreamResult result);

    IStreamResolver& m_netease;
    IStreamResolver& m_bili;
    QHash<quint64, Pending> m_pending;    // ours
    QHash<quint64, quint64> m_neteaseIds; // NetEase's -> ours
    QHash<quint64, quint64> m_biliIds;    // Bilibili's -> ours
    quint64 m_nextId = 0;
};

} // namespace cirrus::application

#include "provider_router.h"
#include "logging/log.h"

#include <QThread>
#include <QTimer>

#include <utility>
#include <variant>

namespace cirrus::application
{
using namespace domain;
namespace
{
bool withheld(const StreamResult& result)
{
    const auto* failure = std::get_if<PlaybackFailure>(&result);
    return failure && failure->error == PlaybackError::NeedPermission;
}

bool previewOnly(const StreamResult& result)
{
    const auto* source = std::get_if<StreamSource>(&result);
    return source && source->preview.has_value();
}
} // namespace

ProviderRouter::ProviderRouter(IStreamResolver& netease, IStreamResolver& bilibili, QObject* parent)
    : IStreamResolver(parent), m_netease(netease), m_bili(bilibili)
{
    Q_ASSERT(netease.thread() == thread() && bilibili.thread() == thread());
    connect(&m_netease, &IStreamResolver::completed, this, &ProviderRouter::neteaseDone);
    connect(&m_bili, &IStreamResolver::completed, this, &ProviderRouter::biliDone);
}

ProviderRouter::~ProviderRouter()
{
    disconnect(&m_netease, nullptr, this, nullptr);
    disconnect(&m_bili, nullptr, this, nullptr);
    const auto pending = std::exchange(m_pending, {});
    for (const auto& request : pending) {
        if (request.neteaseId)
            m_netease.cancel(request.neteaseId);
        if (request.biliId)
            m_bili.cancel(request.biliId);
    }
}

quint64 ProviderRouter::resolve(SessionToken context, StreamQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 16)
        return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, Pending{context, query});
    const auto neteaseId = m_netease.resolve(context, query);
    if (!neteaseId) {
        QTimer::singleShot(0, this, [this, id, context] {
            finish(id, context, PlaybackFailure{PlaybackError::Unavailable});
        });
        return id;
    }
    m_pending[id].neteaseId = neteaseId;
    m_neteaseIds.insert(neteaseId, id);
    return id;
}

void ProviderRouter::neteaseDone(quint64 neteaseId, SessionToken context, StreamResult result)
{
    const auto ours = m_neteaseIds.take(neteaseId);
    if (!ours)
        return;
    auto it = m_pending.find(ours);
    if (it == m_pending.end())
        return;
    it->neteaseId = 0;
    if (context != it->context) {
        finish(ours, it->context, PlaybackFailure{PlaybackError::StaleContext});
        return;
    }
    const auto* source = std::get_if<StreamSource>(&result);
    if (source && (source->song != it->query.song || source->provider != SourceProvider::Netease)) {
        finish(ours, context, PlaybackFailure{PlaybackError::InvalidResponse});
        return;
    }
    if (!it->query.allowBiliSource || (!withheld(result) && !previewOnly(result))) {
        finish(ours, context, std::move(result));
        return;
    }
    log::debug(lcPlayback, "router.to_bili",
               {{"song", it->query.song.value}, {"perm", withheld(result)},
                {"preview", previewOnly(result)}});
    const auto biliId = m_bili.resolve(it->context, it->query);
    if (!biliId) {
        finish(ours, context, PlaybackFailure{PlaybackError::Unavailable});
        return;
    }
    it->biliId = biliId;
    m_biliIds.insert(biliId, ours);
}

void ProviderRouter::biliDone(quint64 biliId, SessionToken context, StreamResult result)
{
    const auto ours = m_biliIds.take(biliId);
    if (!ours)
        return;
    auto it = m_pending.find(ours);
    if (it == m_pending.end())
        return;
    it->biliId = 0;
    const auto* source = std::get_if<StreamSource>(&result);
    if (source && (source->song != it->query.song || source->provider != SourceProvider::Bilibili))
        result = PlaybackFailure{PlaybackError::InvalidResponse};
    finish(ours, context, std::move(result));
}

void ProviderRouter::finish(quint64 id, SessionToken context, StreamResult result)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const auto original = it->context;
    m_pending.erase(it);
    if (context != original)
        result = PlaybackFailure{PlaybackError::StaleContext};
    emit completed(id, original, std::move(result));
}

void ProviderRouter::cancel(quint64 id)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const auto pending = *it;
    m_pending.erase(it);
    m_neteaseIds.remove(pending.neteaseId);
    m_biliIds.remove(pending.biliId);
    if (pending.neteaseId)
        m_netease.cancel(pending.neteaseId);
    if (pending.biliId)
        m_bili.cancel(pending.biliId);
}

} // namespace cirrus::application

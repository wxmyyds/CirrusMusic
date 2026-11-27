#include "account_membership.h"

#include "session.h"
#include "sources.h"

namespace cirrus::ui
{

bool MembershipScan::accept(const Outcome<ResultPage<CollectionItem>>& outcome)
{
    ++m_pages;
    m_partial = m_partial || outcome.partial;
    if (outcome.failure || !outcome.value)
        return false;
    const auto& result = *outcome.value;
    for (const auto& item : result.items) {
        if (item.kind != m_kind || item.key != m_key)
            continue;
        if (m_kind == collection::Playlist) {
            // Presence without created is not ownership or subscription evidence.
            m_evidence.own = item.created;
            if (item.created)
                m_evidence.saved = !*item.created;
        } else {
            m_evidence.saved = true;
        }
        return false;
    }
    const bool complete =
        result.hasMore == false || (!result.hasMore && result.items.isEmpty());
    if (complete) {
        if (!m_partial) {
            m_evidence.saved = false;
            if (m_kind == collection::Playlist)
                m_evidence.own = false;
        }
        return false;
    }
    // An empty page claiming more, no progress, or the bound cannot prove absence.
    const qint64 next = result.next.value_or(m_offset + result.items.size());
    if (result.items.isEmpty() || next <= m_offset || next >= PageSize * MaxPages ||
        m_pages >= MaxPages)
        return false;
    m_offset = int(next);
    return true;
}

AccountMembership::AccountMembership(QObject* parent, std::function<void()> changed)
    : QObject(parent), m_changed(std::move(changed))
{
}

void AccountMembership::start(LibrarySource* source, Session* session, collection::Kind kind,
                              const QString& key)
{
    start({[session] { return session->signedIn() ? session->accountKey() : QString(); },
           [source](collection::Kind kind, PageRequest page,
                    Reply<ResultPage<CollectionItem>> reply) {
               return source->libraryCollections(kind, page, std::move(reply));
           },
           [source](Reply<CollectionItem> reply) { return source->likedPlaylist(std::move(reply)); }},
          kind, key);
}

void AccountMembership::start(Queries queries, collection::Kind kind, const QString& key)
{
    clear();
    m_queries = std::move(queries);
    m_account = m_queries.account();
    m_kind = kind;
    m_key = key;
    if (m_account.isEmpty() || key.isEmpty())
        return;
    m_scan.emplace(kind, key);
    readPage();
    if (kind == collection::Playlist) {
        m_likedRequest = m_queries.liked(guard<CollectionItem>([this](Outcome<CollectionItem> outcome) {
            m_likedRequest.release();
            if (!outcome.failure && outcome.value && !outcome.partial &&
                outcome.value->kind == collection::Playlist && !outcome.value->key.isEmpty()) {
                m_evidence.liked = outcome.value->key == m_key;
                m_changed();
            }
        }));
    }
}

void AccountMembership::readPage()
{
    m_collectionsRequest = m_queries.collections(
        m_kind, m_scan->page(),
        guard<ResultPage<CollectionItem>>([this](Outcome<ResultPage<CollectionItem>> outcome) {
            m_collectionsRequest.release();
            if (m_scan->accept(outcome)) {
                readPage();
                return;
            }
            const auto evidence = m_scan->evidence();
            m_evidence.own = evidence.own;
            m_evidence.saved = evidence.saved;
            m_changed();
        }));
}

void AccountMembership::clear()
{
    ++m_generation; // Invalidate before cancellation, including account A -> B -> A.
    m_collectionsRequest.cancel();
    m_likedRequest.cancel();
    m_scan.reset();
    m_evidence = {};
    m_account.clear();
    m_changed();
}

MembershipEvidence AccountMembership::evidence() const
{
    if (m_account.isEmpty() || !m_queries.account || m_queries.account() != m_account)
        return {};
    return m_evidence;
}

void AccountMembership::forgetSaved()
{
    m_evidence.saved.reset();
    m_changed();
}

} // namespace cirrus::ui

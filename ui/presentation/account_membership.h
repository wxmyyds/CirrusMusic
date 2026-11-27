#pragma once

#include "request.h"
#include "types.h"

#include <QPointer>

namespace cirrus::ui
{

class LibrarySource;
class Session;

// Account evidence, never inferred from public catalog metadata. Disengaged means
// unknown, including failed/truncated reads; absence requires a complete library scan.
struct MembershipEvidence {
    std::optional<bool> own;
    std::optional<bool> saved; // subscribed playlist/album or followed artist
    std::optional<bool> liked;
};

// Pure, bounded reducer shared by the detail models and behavior tests.
class MembershipScan
{
  public:
    static constexpr int PageSize = 100;
    static constexpr int MaxPages = 100; // at most 10,000 album/artist rows

    MembershipScan(collection::Kind kind, QString key) : m_kind(kind), m_key(std::move(key)) {}
    // True requests another page; false leaves either evidence or unknown.
    bool accept(const Outcome<ResultPage<CollectionItem>>& outcome);
    [[nodiscard]] PageRequest page() const { return {m_offset, PageSize}; }
    [[nodiscard]] MembershipEvidence evidence() const { return m_evidence; }

  private:
    collection::Kind m_kind;
    QString m_key;
    MembershipEvidence m_evidence;
    int m_offset = 0;
    int m_pages = 0;
    bool m_partial = false;
};

// Owns both reads, cancellation and identity fencing. Queries is also the test seam:
// tests can deliver canceled replies and switch identity without a backend/runtime.
class AccountMembership : public QObject
{
  public:
    struct Queries {
        std::function<QString()> account;
        std::function<Request(collection::Kind, PageRequest,
                              Reply<ResultPage<CollectionItem>>)> collections;
        std::function<Request(Reply<CollectionItem>)> liked;
    };

    AccountMembership(QObject* parent, std::function<void()> changed);
    void start(LibrarySource* source, Session* session, collection::Kind kind, const QString& key);
    void start(Queries queries, collection::Kind kind, const QString& key);
    void clear();
    void forgetSaved();
    [[nodiscard]] MembershipEvidence evidence() const;

    template <class T> Reply<T> guard(Reply<T> reply)
    {
        return [weak = QPointer<AccountMembership>(this), generation = m_generation,
                account = m_account, reply = std::move(reply)](Outcome<T> outcome) {
            if (weak && weak->m_generation == generation && weak->m_queries.account &&
                weak->m_queries.account() == account)
                reply(std::move(outcome));
        };
    }

  private:
    void readPage();

    std::function<void()> m_changed;
    Queries m_queries;
    QString m_account;
    quint64 m_generation = 0;
    collection::Kind m_kind = collection::Playlist;
    QString m_key;
    std::optional<MembershipScan> m_scan;
    MembershipEvidence m_evidence;
    Request m_collectionsRequest;
    Request m_likedRequest;
};

} // namespace cirrus::ui

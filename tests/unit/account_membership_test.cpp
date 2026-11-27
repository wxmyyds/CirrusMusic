#include "account_membership.h"

#include <QtTest>

using namespace cirrus::ui;

namespace
{
CollectionItem item(collection::Kind kind, QString key, std::optional<bool> created = {})
{
    CollectionItem result;
    result.kind = kind;
    result.key = std::move(key);
    result.created = created;
    return result;
}
Outcome<ResultPage<CollectionItem>> page(QList<CollectionItem> items,
                                        std::optional<bool> more = false, bool partial = false,
                                        std::optional<qint64> next = {})
{
    return {ResultPage<CollectionItem>{std::move(items), more, {}, next}, partial, {}};
}

struct ControlledQueries {
    QString account = "A";
    QList<Reply<ResultPage<CollectionItem>>> replies;
    QList<Reply<CollectionItem>> likedReplies;
    QList<PageRequest> pages;
    int canceled = 0;
    AccountMembership::Queries queries()
    {
        return {[this] { return account; },
                [this](collection::Kind, PageRequest page,
                       Reply<ResultPage<CollectionItem>> reply) {
                    pages.append(page);
                    replies.append(std::move(reply));
                    return Request([this] { ++canceled; });
                },
                [this](Reply<CollectionItem> reply) {
                    likedReplies.append(std::move(reply));
                    return Request([this] { ++canceled; });
                }};
    }
};
} // namespace

class AccountMembershipTest : public QObject
{
    Q_OBJECT
  private slots:
    void playlistCreatedIsRequired()
    {
        for (const auto created : {std::optional<bool>{}, std::optional<bool>{false},
                                   std::optional<bool>{true}}) {
            MembershipScan scan(collection::Playlist, "42");
            // A matching row can prove membership even in a truncated result.
            QVERIFY(!scan.accept(page({item(collection::Playlist, "42", created)}, true, true)));
            QCOMPARE(scan.evidence().own, created);
            const std::optional<bool> saved = created ? std::optional<bool>{!*created}
                                                      : std::nullopt;
            QCOMPARE(scan.evidence().saved, saved);
            QVERIFY(!scan.evidence().liked); // Special identity is a separate query.
        }
    }

    void savedAlbumAndFollowedArtist()
    {
        for (const auto kind : {collection::Album, collection::Artist}) {
            MembershipScan scan(kind, "42");
            QVERIFY(scan.accept(page({item(kind, "41")}, true)));
            QCOMPARE(scan.page().offset, 1);
            QCOMPARE(scan.page().limit, 100);
            QVERIFY(!scan.evidence().saved);
            QVERIFY(!scan.accept(page({item(kind, "42")}, true, true)));
            QCOMPARE(scan.evidence().saved, std::optional<bool>{true});
            QVERIFY(!scan.evidence().own);
        }
    }

    void absenceNeedsCompleteScan()
    {
        for (const auto kind : {collection::Playlist, collection::Album, collection::Artist}) {
            MembershipScan wrongKind(kind, "42");
            const auto otherKind = kind == collection::Album ? collection::Artist : collection::Album;
            QVERIFY(!wrongKind.accept(page({item(otherKind, "42")}, false)));
            QCOMPARE(wrongKind.evidence().saved, std::optional<bool>{false});

            MembershipScan complete(kind, "42");
            // A different id cannot match.
            QVERIFY(complete.accept(page({item(kind, "41")}, std::nullopt)));
            QVERIFY(!complete.accept(page({}, std::nullopt)));
            QCOMPARE(complete.evidence().saved, std::optional<bool>{false});
            if (kind == collection::Playlist)
                QCOMPARE(complete.evidence().own, std::optional<bool>{false});

            MembershipScan truncated(kind, "42");
            QVERIFY(truncated.accept(page({item(kind, "41")}, true, true)));
            QVERIFY(!truncated.accept(page({}, false)));
            QVERIFY(!truncated.evidence().saved);
            QVERIFY(!truncated.evidence().own);

            MembershipScan failed(kind, "42");
            QVERIFY(failed.accept(page({item(kind, "41")}, true)));
            QVERIFY(!failed.accept({{}, false, Failure{failure::Network, {}}}));
            QVERIFY(!failed.evidence().saved);

            MembershipScan emptyMore(kind, "42");
            QVERIFY(!emptyMore.accept(page({}, true)));
            QVERIFY(!emptyMore.evidence().saved);
        }
    }

    void boundsAndProgressDoNotProveAbsence()
    {
        MembershipScan stalled(collection::Album, "42");
        QVERIFY(!stalled.accept(page({item(collection::Album, "41")}, true, false, 0)));
        QVERIFY(!stalled.evidence().saved);

        MembershipScan bounded(collection::Artist, "42");
        for (int n = 0; n < MembershipScan::MaxPages; ++n) {
            const bool more = bounded.accept(page({item(collection::Artist, "41")}, true));
            QCOMPARE(more, n + 1 < MembershipScan::MaxPages);
        }
        QVERIFY(!bounded.evidence().saved);

        MembershipScan offsetBound(collection::Album, "42");
        QVERIFY(!offsetBound.accept(page({item(collection::Album, "41")}, true, false,
                                        qint64(10000))));
        QVERIFY(!offsetBound.evidence().saved);

        MembershipScan positiveAtBound(collection::Artist, "42");
        for (int n = 1; n < MembershipScan::MaxPages; ++n)
            QVERIFY(positiveAtBound.accept(page({item(collection::Artist, "41")}, true)));
        QVERIFY(!positiveAtBound.accept(page({item(collection::Artist, "42")}, true)));
        QCOMPARE(positiveAtBound.evidence().saved, std::optional<bool>{true});
    }

    void specialLikedIdentityDoesNotGrantOwnership()
    {
        ControlledQueries source;
        AccountMembership membership(nullptr, [] {});
        membership.start(source.queries(), collection::Playlist, "42");
        source.likedReplies[0]({item(collection::Playlist, "42"), false, {}});
        QCOMPARE(membership.evidence().liked, std::optional<bool>{true});
        QVERIFY(!membership.evidence().own);
        source.replies[0](page({item(collection::Playlist, "42", true)}));
        QCOMPARE(membership.evidence().own, std::optional<bool>{true});
        QCOMPARE(membership.evidence().saved, std::optional<bool>{false});

        membership.start(source.queries(), collection::Playlist, "43");
        source.likedReplies[1]({item(collection::Playlist, "42"), false, {}});
        QCOMPARE(membership.evidence().liked, std::optional<bool>{false});
        source.replies[1](page({item(collection::Playlist, "43", true)}));
        QCOMPARE(membership.evidence().own, std::optional<bool>{true});

        membership.start(source.queries(), collection::Playlist, "43");
        source.replies[2](page({item(collection::Playlist, "43", true)}));
        source.likedReplies[2]({{}, false, Failure{failure::Network, {}}});
        QCOMPARE(membership.evidence().own, std::optional<bool>{true});
        QVERIFY(!membership.evidence().liked); // Rename/delete remain blocked.
    }

    void accountChangeAndLateReplies()
    {
        ControlledQueries source;
        int changes = 0;
        AccountMembership membership(nullptr, [&] { ++changes; });
        membership.start(source.queries(), collection::Playlist, "42");
        int catalogApplied = 0, writeApplied = 0;
        const auto oldCatalog = membership.guard<PlaylistPage>(
            [&](Outcome<PlaylistPage>) { ++catalogApplied; });
        const auto oldWrite = membership.guard<bool>([&](Outcome<bool>) { ++writeApplied; });

        source.account = "B";
        // The account fence works even before the model's accountChanged reload.
        source.replies[0](page({item(collection::Playlist, "42", true)}));
        QVERIFY(!membership.evidence().own);
        oldCatalog({PlaylistPage{}});
        oldWrite({true});
        QCOMPARE(catalogApplied, 0);
        QCOMPARE(writeApplied, 0);

        membership.start(source.queries(), collection::Playlist, "42");
        QCOMPARE(source.canceled, 2);
        const int beforeLate = changes;
        source.replies[0](page({item(collection::Playlist, "42", true)}));
        source.likedReplies[0]({item(collection::Playlist, "42"), false, {}});
        QCOMPARE(changes, beforeLate);
        source.replies[1](page({item(collection::Playlist, "42", false)}));
        source.likedReplies[1]({item(collection::Playlist, "99"), false, {}});
        QCOMPARE(membership.evidence().own, std::optional<bool>{false});
        QCOMPARE(membership.evidence().saved, std::optional<bool>{true});
        QCOMPARE(membership.evidence().liked, std::optional<bool>{false});

        source.account = "A";
        membership.start(source.queries(), collection::Playlist, "42");
        // A -> B -> A cannot revive A's original catalog/write/membership replies.
        oldCatalog({PlaylistPage{}});
        oldWrite({true});
        source.replies[0](page({item(collection::Playlist, "42", true)}));
        source.likedReplies[0]({item(collection::Playlist, "42"), false, {}});
        QCOMPARE(catalogApplied, 0);
        QCOMPARE(writeApplied, 0);
        QVERIFY(!membership.evidence().own);
        QVERIFY(!membership.evidence().liked);
    }

    void clearReloadAndDestructionCancelReads()
    {
        ControlledQueries source;
        int changes = 0;
        {
            AccountMembership membership(nullptr, [&] { ++changes; });
            membership.start(source.queries(), collection::Album, "42");
            const auto firstReply = source.replies[0];
            firstReply(page({item(collection::Album, "41")}, true));
            QCOMPARE(source.pages.size(), 2);
            QCOMPARE(source.pages[1].offset, 1);
            membership.clear();
            QCOMPARE(source.canceled, 1); // The second page only; first completed.
            const int beforeLate = changes;
            source.replies[1](page({item(collection::Album, "42")}));
            QCOMPARE(changes, beforeLate);
            QVERIFY(!membership.evidence().saved);
            membership.start(source.queries(), collection::Artist, "42");
        }
        QCOMPARE(source.canceled, 2);
        const int beforeDestroyedReply = changes;
        source.replies[2](page({item(collection::Artist, "42")}));
        QCOMPARE(changes, beforeDestroyedReply);
    }

    void guestsStayUnknownButCanReadCatalog()
    {
        ControlledQueries source;
        source.account.clear();
        AccountMembership membership(nullptr, [] {});
        membership.start(source.queries(), collection::Playlist, "42");
        QVERIFY(source.replies.isEmpty());
        QVERIFY(source.likedReplies.isEmpty());
        QVERIFY(!membership.evidence().own);
        QVERIFY(!membership.evidence().saved);
        QVERIFY(!membership.evidence().liked);
        int applied = 0;
        const auto reply = membership.guard<PlaylistPage>([&](Outcome<PlaylistPage>) { ++applied; });
        reply({PlaylistPage{}});
        QCOMPARE(applied, 1);
        source.account = "A";
        reply({PlaylistPage{}});
        QCOMPARE(applied, 1);
    }
};

QTEST_GUILESS_MAIN(AccountMembershipTest)
#include "account_membership_test.moc"

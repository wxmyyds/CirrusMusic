#include "playlist_order_revision.h"

#include <QtTest>

using cirrus::ui::PlaylistOrderRevision;

class PlaylistOrderRevisionTest : public QObject
{
    Q_OBJECT
  private slots:
    void unchangedSnapshotMayClose()
    {
        PlaylistOrderRevision revision;
        const QStringList order{"1", "2", "3"};
        const auto submitted = revision.snapshot(order);
        QVERIFY(revision.isCurrent(submitted, order));
    }

    void editsDuringSaveKeepThePageOpen()
    {
        PlaylistOrderRevision revision;
        const QStringList submittedOrder{"1", "2", "3"};
        const auto submitted = revision.snapshot(submittedOrder);

        revision.changed();
        QVERIFY(!revision.isCurrent(submitted, {"2", "1", "3"}));
        // Even if the user edits back to the same values, the old acknowledgement must
        // not close a page whose state changed after that request was sent.
        revision.changed();
        QVERIFY(!revision.isCurrent(submitted, submittedOrder));

        const auto next = revision.snapshot(submittedOrder);
        QVERIFY(revision.isCurrent(next, submittedOrder));
    }
};

QTEST_GUILESS_MAIN(PlaylistOrderRevisionTest)
#include "playlist_order_revision_test.moc"

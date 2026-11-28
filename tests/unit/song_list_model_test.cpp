#include "song_list_model.h"
#include "sorted_songs_model.h"

#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::ui;

class SongListModelTest final : public QObject
{
    Q_OBJECT

  private slots:
    void fillNotifiesOneChangedRangePerBatch()
    {
        SongListModel model;
        model.reset({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                     QStringLiteral("d")}, {});
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        QVERIFY(changes.isValid());

        SongInfo b;
        b.key = QStringLiteral("b");
        b.title = QStringLiteral("Beta");
        SongInfo d;
        d.key = QStringLiteral("d");
        d.title = QStringLiteral("Delta");
        model.fill({b.key, d.key}, {b, d});

        QCOMPARE(changes.size(), 1);
        const QList<QVariant> arguments = changes.takeFirst();
        QCOMPARE(arguments.at(0).value<QModelIndex>().row(), 1);
        QCOMPARE(arguments.at(1).value<QModelIndex>().row(), 3);
        const QList<int> roles = arguments.at(2).value<QList<int>>();
        QVERIFY(roles.isEmpty());
        QCOMPARE(model.data(model.index(1, 0), SongListModel::TitleRole).toString(),
                 QStringLiteral("Beta"));
        QCOMPARE(model.data(model.index(3, 0), SongListModel::TitleRole).toString(),
                 QStringLiteral("Delta"));
    }

    void dynamicProxyReevaluatesAfterFillBatch()
    {
        SongListModel model;
        model.reset({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, {});
        SortedSongsModel sorted;
        sorted.setSource(&model);
        sorted.setSortField(SortedSongsModel::Title);
        sorted.setFilterText(QStringLiteral("Beta"));
        QCOMPARE(sorted.count(), 0);

        SongInfo b;
        b.key = QStringLiteral("b");
        b.title = QStringLiteral("Beta");
        model.fill({b.key}, {b});

        QCOMPARE(sorted.count(), 1);
        QCOMPARE(sorted.keyAt(0), QStringLiteral("b"));
    }

    void dynamicSortReordersAfterFillBatch()
    {
        SongListModel model;
        model.reset({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, {});
        SortedSongsModel sorted;
        sorted.setSource(&model);
        sorted.setSortField(SortedSongsModel::Title);

        SongInfo a;
        a.key = QStringLiteral("a");
        a.title = QStringLiteral("Zulu");
        SongInfo c;
        c.key = QStringLiteral("c");
        c.title = QStringLiteral("Alpha");
        model.fill({a.key, c.key}, {a, c});

        QCOMPARE(sorted.keyAt(0), QStringLiteral("c"));
        QCOMPARE(sorted.keyAt(1), QStringLiteral("a"));
        QCOMPARE(sorted.keyAt(2), QStringLiteral("b"));
    }

    void fillIgnoresRowsOutsideRequest()
    {
        SongListModel model;
        model.reset({QStringLiteral("a"), QStringLiteral("b")}, {});
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        QVERIFY(changes.isValid());

        SongInfo a;
        a.key = QStringLiteral("a");
        a.title = QStringLiteral("Alpha");
        model.fill({QStringLiteral("not-requested")}, {a});

        QCOMPARE(changes.size(), 0);
        QCOMPARE(model.data(model.index(0, 0), SongListModel::LoadedRole).toBool(), false);
    }
};

QTEST_GUILESS_MAIN(SongListModelTest)
#include "song_list_model_test.moc"

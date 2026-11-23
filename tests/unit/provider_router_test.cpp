#include "application/playback/provider_router.h"
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace cirrus;
using namespace cirrus::application;
using namespace cirrus::domain;

namespace
{
// A resolver whose answer the test supplies; completion is deferred, like the real ones.
class FakeResolver final : public IStreamResolver
{
  public:
    using IStreamResolver::IStreamResolver;
    explicit FakeResolver(QObject* parent = nullptr) : IStreamResolver(parent) {}
    quint64 resolve(SessionToken, StreamQuery query) override
    {
        const auto id = ++m_next;
        m_queries.append(query);
        return id;
    }
    void cancel(quint64 id) override { m_canceled.append(id); }

    void answer(quint64 id, SessionToken context, StreamResult result)
    {
        QTimer::singleShot(0, this,
                           [this, id, context, result = std::move(result)]() mutable {
                               emit completed(id, context, std::move(result));
                           });
    }

    QList<StreamQuery> m_queries;
    QList<quint64> m_canceled;
    quint64 m_next = 0;
};

StreamSource source(const QString& song, std::optional<PreviewRange> preview = std::nullopt)
{
    return StreamSource{SongId{song}, QUrl(QStringLiteral("https://m801.music.126.net/a.mp3")),
                        QStringLiteral("exhigh"), QStringLiteral("mp3"), 1024,
                        QDeadlineTimer(60000), preview};
}
} // namespace

class ProviderRouterTest : public QObject
{
    Q_OBJECT
  private slots:
    void disabledNeverUsesBilibili()
    {
        FakeResolver netease, bili;
        ProviderRouter router(netease, bili);
        QSignalSpy done(&router, &IStreamResolver::completed);
        StreamQuery query{SongId{QStringLiteral("s1")}, AudioQuality::Exhigh};
        query.allowBiliSource = false;
        const auto id = router.resolve(SessionToken{}, query);
        QVERIFY(id);
        QCOMPARE(netease.m_queries.size(), 1);
        netease.answer(netease.m_queries.size(), SessionToken{},
                       PlaybackFailure{PlaybackError::NeedPermission});
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(bili.m_queries.isEmpty());
        const auto result = qvariant_cast<application::StreamResult>(done.first().at(2));
        QCOMPARE(std::get<PlaybackFailure>(result).error, PlaybackError::NeedPermission);
    }

    void needPermissionRoutesToBilibili()
    {
        FakeResolver netease, bili;
        ProviderRouter router(netease, bili);
        QSignalSpy done(&router, &IStreamResolver::completed);
        StreamQuery query{SongId{QStringLiteral("s1")}, AudioQuality::Exhigh};
        query.allowBiliSource = true;
        router.resolve(SessionToken{}, query);
        netease.answer(netease.m_queries.size(), SessionToken{},
                       PlaybackFailure{PlaybackError::NeedPermission});
        QTRY_COMPARE(bili.m_queries.size(), 1);
        auto selected = source(QStringLiteral("s1"));
        selected.provider = SourceProvider::Bilibili;
        bili.answer(bili.m_queries.size(), SessionToken{}, selected);
        QTRY_COMPARE(done.count(), 1);
        const auto result = qvariant_cast<application::StreamResult>(done.first().at(2));
        QVERIFY(std::holds_alternative<StreamSource>(result));
        QCOMPARE(std::get<StreamSource>(result).song.value, QStringLiteral("s1"));
    }

    void previewRoutesToBilibili()
    {
        FakeResolver netease, bili;
        ProviderRouter router(netease, bili);
        QSignalSpy done(&router, &IStreamResolver::completed);
        StreamQuery query{SongId{QStringLiteral("s1")}, AudioQuality::Exhigh};
        query.allowBiliSource = true;
        router.resolve(SessionToken{}, query);
        netease.answer(netease.m_queries.size(), SessionToken{},
                       source(QStringLiteral("s1"), PreviewRange{0, 30000}));
        QTRY_COMPARE(bili.m_queries.size(), 1);
        QCOMPARE(netease.m_queries.size(), 1);
        bili.answer(bili.m_queries.size(), SessionToken{},
                    PlaybackFailure{PlaybackError::Unavailable});
        QTRY_COMPARE(done.count(), 1);
        // Bilibili's failure is the song's failure; the NetEase preview is NOT played.
        const auto result = qvariant_cast<application::StreamResult>(done.first().at(2));
        QVERIFY(std::holds_alternative<PlaybackFailure>(result));
    }

    void otherFailuresStayNetease()
    {
        for (const auto error :
             {PlaybackError::Unavailable, PlaybackError::Network, PlaybackError::Timeout,
              PlaybackError::RateLimited, PlaybackError::Decode}) {
            FakeResolver netease, bili;
            ProviderRouter router(netease, bili);
            QSignalSpy done(&router, &IStreamResolver::completed);
            StreamQuery query{SongId{QStringLiteral("s1")}, AudioQuality::Exhigh};
            query.allowBiliSource = true;
            router.resolve(SessionToken{}, query);
            netease.answer(netease.m_queries.size(), SessionToken{}, PlaybackFailure{error});
            QTRY_COMPARE(done.count(), 1);
            const auto result = qvariant_cast<application::StreamResult>(done.first().at(2));
            QCOMPARE(std::get<PlaybackFailure>(result).error, error);
            QVERIFY(bili.m_queries.isEmpty());
        }
    }

    void normalNeteaseStaysNetease()
    {
        FakeResolver netease, bili;
        ProviderRouter router(netease, bili);
        QSignalSpy done(&router, &IStreamResolver::completed);
        StreamQuery query{SongId{QStringLiteral("s1")}, AudioQuality::Exhigh};
        query.allowBiliSource = true;
        router.resolve(SessionToken{}, query);
        netease.answer(netease.m_queries.size(), SessionToken{}, source(QStringLiteral("s1")));
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(bili.m_queries.isEmpty());
        QVERIFY(std::holds_alternative<StreamSource>(
            qvariant_cast<application::StreamResult>(done.first().at(2))));
    }

    void cancelDuringBilibiliDropsLateCompletion()
    {
        FakeResolver netease, bili;
        ProviderRouter router(netease, bili);
        QSignalSpy done(&router, &IStreamResolver::completed);
        const auto id = router.resolve({}, {{QStringLiteral("s1")}, AudioQuality::Exhigh, true});
        netease.answer(1, {}, PlaybackFailure{PlaybackError::NeedPermission});
        QTRY_COMPARE(bili.m_queries.size(), 1);
        router.cancel(id);
        QCOMPARE(bili.m_canceled, QList<quint64>{1});
        emit bili.completed(1, {}, PlaybackFailure{PlaybackError::Unavailable});
        QCOMPARE(done.count(), 0);
    }

    void staleContextCannotRoute()
    {
        FakeResolver netease, bili;
        ProviderRouter router(netease, bili);
        QSignalSpy done(&router, &IStreamResolver::completed);
        const SessionToken original{QUuid::createUuid(), 1};
        router.resolve(original, {{QStringLiteral("s1")}, AudioQuality::Exhigh, true});
        netease.answer(1, {}, PlaybackFailure{PlaybackError::NeedPermission});
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(bili.m_queries.isEmpty());
        QCOMPARE(qvariant_cast<SessionToken>(done.first().at(1)), original);
        QCOMPARE(std::get<PlaybackFailure>(
                     qvariant_cast<StreamResult>(done.first().at(2))).error,
                 PlaybackError::StaleContext);
    }

    void cancelReachesTheWaitingProvider()
    {
        FakeResolver netease, bili;
        ProviderRouter router(netease, bili);
        StreamQuery query{SongId{QStringLiteral("s1")}, AudioQuality::Exhigh};
        query.allowBiliSource = true;
        const auto id = router.resolve(SessionToken{}, query);
        QVERIFY(netease.m_queries.size() >= 1);
        router.cancel(id);
        QCOMPARE(netease.m_canceled.size(), 1);
    }
};

QTEST_GUILESS_MAIN(ProviderRouterTest)
#include "provider_router_test.moc"

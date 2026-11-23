#include "infrastructure/bilibili/bili_account.h"
#include "../support/fake_secret_store.h"
#include "../support/synthetic_bilibili.h"

#include <QSignalSpy>
#include <QTest>

using namespace cirrus;
using namespace cirrus::bilibili;
using namespace cirrus::domain;

class BiliAccountTest : public QObject
{
    Q_OBJECT
  private slots:
    void storedCookieRestoresAndClears()
    {
        test::FakeSecretStore store;
        QVERIFY(secrets::SecretStore::validKey("bilibili.cookie"));
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        fake->plans = {test::biliJson("{\"code\":0,\"data\":{\"isLogin\":true,\"uname\":\"user\"}}")};
        Account account(store, std::move(manager));
        QSignalSpy states(&account, &Account::changed);
        QSignalSpy applied(&account, &Account::cookieChanged);
        account.save("SESSDATA=synthetic");
        QVERIFY(account.cookie().isEmpty());
        QTRY_COMPARE(states.size(), 2);
        QCOMPARE(store.entries.value("bilibili.cookie"), QByteArray("SESSDATA=synthetic"));
        QCOMPARE(account.cookie(), QByteArray("SESSDATA=synthetic"));
        QCOMPARE(qvariant_cast<BiliAccountState>(states.last().first()).status,
                 BiliAccountStatus::LoggedIn);
        QCOMPARE(fake->requests.size(), 1);
        QCOMPARE(fake->requests.first().rawHeader("Cookie"), QByteArray("SESSDATA=synthetic"));
        account.clear();
        QTRY_COMPARE(states.size(), 4);
        QVERIFY(account.cookie().isEmpty());
        QVERIFY(!store.entries.contains("bilibili.cookie"));
        QCOMPARE(applied.size(), 2);
        account.restore();
        QTRY_COMPARE(states.size(), 6);
        QCOMPARE(qvariant_cast<BiliAccountState>(states.last().first()).status,
                 BiliAccountStatus::LoggedOut);
    }
    void clearSupersedesPendingRestore()
    {
        test::FakeSecretStore store;
        store.entries.insert("bilibili.cookie", "SESSDATA=synthetic");
        store.hold = true;
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        Account account(store, std::move(manager));
        account.restore();
        account.clear();
        store.release();
        store.release();
        QVERIFY(!store.entries.contains("bilibili.cookie"));
        QVERIFY(account.cookie().isEmpty());
        QCOMPARE(fake->requests.size(), 0);
    }
    void clearDuringWriteStillDrainsWhenClosing()
    {
        test::FakeSecretStore store;
        store.hold = true;
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        Account account(store, std::move(manager));
        account.save("SESSDATA=synthetic");
        account.clear();
        account.close();
        store.release();
        store.release();
        QVERIFY(!store.entries.contains("bilibili.cookie"));
        QVERIFY(account.cookie().isEmpty());
        QCOMPARE(fake->requests.size(), 0);
    }
    void storageFailureNeverAppliesCookie()
    {
        test::FakeSecretStore store;
        store.failWith = secrets::SecretError::Unavailable;
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        Account account(store, std::move(manager));
        QSignalSpy states(&account, &Account::changed);
        account.save("SESSDATA=synthetic");
        QTRY_COMPARE(states.size(), 2);
        QCOMPARE(qvariant_cast<BiliAccountState>(states.last().first()).status,
                 BiliAccountStatus::StorageError);
        QVERIFY(store.entries.isEmpty());
        QVERIFY(account.cookie().isEmpty());
        QCOMPARE(fake->requests.size(), 0);
    }
    void malformedCookieNeverStored()
    {
        test::FakeSecretStore store;
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        Account account(store, std::move(manager));
        for (const auto& cookie : {QByteArray(), QByteArray("SESSDATA=a\r\nX: y"),
                                  QByteArray("foo=bar"), QByteArray(20000, 'a')})
            account.save(cookie);
        QCOMPARE(store.writes, 0);
        QCOMPARE(fake->requests.size(), 0);
    }
};
QTEST_GUILESS_MAIN(BiliAccountTest)
#include "bili_account_test.moc"

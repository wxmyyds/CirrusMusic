// The update check's pure parts: release metadata, exact package/architecture assets,
// installer verification and notes. Expected filenames are the production contract.
#include "update_release.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtTest>

#include <utility>

using namespace cirrus::ui::updates;
using namespace Qt::StringLiterals;

namespace
{
struct PackageAsset {
    Package package;
    Architecture architecture;
    const char* name;
};

constexpr std::array<PackageAsset, 12> kAssets{{
    {Package::Arch, Architecture::X86_64, "cirrus-0.5.0-1-x86_64.pkg.tar.zst"},
    {Package::Arch, Architecture::Arm64, "cirrus-0.5.0-1-aarch64.pkg.tar.zst"},
    {Package::Debian, Architecture::X86_64, "cirrus_0.5.0_amd64.deb"},
    {Package::Debian, Architecture::Arm64, "cirrus_0.5.0_arm64.deb"},
    {Package::Fedora, Architecture::X86_64, "cirrus-0.5.0-1.fc44.x86_64.rpm"},
    {Package::Fedora, Architecture::Arm64, "cirrus-0.5.0-1.fc44.aarch64.rpm"},
    {Package::WindowsSetup, Architecture::X86_64, "Cirrus-0.5.0-windows-x64-setup.exe"},
    {Package::WindowsSetup, Architecture::Arm64, "Cirrus-0.5.0-windows-arm64-setup.exe"},
    {Package::WindowsZip, Architecture::X86_64, "Cirrus-0.5.0-windows-x64.zip"},
    {Package::WindowsZip, Architecture::Arm64, "Cirrus-0.5.0-windows-arm64.zip"},
    {Package::MacDmg, Architecture::X86_64, "Cirrus-0.5.0-macos-x86_64.dmg"},
    {Package::MacDmg, Architecture::Arm64, "Cirrus-0.5.0-macos-arm64.dmg"},
}};

QUrl downloadUrl(const QString& name)
{
    return QUrl(u"https://github.com/wxmyyds/CirrusMusic/releases/download/v0.5.0/"_s + name);
}

QJsonObject asset(const QString& name)
{
    return {{u"name"_s, name},
            {u"size"_s, 1234},
            {u"browser_download_url"_s, downloadUrl(name).toString()},
            {u"digest"_s, u"sha256:"_s + QString(64, u'a')}};
}

QByteArray releaseJson(const QJsonArray& assets)
{
    return QJsonDocument(QJsonObject{
                             {u"tag_name"_s, u"v0.5.0"_s},
                             {u"html_url"_s,
                              u"https://github.com/wxmyyds/CirrusMusic/releases/tag/v0.5.0"_s},
                             {u"assets"_s, assets}})
        .toJson();
}

QJsonArray allAssets()
{
    QJsonArray assets;
    for (const auto& entry : kAssets)
        assets.append(asset(QString::fromLatin1(entry.name)));
    // Old distribution formats and library sources must never become an update.
    for (const auto* name : {"Cirrus-0.5.0-x86_64.AppImage", "Cirrus-0.5.0-library-sources.tar.xz"})
        assets.append(asset(QString::fromLatin1(name)));
    return assets;
}
} // namespace

class UpdateReleaseTest : public QObject
{
    Q_OBJECT

  private slots:
    void parsesVersions()
    {
        QCOMPARE(parseVersion(u"v0.1.2"), (std::array<int, 3>{0, 1, 2}));
        QCOMPARE(parseVersion(u"1.10.0"), (std::array<int, 3>{1, 10, 0}));
        QVERIFY(!parseVersion(u"v0.2.0-beta.1"));
        QVERIFY(!parseVersion(u"0.2"));
        QVERIFY(!parseVersion(u"0.2.0.1"));
        QVERIFY(!parseVersion(u"v0.+1.0"));
        QVERIFY(!parseVersion(u""));
    }

    void comparesVersions()
    {
        QVERIFY(isNewer(u"0.1.2", u"0.1.1"));
        QVERIFY(isNewer(u"v0.2.0", u"0.1.9"));
        QVERIFY(isNewer(u"0.10.0", u"0.9.0")); // numbers, not text
        QVERIFY(!isNewer(u"0.1.1", u"0.1.1"));
        QVERIFY(!isNewer(u"0.1.0", u"0.1.1"));
        QVERIFY(!isNewer(u"0.2.0-rc1", u"0.1.1"));
        QVERIFY(!isNewer(u"0.2.0", u"dev"));
    }

    void acceptsOnlyTheProjectsReleasePages()
    {
        QVERIFY(
            isReleasePage(QUrl(u"https://github.com/wxmyyds/CirrusMusic/releases/tag/v0.1.2"_s)));
        QVERIFY(
            !isReleasePage(QUrl(u"http://github.com/wxmyyds/CirrusMusic/releases/tag/v0.1.2"_s)));
        QVERIFY(
            !isReleasePage(QUrl(u"https://github.com/someone/CirrusMusic/releases/tag/v0.1.2"_s)));
        // Upstream releases would replace this fork's provider support and exclusions.
        QVERIFY(
            !isReleasePage(QUrl(u"https://github.com/Guang233/CirrusMusic/releases/tag/v0.5.0"_s)));
        QVERIFY(!isReleasePage(
            QUrl(u"https://github.com.evil.example/wxmyyds/CirrusMusic/releases/x"_s)));
        QVERIFY(!isReleasePage(QUrl(u"https://user@github.com/wxmyyds/CirrusMusic/releases/x"_s)));
        QVERIFY(!isReleasePage(QUrl(u"https://github.com/wxmyyds/CirrusMusic/archive/x"_s)));
    }

    void parsesTheLatestRelease()
    {
        // Plain literals: moc does not see the class around multi-line raw strings.
        const QByteArray json =
            "{\"tag_name\": \"v0.1.2\", \"name\": \"Cirrus 0.1.2\", \"draft\": false,"
            " \"prerelease\": false,"
            " \"html_url\": \"https://github.com/wxmyyds/CirrusMusic/releases/tag/v0.1.2\","
            " \"published_at\": \"2026-11-20T08:00:00Z\","
            " \"body\": \"### 修复\\r\\n\\r\\n- 一处 **问题**。\\r\\n\"}";
        const auto release = parseRelease(json, Package::Unknown, Architecture::Unknown);
        QVERIFY(release);
        QCOMPARE(release->version, u"0.1.2"_s);
        QCOMPARE(release->title, u"Cirrus 0.1.2"_s);
        QCOMPARE(release->notes, u"修复\n\n• 一处 问题。"_s);
        QCOMPARE(release->published, QDateTime(QDate(2026, 11, 20), QTime(8, 0), QTimeZone::UTC));
        QVERIFY(!release->asset);
    }

    void rejectsWhatIsNotARelease()
    {
        const auto invalid = [](const QByteArray& json) {
            return !parseRelease(json, Package::Unknown, Architecture::Unknown);
        };
        QVERIFY(invalid("not json"));
        const char* page =
            "\"html_url\": \"https://github.com/wxmyyds/CirrusMusic/releases/tag/v0.2.0\"";
        QVERIFY(invalid(QByteArray("{\"tag_name\": \"v0.2.0\", \"prerelease\": true, ") + page + '}'));
        QVERIFY(invalid(QByteArray("{\"tag_name\": \"v0.2.0\", \"draft\": true, ") + page + '}'));
        QVERIFY(invalid(QByteArray("{\"tag_name\": \"nightly\", ") + page + '}'));
        QVERIFY(invalid("{\"tag_name\": \"v0.2.0\", \"html_url\": \"https://example.com/x\"}"));
    }

    void selectsOnlyThePackageAndArchitecture_data()
    {
        QTest::addColumn<int>("package");
        QTest::addColumn<int>("architecture");
        QTest::addColumn<QString>("expected");
        for (const auto& entry : kAssets)
            QTest::newRow(entry.name) << int(entry.package) << int(entry.architecture)
                                      << QString::fromLatin1(entry.name);
    }

    void selectsOnlyThePackageAndArchitecture()
    {
        QFETCH(int, package);
        QFETCH(int, architecture);
        QFETCH(QString, expected);
        const auto kind = Package(package);
        const auto cpu = Architecture(architecture);
        QCOMPARE(assetName(u"0.5.0", kind, cpu), expected);
        // Check another version to ensure filenames are not fixed to the fixture.
        QString next = expected;
        next.replace(u"0.5.0"_s, u"1.12.3"_s);
        QCOMPARE(assetName(u"1.12.3", kind, cpu), next);

        const QJsonArray assets = allAssets();
        QJsonArray reversed;
        for (qsizetype i = assets.size(); i > 0; --i)
            reversed.append(assets.at(i - 1));
        for (const auto& ordered : {assets, reversed}) {
            const auto parsed = parseRelease(releaseJson(ordered), kind, cpu);
            QVERIFY(parsed && parsed->asset);
            QCOMPARE(parsed->asset->name, expected);
            QCOMPARE(parsed->asset->url, downloadUrl(expected));
            if (kind == Package::WindowsSetup) {
                QCOMPARE(parsed->asset->size, qint64(1234));
                QCOMPARE(parsed->asset->sha256, QByteArray(32, '\xaa'));
            } else {
                QCOMPARE(parsed->asset->size, qint64(0));
                QVERIFY(parsed->asset->sha256.isEmpty());
            }
        }

        QJsonArray others;
        for (const auto& value : assets)
            if (value.toObject().value(u"name"_s).toString() != expected)
                others.append(value);
        const auto missing = parseRelease(releaseJson(others), kind, cpu);
        QVERIFY(missing && !missing->asset);
        const auto empty = parseRelease(releaseJson({}), kind, cpu);
        QVERIFY(empty && !empty->asset);

        auto mismatched = asset(expected);
        const QString otherName = others.first().toObject().value(u"name"_s).toString();
        mismatched[u"browser_download_url"_s] = downloadUrl(otherName).toString();
        const auto wrongUrl = parseRelease(releaseJson({mismatched}), kind, cpu);
        QVERIFY(wrongUrl && !wrongUrl->asset);

        // Browser downloads do not require installer metadata. Setups must have it.
        auto unverified = asset(expected);
        unverified.remove(u"digest"_s);
        unverified.remove(u"size"_s);
        const auto noDigest = parseRelease(releaseJson({unverified}), kind, cpu);
        QVERIFY(noDigest);
        QCOMPARE(noDigest->asset.has_value(), kind != Package::WindowsSetup);
    }

    void unknownIdentityNeverSelectsAnAsset()
    {
        const auto json = releaseJson(allAssets());
        for (const auto cpu : {Architecture::Unknown, Architecture::X86_64, Architecture::Arm64}) {
            QVERIFY(assetName(u"0.5.0", Package::Unknown, cpu).isEmpty());
            const auto parsed = parseRelease(json, Package::Unknown, cpu);
            QVERIFY(parsed && !parsed->asset);
            QVERIFY(isReleasePage(parsed->page)); // manual checks can still open the release
        }
        for (const auto& entry : kAssets) {
            QVERIFY(assetName(u"0.5.0", entry.package, Architecture::Unknown).isEmpty());
            const auto parsed = parseRelease(json, entry.package, Architecture::Unknown);
            QVERIFY(parsed && !parsed->asset);
        }
    }

    void rejectsUnsafeAssetUrls()
    {
        // The URL rules apply to every package, including those opened in a browser.
        for (const auto& entry : kAssets) {
            const QString name = QString::fromLatin1(entry.name);
            const QString valid = downloadUrl(name).toString();
            QStringList invalid{valid + QStringLiteral("?x=1"),
                                valid + QStringLiteral("#fragment")};
            for (const auto& replacement : {
                     std::pair{u"https://"_s, u"http://"_s},
                     std::pair{u"github.com/"_s, u"example.com/"_s},
                     std::pair{u"github.com/"_s, u"github.com.evil.example/"_s},
                     std::pair{u"github.com/"_s, u"user@github.com/"_s},
                     std::pair{u"github.com/"_s, u"github.com:443/"_s},
                     std::pair{u"/wxmyyds/"_s, u"/Guang233/"_s},
                     std::pair{u"/v0.5.0/"_s, u"/v0.4.0/"_s},
                 }) {
                QString url = valid;
                invalid.append(url.replace(replacement.first, replacement.second));
            }
            for (const auto& url : invalid) {
                auto unsafe = asset(name);
                unsafe[u"browser_download_url"_s] = url;
                const auto parsed =
                    parseRelease(releaseJson({unsafe}), entry.package, entry.architecture);
                QVERIFY2(parsed && !parsed->asset, qPrintable(url));
            }
        }
    }

    void rejectsUnverifiedWindowsInstallers()
    {
        for (const auto& entry : kAssets) {
            if (entry.package != Package::WindowsSetup)
                continue;
            const auto setup = asset(QString::fromLatin1(entry.name));
            const auto rejects = [&](const QJsonObject& candidate) {
                const auto parsed =
                    parseRelease(releaseJson({candidate}), entry.package, entry.architecture);
                return parsed && !parsed->asset;
            };
            for (const auto& digest : {u""_s, u"sha1:"_s + QString(40, u'0'),
                                       u"sha256:"_s + QString(64, u'A'),
                                       u"sha256:"_s + QString(63, u'a')}) {
                auto candidate = setup;
                candidate[u"digest"_s] = digest;
                QVERIFY(rejects(candidate));
            }
            for (const QJsonValue& size : {QJsonValue(0), QJsonValue(-1), QJsonValue(1.5),
                                           QJsonValue(qint64(512LL * 1024 * 1024 + 1)),
                                           QJsonValue(1e30), QJsonValue(u"1234"_s)}) {
                auto candidate = setup;
                candidate[u"size"_s] = size;
                QVERIFY(rejects(candidate));
            }
            auto maximum = setup;
            maximum[u"size"_s] = qint64(512LL * 1024 * 1024);
            const auto parsed =
                parseRelease(releaseJson({maximum}), entry.package, entry.architecture);
            QVERIFY(parsed && parsed->asset);
            QCOMPARE(parsed->asset->size, qint64(512LL * 1024 * 1024));
        }
    }

    void writesTheNotesAsPlainText()
    {
        const QString markdown =
            u"## [0.2.0]\n\n### 界面\n\n- 新的 [播放页](https://x.example/a)。\n"
            "* `代码` 和 __强调__\n  - 子项<br>\n\n\n\n![图](https://x.example/i.png)\n"_s;
        QCOMPARE(notesToPlainText(markdown),
                 u"[0.2.0]\n\n界面\n\n• 新的 播放页。\n• 代码 和 强调\n  • 子项\n\n图"_s);
    }
};

QTEST_GUILESS_MAIN(UpdateReleaseTest)
#include "update_release_test.moc"

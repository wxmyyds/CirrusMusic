// The update check's pure parts (ui/presentation/update_release.h): versions, GitHub's
// latest-release answer, the release page rule and the notes as plain text.
#include "update_release.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace cirrus::ui::updates;
using namespace Qt::StringLiterals;

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
        const auto release = parseRelease(json, WindowsArchitecture::Unknown);
        QVERIFY(release);
        QCOMPARE(release->version, u"0.1.2"_s);
        QCOMPARE(release->title, u"Cirrus 0.1.2"_s);
        QCOMPARE(release->notes, u"修复\n\n• 一处 问题。"_s);
        QCOMPARE(release->published, QDateTime(QDate(2026, 11, 20), QTime(8, 0), QTimeZone::UTC));
    }

    void rejectsWhatIsNotARelease()
    {
        QVERIFY(!parseRelease("not json", WindowsArchitecture::Unknown));
        const char* page =
            "\"html_url\": \"https://github.com/wxmyyds/CirrusMusic/releases/tag/v0.2.0\"";
        const auto invalid = [](const QByteArray& json) {
            return !parseRelease(json, WindowsArchitecture::Unknown);
        };
        QVERIFY(
            invalid(QByteArray("{\"tag_name\": \"v0.2.0\", \"prerelease\": true, ") + page + '}'));
        QVERIFY(invalid(QByteArray("{\"tag_name\": \"v0.2.0\", \"draft\": true, ") + page + '}'));
        QVERIFY(invalid(QByteArray("{\"tag_name\": \"nightly\", ") + page + '}'));
        QVERIFY(invalid("{\"tag_name\": \"v0.2.0\", \"html_url\": \"https://example.com/x\"}"));
    }

    void findsTheWindowsInstaller()
    {
        const QByteArray head =
            "{\"tag_name\": \"v0.5.0\", \"draft\": false, \"prerelease\": false,"
            " \"html_url\": \"https://github.com/wxmyyds/CirrusMusic/releases/tag/v0.5.0\","
            " \"assets\": [";
        const QByteArray zip = "{\"name\": \"Cirrus-0.5.0-windows-x64.zip\", \"size\": 9,"
                               " \"browser_download_url\": "
                               "\"https://github.com/wxmyyds/CirrusMusic/releases/download/v0.5.0/"
                               "Cirrus-0.5.0-windows-x64.zip\","
                               " \"digest\": \"sha256:" +
                               QByteArray(64, 'a') + "\"}";
        const auto setup = [](const QByteArray& url, const QByteArray& digest, int size) {
            return "{\"name\": \"Cirrus-0.5.0-windows-x64-setup.exe\", \"size\": " +
                   QByteArray::number(size) + ", \"browser_download_url\": \"" + url +
                   "\", \"digest\": \"" + digest + "\"}";
        };
        const QByteArray url = "https://github.com/wxmyyds/CirrusMusic/releases/download/v0.5.0/"
                               "Cirrus-0.5.0-windows-x64-setup.exe";
        const QByteArray digest = "sha256:" + QByteArray(62, '0') + "ff";

        const auto release = parseRelease(head + zip + ", " + setup(url, digest, 1234) + "]}",
                                          WindowsArchitecture::X86_64);
        QVERIFY(release && release->installer);
        QCOMPARE(release->installer->name, u"Cirrus-0.5.0-windows-x64-setup.exe"_s);
        QCOMPARE(release->installer->url, QUrl(QString::fromLatin1(url)));
        QCOMPARE(release->installer->size, 1234);
        QCOMPARE(release->installer->sha256, QByteArray(31, '\0') + '\xff');

        // The release is still one; only its installer is not taken.
        const auto without = [&](const QByteArray& asset) {
            const auto r = parseRelease(head + asset + "]}", WindowsArchitecture::X86_64);
            return r && !r->installer;
        };
        QVERIFY(without(zip));
        QVERIFY(without(setup("https://github.com/Guang233/CirrusMusic/releases/download/v0.5.0/"
                              "Cirrus-0.5.0-windows-x64-setup.exe",
                              digest, 1234)));
        QVERIFY(without(setup(url, "sha1:" + QByteArray(40, '0'), 1234)));
        QVERIFY(without(setup(url, "sha256:" + QByteArray(64, 'A'), 1234)));
        QVERIFY(without(setup(url, digest, 0)));
        QVERIFY(without(setup("http://github.com/wxmyyds/CirrusMusic/releases/download/v0.5.0/"
                              "Cirrus-0.5.0-windows-x64-setup.exe",
                              digest, 1234)));
        QVERIFY(without(setup("https://github.com/wxmyyds/CirrusMusic/releases/download/v0.4.0/"
                              "Cirrus-0.5.0-windows-x64-setup.exe",
                              digest, 1234)));
        QVERIFY(without(setup("https://example.com/wxmyyds/CirrusMusic/releases/download/v0.5.0/"
                              "Cirrus-0.5.0-windows-x64-setup.exe",
                              digest, 1234)));
        QVERIFY(without(setup(url + "?x=1", digest, 1234)));
    }

    void selectsOnlyTheBuildArchitecturesInstaller()
    {
        QCOMPARE(installerName(u"0.5.0", WindowsArchitecture::X86_64),
                 u"Cirrus-0.5.0-windows-x64-setup.exe"_s);
        QCOMPARE(installerName(u"0.5.0", WindowsArchitecture::Arm64),
                 u"Cirrus-0.5.0-windows-arm64-setup.exe"_s);
        QVERIFY(installerName(u"0.5.0", WindowsArchitecture::Unknown).isEmpty());

        const auto asset = [](WindowsArchitecture architecture) {
            const QString name = installerName(u"0.5.0", architecture);
            return QJsonObject{
                {u"name"_s, name},
                {u"size"_s, 1234},
                {u"browser_download_url"_s,
                 u"https://github.com/wxmyyds/CirrusMusic/releases/download/v0.5.0/"_s + name},
                {u"digest"_s, u"sha256:"_s + QString(64, u'a')}};
        };
        const auto release = [](const QJsonArray& assets, WindowsArchitecture architecture) {
            const QJsonObject json{
                {u"tag_name"_s, u"v0.5.0"_s},
                {u"html_url"_s, u"https://github.com/wxmyyds/CirrusMusic/releases/tag/v0.5.0"_s},
                {u"assets"_s, assets}};
            return parseRelease(QJsonDocument(json).toJson(), architecture);
        };
        const auto x64 = asset(WindowsArchitecture::X86_64);
        const auto arm64 = asset(WindowsArchitecture::Arm64);
        // Asset order must not choose the CPU: check both orderings for each build.
        for (auto architecture : {WindowsArchitecture::X86_64, WindowsArchitecture::Arm64}) {
            for (const auto& assets : {QJsonArray{x64, arm64}, QJsonArray{arm64, x64}}) {
                const auto parsed = release(assets, architecture);
                QVERIFY(parsed && parsed->installer);
                QCOMPARE(parsed->installer->name, installerName(u"0.5.0", architecture));
                QCOMPARE(parsed->installer->sha256, QByteArray(32, '\xaa'));
            }
            const auto other = architecture == WindowsArchitecture::X86_64 ? arm64 : x64;
            const auto wrong = release(QJsonArray{other}, architecture);
            QVERIFY(wrong && !wrong->installer);

            auto mismatched = asset(architecture);
            mismatched[u"browser_download_url"_s] = other[u"browser_download_url"_s];
            const auto wrongUrl = release(QJsonArray{mismatched}, architecture);
            QVERIFY(wrongUrl && !wrongUrl->installer);

            auto unverified = asset(architecture);
            unverified.remove(u"digest"_s);
            const auto noDigest = release(QJsonArray{unverified}, architecture);
            QVERIFY(noDigest && !noDigest->installer);
        }
        const auto unknown = release(QJsonArray{x64, arm64}, WindowsArchitecture::Unknown);
        QVERIFY(unknown && !unknown->installer);
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

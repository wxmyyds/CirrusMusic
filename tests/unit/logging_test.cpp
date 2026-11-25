#include "logging/crash.h"
#include "logging/durations.h"
#include "logging/log.h"
#include "logging/logger.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtTest>

#include <csignal>
#ifdef Q_OS_WIN
#include "infrastructure/storage/private_files.h"
#endif
#ifdef Q_OS_LINUX
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace demo
{
enum class Plain { A, B, C };
enum class Named { Ready, Broken };
QString logName(Named n)
{
    return n == Named::Ready ? QStringLiteral("ready") : QStringLiteral("broken");
}
} // namespace demo

using namespace cirrus;

Q_STATIC_LOGGING_CATEGORY(lcLibrary, "qt.demo")

namespace
{
QString text(const log::Fields& fields) { return fields.text(); }

QString readAll(const QString& directory)
{
    QString all;
    auto files = QDir(directory).entryInfoList({QStringLiteral("cirrus-*.log")}, QDir::Files,
                                               QDir::Time | QDir::Reversed);
    for (const QFileInfo& info : std::as_const(files)) {
        QFile f(info.filePath());
        if (f.open(QIODevice::ReadOnly))
            all += QString::fromUtf8(f.readAll());
    }
    return all;
}

log::FileOptions options(const QTemporaryDir& dir)
{
    log::FileOptions o;
    o.directory = dir.filePath(QStringLiteral("logs"));
    o.header = QStringLiteral("commit=abc123");
    return o;
}

// A file of an earlier session, `days` old.
void oldFile(const QString& directory, const QString& name, qint64 bytes, int days,
             const QByteArray& last = " app.exit\n")
{
    QDir().mkpath(directory);
    QFile f(QDir(directory).filePath(name));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(bytes, 'x') + '\n' + last);
    f.close();
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-days),
                          QFileDevice::FileModificationTime));
}
} // namespace

class LoggingTest : public QObject
{
    Q_OBJECT
  private slots:
    void cleanup() { log::shutdown(); }

    void fieldsAreSafeAndReadable()
    {
        QCOMPARE(text({"ms", 203}), "ms=203");
        QCOMPARE(text({"ok", true}), "ok=true");
        QCOMPARE(text({"ratio", 0.5}), "ratio=0.5");
        QCOMPARE(text({"op", QStringLiteral("song/url/v1")}), "op=song/url/v1");
        QCOMPARE(text({"os", "Arch Linux"}), "os=\"Arch Linux\"");
        QCOMPARE(text({"v", QStringLiteral("a\"b\\c")}), "v=\"a\\\"b\\\\c\"");
        QCOMPARE(text({"v", QString()}), "v=\"\"");
        QCOMPARE(text({"v", QStringLiteral("a=b")}), "v=\"a=b\"");
        QCOMPARE(text({"key", Qt::Key_A}), "key=Key_A"); // Q_ENUM names
        QCOMPARE(text({"e", demo::Plain::C}), "e=2");
        QCOMPARE(text({"e", demo::Named::Broken}), "e=broken"); // logName()
        const QString longValue(500, u'a');
        QCOMPARE(text({"v", longValue}).size(), 2 + 200 + 1);
    }

    void safeForms()
    {
        QCOMPARE(log::host(QUrl("https://M10.music.126.net/a/b.flac?vuutv=secret")).value,
                 "m10.music.126.net");
        QCOMPARE(log::host(QUrl()).value, "-");
        QCOMPARE(log::message("Error transferring https://u:p@m701.music.126.net/2026/a1b2/x.flac"
                              "?vuutv=secret - server replied: Forbidden")
                     .value,
                 "Error transferring https://m701.music.126.net/… - server replied: Forbidden");
        QCOMPARE(log::message("Proxy connection refused").value, "Proxy connection refused");
        log::setPathRoots(
            {{"<cache>", "/home/u/.cache/Cirrus"}, {"<audio>", "/home/u/.cache/Cirrus/audio"}});
        QCOMPARE(log::path("/home/u/.cache/Cirrus/audio/12.flac").value, "<audio>/12.flac");
        QCOMPARE(log::path("/home/u/.cache/Cirrus/resources/a").value, "<cache>/resources/a");
        QCOMPARE(log::path("/srv/music/song.mp3").value, "song.mp3");
        log::setAccountKey("k1");
        const auto a = log::account(u"101");
        QCOMPARE(a.value.size(), 12);
        QCOMPARE(log::account(u"101").value, a.value);
        QVERIFY(log::account(u"102").value != a.value);
        QVERIFY(!a.value.contains("101"));
        log::setAccountKey("k2");
        QVERIFY(log::account(u"101").value != a.value);
        QCOMPARE(log::account(u"").value, "-");
        log::setPathRoots({});
    }

    void sessionFile()
    {
        QTemporaryDir dir;
        log::install(false);
        log::info(lcPlayback, "held", {"n", 1}); // before the file
        QVERIFY(log::currentFile().isEmpty());
        QVERIFY(log::start(options(dir)));
        QVERIFY(QFileInfo(log::currentFile()).fileName().startsWith("cirrus-"));
        log::warn(lcNet, "request.failed", {"req", 7});
        qWarning("two\nlines");
        qWarning("see https://interface.music.163.com/eapi/x?token=secret#frag now");
        qWarning("%s", qPrintable(QDir::homePath() + "/private/file"));
        qCDebug(lcMedia) << "hidden by default";
        log::setVerbose(true);
        log::debug(lcMedia, "shown");
        log::setVerbose(false);
        log::flush();
        const QString file = log::currentFile();
        log::shutdown();

        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts);
        // The header first, then what was held, then the rest; each record one line.
        QVERIFY2(lines.value(0).contains(" I main    cirrus.app app.start version="),
                 qPrintable(lines.value(0)));
        QVERIFY(lines.value(0).contains("commit=abc123"));
        QVERIFY(lines.value(0).contains("part=1"));
        static const QRegularExpression record("^\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\.\\d{3}"
                                               "(Z|[+-]\\d\\d:\\d\\d) [DIWCF] \\S+\\s+\\S+ .+$");
        for (const QString& line : lines)
            QVERIFY2(record.match(line).hasMatch(), qPrintable(line));
        QVERIFY(lines.value(1).endsWith("cirrus.playback held n=1"));
        // Warnings say where in the code they were written.
        QVERIFY2(lines.value(2).contains(" W main    cirrus.net request.failed req=7 "
                                         "src=unit/logging_test.cpp:"),
                 qPrintable(lines.value(2)));
        QVERIFY(lines.value(2).endsWith(" fn=LoggingTest::sessionFile"));
        QVERIFY(lines.value(3).contains("default two\\nlines src="));
        QVERIFY2(lines.value(4).contains("see https://interface.music.163.com/eapi/x?… now src="),
                 qPrintable(lines.value(4)));
        QVERIFY(lines.value(5).contains("~/private/file src="));
        QVERIFY(lines.value(6).endsWith(" D main    cirrus.media shown"));
        QVERIFY(lines.last().endsWith("cirrus.app app.exit"));
        const QString all = lines.join(u'\n');
        QVERIFY(!all.contains("secret"));
        QVERIFY(!all.contains("hidden by default"));
#ifdef Q_OS_WIN
        QVERIFY2(storage::isPrivateDirectory(dir.filePath("logs")), storage::lastRefusal().reason);
        QVERIFY2(storage::isPrivateFileOrAbsent(file), storage::lastRefusal().reason);
#else
        QCOMPARE(QFileInfo(dir.filePath("logs")).permissions() & QFileDevice::ReadOther,
                 QFileDevice::Permissions{});
        QCOMPARE(QFileInfo(file).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions{});
#endif
    }

    void optionalFieldsAndDurations()
    {
        QVERIFY(text(log::fieldIf(false, "retry_after", QStringLiteral("5"))).isEmpty());
        QCOMPARE(text(log::fieldIf(true, "status", 503)), "status=503");
        QCOMPARE(text({{"a", 1}, log::fieldIf(false, "b", 2), {"c", 3}}), "a=1 c=3");
        // Fields within fields, and built up a field at a time.
        const log::Fields more{{"b", 2}, {"c", 3}};
        QCOMPARE(text({{"a", 1}, more, log::Fields()}), "a=1 b=2 c=3");
        log::Fields built;
        built.add("a", 1).add(log::fieldIf(false, "x", 0)).add(more);
        QCOMPARE(text(built), "a=1 b=2 c=3");

        log::Durations stats(0);
        QVERIFY(!stats.due());
        stats.add(10, QStringLiteral("/api/a"));
        stats.add(30, QStringLiteral("/api/b"), QStringLiteral("Network"));
        stats.add(20, QStringLiteral("/api/a"));
        QVERIFY(stats.due());
        const QString summary = text(stats.take());
        QVERIFY2(summary.startsWith("n=3 failed=1 errors=Network:1 p50_ms=20 p95_ms=30 "
                                    "max_ms=30 slowest=/api/b window_s="),
                 qPrintable(summary));
        QVERIFY(stats.empty());
        QVERIFY(!stats.due());

        // A warning for the third failure of a window, and not again within it.
        log::Burst burst(3, 60 * 1000);
        QVERIFY(!burst.add());
        QVERIFY(!burst.add());
        QVERIFY(burst.add());
        QVERIFY(!burst.add());
        QCOMPARE(burst.count(), 4);
        // Failures further apart than the window never pile up.
        log::Burst apart(2, 0);
        QVERIFY(!apart.add());
        QVERIFY(!apart.add());
        QCOMPARE(apart.count(), 1);
    }

    // Debug records wait in memory, and come to the file before a warning (the last 30 s)
    // or all at once (a mark); a warning carries where the user was.
    void flightRecorder()
    {
        QTemporaryDir dir;
        log::install(false);
        QVERIFY(log::start(options(dir)));
        log::debug(lcMedia, "before", {"n", 1});
        log::info(lcPlayback, "info");
        log::warn(lcNet, "failed");
        log::warn(lcNet, "failed_again"); // nothing new to bring
        log::debug(lcMedia, "between");
        log::setContext("page", QStringLiteral("AlbumPage"));
        log::setContext("dialog", QStringLiteral("LoginDialog"));
        log::setContext("dialog", QString());
        qWarning("from qt");
        log::setContext("page", QString());
        log::debug(lcMedia, "marked");
        qCWarning(lcLibrary) << "a library's own"; // speaks of the system: brings nothing
        log::writeContext("mark");
        log::writeContext("mark"); // nothing new: no empty block
        log::debug(lcMedia, "never");
        log::flush();
        const QString file = log::currentFile();
        log::shutdown();

        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts);
        static const QRegularExpression prefix("^\\S+ [DIWCF] \\S+\\s+");
        static const QRegularExpression source(" src=\\S+ fn=\\S+");
        for (QString& line : lines) {
            line.remove(prefix); // without time, level and thread
            line.remove(source); // and where in this file (sessionFile() checks it)
        }
        const QStringList expected = {
            "cirrus.playback info",
            "cirrus.log log.context_begin reason=warning count=1",
            "cirrus.media before n=1",
            "cirrus.log log.context_end reason=warning count=1",
            "cirrus.net failed",
            "cirrus.net failed_again",
            "cirrus.log log.context_begin reason=warning count=1",
            "cirrus.media between",
            "cirrus.log log.context_end reason=warning count=1",
            "default from qt ctx.page=AlbumPage",
            "qt.demo a library's own",
            "cirrus.log log.context_begin reason=mark count=1",
            "cirrus.media marked",
            "cirrus.log log.context_end reason=mark count=1",
            "cirrus.app app.exit",
        };
        QCOMPARE(lines.mid(1), expected); // after the header
    }

#ifdef Q_OS_LINUX
    // A crash in a child process leaves its stack; the next run logs it as module and
    // offset records, without paths, and removes the raw file.
    void crashStack()
    {
        QTemporaryDir dir;
        const QString raw = dir.filePath("crash-cirrus-20261111-120000-42.txt");
        const pid_t child = fork();
        if (child == 0) {
            log::crash::install(raw);
            std::raise(SIGSEGV);
            _exit(0);
        }
        int status = 0;
        QCOMPARE(waitpid(child, &status, 0), child);
        QVERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
        QVERIFY(QFile::exists(raw));

        log::install(false);
        QVERIFY(log::start(options(dir)));
        log::crash::report(dir.path());
        log::flush();
        const QString all = readAll(dir.filePath("logs"));
        log::shutdown();
        QVERIFY(!QFile::exists(raw));
        QFile kept(dir.filePath("crashes/cirrus-20261111-120000-42.txt"));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        const QString table = QString::fromUtf8(kept.readAll());
        QVERIFY2(table.startsWith("session: cirrus-20261111-120000-42\nsignal: 11\n"),
                 qPrintable(table));
        QVERIFY(table.contains("\n# 0 "));
        QVERIFY(!table.contains(QCoreApplication::applicationDirPath()));
        if (qEnvironmentVariableIsSet("CIRRUS_KEEP_CRASH")) {
            QFile keep(qEnvironmentVariable("CIRRUS_KEEP_CRASH"));
            QVERIFY(keep.open(QIODevice::WriteOnly));
            keep.write(all.toUtf8());
        }
        QVERIFY2(all.contains("cirrus.app app.crash session=cirrus-20261111-120000-42 signal=11 "
                              "frames="),
                 qPrintable(all));
        QVERIFY(all.contains("app.crash_frame i=0 module="));
        QVERIFY(
            all.contains(QStringLiteral("module=%1")
                             .arg(QFileInfo(QCoreApplication::applicationFilePath()).fileName())));
        QVERIFY(!all.contains(QCoreApplication::applicationDirPath()));
        QVERIFY(QRegularExpression("build_id=[0-9a-f]{16,}").match(all).hasMatch());
    }
#endif

    void previousSessionThatDidNotEnd()
    {
        QTemporaryDir dir;
        const auto o = options(dir);
        oldFile(o.directory, "cirrus-20261101-100000-11.log", 10, 2);
        oldFile(o.directory, "cirrus-20261108-100000-12.log", 10, 1,
                "... I main    cirrus.app app.start\nfatal signal 11\n");
        log::install(false);
        QVERIFY(log::start(o));
        log::flush();
        const auto recent = log::recent();
        QVERIFY(std::any_of(recent.cbegin(), recent.cend(), [](const QString& line) {
            return line.contains(
                "app.previous_session_unclean file=cirrus-20261108-100000-12.log signal=11");
        }));
        log::shutdown();
        // A previous session that ended: no complaint.
        oldFile(o.directory, "cirrus-20261109-100000-13.log", 10, 0);
        log::install(false);
        QVERIFY(log::start(o));
        log::flush();
        const auto again = log::recent();
        QVERIFY(std::none_of(again.cbegin(), again.cend(),
                             [](const QString& line) { return line.contains("unclean"); }));
    }

    void partsAndRetention()
    {
        QTemporaryDir dir;
        auto o = options(dir);
        o.fileLimit = 4000;
        o.totalLimit = 30000;
        oldFile(o.directory, "cirrus-20261001-100000-1.log", 100, 30);  // too old
        oldFile(o.directory, "cirrus-20261105-100000-2.log", 20000, 4); // over budget, oldest
        oldFile(o.directory, "cirrus-20261106-100000-3.log", 5000, 3);  // kept within budget
        oldFile(o.directory, "cirrus-20261107-100000-4.log", 9000, 2);  // the previous session
        oldFile(o.directory, "cirrus-20261107-100000-4-2.log", 9000, 2);
        log::install(false);
        QVERIFY(log::start(o));
        for (int i = 0; i < 200; ++i)
            log::info(lcApp, "filler", {{"i", i}, {"pad", QString(40, u'y')}});
        log::flush();
        QVERIFY(QFileInfo(log::currentFile())
                    .fileName()
                    .contains(QRegularExpression("-\\d+-\\d+\\.log$")));
        const QDir logs(o.directory);
        QVERIFY(!logs.exists("cirrus-20261001-100000-1.log"));
        QVERIFY(!logs.exists("cirrus-20261105-100000-2.log"));
        QVERIFY(logs.exists("cirrus-20261107-100000-4.log"));
        QVERIFY(logs.exists("cirrus-20261107-100000-4-2.log"));
        // Every part of this session starts with the header, and nothing was lost.
        int parts = 0;
        int records = 0;
        for (const QFileInfo& info : logs.entryInfoList({"cirrus-*.log"}, QDir::Files)) {
            if (info.fileName().startsWith("cirrus-202611") && info.fileName().contains("-1000"))
                continue; // the fakes
            QFile f(info.filePath());
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString content = QString::fromUtf8(f.readAll());
            QVERIFY(content.section(u'\n', 0, 0).contains("app.start"));
            QVERIFY(info.size() <= o.fileLimit + 200);
            records += content.count(" filler i=");
            ++parts;
        }
        QVERIFY2(parts > 3, qPrintable(QString::number(parts)));
        QCOMPARE(records, 200);
    }

    void repeatsAreCounted()
    {
        QTemporaryDir dir;
        log::install(false);
        QVERIFY(log::start(options(dir)));
        for (int i = 0; i < 100; ++i)
            log::warn(lcMedia, "same");
        log::warn(lcMedia, "different");
        log::shutdown();
        const QString all = readAll(dir.filePath("logs"));
        QCOMPARE(all.count("cirrus.media same"), 20);
        QVERIFY(all.contains("cirrus.log log.repeated count=80\n"));
        QVERIFY(all.indexOf("log.repeated") < all.indexOf("cirrus.media different"));
    }

    void filesClearAndArchive()
    {
        QTemporaryDir dir;
        const auto o = options(dir);
        oldFile(o.directory, "cirrus-20261108-100000-12.log", 1000, 1);
        log::install(false);
        QVERIFY(log::start(o));
        log::info(lcApp, "hello");
        log::flush();
        auto list = log::files();
        QCOMPARE(list.size(), 2);
        QVERIFY(list.first().current); // newest first
        QVERIFY(!list.last().current);
        QCOMPARE(list.last().name, "cirrus-20261108-100000-12.log");
        const QString zip = dir.filePath("out.zip");
        QVERIFY(log::writeArchive(zip, {list.first().path, list.last().path},
                                  {{"summary.txt", "app: test\n"}}));
        QFile f(zip);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.startsWith("PK\x03\x04"));
        QVERIFY(bytes.contains("logs/cirrus-20261108-100000-12.log"));
        QVERIFY(bytes.contains("summary.txt"));
        QVERIFY(bytes.contains("PK\x05\x06"));
        QVERIFY(bytes.size() < list.last().bytes); // deflated
        QVERIFY(!log::writeArchive(dir.filePath("bad.zip"), {dir.filePath("missing.log")}));
        QVERIFY(!QFile::exists(dir.filePath("bad.zip")));
        QCOMPARE(log::clearOld(), list.last().bytes);
        list = log::files();
        QCOMPARE(list.size(), 1);
        QVERIFY(list.first().current);
        if (qEnvironmentVariableIsSet("CIRRUS_KEEP_ZIP"))
            QFile::copy(zip, qEnvironmentVariable("CIRRUS_KEEP_ZIP"));
    }

    void manyThreads()
    {
        QTemporaryDir dir;
        log::install(false);
        QVERIFY(log::start(options(dir)));
        QList<QThread*> threads;
        for (int t = 0; t < 4; ++t) {
            threads.append(QThread::create([t] {
                for (int i = 0; i < 500; ++i)
                    log::info(lcPlayback, "tick", {{"t", t}, {"i", i}});
            }));
            threads.last()->setObjectName(QStringLiteral("worker%1").arg(t));
            threads.last()->start();
        }
        for (QThread* thread : std::as_const(threads)) {
            QVERIFY(thread->wait(10000));
            delete thread;
        }
        log::shutdown();
        const QString all = readAll(dir.filePath("logs"));
        QCOMPARE(all.count(" tick t="), 2000);
        QVERIFY(all.contains(" I worker2 cirrus.playback tick t=2 i=499"));
    }
};
QTEST_GUILESS_MAIN(LoggingTest)
#include "logging_test.moc"

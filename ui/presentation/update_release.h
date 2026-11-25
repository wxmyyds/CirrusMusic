#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <array>
#include <optional>

namespace cirrus::ui::updates
{
// The pure parts of the update check (docs/tasks/update-check.md): no network, so they
// are tested on their own.

// A file of a release that the app downloads itself: the Windows installer.
struct Asset {
    QString name;      // "Cirrus-0.5.0-windows-x64-setup.exe"
    QUrl url;          // its download address (isDownloadUrl)
    qint64 size = 0;   // in bytes, as GitHub has it
    QByteArray sha256; // the 32 bytes of GitHub's digest
};

// The latest release, as GitHub's "latest release" answer gives it.
struct Release {
    QString version; // "0.1.2", the tag without its "v"
    QString title;   // the release's name ("Cirrus 0.1.2")
    QString notes;   // its body (the CHANGELOG section) as plain text
    QUrl page;       // the release page on github.com
    QDateTime published;
    // The Windows installer, when the release has one with a size and a sha256 digest.
    std::optional<Asset> installer;
};

// "v1.2.3" or "1.2.3" as {1, 2, 3}; anything else (a pre-release suffix, missing or
// extra parts, letters) is nullopt.
std::optional<std::array<int, 3>> parseVersion(QStringView text);
// Whether `latest` is a newer version than `current`; false when either is not one.
bool isNewer(QStringView latest, QStringView current);
// GitHub's latest-release JSON; nullopt when it is malformed, a draft, a pre-release,
// has no usable version or its page is not the project's (isReleasePage).
std::optional<Release> parseRelease(const QByteArray& json);
// A release page of the project on GitHub (https only): the only page the app opens
// for an update.
bool isReleasePage(const QUrl& url);
// The address of a release file of the project on GitHub (https only, the tag's own):
// the only place the app downloads an update from.
bool isDownloadUrl(const QUrl& url, QStringView version, QStringView name);
// The Windows installer's file name for a version.
QString installerName(QStringView version);
// The release notes (GitHub Markdown, the CHANGELOG's style) as plain text for a
// selectable text block: headings as their text, list items as "• ", links as their
// text, emphasis and code marks dropped; no HTML ever reaches the UI.
QString notesToPlainText(const QString& markdown);
} // namespace cirrus::ui::updates

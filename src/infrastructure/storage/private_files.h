#pragma once

#include <QFile>
#include <QString>

#include <memory>

namespace cirrus::storage
{

// Directories and files the caches keep (resource cache, audio cache): ours alone and
// never reached through a link. On Linux and macOS: owned by us, 0700/0600, one link,
// no symlink, and no ancestor that someone else owns or can write (group/world-writable);
// the process must not run set-uid/set-gid. On Windows: owned by our user (or the token's
// default owner, Administrators when elevated), no reparse point, one link, and no ACE that
// lets Everyone, Authenticated Users, Users, Interactive or Anonymous write it. Windows
// ancestors are not checked; the profile's default ACL keeps %LOCALAPPDATA% private.

// `dir` is absolute, clean and a private directory.
[[nodiscard]] bool isPrivateDirectory(const QString& dir);
// Creates `dir` (not its parents) owner-only when missing, then checks it.
[[nodiscard]] bool makePrivateDirectory(const QString& dir);
// `path` does not exist, or is a private regular file.
[[nodiscard]] bool isPrivateFileOrAbsent(const QString& path);
// Opens `path` read-write, creating it owner-only, never through a link; null unless the
// opened file is private. Read-only opens require an existing file. Either access mode may
// include Unbuffered. Handles share read/write/delete with other authorized handles, as
// on POSIX; a cache reader must not block invalidation or eviction.
[[nodiscard]] std::unique_ptr<QFile>
openPrivateFile(const QString& path, QIODevice::OpenMode mode = QIODevice::ReadWrite);

// Why the last check on this thread said no: a short fixed id ("mode", "owner",
// "ancestor_writable", "acl", "open" …) and the system's error number when a call failed
// (errno, or GetLastError() on Windows), else 0. For the log; reset by every check.
struct Refusal {
    const char* reason = nullptr;
    long error = 0;
};
[[nodiscard]] Refusal lastRefusal();

} // namespace cirrus::storage

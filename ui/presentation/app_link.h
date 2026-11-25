#pragma once
#include <QString>
#include <optional>

// The links the NetEase web pages open "in the client" (docs/ui/pages/open-links.md):
// orpheus://<base64 of {"type":…,"id":…,"cmd":"play"|"download",…}>[?…]. Pure; the
// text comes from a browser, so only known types and numeric ids get through.
namespace cirrus::ui::applink
{
// Append only: App::LinkTarget has the same values.
enum class Target { Song, Voice, Playlist, Album, Artist, Podcast, User, Unsupported };

struct Request {
    Target target = Target::Unsupported;
    QString id;        // digits; empty for Unsupported
    bool play = false; // cmd "play" (去客户端播放)
};

// None when it is no orpheus link this can read; Unsupported for kinds Cirrus has no
// page for (MV, video, event…).
std::optional<Request> parse(const QString& url);
// What a launch argument must look like to be passed on (single instance).
bool plausible(const QString& url);
} // namespace cirrus::ui::applink

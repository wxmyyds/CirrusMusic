#!/usr/bin/env python3
"""Shows what Cirrus is playing and the current lyric line, from Cirrus's local
now-playing interface (README.md next to this file). Linux; Python 3.9+, no packages.

  cirrus_now_playing.py            follow: a line per song and per lyric line
  cirrus_now_playing.py --once     print one snapshot as JSON and exit
  cirrus_now_playing.py --track translation.standard

Cirrus must be running with Settings > 窗口与系统 > 向其他程序提供播放状态 on.
"""

import argparse
import glob
import json
import os
import socket
import stat
import struct
import sys

NAMESPACE = "com.guang233.Cirrus"
MANIFEST = "observation-v1.json"
TRACKS = (
    "original.standard", "original.enhanced",
    "translation.standard", "translation.enhanced",
    "romanization.standard", "romanization.enhanced",
)
MAX_FRAME = 1024 * 1024


class Unavailable(Exception):
    pass


def private(path, kind, mode):
    """The path is ours, not a link, and only we can use it."""
    s = os.lstat(path)
    return kind(s.st_mode) and s.st_uid == os.getuid() and stat.S_IMODE(s.st_mode) == mode


def endpoints():
    """Sockets of running Cirrus instances, newest first (a crash can leave old ones)."""
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if not runtime:
        raise Unavailable("XDG_RUNTIME_DIR is not set")
    space = os.path.join(runtime, NAMESPACE)
    if not os.path.isdir(space) or not private(space, stat.S_ISDIR, 0o700):
        raise Unavailable("Cirrus is not sharing what it plays")
    found = []
    for manifest in glob.glob(os.path.join(space, "i-*", MANIFEST)):
        leaf = os.path.dirname(manifest)
        try:
            if not private(leaf, stat.S_ISDIR, 0o700) or not private(manifest, stat.S_ISREG, 0o600):
                continue
            with open(manifest, "rb") as f:
                info = json.loads(f.read(4096))
        except (OSError, ValueError):
            continue
        # The endpoint is always the socket next to the manifest.
        socket_path = os.path.join(leaf, "s")
        if (info.get("schema") != 1 or info.get("endpoint") != socket_path
                or os.path.basename(leaf) != "i-" + str(info.get("instanceId"))):
            continue
        found.append((os.lstat(manifest).st_mtime, socket_path, info["instanceId"]))
    return [(path, instance) for _, path, instance in sorted(found, reverse=True)]


class Connection:
    def __init__(self, path, instance):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect(path)
        # Talk only to a server of our own user.
        creds = self.sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i"))
        _, uid, _ = struct.unpack("3i", creds)
        if uid != os.getuid():
            raise Unavailable("the socket belongs to another user")
        self.next_id = 0
        self.pending_events = []
        hello = self.call("hello")
        if hello.get("instanceId") != instance:
            raise Unavailable("the socket is another instance's")
        self.sock.settimeout(None)

    def send(self, message):
        body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
        self.sock.sendall(struct.pack(">I", len(body)) + body)

    def receive_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise Unavailable("Cirrus closed the connection")
            data += chunk
        return data

    def receive(self):
        (n,) = struct.unpack(">I", self.receive_exact(4))
        if n > MAX_FRAME:
            raise Unavailable("frame too large")
        return json.loads(self.receive_exact(n))

    def call(self, method, **fields):
        self.next_id += 1
        rid = "r%d" % self.next_id
        self.send({"v": 1, "kind": "request", "id": rid, "method": method, **fields})
        while True:
            message = self.receive()
            if message.get("kind") == "event":
                self.pending_events.append(message)  # events may come before the reply
                continue
            if message.get("id") == rid:
                if not message.get("ok"):
                    raise Unavailable("%s failed: %s" % (method, message["error"]["code"]))
                return message

    def event(self):
        if self.pending_events:
            return self.pending_events.pop(0)
        return self.receive()

    def lyrics(self, snapshot, track):
        """All lines of one lyric track of the snapshot's song, or None when it changed."""
        lines, start = [], 0
        while start is not None:
            try:
                page = self.call(
                    "getLyricsSlice",
                    instanceId=snapshot["instanceId"], viewEpoch=snapshot["viewEpoch"],
                    trackId=snapshot["trackId"], lyricsRevision=snapshot["lyricsRevision"],
                    track=track, fromLine=start, maxLines=128)
            except Unavailable as e:
                if "StaleView" in str(e) or "LyricsNotReady" in str(e):
                    return None
                raise
            lines += page["lines"]
            start = page["nextLine"]
        return lines


def connect():
    errors = []
    for path, instance in endpoints():
        try:
            return Connection(path, instance)
        except (OSError, Unavailable) as e:
            errors.append(str(e))
    raise Unavailable(errors[0] if errors else "Cirrus is not running or not sharing")


def title_of(snapshot):
    meta = snapshot.get("metadata")
    if not meta:
        song = (snapshot.get("track") or {}).get("providerSongId")
        return "song %s" % song if song else "nothing"
    artists = " / ".join(a for a in meta.get("artists") or [] if a)
    return "%s — %s" % (meta.get("title"), artists) if artists else meta.get("title")


def follow(conn, track):
    reply = conn.call("subscribe", topics=["snapshot", "lyrics", "cursor"])
    snapshot = reply["snapshot"]
    song, revision, lines, shown = None, None, None, None
    while True:
        if snapshot["trackId"] != song:
            song, revision, lines, shown = snapshot["trackId"], None, None, None
            print("♪ %s" % title_of(snapshot), flush=True)
        lyrics = snapshot["lyrics"]
        if lyrics["state"] == "ready" and snapshot["lyricsRevision"] != revision:
            revision = snapshot["lyricsRevision"]
            summary = lyrics["tracks"][track]["summary"]
            lines = conn.lyrics(snapshot, track) if summary and summary["lineCount"] else []
        cursor = lyrics["tracks"][track]["cursor"] if lines else None
        current = tuple(cursor["lines"]) if cursor else ()
        if current and current != shown:
            shown = current
            by_index = {line["index"]: line["text"] for line in lines}
            print("  " + " / ".join(by_index.get(i, "") for i in current), flush=True)
        snapshot = conn.event()["snapshot"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--once", action="store_true", help="print one snapshot as JSON")
    parser.add_argument("--track", choices=TRACKS, default="original.standard",
                        help="lyric track to follow (default: original.standard)")
    args = parser.parse_args()
    try:
        conn = connect()
        if args.once:
            print(json.dumps(conn.call("getSnapshot")["snapshot"], ensure_ascii=False, indent=2))
        else:
            follow(conn, args.track)
    except Unavailable as e:
        print("cirrus_now_playing: %s" % e, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

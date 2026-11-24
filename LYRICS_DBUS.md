# Lyrics D-Bus extension, version 1

This is Cirrus's **custom, open, versioned** session-bus interface, not a freedesktop
lyrics standard. D-Bus is the transport; applications must implement this contract
to consume it. Ordinary MPRIS remains unchanged except that lyric text is now opt-in.

## Consent and scope

Settings → Lyrics → **向外部提供歌词 (私货)** (`lyrics/externalExport`) is **off by
default**. When off:

- Standard MPRIS track metadata, playback position and controls remain available.
- `Metadata["xesam:asText"]` is omitted. Other lyric applications can use ordinary
  artist/title metadata to search independently; Cirrus does not control them.
- This object's `Enabled` is false and `Document` / `State` are empty dictionaries.
  Turning it off immediately notifies clients to discard previously received data.

When on, any application able to access the current user's session bus can read
lyrics, aligned translation/romanization and playback information. This is **not**
a per-client access-control mechanism; disabling cannot revoke data already copied
by another process. No Cookie, account identity, access token, stream URL, private
filesystem path or network error body is exported. No intermediate lyric file is
written. Lyrics come from the exact active playback request, never another search.
`playbackProvider` describes the audio provider, **not** guaranteed lyric provenance
(Bilibili subtitle absence can use the existing NetEase fallback).

## Discovery and introspection

Use the active player's MPRIS service address (Cirrus currently owns
`org.mpris.MediaPlayer2.cirrus`). Do not identify a bus service by its display name.
The lyric extension shares that service's owner/lifetime but uses its own path:

- Path: `/org/cirrus/Lyrics`
- Interface: `org.cirrus.Lyrics1`
- `org.freedesktop.DBus.Introspectable` and `org.freedesktop.DBus.Properties`:
  normal Qt D-Bus introspection / `Get` / `GetAll`.

Properties (all read-only):

| Property | D-Bus signature | Meaning |
|---|---|---|
| `Version` | `u` | 1; the interface suffix changes for incompatible revisions |
| `Enabled` | `b` | explicit opt-in |
| `Document` | `a{sv}` | current document; empty when disabled |
| `State` | `a{sv}` | current clock and cursor; empty when disabled |

Method `GetSnapshot() → a{sv}` returns `{version:u, enabled:b, document:a{sv},
state:a{sv}}` as one coherent read. There are no write, search, retry or playback
control methods here; playback controls remain on MPRIS.

Updates use `org.freedesktop.DBus.Properties.PropertiesChanged(s, a{sv}, as)` on
this path, with interface name `org.cirrus.Lyrics1`. A document/consent change
includes `Enabled`, `Document`, `State`; cursor/playback changes include `State`.
Full text is not retransmitted on each playback tick. No periodic position signal
is promised: get a fresh snapshot when attaching/resynchronizing and extrapolate
using a local monotonic clock and `rate` if rendering continuous word progress.

Subscribe **before** the initial asynchronous snapshot, bind to the current unique
bus owner, and reject a delayed initial reply if a newer update already arrived.
On owner change, active-player change, missing object or disabled export, clear old
text. Process a changed `Document` before its `State`. A consumer can keep standard
`xesam:asText` support for other players, but must not treat a disabled extension as
permission to retain old text. The Clavis patch never searches files/cache/network.

## Document dictionary

| Key | Type | Meaning |
|---|---|---|
| `revision` | `t` | increasing document/consent revision within this bus-owner lifetime |
| `trackId` | `o` | corresponding standard `mpris:trackid` |
| `songKey` | `s` | catalog key, not unique per load |
| `generation` | `t` | playback-load generation; distinguishes repeated loads of the same song |
| `playbackProvider` | `s` | `netease` or `bilibili`; audio provider, not lyric provenance |
| `title`, `artist`, `album` | `s` | current displayed metadata |
| `status` | `s` | `idle`, `loading`, `ready`, `empty`, `instrumental`, `error` |
| `timing` | `s` | `plain`, `lines`, `words`; only real source timing, never estimated |
| `shiftMs` | `x` | total timing shift: lyric time = playback position − shift |
| `userOffsetMs` | `x` | user adjustment; positive delays lyrics |
| `previewStartMs` | `x` | original-song preview origin, −1 for full-track playback |
| `plainText` | `s` | original text only, newline-separated; no LRC tags or markup |
| `lines` | `av` | variants containing the line dictionaries below |
| `errorCode` | `s` | optional safe machine reason (`invalid-document`), no error body |

When not ready, `lines` is empty and `plainText` is empty. An `error` is not evidence
that another provider should be queried. Consumers must ignore unknown optional
keys and must not assume all tracks support translations or word timing.

Each line dictionary (`a{sv}`):

- `text`, `translation`, `romanization`: `s`, aligned display text; absent auxiliary
  content is an empty string. Plain text must never be interpreted as rich text.
- `startMs`, `endMs`: `x`, original lyric timeline in milliseconds; −1 means unknown
  (both unknown for untimed lines). End is **exclusive**. Apply `shiftMs` once.
- `words`: `av`, each entry an `a{sv}` below. Empty for line-only/untimed material.

Each word/character/syllable segment dictionary:

- `text`: `s`, original segment, which may be a word, character or syllable depending
  on the provider. This is not a promise that every Unicode character has a timestamp.
- `startMs`, `endMs`, `durationMs`: `x`, absolute lyric-timeline times; duration = end
  − start, end exclusive. A zero-duration segment cannot be actively highlighted.
- `from`, `to`: `i`, substring boundaries in the parent line's original text, in
  **UTF-16 code units**, with `to` exclusive. Surrogate pairs count as two units.
  Do not use these indices as UTF-8 byte or grapheme indices.

`timing=words` means at least one line contains trustworthy aligned timed segments;
individual lines can still have empty `words`. Bilibili subtitles normally offer
line timestamps only. No artificial character timings are invented. Translation
and romanization are supplied as aligned strings, not a separate timed word track.

## State dictionary

| Key | Type | Meaning |
|---|---|---|
| `revision` | `t` | must equal the accepted document's revision |
| `trackId` | `o` | must equal the document and current MPRIS track |
| `positionMs` | `x` | playback-relative position at the sample |
| `sampledAtMs` | `x` | producer's monotonic-clock milliseconds; not wall-clock time |
| `durationMs` | `x` | playback duration, −1 unknown |
| `rate` | `d` | clock advancement; 0 while paused/loading, normally 1 while playing |
| `playbackStatus` | `s` | `playing`, `paused`, `loading`, `stopped` |
| `currentLine` | `i` | last line that started; −1 before the first; holds across gaps |
| `activeLines` | `av` | variants of `i`; all currently active lines, including overlaps |
| `activeWords` | `av` | variants of `{line:i, word:i}` for active timed segments |

Untimed text uses `currentLine=0` when nonempty and has no active timed entries.
Overlapping lyrics may have several active lines/segments. A seek backwards can
move the cursor backwards immediately. State publication is evaluated independently
of the QML lyric page, so it keeps working with the window hidden. While lyrics are
ready and the playback clock advances, the producer checks boundaries every 25 ms;
this is a scheduling interval, not a real-time latency guarantee. It sends state
only on cursor changes or playback snapshots, not 40 complete documents per second.

Consumers must not mix revisions/tracks, use a stale previous player's timeline, or
poll/retransmit the full document on every display frame. Future Clavis word-fill can
use these segments plus the state clock; current Clavis is display-only line sync,
with no mouse dragging, wheel scrolling, lyric clicks or word-fill implementation.

## Limits and evolution

Version 1 rejects invalid UTF-16, NUL, or more than 512 Ki UTF-16 units across original,
aligned auxiliary text and segment text; at most 4096 lines and 32768 segments.
Invalid documents produce `status=error` with no lyric payload, never partial text.
Clients should bound input and validate dictionaries, indices and time ordering too.
The session bus is not an untrusted-network security boundary, but other processes
can publish malformed data. Optional compatible fields may be added within version
1; incompatible types or semantics require a new interface version.

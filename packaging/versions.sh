# Pinned versions of what the packages bundle (packaging/README.md). Sourced by the
# build scripts and the workflows; bump a version together with its SHA-256.
QT_VERSION=6.11.2
FFMPEG_VERSION=9.0.2
FFMPEG_SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
QTKEYCHAIN_VERSION=0.17.0
QTKEYCHAIN_SHA256=3b85c3929034b0a99da777130c34d99f006fcd3a9d56564159399a33fee0e504
QTKEYCHAIN_URL="https://github.com/frankosterfeld/qtkeychain/archive/refs/tags/$QTKEYCHAIN_VERSION.tar.gz"

# fetch <url> <sha256> <file>: download and check.
fetch() {
    curl -fsSL --retry 3 -o "$3" "$1"
    echo "$2  $3" | sha256sum -c --quiet -
}

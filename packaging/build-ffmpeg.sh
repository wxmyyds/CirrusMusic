#!/usr/bin/env bash
# Builds the FFmpeg that macOS packages bundle (packaging/README.md): shared, LGPL
# (no --enable-gpl, no external libraries) and only what the audio decoder opens: the MP3 and
# FLAC demuxers/decoders, plus the MP4 (mov) demuxer with its AAC decoder (the
# Bilibili CDN's audio/mp4). See src/infrastructure/media/audio_decoder.cpp.
# Linux uses distribution FFmpeg; Windows uses the pinned vcpkg port.
#
#   packaging/build-ffmpeg.sh <prefix>
#
# Needs a C compiler and make. Libraries keep absolute install names, which
# macdeployqt follows and rewrites into the app bundle. Versions/checksums: versions.sh.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true ]]; then
    printf '%s\n' '该操作需要通过 GitHub Actions 执行。' >&2
    exit 1
fi
[[ $(uname -s) == Darwin ]] || {
    printf '%s\n' 'FFmpeg source builds are macOS-only; Linux requires distribution libraries.' >&2
    exit 1
}

source "$(dirname "$0")/versions.sh"
VERSION=$FFMPEG_VERSION

prefix=${1:?usage: build-ffmpeg.sh <prefix>}
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fetch "$FFMPEG_URL" "$FFMPEG_SHA256" "$work/ffmpeg.tar.xz"
tar -xf "$work/ffmpeg.tar.xz" -C "$work"
cd "$work/ffmpeg-$VERSION"

./configure \
    --prefix="$prefix" \
    --libdir="$prefix/lib" \
    --enable-shared --disable-static --enable-pic \
    --disable-programs --disable-doc --disable-debug \
    --disable-autodetect --disable-network \
    --disable-avdevice --disable-avfilter --disable-swscale \
    --disable-everything \
    --enable-demuxer=mp3,flac,mov \
    --enable-decoder=mp3,mp3float,flac,aac \
    --enable-parser=mpegaudio,flac,aac
make -j"${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
make install

mkdir -p "$prefix/share/licenses/ffmpeg"
cp COPYING.LGPLv2.1 LICENSE.md "$prefix/share/licenses/ffmpeg/"
echo "FFmpeg $VERSION installed in $prefix"

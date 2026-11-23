#!/usr/bin/env bash
# Builds the FFmpeg that the Linux and macOS packages bundle (packaging/README.md): shared, LGPL
# (no --enable-gpl, no external libraries) and only what the audio decoder opens: the MP3 and
# FLAC demuxers/decoders, plus the MP4 (mov) demuxer with its AAC and E-AC-3 decoders (the
# Bilibili CDN's audio/mp4). See src/infrastructure/media/audio_decoder.cpp.
# Its sonames (libavcodec.so.63 …) differ from the FFmpeg 7.1 that Qt Multimedia ships
# (libavcodec.so.61), so both live in the bundle's lib/.
#
#   packaging/deps/build-ffmpeg.sh <prefix>
#
# Needs a C compiler and make; on Linux also nasm and patchelf. On macOS the libraries keep
# their absolute install names, which macdeployqt follows and rewrites when it copies them
# into the app bundle. Versions and checksums: versions.sh.
set -euo pipefail

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
    --enable-decoder=mp3,mp3float,flac,aac,eac3 \
    --enable-parser=mpegaudio,flac,aac,ac3
make -j"${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
make install

if [[ "$(uname -s)" == Linux ]]; then
    # The libraries find each other in their own directory (lib/ of the bundle).
    for lib in "$prefix"/lib/lib{avformat,avcodec,avutil,swresample}.so.*.*.*; do
        patchelf --set-rpath '$ORIGIN' "$lib"
    done
fi
mkdir -p "$prefix/share/licenses/ffmpeg"
cp COPYING.LGPLv2.1 LICENSE.md "$prefix/share/licenses/ffmpeg/"
echo "FFmpeg $VERSION installed in $prefix"

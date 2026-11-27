#!/usr/bin/env bash
# Builds QtKeychain against the bundled Qt for Windows/macOS packages. Linux packages
# use distribution QtKeychain and this script rejects Linux before downloading anything.
#
#   packaging/build-qtkeychain.sh <prefix> <qt prefix>
#
# Windows runs this in Git Bash inside an MSVC developer environment and builds a static
# library; on macOS the library has an absolute install name for macdeployqt. Versions and
# checksums: versions.sh.
set -euo pipefail

source "$(dirname "$0")/versions.sh"
VERSION=$QTKEYCHAIN_VERSION

prefix=${1:?usage: build-qtkeychain.sh <prefix> <qt prefix>}
qt=${2:?usage: build-qtkeychain.sh <prefix> <qt prefix>}
host=$(uname -s)
shared=ON
extra=()
case "$host" in
    MINGW* | MSYS* | CYGWIN*) shared=OFF ;;
    # Not @rpath: the app's install rpath does not name the deps prefix.
    Darwin) extra=(-DCMAKE_INSTALL_NAME_DIR="$prefix/lib") ;;
    Linux*)
        printf '%s\n' 'Linux packages use distribution QtKeychain; refusing to build a private copy.' >&2
        exit 2
        ;;
    *)
        printf 'Unsupported QtKeychain build host: %s\n' "$host" >&2
        exit 2
        ;;
esac

mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fetch "$QTKEYCHAIN_URL" "$QTKEYCHAIN_SHA256" "$work/qtkeychain.tar.gz"
tar -xzf "$work/qtkeychain.tar.gz" -C "$work"

# Windows: static. windeployqt takes any qt6*.dll for a Qt module and fails on
# qt6keychain.dll; linked into Cirrus.exe there is no such DLL (BSD-3-Clause allows it).
cmake -S "$work/qtkeychain-$VERSION" -B "$work/build" -G Ninja \
    -DBUILD_SHARED_LIBS=$shared \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_WITH_QT6=ON \
    -DBUILD_TRANSLATIONS=OFF \
    -DBUILD_TEST_APPLICATION=OFF \
    ${extra[@]+"${extra[@]}"}
cmake --build "$work/build" --parallel "${JOBS:-4}"
cmake --install "$work/build"

mkdir -p "$prefix/share/licenses/qtkeychain"
cp "$work/qtkeychain-$VERSION/COPYING" "$prefix/share/licenses/qtkeychain/"
echo "QtKeychain $VERSION installed in $prefix"

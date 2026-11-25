#!/usr/bin/env bash
# Builds the macOS package (packaging/README.md): Cirrus.app with its own Qt and libraries,
# ad-hoc signed, in a dmg next to a link to /Applications.
#
#   packaging/macos/build-package.sh <arm64|x86_64> <qt prefix> <deps prefix> <out dir>
#
# <deps prefix> holds FFmpeg and QtKeychain from packaging/; OpenSSL and zstd come
# from Homebrew (openssl@3, zstd), so the package needs the macOS version Homebrew built them
# for (macOS 15.0). Each package is built and checked on its matching native CPU.
set -euo pipefail

if (( $# != 4 )); then
    echo "usage: build-package.sh <arm64|x86_64> <qt prefix> <deps prefix> <out dir>" >&2
    exit 1
fi
arch=$1
case "$arch" in
    arm64 | x86_64) ;;
    *) echo "::error::Unsupported macOS architecture: $arch"; exit 1 ;;
esac
[[ "$(uname -s)" == Darwin && "$(uname -m)" == "$arch" ]] ||
    { echo "::error::A native $arch macOS runner is required"; exit 1; }
[[ "$(sysctl -in sysctl.proc_translated)" != 1 ]] ||
    { echo "::error::Rosetta builds are not supported"; exit 1; }
[[ "$(sw_vers -productVersion)" == 15.* ]] ||
    { echo "::error::The macOS package requires a macOS 15 runner"; exit 1; }
export MACOSX_DEPLOYMENT_TARGET=15.0

root=$(cd "$(dirname "$0")/../.." && pwd)
qt=$(cd "$2" && pwd)
deps=$(cd "$3" && pwd)
mkdir -p "$4"
out=$(cd "$4" && pwd)

version=$(sed -nE 's/^project\(CirrusMusic VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
build=${CIRRUS_PACKAGE_BUILD_DIR:-$root/build/package-macos-$arch}
stage=$build/stage
app=$stage/Cirrus.app
openssl=$(brew --prefix openssl@3)
zstd=$(brew --prefix zstd)
pkg_config="$(brew --prefix pkgconf)/bin/pkg-config"

PKG_CONFIG_PATH="$deps/lib/pkgconfig" cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt;$deps;$openssl;$zstd" \
    -DPKG_CONFIG_EXECUTABLE="$pkg_config" \
    -DOPENSSL_ROOT_DIR="$openssl" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DBUILD_TESTING=OFF \
    -DCIRRUS_BUILD_GALLERY=OFF \
    -DCIRRUS_EXECUTABLE=Cirrus \
    -DCIRRUS_PACKAGE=macos \
    -DCIRRUS_DEPLOY_QT=ON
cmake --build "$build" --parallel "${JOBS:-4}"

rm -rf "$stage"
cmake --install "$build" --prefix "$stage"

# Only SQLite: the other drivers need client libraries nobody has (macdeployqt deploys every
# driver Qt ships and ignores the deploy script's plugin lists).
find "$app/Contents/PlugIns/sqldrivers" -type f ! -name 'libqsqlite.dylib' -delete

# The executable is single-architecture; every bundled Mach-O (including Qt frameworks
# and plugins) must contain that slice. Universal dependencies may retain their other slice.
target="$app/Contents/MacOS/Cirrus"
target_archs=$(lipo -archs "$target")
[[ "$target_archs" == "$arch" ]] ||
    { echo "::error::Cirrus targets $target_archs, expected only $arch"; exit 1; }

# Every library must come from the bundle or the system. A path into the build machine's
# Qt, deps or Homebrew would make the app fail to start anywhere else. Inspect only the
# selected slice, and parse indented dependency lines, not universal-binary section headers.
bad=0
while IFS= read -r -d '' f; do
    description=$(file -b "$f")
    [[ "$description" == *Mach-O* ]] || continue
    if ! lipo "$f" -verify_arch "$arch"; then
        echo "::error::$f does not contain $arch"
        bad=1
        continue
    fi
    dependencies=$(otool -arch "$arch" -L "$f")
    while IFS= read -r dep; do
        case "$dep" in
            /System/* | /usr/lib/* | @executable_path/* | @loader_path/* | @rpath/*) ;;
            *) echo "::error::$(basename "$f") links $dep"; bad=1 ;;
        esac
    done < <(printf '%s\n' "$dependencies" | awk '
        /^[[:space:]]/ {
            sub(/^[[:space:]]+/, "")
            sub(/ \(compatibility version.*$/, "")
            print
        }')
done < <(find "$app/Contents" -type f -print0)
(( bad == 0 ))
for lib in libavcodec.63 libavformat.63 libqt6keychain libcrypto.3 libzstd.1; do
    ls "$app/Contents/Frameworks/$lib"* > /dev/null 2>&1 ||
        { echo "::error::$lib is not in the bundle"; exit 1; }
done

# Ad-hoc signature: Apple Silicon runs only signed code, and macdeployqt's install_name_tool
# changes invalidated the signatures the linker made. Not notarized (no developer ID):
# Gatekeeper asks the user once (README).
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"
# For the log: what the bundle says about itself, its architecture and oldest macOS, its size.
plutil -p "$app/Contents/Info.plist"
lipo -archs "$target"
otool -arch "$arch" -l "$target" | grep -A3 LC_BUILD_VERSION | grep -E 'minos|sdk'
du -sh "$app"

dmgroot=$build/dmg
rm -rf "$dmgroot"
mkdir -p "$dmgroot"
cp -R "$app" "$dmgroot/"
ln -s /Applications "$dmgroot/Applications"
dmg="$out/Cirrus-$version-macos-$arch.dmg"
rm -f "$dmg"
hdiutil create -volname "Cirrus $version" -srcfolder "$dmgroot" -fs HFS+ -format UDZO "$dmg"
ls -l "$dmg"
echo "Built $dmg"

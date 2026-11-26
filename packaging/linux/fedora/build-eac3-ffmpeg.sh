#!/usr/bin/env bash
# Unmodified upstream sources; the Cirrus application patch is a separate build input.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo "该操作需要通过 GitHub Actions 执行。" >&2; exit 1; }
source /etc/os-release
[[ $ID == fedora && $VERSION_ID == 44 ]]
case $(uname -m) in x86_64|aarch64) ;; *) exit 1 ;; esac
source "$(dirname "$0")/../../versions.sh"
prefix=${1:?missing EAC3 prefix}
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
[[ ! -e $prefix/complete ]]
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fetch "$FFMPEG_URL" "$FFMPEG_SHA256" "$work/ffmpeg.tar.xz"
tar -xf "$work/ffmpeg.tar.xz" -C "$work"
cd "$work/ffmpeg-$FFMPEG_VERSION"
./configure --prefix="$prefix" --libdir="$prefix/lib" \
    --disable-all --disable-autodetect \
    --enable-avcodec --enable-swresample --enable-decoder=eac3 \
    --enable-static --disable-shared --enable-pic \
    --disable-debug --disable-doc --disable-programs --disable-network
# EAC3 selects AC3 and its parser upstream. No other decoder belongs in this build.
python3 - <<'PY'
import pathlib, re
enabled = set(re.findall(r"^#define (CONFIG_\w+_DECODER) 1$", pathlib.Path("config_components.h").read_text(), re.M))
assert enabled == {"CONFIG_AC3_DECODER", "CONFIG_EAC3_DECODER"}, enabled
print("Fedora EAC3 decoder dependency closure:", sorted(enabled))
PY
make -j"${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
make install
mkdir -p "$prefix/share/licenses/ffmpeg" "$prefix/share/cirrus-eac3"
cp COPYING.LGPLv2.1 LICENSE.md "$prefix/share/licenses/ffmpeg/"
cp config_components.h "$prefix/share/cirrus-eac3/"
printf '%s %s %s\n' "$FFMPEG_VERSION" "$FFMPEG_SHA256" "$(uname -m)" > "$prefix/complete"

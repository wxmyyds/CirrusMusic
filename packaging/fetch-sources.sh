#!/usr/bin/env bash
# Downloads the exact sources of the LGPL/BSD libraries the packages bundle, for the
# release's source offer (packaging/THIRD-PARTY-NOTICES.md).
#
#   packaging/fetch-sources.sh <dir>
set -euo pipefail
source "$(dirname "$0")/versions.sh"
dir=${1:?usage: fetch-sources.sh <dir>}
mkdir -p "$dir"
fetch "$FFMPEG_URL" "$FFMPEG_SHA256" "$dir/ffmpeg-$FFMPEG_VERSION.tar.xz"
fetch "$QTKEYCHAIN_URL" "$QTKEYCHAIN_SHA256" "$dir/qtkeychain-$QTKEYCHAIN_VERSION.tar.gz"

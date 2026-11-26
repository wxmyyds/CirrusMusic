#!/usr/bin/env bash
set -euo pipefail
library=${1:?missing Fedora EAC3 shared library}
[[ -f $library ]]
needed=$(LC_ALL=C readelf -dW "$library")
if grep -Eq 'NEEDED.*lib(av[^]]*|swresample)[.]so|TEXTREL' <<< "$needed"; then
    echo "Fedora EAC3 backend must be self-contained and position independent" >&2; exit 1
fi
symbols=$(LC_ALL=C readelf --dyn-syms -W "$library")
if awk '$4 == "FUNC" || $7 == "UND" {print $8}' <<< "$symbols" | grep -Eq '^(av_|avcodec_|avformat_|avutil_|avpriv_|swr_|ff_)'; then
    echo "FFmpeg dynamic symbol escaped the Fedora EAC3 boundary" >&2; exit 1
fi
exports=$(nm -D --defined-only --format=posix "$library" | awk '{print $1}')
while IFS= read -r symbol; do
    case "$symbol" in cirrus_eac3_*|CIRRUS_EAC3_1) ;; *) echo "Unexpected EAC3 export: $symbol" >&2; exit 1 ;; esac
done <<< "$exports"
for symbol in create destroy sample_rate send receive reset; do
    grep -E "^cirrus_eac3_${symbol}(@@CIRRUS_EAC3_1)?$" <<< "$exports" >/dev/null
done
printf 'Verified isolated Fedora EAC3 backend: %s\n%s\n' "$library" "$exports"

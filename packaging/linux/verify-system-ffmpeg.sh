#!/usr/bin/env bash
# Verify the official native FFmpeg provider and real audio fixtures in Actions.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true ]]; then
    printf '%s\n' '该操作需要通过 GitHub Actions 执行。' >&2
    exit 1
fi
[[ $(uname -s) == Linux ]]
root=$(cd "$(dirname "$0")/../.." && pwd)
source /etc/os-release
# Ignore the Qt SDK and private dependency prefixes for this system-library audit.
unset PKG_CONFIG_PATH PKG_CONFIG_LIBDIR LD_LIBRARY_PATH
export LC_ALL=C

for component in avformat avcodec avutil swresample; do
    module="lib$component"
    directory=$(pkg-config --variable=libdir "$module")
    library=$(readlink -e "$directory/$module.so")
    metadata=$(readlink -e "$(pkg-config --variable=pcfiledir "$module")/$module.pc")
    for path in "$library" "$metadata"; do
        case "$path" in
            /lib/* | /lib64/* | /usr/lib/* | /usr/lib64/* | /usr/share/pkgconfig/*) ;;
            *) printf 'Non-system FFmpeg development file: %s\n' "$path" >&2; exit 1 ;;
        esac
        case "$ID" in
            ubuntu | debian) dpkg-query --search "$path" ;;
            fedora) rpm --query --file "$path" ;;
            arch | archarm) pacman --query --owns "$path" ;;
            *) printf 'Unsupported FFmpeg distribution: %s\n' "$ID" >&2; exit 1 ;;
        esac
    done
    printf 'System FFmpeg: %s %s => %s\n' "$module" "$(pkg-config --modversion "$module")" "$library"
done

# A decoder-list check alone would miss container/codec integration failures.
# Use the distribution's CLI and actual MP3, FLAC, MP4 and fragmented MP4 fixtures.
ffmpeg=$(readlink -e "$(command -v ffmpeg)")
[[ $ffmpeg == /usr/bin/ffmpeg ]]
"$ffmpeg" -version
decode() {
    "$ffmpeg" -hide_banner -loglevel error -xerror -nostdin \
        -i "$root/tests/fixtures/audio/$1" -map 0:a:0 -f null -
}
for fixture in tone.mp3 tone.flac tone_aac.m4a tone_fmp4.m4a tone_flac.m4a; do
    decode "$fixture"
    printf 'System FFmpeg fixture decoded: %s\n' "$fixture"
done

decoders=$("$ffmpeg" -hide_banner -decoders)
if awk '$2 == "eac3" { found=1 } END { exit !found }' <<< "$decoders"; then
    decode tone_eac3.m4a
    printf '%s\n' 'System FFmpeg fixture decoded: tone_eac3.m4a'
else
    # Official Fedora FFmpeg deliberately omits optional Dolby/E-AC-3 support.
    # Verify rejection of the same fixture rather than skipping its test.
    [[ $ID == fedora ]] || { printf '%s\n' 'Unexpected missing E-AC-3 decoder' >&2; exit 1; }
    log=$(mktemp)
    trap 'rm -f "$log"' EXIT
    if decode tone_eac3.m4a >"$log" 2>&1; then
        printf '%s\n' 'E-AC-3 decoded despite an absent decoder registration' >&2
        exit 1
    fi
    cat "$log"
    grep -qi 'eac3' "$log"
    grep -qi 'decoder' "$log"
    printf '%s\n' 'Official Fedora FFmpeg: optional E-AC-3 rejection verified'
fi

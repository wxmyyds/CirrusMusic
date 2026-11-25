#!/usr/bin/env bash
# Shared native Debian/Fedora build and ELF validation; sourced by distro entrypoints.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true ]]; then
    printf '%s\n' '该操作需要通过 GitHub Actions 执行。' >&2
    exit 1
fi
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
: "${distribution:?distribution entrypoint is required}"
arch=${1:?missing architecture}
qt=${2:?missing Qt prefix}
deps=$(cd "${3:?missing dependency prefix}" && pwd)
mkdir -p "${4:?missing output directory}"
out=$(cd "$4" && pwd)
source /etc/os-release
[[ $ID == "$distribution" ]]
case "$distribution" in
debian) [[ $VERSION_ID == 13 ]] ;;
fedora) [[ $VERSION_ID == 44 ]] ;;
*)
    printf 'Unsupported native package distribution: %s\n' "$distribution" >&2
    exit 2
    ;;
esac
case "$arch" in
x86_64)
    native=x86_64
    deb_arch=amd64
    machine='Advanced Micro Devices X86-64'
    ;;
arm64)
    native=aarch64
    deb_arch=arm64
    machine=AArch64
    ;;
*)
    printf 'Unsupported package architecture: %s\n' "$arch" >&2
    exit 2
    ;;
esac
[[ $(uname -m) == "$native" ]]
version=$(sed -nE 's/^project\(CirrusMusic VERSION ([0-9]+\.[0-9]+\.[0-9]+)([[:space:]]|\)).*/\1/p' "$root/CMakeLists.txt")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
build="$root/build/package-$distribution-$arch"
stage="$build/stage"

PKG_CONFIG_PATH="$deps/lib/pkgconfig" cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt;$deps" \
    -DPKG_CONFIG_EXECUTABLE="${CIRRUS_PKG_CONFIG_EXECUTABLE:?missing pkg-config}" \
    -DOPENSSL_ROOT_DIR="${CIRRUS_OPENSSL_ROOT:?missing OpenSSL root}" \
    -DBUILD_TESTING=OFF \
    -DCIRRUS_BUILD_GALLERY=OFF \
    -DCIRRUS_EXECUTABLE=cirrus \
    -DCIRRUS_PACKAGE=linux \
    -DCIRRUS_DEPLOY_QT=ON
cmake --build "$build" --parallel "${JOBS:-4}"
rm -rf "$stage"
DESTDIR="$stage" cmake --install "$build" --prefix /opt/cirrus

mkdir -p "$stage/usr/bin" "$stage/usr/share"
for directory in applications metainfo icons doc; do
    mv "$stage/opt/cirrus/share/$directory" "$stage/usr/share/"
done
ln -s /opt/cirrus/bin/cirrus "$stage/usr/bin/cirrus"
find "$stage/opt/cirrus/share" -depth -type d -empty -delete

desktop-file-validate "$stage/usr/share/applications/com.guang233.Cirrus.desktop"
appstreamcli validate --no-net "$stage/usr/share/metainfo/com.guang233.Cirrus.metainfo.xml"

# Inspect the deployed tree, never the SDK/dependency directories. A bundle that
# accidentally loads an SDK library must fail before dependency generation.
mapfile -t library_dirs < <(find "$stage/opt/cirrus" -name '*.so*' -printf '%h\n' | sort -u)
library_path=$(
    IFS=:
    printf '%s' "${library_dirs[*]}"
)
elves=()
while IFS= read -r -d '' path; do
    description=$(LC_ALL=C file -b "$path")
    [[ $description == ELF* ]] || continue
    actual=$(LC_ALL=C readelf -h "$path" | awk -F ': *' '/Machine:/ {print $2}')
    [[ $actual == "$machine" ]] || {
        printf 'Wrong ELF architecture: %s (%s)\n' "$path" "$actual" >&2
        exit 1
    }
    elves+=("$path")
done < <(find "$stage/opt/cirrus" -type f -print0)
((${#elves[@]} > 0))

# Resolve the target distribution's bzip2 ABI through its native compiler.
# Qt's x86_64 and ARM64 SDK FFmpeg builds use different bzip2 SONAMEs.
# Rewrite their imports to the native SONAME before inspecting the entire tree.
bzip_provider=$("$CC" -print-file-name=libbz2.so)
[[ $bzip_provider == /* && -f $bzip_provider ]]
bzip_provider=$(readlink -e "$bzip_provider")
case "$bzip_provider" in
    /lib/* | /lib64/* | /usr/lib/* | /usr/lib64/*) ;;
    *) printf 'Non-system bzip2 provider: %s\n' "$bzip_provider" >&2; exit 1 ;;
esac
[[ $(LC_ALL=C readelf -h "$bzip_provider" | awk -F ': *' '/Machine:/ {print $2}') == "$machine" ]]
bzip_soname=$(LC_ALL=C readelf -d "$bzip_provider" | awk -F '[][]' '/\(SONAME\)/ {print $2}')
case "$bzip_soname" in
    libbz2.so.1 | libbz2.so.1.0) ;;
    *) printf 'Unsupported native bzip2 SONAME: %s\n' "$bzip_soname" >&2; exit 1 ;;
esac
bzip_exports=$(LC_ALL=C readelf --dyn-syms --wide "$bzip_provider" |
    awk '$7 != "UND" && $6 == "DEFAULT" && $8 ~ /^BZ2_/ {print $8}' | sort -u)
[[ -n $bzip_exports ]]
for path in "${elves[@]}"; do
    mapfile -t bzip_imports < <(LC_ALL=C readelf -d "$path" |
        awk -F '[][]' '/\(NEEDED\)/ && $2 ~ /^libbz2\.so/ {print $2}')
    for needed in "${bzip_imports[@]}"; do
        case "$needed" in
            libbz2.so.1 | libbz2.so.1.0) ;;
            *) printf 'Unsupported bzip2 import: %s (%s)\n' "$needed" "$path" >&2; exit 1 ;;
        esac
        [[ $needed != "$bzip_soname" ]] || continue
        mapfile -t bzip_symbols < <(LC_ALL=C readelf --dyn-syms --wide "$path" |
            awk '$7 == "UND" && $8 ~ /^BZ2_/ {print $8}')
        for symbol in "${bzip_symbols[@]}"; do
            grep -Fx -- "$symbol" <<< "$bzip_exports" >/dev/null || {
                printf 'Native bzip2 does not export %s required by %s\n' "$symbol" "$path" >&2
                exit 1
            }
        done
        printf 'Native bzip2 import: %s: %s -> %s\n' "$path" "$needed" "$bzip_soname"
        patchelf --replace-needed "$needed" "$bzip_soname" "$path"
    done
done

for path in "${elves[@]}"; do
    description=$(LC_ALL=C file -b "$path")
    while IFS= read -r runpath; do
        [[ -n $runpath ]] || { printf 'Empty runtime search path: %s\n' "$path" >&2; exit 1; }
        IFS=: read -r -a runtime_paths <<< "$runpath"
        for entry in "${runtime_paths[@]}"; do
            case "$entry" in
                '$ORIGIN' | '$ORIGIN/'* | '${ORIGIN}' | '${ORIGIN}/'* | /opt/cirrus/lib | /opt/cirrus/lib/* | /lib | /lib/* | /lib64 | /lib64/* | /usr/lib | /usr/lib/* | /usr/lib64 | /usr/lib64/*) ;;
                *) printf 'Build-machine runtime search path in %s: %s\n' "$path" "$entry" >&2; exit 1 ;;
            esac
        done
    done < <(LC_ALL=C readelf -d "$path" | awk -F '[][]' '/\((RPATH|RUNPATH)\)/ {print $2}')
    if [[ $description == *'not stripped'* ]]; then
        strip --strip-unneeded "$path"
    fi
    dependencies=$(LD_LIBRARY_PATH="$library_path" ldd "$path")
    if grep -q 'not found' <<<"$dependencies"; then
        printf 'Unresolved dependencies: %s\n%s\n' "$path" "$dependencies" >&2
        exit 1
    fi
    resolved_bzip=$(awk -v name="$bzip_soname" '$1 == name && $2 == "=>" {print $3}' <<< "$dependencies")
    if [[ -n $resolved_bzip ]]; then
        [[ $(readlink -e "$resolved_bzip") == "$bzip_provider" ]] || {
            printf 'Non-native bzip2 resolved by %s: %s\n' "$path" "$resolved_bzip" >&2
            exit 1
        }
    fi
    while IFS= read -r dependency; do
        case "$dependency" in
        "$stage"/* | /lib/* | /lib64/* | /usr/lib/* | /usr/lib64/*) ;;
        *)
            printf 'Build-machine dependency leaked into %s: %s\n' "$path" "$dependency" >&2
            exit 1
            ;;
        esac
    done < <(awk '$2 == "=>" && $3 ~ /^\// {print $3} $1 ~ /^\// {print $1}' <<<"$dependencies")
done


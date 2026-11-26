#!/usr/bin/env bash
# Native fedora package entrypoint; shared compilation stays in ../build-common.sh.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo '该操作需要通过 GitHub Actions 执行。' >&2; exit 1; }
source /etc/os-release
[[ $ID == fedora && $VERSION_ID == 44 ]]
original=$(cd "$(dirname "$0")/../../.." && pwd)
case "${1:?missing architecture}" in x86_64|arm64) ;; *) exit 1 ;; esac
# The same patch preparation is used by native CI and production packaging.
patched=$("$original/packaging/linux/prepare-source.sh" fedora "$1")
distribution=fedora
source "$patched/packaging/linux/build-common.sh"

# Exercise the actual patched packet-to-PCM path on each native Fedora architecture.
# These are mandatory positive EAC3 tests, including seek/reset and backend identity;
# ordinary AAC/FLAC must still use the system decoder.
codec_tests="$build/codec-tests"
cmake -S "$root" -B "$codec_tests" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr \
    -DPKG_CONFIG_EXECUTABLE="$CIRRUS_PKG_CONFIG_EXECUTABLE" \
    -DOPENSSL_ROOT_DIR="$CIRRUS_OPENSSL_ROOT" \
    -DBUILD_TESTING=ON -DCIRRUS_BUILD_GALLERY=OFF -DCIRRUS_DEPLOY_QT=OFF
cmake --build "$codec_tests" --target tst_audio_decoder --parallel "${JOBS:-4}"
QT_QPA_PLATFORM=offscreen \
    ctest --test-dir "$codec_tests" --no-tests=error --verbose -R '^audio_decoder$' \
    --output-junit "$build/fedora-codec-tests.xml"
mapfile -t eac3_backends < <(find "$stage/opt/cirrus" -type f -name 'libcirrus_eac3.so.*')
((${#eac3_backends[@]} == 1))
"$root/packaging/linux/fedora/verify-eac3-backend.sh" "${eac3_backends[0]}"

make_rpm() {
    local top="$build/rpmbuild" spec="$build/cirrus.spec" sonames="$build/private-sonames.txt"
    mkdir -p "$top"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
    for path in "${elves[@]}"; do
        readelf -d "$path" | awk '/\(SONAME\)/ {sub(/^.*\[/, ""); sub(/\].*$/, ""); print}'
    done | sort -u >"$sonames"
    # Every shared library in this application is private. Do not advertise its
    # SONAME globally, or require a system copy of a library shipped in the bundle.
    # Keep native RPM automatic requirements for every external library and ABI.
    local private_requires
    private_requires=$(
        python3 - "$sonames" <<'PY'
import pathlib, re, sys
names = pathlib.Path(sys.argv[1]).read_text().splitlines()
if not names or any(not re.fullmatch(r"[A-Za-z0-9_.+-]+", name) for name in names):
    raise SystemExit("Invalid private ELF SONAME list")
escaped = [name.replace(".", "[.]").replace("+", "[+]") for name in names]
print("^(" + "|".join(escaped) + ")([(].*[)])?$")
PY
    )
    local qt_requires
    printf -v qt_requires ', %s' "${qt_runtime_packages[@]}"
    qt_requires=${qt_requires:2}
    cat >"$spec" <<EOF
%global debug_package %{nil}
%global _build_id_links none
%global __requires_exclude $private_requires
Name: cirrus
Version: $version
Release: 1%{?dist}
Summary: Desktop NetEase Cloud Music client with Bilibili audio support
License: MIT
URL: https://github.com/wxmyyds/CirrusMusic
BuildArch: $native
AutoProv: no
Requires: ca-certificates, libsecret, hicolor-icon-theme
Requires: $qt_requires
Recommends: gnome-keyring

%description
Qt, QtKeychain and ordinary FFmpeg decoding use official Fedora system libraries.
ICU is managed by Fedora's Qt dependencies.
E-AC-3 uses an isolated, private, minimal FFmpeg backend.
It is not affiliated with NetEase or Bilibili.

%install
mkdir -p %{buildroot}
cp -a "$stage/." %{buildroot}/

%files
%defattr(-,root,root,-)
/opt/cirrus
/usr/bin/cirrus
/usr/share/applications/com.guang233.Cirrus.desktop
/usr/share/metainfo/com.guang233.Cirrus.metainfo.xml
/usr/share/icons/hicolor
%dir /usr/share/doc/cirrus
%license /usr/share/doc/cirrus/LICENSE
%doc /usr/share/doc/cirrus/THIRD-PARTY-NOTICES.md
%license /usr/share/doc/cirrus/licenses
EOF
    rpmbuild -bb --define "_topdir $top" "$spec"
    mapfile -t packages < <(find "$top/RPMS" -name '*.rpm' -type f)
    ((${#packages[@]} == 1))
    local package=${packages[0]}
    [[ $(rpm -qp --qf '%{NAME}' "$package") == cirrus ]]
    [[ $(rpm -qp --qf '%{VERSION}' "$package") == "$version" ]]
    [[ $(rpm -qp --qf '%{ARCH}' "$package") == "$native" ]]
    rpm -qip "$package"
    rpm -qlp "$package"
    local requirements
    requirements=$(rpm -qp --requires "$package")
    printf '%s\n' "$requirements"
    for component in avformat avcodec avutil swresample; do
        grep -F "lib$component.so." <<< "$requirements" >/dev/null || {
            printf 'Missing RPM FFmpeg runtime dependency: lib%s\n' "$component" >&2
            exit 1
        }
    done
    cp "$package" "$out/"
}

make_rpm
ls -l "$out"

#!/usr/bin/env bash
# Native fedora package entrypoint; shared compilation stays in ../build-common.sh.
set -euo pipefail
distribution=fedora
source "$(dirname "$0")/../build-common.sh"

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
Recommends: gnome-keyring

%description
Cirrus carries private Qt and QtKeychain libraries in /opt/cirrus.
FFmpeg comes from official Fedora system packages; E-AC-3 is not required.
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

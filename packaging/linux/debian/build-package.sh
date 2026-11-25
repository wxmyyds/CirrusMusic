#!/usr/bin/env bash
# Native debian package entrypoint; shared compilation stays in ../build-common.sh.
set -euo pipefail
distribution=debian
source "$(dirname "$0")/../build-common.sh"

make_deb() {
    local temporary="$build/debian-metadata" depends
    mkdir -p "$temporary/debian" "$stage/DEBIAN"
    printf 'Source: cirrus\n\nPackage: cirrus\nArchitecture: any\n' >"$temporary/debian/control"
    local elf_arguments=() library_arguments=() path
    for path in "${elves[@]}"; do elf_arguments+=("-e$path"); done
    for path in "${library_dirs[@]}"; do library_arguments+=("-l$path"); done
    # Bundled libraries have no Debian shlibs records; all external libraries were
    # resolved above and their ABI requirements come from the native dpkg database.
    depends=$(cd "$temporary" && dpkg-shlibdeps -O --ignore-missing-info \
        "${library_arguments[@]}" "${elf_arguments[@]}" | sed -n 's/^shlibs:Depends=//p')
    [[ -n $depends ]]
    depends+=", ca-certificates, libsecret-1-0, hicolor-icon-theme"
    {
        cat "$root/LICENSE"
        printf '\n'
        cat "$root/packaging/THIRD-PARTY-NOTICES.md"
    } \
        >"$stage/usr/share/doc/cirrus/copyright"
    cat >"$stage/DEBIAN/control" <<EOF
Package: cirrus
Version: $version
Architecture: $deb_arch
Maintainer: guang233
Section: sound
Priority: optional
Installed-Size: $(du -sk --exclude=DEBIAN "$stage" | cut -f1)
Depends: $depends
Recommends: gnome-keyring | kwalletmanager | keepassxc
Description: Desktop NetEase Cloud Music client with Bilibili audio support
 Cirrus carries its private Qt and audio libraries in /opt/cirrus.
 It is not affiliated with NetEase or Bilibili.
EOF
    local package="$out/cirrus_${version}_$deb_arch.deb"
    dpkg-deb --build --root-owner-group -Zxz "$stage" "$package"
    [[ $(dpkg-deb --field "$package" Package) == cirrus ]]
    [[ $(dpkg-deb --field "$package" Version) == "$version" ]]
    [[ $(dpkg-deb --field "$package" Architecture) == "$deb_arch" ]]
    dpkg-deb --info "$package"
    dpkg-deb --contents "$package"
}

make_deb
ls -l "$out"

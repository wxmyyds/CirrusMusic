#!/usr/bin/env bash
# Native debian package entrypoint; shared compilation stays in ../build-common.sh.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo '该操作需要通过 GitHub Actions 执行。' >&2; exit 1; }
original=$(cd "$(dirname "$0")/../../.." && pwd)
patched=$("$original/packaging/linux/prepare-source.sh" debian "${1:?missing architecture}")
distribution=debian
source "$patched/packaging/linux/build-common.sh"

make_deb() {
    local temporary="$build/debian-metadata" depends
    mkdir -p "$temporary/debian" "$stage/DEBIAN"
    printf 'Source: cirrus\n\nPackage: cirrus\nArchitecture: any\n' >"$temporary/debian/control"
    local elf_arguments=() library_arguments=() path
    for path in "${elves[@]}"; do elf_arguments+=("-e$path"); done
    for path in "${library_dirs[@]}"; do library_arguments+=("-l$path"); done
    # Give only the shipped SONAMEs explicit package-local ownership. Exclude
    # that self-dependency; every external ABI must have native Debian metadata.
    for path in "${elves[@]}"; do
        soname=$(LC_ALL=C readelf -d "$path" | awk -F '[][]' '/\(SONAME\)/ {print $2}')
        if [[ $soname =~ ^(.+)\.so\.([0-9][0-9.]*)$ ]]; then
            printf '%s %s cirrus\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
        fi
    done | sort -u > "$temporary/debian/shlibs.local"
    # Use the same deployed-tree search path as ELF validation, not Qt installer's
    # inherited SDK path (whose FFmpeg can have the same SONAME as Debian's).
    depends=$(cd "$temporary" && LD_LIBRARY_PATH="$library_path" dpkg-shlibdeps -O -xcirrus \
        "${library_arguments[@]}" "${elf_arguments[@]}" | sed -n 's/^shlibs:Depends=//p')
    [[ -n $depends ]]
    for component in avformat avcodec avutil swresample; do
        grep -Eq "(^|, )lib${component}[0-9]+([[:space:](,]|$)" <<< "$depends" || {
            printf 'Missing Debian FFmpeg runtime dependency: lib%s\n' "$component" >&2
            exit 1
        }
    done
    for package in "${qt_runtime_packages[@]}"; do
        case ", $depends," in
            *", $package,"* | *", $package ("*) ;;
            *) depends+=", $package" ;;
        esac
    done
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
 Qt, QtKeychain and FFmpeg come from Debian system packages.
 ICU is managed by Debian's Qt dependencies.
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

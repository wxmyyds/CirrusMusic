#!/usr/bin/env bash
# Verify native Qt/QtKeychain ownership; no SDK or bundled ICU is used on Linux.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo '该操作需要通过 GitHub Actions 执行。' >&2; exit 1; }
source /etc/os-release
unset PKG_CONFIG_PATH PKG_CONFIG_LIBDIR LD_LIBRARY_PATH
export LC_ALL=C
owner() {
    local path=$1
    case "$path" in /usr/lib/*|/usr/lib64/*|/usr/share/*) ;; *) echo "Non-system Qt file: $path" >&2; exit 1 ;; esac
    case "$ID" in
        debian) dpkg-query --search "$path" ;;
        fedora) rpm --query --file "$path" ;;
        arch|archarm) pacman --query --owns "$path" ;;
        *) exit 2 ;;
    esac
}
for module in Core Gui Qml Quick QuickControls2 Multimedia Sql ShaderTools; do
    library=$(readlink -e "$(pkg-config --variable=libdir "Qt6$module")/libQt6$module.so")
    metadata=$(readlink -e "$(pkg-config --variable=pcfiledir "Qt6$module")/Qt6$module.pc")
    owner "$library"
    owner "$metadata"
    printf 'System Qt: %s %s => %s\n' "$module" "$(pkg-config --modversion "Qt6$module")" "$library"
done
keychain=$(readlink -e "$(pkg-config --variable=libdir Qt6Core)/libqt6keychain.so")
owner "$keychain"
printf 'System QtKeychain: %s\n' "$keychain"

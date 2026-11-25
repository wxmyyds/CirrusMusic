#!/usr/bin/env bash
# Native Linux build tools. 该操作需要通过 GitHub Actions 执行。
# Arch uses distribution Qt/FFmpeg; Debian/Fedora and the Ubuntu test runner use
# the pinned Qt SDK and packaging/ libraries.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true ]]; then
    printf '%s\n' '该操作需要通过 GitHub Actions 执行。' >&2
    exit 1
fi

distribution=${1:?usage: install-build-deps.sh <arch|debian|fedora|ubuntu>}
source /etc/os-release
printf 'Build distribution: %s (%s)\nNative architecture: %s\n' "$PRETTY_NAME" "$ID" "$(uname -m)"
privilege=()
if (( EUID != 0 )); then
    privilege=(sudo)
fi

case "$distribution" in
    ubuntu | debian)
        [[ $ID == "$distribution" ]]
        if [[ $distribution == ubuntu ]]; then
            [[ $VERSION_ID == 22.04 ]]
            compiler=(g++-12)
            runtime=(libgtk-3-0 libcups2)
            cc=gcc-12 cxx=g++-12
        else
            [[ $VERSION_ID == 13 ]]
            compiler=(g++)
            runtime=(libgtk-3-0t64 libcups2t64)
            cc=gcc cxx=g++
        fi
        "${privilege[@]}" apt-get update
        "${privilege[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            "${compiler[@]}" "${runtime[@]}" \
            make cmake ccache ninja-build nasm patchelf pkg-config file binutils dpkg-dev \
            git curl ca-certificates tar xz-utils gzip zstd python3 python3-venv python3-pip \
            libssl-dev zlib1g-dev libzstd-dev libbz2-dev libsecret-1-dev \
            libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libwayland-dev \
            libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
            libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
            libfontconfig1 libdbus-1-3 dbus libpulse0 libxrandr2 libgbm1 libsm6 libice6 \
            desktop-file-utils appstream hicolor-icon-theme passwd util-linux
        ;;
    fedora)
        [[ $ID == fedora && $VERSION_ID == 44 ]]
        "${privilege[@]}" dnf install -y \
            gcc gcc-c++ make cmake ccache ninja-build nasm patchelf pkgconf-pkg-config file binutils \
            git /usr/bin/curl ca-certificates tar xz gzip zstd python3 python3-pip \
            openssl-devel zlib-devel libzstd-devel bzip2-devel libsecret-devel \
            mesa-libGL-devel mesa-libEGL-devel mesa-libgbm libxkbcommon-devel libxkbcommon-x11 wayland-devel \
            libxcb xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil \
            fontconfig dbus-libs dbus-daemon pulseaudio-libs gtk3 libXrandr libSM libICE cups-libs libxcrypt-compat \
            desktop-file-utils appstream hicolor-icon-theme rpm-build redhat-rpm-config \
            shadow-utils util-linux findutils diffutils which
        cc=gcc cxx=g++
        ;;
    arch)
        [[ $ID == arch || $ID == archarm ]]
        "${privilege[@]}" pacman -Syu --noconfirm --needed \
            base-devel cmake ninja ccache pkgconf git \
            qt6-base qt6-declarative qt6-multimedia qt6-multimedia-ffmpeg qt6-wayland \
            qt6-imageformats qt6-shadertools qt6-tools qtkeychain-qt6 \
            ffmpeg openssl zlib zstd libsecret hicolor-icon-theme ca-certificates \
            desktop-file-utils appstream
        cc=gcc cxx=g++
        ;;
    *)
        printf 'Unsupported build distribution: %s\n' "$distribution" >&2
        exit 2
        ;;
esac

printf 'CC=%s\nCXX=%s\n' "$cc" "$cxx" >> "${GITHUB_ENV:?GITHUB_ENV is required}"

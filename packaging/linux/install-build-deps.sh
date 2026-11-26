#!/usr/bin/env bash
# Native Linux build tools. 该操作需要通过 GitHub Actions 执行。
# All targets use their official distribution FFmpeg packages.
# Arch uses system Qt/QtKeychain; other targets use the SDK and built QtKeychain.
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
            [[ $VERSION_ID == 24.04 ]]
        else
            [[ $VERSION_ID == 13 ]]
        fi
        compiler=(g++)
        cc=gcc cxx=g++
        runtime=(libgtk-3-0t64 libcups2t64
            libgstreamer-plugins-base1.0-0 libgstreamer-plugins-bad1.0-0 libgstreamer-gl1.0-0)
        "${privilege[@]}" apt-get update
        "${privilege[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            "${compiler[@]}" "${runtime[@]}" \
            make cmake ccache ninja-build patchelf pkg-config file binutils dpkg-dev \
            git curl ca-certificates tar xz-utils gzip zstd python3 python3-venv python3-pip \
            libssl-dev zlib1g-dev libzstd-dev libsecret-1-dev \
            ffmpeg libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
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
            openssl-devel zlib-devel libzstd-devel libsecret-devel \
            ffmpeg-free libavformat-free-devel libavcodec-free-devel libavutil-free-devel libswresample-free-devel \
            mesa-libGL-devel mesa-libEGL-devel mesa-libgbm libxkbcommon-devel libxkbcommon-x11 wayland-devel \
            libxcb xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil \
            fontconfig dbus-libs dbus-daemon pulseaudio-libs gtk3 libXrandr libSM libICE cups-libs libxcrypt-compat \
            gstreamer1-plugins-bad-free-libs \
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

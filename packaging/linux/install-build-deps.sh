#!/usr/bin/env bash
# Native Linux build tools. 该操作需要通过 GitHub Actions 执行。
# All targets use their official distribution Qt, QtKeychain and FFmpeg packages.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true ]]; then
    printf '%s\n' '该操作需要通过 GitHub Actions 执行。' >&2
    exit 1
fi

distribution=${1:?usage: install-build-deps.sh <arch|debian|fedora>}
source /etc/os-release
printf 'Build distribution: %s (%s)\nNative architecture: %s\n' "$PRETTY_NAME" "$ID" "$(uname -m)"
privilege=()
if (( EUID != 0 )); then
    privilege=(sudo)
fi

case "$distribution" in
    debian)
        [[ $ID == debian && $VERSION_ID == 13 ]]
        cc=gcc cxx=g++
        "${privilege[@]}" apt-get update
        "${privilege[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            g++ \
            make cmake ccache ninja-build patchelf pkg-config file binutils dpkg-dev \
            git curl ca-certificates tar xz-utils gzip zstd python3 \
            libssl-dev zlib1g-dev libzstd-dev libsecret-1-dev \
            qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-private-dev \
            qt6-multimedia-dev qt6-shadertools-dev qt6-wayland-dev qt6-wayland-private-dev \
            qt6-image-formats-plugins qt6-wayland qt6-qpa-plugins \
            qtkeychain-qt6-dev libqt6sql6-sqlite qt6-svg-plugins pulseaudio \
            qml6-module-qtcore qml6-module-qtqml qml6-module-qtqml-models \
            qml6-module-qtqml-workerscript qml6-module-qtquick qml6-module-qtquick-controls \
            qml6-module-qtquick-dialogs qml6-module-qtquick-effects qml6-module-qtquick-layouts \
            qml6-module-qtquick-shapes qml6-module-qtquick-templates qml6-module-qtquick-window \
            qml6-module-qttest \
            ffmpeg libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
            libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libwayland-dev \
            dbus \
            desktop-file-utils appstream hicolor-icon-theme passwd util-linux
        ;;
    fedora)
        [[ $ID == fedora && $VERSION_ID == 44 ]]
        "${privilege[@]}" dnf install -y \
            gcc gcc-c++ make cmake ccache ninja-build nasm patchelf pkgconf-pkg-config file binutils \
            git /usr/bin/curl ca-certificates tar xz gzip zstd python3 \
            openssl-devel zlib-devel libzstd-devel libsecret-devel \
            qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel \
            qt6-qtmultimedia-devel qt6-qtshadertools-devel \
            qt6-qtimageformats qt6-qtsvg qtkeychain-qt6-devel \
            ffmpeg-free libavformat-free-devel libavcodec-free-devel libavutil-free-devel libswresample-free-devel \
            mesa-libGL-devel mesa-libEGL-devel libxkbcommon-devel wayland-devel dbus-daemon \
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

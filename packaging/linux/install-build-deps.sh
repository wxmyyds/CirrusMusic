#!/usr/bin/env bash
# Native Linux build tools. 该操作需要通过 GitHub Actions 执行。
# All targets use their official distribution Qt, QtKeychain and FFmpeg packages.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true ]]; then
    printf '%s\n' '该操作需要通过 GitHub Actions 执行。' >&2
    exit 1
fi

distribution=${1:?usage: install-build-deps.sh <arch|debian|fedora> <build|tests>}
purpose=${2:?missing build or tests purpose}
case "$purpose" in build|tests) ;; *) exit 2 ;; esac
test_packages=()
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
        if [[ $purpose == tests ]]; then
            test_packages=(pulseaudio dbus qt6-qpa-plugins qml6-module-qttest)
        fi
        "${privilege[@]}" apt-get update
        "${privilege[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            g++ \
            make cmake ccache ninja-build patchelf pkg-config file binutils dpkg-dev \
            git curl ca-certificates tar xz-utils gzip zstd python3 \
            libssl-dev zlib1g-dev libzstd-dev libsecret-1-dev \
            qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-private-dev \
            qt6-multimedia-dev qt6-shadertools-dev qt6-wayland-dev qt6-wayland-private-dev \
            qt6-image-formats-plugins qt6-wayland \
            qtkeychain-qt6-dev libqt6sql6-sqlite qt6-svg-plugins \
            qml6-module-qtcore qml6-module-qtqml qml6-module-qtqml-models \
            qml6-module-qtqml-workerscript qml6-module-qtquick qml6-module-qtquick-controls \
            qml6-module-qtquick-dialogs qml6-module-qtquick-effects qml6-module-qtquick-layouts \
            qml6-module-qtquick-shapes qml6-module-qtquick-templates qml6-module-qtquick-window \
            ffmpeg libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
            libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libwayland-dev \
            desktop-file-utils appstream hicolor-icon-theme passwd util-linux \
            "${test_packages[@]}"
        ;;
    fedora)
        [[ $ID == fedora && $VERSION_ID == 44 ]]
        if [[ $purpose == tests ]]; then
            # Select the test server in this transaction, before weak dependencies
            # choose the conflicting pipewire-pulseaudio provider.
            test_packages=(pulseaudio dbus-daemon)
        fi
        "${privilege[@]}" dnf install -y \
            gcc gcc-c++ make cmake ccache ninja-build nasm patchelf pkgconf-pkg-config file binutils \
            git /usr/bin/curl ca-certificates tar xz gzip zstd python3 \
            openssl-devel zlib-devel libzstd-devel libsecret-devel \
            qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel \
            qt6-qtmultimedia-devel qt6-qtshadertools-devel \
            qt6-qtimageformats qt6-qtsvg qtkeychain-qt6-devel \
            ffmpeg-free libavformat-free-devel libavcodec-free-devel libavutil-free-devel libswresample-free-devel \
            mesa-libGL-devel mesa-libEGL-devel libxkbcommon-devel wayland-devel \
            desktop-file-utils appstream hicolor-icon-theme rpm-build redhat-rpm-config \
            shadow-utils util-linux findutils diffutils which \
            "${test_packages[@]}"
        cc=gcc cxx=g++
        ;;
    arch)
        [[ $ID == arch || $ID == archarm ]]
        if [[ $purpose == tests ]]; then
            test_packages=(pulseaudio dbus)
        fi
        "${privilege[@]}" pacman -Syu --noconfirm --needed \
            base-devel cmake ninja ccache pkgconf git \
            qt6-base qt6-declarative qt6-multimedia qt6-multimedia-ffmpeg qt6-wayland \
            qt6-imageformats qt6-shadertools qt6-tools qtkeychain-qt6 \
            ffmpeg openssl zlib zstd libsecret hicolor-icon-theme ca-certificates \
            desktop-file-utils appstream \
            "${test_packages[@]}"
        cc=gcc cxx=g++
        ;;
    *)
        printf 'Unsupported build distribution: %s\n' "$distribution" >&2
        exit 2
        ;;
esac

printf 'CC=%s\nCXX=%s\n' "$cc" "$cxx" >> "${GITHUB_ENV:?GITHUB_ENV is required}"

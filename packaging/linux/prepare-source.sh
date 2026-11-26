#!/usr/bin/env bash
# Prepare detached native source; apply the required application-only distro patches.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo '该操作需要通过 GitHub Actions 执行。' >&2; exit 1; }
distribution=${1:?usage: prepare-source.sh <arch|debian|fedora> <x86_64|arm64>}
arch=${2:?missing architecture}
case "$distribution" in arch|debian|fedora) ;; *) exit 2 ;; esac
case "$arch" in x86_64|arm64) ;; *) exit 2 ;; esac
original=$(cd "$(dirname "$0")/../.." && pwd)
if [[ $distribution == debian ]]; then
    qt=$(pkg-config --modversion Qt6Core)
    # The newest replaced API is QAudioSink's callback interface (Qt 6.11).
    native_api_minimum=6.11
    if dpkg --compare-versions "$qt" ge "$native_api_minimum"; then
        printf 'Debian Qt %s supports the replaced Qt %s APIs: remove the Debian Qt patch and its build integration.\n' "$qt" "$native_api_minimum" >&2
        exit 1
    fi
    dpkg --compare-versions "$qt" ge 6.8 || { echo 'Debian patch requires Qt >= 6.8' >&2; exit 1; }
fi
patched="$original/build/$distribution-source-$arch"
[[ ! -e $patched ]]
mkdir -p "$original/build"
git -C "$original" worktree add --detach "$patched" HEAD >&2
shopt -s nullglob
patches=("$original/packaging/linux/$distribution/patches/"*.patch)
if [[ $distribution == arch ]]; then
    ((${#patches[@]} == 0))
else
    ((${#patches[@]} > 0))
fi
for patch in "${patches[@]}"; do
    sha256sum "$patch" >&2
    git -C "$patched" apply --check --whitespace=error-all "$patch"
    git -C "$patched" apply --whitespace=error-all "$patch"
done
git -C "$patched" diff --stat >&2
printf '%s\n' "$patched"

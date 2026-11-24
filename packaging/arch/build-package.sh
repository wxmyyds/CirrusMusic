#!/usr/bin/env bash
# CI-only native Arch packaging. 该操作需要通过 GitHub Actions 执行。
# The workflow installs dependencies first, then calls this script as a nonroot user.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true ]]; then
    printf '%s\n' '该操作需要通过 GitHub Actions 执行。' >&2
    exit 1
fi
if (( EUID == 0 )); then
    printf '%s\n' 'Refusing to run makepkg as root; use the workflow builder user.' >&2
    exit 1
fi

root=$(cd "$(dirname "$0")/../.." && pwd)
build="$root/build/package-arch"
mkdir -p "$build" "${1:-$root/dist/pacman}"
out=$(cd "${1:-$root/dist/pacman}" && pwd)

# Use the actual checked-out commit (including a PR merge commit when applicable),
# not github.sha, a release tag URL, or uncommitted files in the workspace.
commit=$(git -C "$root" rev-parse --verify HEAD)
version=$(git -C "$root" show "$commit:CMakeLists.txt" |
    sed -nE 's/^project\(CirrusMusic VERSION ([0-9]+\.[0-9]+\.[0-9]+)([[:space:]]|\)).*/\1/p')
if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf '%s\n' 'Cannot extract a unique project version from HEAD:CMakeLists.txt.' >&2
    exit 1
fi
source_dir="CirrusMusic-$commit"
archive="$build/$source_dir.tar.gz"
git -C "$root" archive --format=tar --prefix="$source_dir/" "$commit" | gzip -n > "$archive"
source_sha=$(sha256sum "$archive")
source_sha=${source_sha%% *}
printf 'Source commit: %s\nPackage version: %s\nSource SHA-256: %s\n' "$commit" "$version" "$source_sha"

sed -e "s/@PKGVER@/$version/g" \
    -e "s/@SOURCE_DIR@/$source_dir/g" \
    -e "s/@SHA256@/$source_sha/g" \
    "$root/packaging/arch/PKGBUILD.in" > "$build/PKGBUILD"

# All dependencies are already installed; no --syncdeps, sudo, or root makepkg.
export PKGDEST="$out" SRCDEST="$build" BUILDDIR="$build" PKGEXT='.pkg.tar.zst'
export MAKEFLAGS="-j${JOBS:-4}"
cd "$build"
makepkg --noconfirm --cleanbuild --clean

# Pacman queries do not install or run the application and require no keyring or
# runtime credentials. Fail if the app, desktop integration, or licenses are absent.
shopt -s nullglob
packages=("$out"/*.pkg.tar.zst)
if (( ${#packages[@]} != 1 )); then
    printf '%s\n' 'Expected exactly one pacman package (no debug package).' >&2
    exit 1
fi
package=${packages[0]}
export LC_ALL=C
pacman -Qip -- "$package"
[[ $(pacman -Qp -- "$package") == "cirrus $version-1" ]]
files=$(pacman -Qlp -- "$package")
printf '%s\n' "$files"
for path in \
    usr/bin/cirrus \
    usr/share/applications/com.guang233.Cirrus.desktop \
    usr/share/metainfo/com.guang233.Cirrus.metainfo.xml \
    usr/share/licenses/cirrus/LICENSE \
    usr/share/doc/cirrus/LICENSE \
    usr/share/doc/cirrus/THIRD-PARTY-NOTICES.md; do
    if ! grep -Fxq "cirrus /$path" <<< "$files"; then
        printf 'Missing package file: /%s\n' "$path" >&2
        exit 1
    fi
done

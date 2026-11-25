#!/usr/bin/env bash
# Canonical production Windows package path. Run only in GitHub Actions, after
# .github/actions/setup-windows; tests use that action and configure independently.
set -euo pipefail

arch=${1:?usage: build-package.sh <x86_64|arm64>}
case "$arch" in
    x86_64) asset_arch=x64; expected_triplet=x64-windows ;;
    arm64) asset_arch=arm64; expected_triplet=arm64-windows ;;
    *) echo "::error::Unsupported Windows architecture: $arch"; exit 1 ;;
esac
[[ "${GITHUB_ACTIONS:-}" == true ]] || { echo 'Run packaging through GitHub Actions'; exit 1; }
[[ "${VCPKG_TRIPLET:?}" == "$expected_triplet" ]]
: "${DEPS:?}" "${QT_ROOT_DIR:?}" "${VCPKG_INSTALLED:?}"
: "${CIRRUS_CMAKE_PREFIX_PATH:?}" "${CIRRUS_PKG_CONFIG_EXECUTABLE:?}" "${CIRRUS_OPENSSL_ROOT:?}"

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
build="$root/build/windows-$arch"
stage="$(cygpath -m "$RUNNER_TEMP")/Cirrus"
version=$(python -c 'import re; print(re.search(r"project\(CirrusMusic VERSION ([0-9.]+)", open("CMakeLists.txt").read()).group(1))')
# A fresh stage prevents removed DLLs/plugins from surviving a repeated install.
rm -rf "$stage"
cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$CIRRUS_CMAKE_PREFIX_PATH" \
    -DPKG_CONFIG_EXECUTABLE="$CIRRUS_PKG_CONFIG_EXECUTABLE" \
    -DOPENSSL_ROOT_DIR="$CIRRUS_OPENSSL_ROOT" \
    -DBUILD_TESTING=OFF \
    -DCIRRUS_BUILD_GALLERY=OFF \
    -DCIRRUS_EXECUTABLE=Cirrus \
    -DCIRRUS_PACKAGE=windows \
    -DCIRRUS_DEPLOY_QT=ON \
    -DCMAKE_INSTALL_BINDIR=. \
    -DCIRRUS_RUNTIME_DIRS="$DEPS/bin;$VCPKG_INSTALLED/$VCPKG_TRIPLET/bin"
cmake --build "$build" --parallel "${JOBS:-4}" -- -k 0
cmake --install "$build" --prefix "$stage"

# Check every deployed PE and its imports, including Qt's dynamically loaded QML,
# platform, and multimedia plugins. Dependencies may resolve only within the package
# or Windows itself, never against the build machine's vcpkg/Qt PATH.
export CIRRUS_PACKAGE_STAGE="$stage" CIRRUS_PACKAGE_ARCH="$arch"
python - <<'PY'
import os
from pathlib import Path
import re
import struct
import subprocess

stage = Path(os.environ['CIRRUS_PACKAGE_STAGE'])
arch = os.environ['CIRRUS_PACKAGE_ARCH']
expected = {'x86_64': {0x8664}, 'arm64': {0xAA64, 0xA64E}}[arch]
images = [p for p in stage.rglob('*') if p.suffix.lower() in {'.dll', '.exe'}]
assert (stage / 'Cirrus.exe').is_file(), 'Missing Cirrus.exe'
assert images, 'No deployed PE images'
for image in images:
    with image.open('rb') as stream:
        assert stream.read(2) == b'MZ', f'Not a PE: {image}'
        stream.seek(0x3C)
        pe_offset = struct.unpack('<I', stream.read(4))[0]
        stream.seek(pe_offset)
        assert stream.read(4) == b'PE\0\0', f'Invalid PE signature: {image}'
        machine = struct.unpack('<H', stream.read(2))[0]
    assert machine in expected, f'{image}: machine {machine:#06x} does not match {arch}'
    # ARM64X is a native hybrid runtime supported on ARM64 Windows.
    if machine == 0xA64E:
        assert image.name.lower().startswith(('msvcp', 'vcruntime', 'concrt')), image
    output = subprocess.check_output(['dumpbin.exe', '/nologo', '/dependents', str(image)],
                                     text=True, errors='replace')
    for dependency in re.findall(r'^\s+([^\s]+\.dll)\s*$', output, re.MULTILINE | re.IGNORECASE):
        name = dependency.lower()
        if name.startswith(('api-ms-', 'ext-ms-')):
            continue
        packaged = (image.parent / dependency).is_file() or (stage / dependency).is_file()
        system = (Path(os.environ['SystemRoot']) / 'System32' / dependency).is_file()
        assert packaged or system, f'{image}: unresolved runtime dependency {dependency}'

# These libraries are required independently of Windows' preinstalled runtimes.
for name in ('vcruntime140.dll', 'msvcp140.dll', 'Qt6Core.dll', 'Qt6Multimedia.dll'):
    assert (stage / name).is_file(), f'Missing bundled runtime {name}'
vcpkg_bin = Path(os.environ['VCPKG_INSTALLED']) / os.environ['VCPKG_TRIPLET'] / 'bin'
for family in ('avcodec', 'avformat', 'avutil', 'swresample', 'libcrypto', 'zlib|z', 'zstd'):
    pattern = re.compile(r'^(?:' + family + r')(?:[-.0-9].*)?\.dll$', re.IGNORECASE)
    dlls = [p for p in vcpkg_bin.glob('*.dll') if pattern.match(p.name)]
    assert dlls, f'vcpkg has no runtime for {family}'
    for dll in dlls:
        assert (stage / dll.name).is_file(), f'Missing vcpkg runtime {dll.name}'
print(f'Validated {len(images)} deployed PE images for {arch} and their runtime imports')
PY

mkdir -p dist
archive="$(cygpath -m "$root")/dist/Cirrus-$version-windows-$asset_arch.zip"
rm -f "$archive"
7z a -tzip "$archive" "$stage" > /dev/null
export CIRRUS_PACKAGE_VERSION="$version" CIRRUS_PACKAGE_OUTPUT="$(cygpath -m "$root")/dist"
pwsh -NoProfile -Command '
    $ErrorActionPreference = "Stop"
    $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (!(Test-Path $iscc)) { throw "Inno Setup 6 is missing" }
    & $iscc "/DAppVersion=$env:CIRRUS_PACKAGE_VERSION" "/DTargetArch=$env:CIRRUS_PACKAGE_ARCH" "/DStageDir=$env:CIRRUS_PACKAGE_STAGE" "/DOutputDir=$env:CIRRUS_PACKAGE_OUTPUT" packaging/windows/cirrus.iss
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed: $LASTEXITCODE" }
'
[[ -s "dist/Cirrus-$version-windows-$asset_arch.zip" ]]
[[ -s "dist/Cirrus-$version-windows-$asset_arch-setup.exe" ]]

#!/usr/bin/env python3
"""Deploy the native MSVC dependency closure and validate the complete PE graph."""

from collections import deque
import filecmp
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess


def machine_of(image: Path) -> int:
    with image.open('rb') as stream:
        assert stream.read(2) == b'MZ', f'Not a PE: {image}'
        stream.seek(0x3C)
        pe_offset = struct.unpack('<I', stream.read(4))[0]
        stream.seek(pe_offset)
        assert stream.read(4) == b'PE\0\0', f'Invalid PE signature: {image}'
        return struct.unpack('<H', stream.read(2))[0]


def is_msvc_runtime(name: str) -> bool:
    # msvcrt.dll and msvcp_win.dll are Windows components, not app-local VC CRTs.
    return re.fullmatch(r'(?:msvc[pr]|vcruntime|concrt|vccorlib|vcomp)[0-9].*\.dll',
                        name, re.IGNORECASE) is not None


def main() -> None:
    assert os.environ.get('GITHUB_ACTIONS') == 'true', 'Run packaging through GitHub Actions'
    stage = Path(os.environ['CIRRUS_PACKAGE_STAGE'])
    arch = os.environ['CIRRUS_PACKAGE_ARCH']
    native_arch = {'x86_64': 'x64', 'arm64': 'arm64'}[arch]
    expected = {'x86_64': {0x8664}, 'arm64': {0xAA64, 0xA64E}}[arch]
    crt_dir = Path(os.environ['CIRRUS_MSVC_CRT_DIR'])
    redist_dir = Path(os.environ['VCToolsRedistDir'])
    assert crt_dir.is_dir(), f'Native MSVC CRT directory is missing: {crt_dir}'
    assert crt_dir.parent.resolve() == (redist_dir / native_arch).resolve(), crt_dir
    assert re.fullmatch(r'Microsoft\.VC[0-9]+\.CRT', crt_dir.name), crt_dir
    providers = {p.name.lower(): p for p in crt_dir.glob('*.dll') if p.is_file()}
    assert providers, f'Native MSVC CRT directory is empty: {crt_dir}'
    print(f'Native MSVC CRT provider: {crt_dir}', flush=True)
    for provider in sorted(providers.values()):
        print(f'CRT candidate: {provider.name}: machine {machine_of(provider):#06x}', flush=True)

    def validate_machine(image: Path) -> None:
        machine = machine_of(image)
        assert machine in expected, f'{image}: machine {machine:#06x} does not match {arch}'
        # A native ARM64X hybrid is permitted only for a compiler runtime.
        if machine == 0xA64E:
            assert is_msvc_runtime(image.name), image

    images = [p for p in stage.rglob('*') if p.is_file() and p.suffix.lower() in {'.dll', '.exe'}]
    assert (stage / 'Cirrus.exe').is_file(), 'Missing Cirrus.exe'
    assert images, 'No deployed PE images'
    pending = deque(images)
    visited = set()
    selected = set()
    while pending:
        image = pending.popleft()
        identity = str(image.resolve()).lower()
        if identity in visited:
            continue
        validate_machine(image)
        visited.add(identity)
        output = subprocess.check_output(['dumpbin.exe', '/nologo', '/dependents', str(image)],
                                         text=True, errors='replace')
        for dependency in re.findall(r'^\s+([^\s]+\.dll)\s*$', output,
                                     re.MULTILINE | re.IGNORECASE):
            name = dependency.lower()
            assert Path(dependency).name == dependency, f'Invalid DLL import: {dependency}'
            if name.startswith(('api-ms-', 'ext-ms-')):
                continue
            if is_msvc_runtime(name):
                # Never resolve a compiler runtime from PATH or preinstalled System32.
                assert name in providers, f'{image}: no native MSVC provider for {dependency}'
                provider = providers[name]
                validate_machine(provider)
                destination = stage / provider.name
                if destination.exists():
                    assert filecmp.cmp(provider, destination, shallow=False), (
                        f'{image}: {dependency} was deployed from a different MSVC provider')
                else:
                    shutil.copy2(provider, destination)
                    print(f'Deployed MSVC import: {image.name} -> {provider}', flush=True)
                selected.add(name)
                pending.append(destination)
                continue
            packaged = (image.parent / dependency).is_file() or (stage / dependency).is_file()
            system = (Path(os.environ['SystemRoot']) / 'System32' / dependency).is_file()
            assert packaged or system, f'{image}: unresolved runtime dependency {dependency}'

    for name in ('vcruntime140.dll', 'msvcp140.dll', 'Qt6Core.dll', 'Qt6Multimedia.dll'):
        assert (stage / name).is_file(), f'Missing bundled runtime {name}'
    vcpkg_bin = Path(os.environ['VCPKG_INSTALLED']) / os.environ['VCPKG_TRIPLET'] / 'bin'
    for family in ('avcodec', 'avformat', 'avutil', 'swresample', 'libcrypto', 'zlib|z', 'zstd'):
        pattern = re.compile(r'^(?:' + family + r')(?:[-.0-9].*)?\.dll$', re.IGNORECASE)
        dlls = [p for p in vcpkg_bin.glob('*.dll') if pattern.match(p.name)]
        assert dlls, f'vcpkg has no runtime for {family}'
        for dll in dlls:
            assert (stage / dll.name).is_file(), f'Missing vcpkg runtime {dll.name}'
    print(f'Validated {len(visited)} deployed PE images for {arch}; '
          f'{len(selected)} app-local MSVC runtimes from the native toolset')


if __name__ == '__main__':
    main()

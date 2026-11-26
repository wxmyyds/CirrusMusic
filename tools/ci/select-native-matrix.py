#!/usr/bin/env python3
"""Select native CI/build matrix rows from validated workflow inputs."""

import json
import sys

LINUX_TESTS = [
    {
        "distribution": "arch",
        "arch": "x86_64",
        "runner": "ubuntu-24.04",
        "image": "archlinux:base-devel",
    },
    {
        "distribution": "arch",
        "arch": "arm64",
        "runner": "ubuntu-24.04-arm",
        "image": "menci/archlinuxarm@sha256:89dd0b1da3cab11ef9a0f12481794cf31fd2cb7377ced80aa2041d3a5cae757d",
    },
    {
        "distribution": "debian",
        "arch": "x86_64",
        "runner": "ubuntu-24.04",
        "image": "debian:13",
    },
    {
        "distribution": "debian",
        "arch": "arm64",
        "runner": "ubuntu-24.04-arm",
        "image": "debian:13",
    },
    {
        "distribution": "fedora",
        "arch": "x86_64",
        "runner": "ubuntu-24.04",
        "image": "registry.fedoraproject.org/fedora:44",
    },
    {
        "distribution": "fedora",
        "arch": "arm64",
        "runner": "ubuntu-24.04-arm",
        "image": "registry.fedoraproject.org/fedora:44",
    },
]
WINDOWS = [
    {"runner": "windows-2022", "arch": "x86_64"},
    {"runner": "windows-11-arm", "arch": "arm64"},
]
MACOS = [
    {"runner": "macos-15", "arch": "arm64"},
    {"runner": "macos-15-intel", "arch": "x86_64"},
]

EMPTY_LINUX = LINUX_TESTS[0]
EMPTY_WINDOWS = WINDOWS[0]
EMPTY_MACOS = MACOS[0]
ARCHITECTURES = {"all", "x86_64", "arm64"}
TEST_TARGETS = {"all", "linux", "arch", "debian", "fedora", "windows", "macos"}
LINUX_DISTRIBUTIONS = {"all", "arch", "debian", "fedora"}


def matrix(rows):
    # GitHub Actions expects a non-empty matrix object. The corresponding job's
    # job-level `if` uses the run_* output to skip this inert placeholder row.
    return {"include": rows}


def select_tests(target, arch):
    if target not in TEST_TARGETS:
        raise ValueError(f"unsupported test target: {target}")
    if arch not in ARCHITECTURES:
        raise ValueError(f"unsupported architecture: {arch}")

    linux = [row for row in LINUX_TESTS if target in {"all", "linux", row["distribution"]}]
    windows = WINDOWS if target in {"all", "windows"} else []
    macos = MACOS if target in {"all", "macos"} else []
    if arch != "all":
        linux = [row for row in linux if row["arch"] == arch]
        windows = [row for row in windows if row["arch"] == arch]
        macos = [row for row in macos if row["arch"] == arch]

    return {
        "linux_matrix": matrix(linux or [EMPTY_LINUX]),
        "windows_matrix": matrix(windows or [EMPTY_WINDOWS]),
        "macos_matrix": matrix(macos or [EMPTY_MACOS]),
        "run_linux": bool(linux),
        "run_windows": bool(windows),
        "run_macos": bool(macos),
    }


def linux_build_rows(rows):
    # Keep the library-source archive available for both a full Release matrix and
    # a filtered manual build. Prefer the Debian x86_64 job when it is selected.
    source_row = next(
        (
            index
            for index, row in enumerate(rows)
            if row["distribution"] == "debian" and row["arch"] == "x86_64"
        ),
        0,
    )
    return [
        {**row, "collect_sources": index == source_row}
        for index, row in enumerate(rows)
    ]


def select_linux_build(distribution, arch):
    if distribution not in LINUX_DISTRIBUTIONS:
        raise ValueError(f"unsupported Linux distribution: {distribution}")
    if arch not in ARCHITECTURES:
        raise ValueError(f"unsupported architecture: {arch}")
    rows = [row for row in LINUX_TESTS if distribution in {"all", row["distribution"]}]
    if arch != "all":
        rows = [row for row in rows if row["arch"] == arch]
    if not rows:
        raise ValueError("the selected Linux build has no native targets")
    return {"matrix": matrix(linux_build_rows(rows))}


def select_platform_build(platform, arch):
    if arch not in ARCHITECTURES:
        raise ValueError(f"unsupported architecture: {arch}")
    if platform == "windows":
        rows = WINDOWS
    elif platform == "macos":
        rows = MACOS
    else:
        raise ValueError(f"unsupported build platform: {platform}")
    if arch != "all":
        rows = [row for row in rows if row["arch"] == arch]
    if not rows:
        raise ValueError(f"the selected {platform} build has no native targets")
    return {"matrix": matrix(rows)}


def main():
    if len(sys.argv) < 2:
        raise ValueError(
            "usage: select-native-matrix.py tests TARGET ARCH | linux-build DIST ARCH | "
            "windows-build|macos-build ARCH"
        )
    kind = sys.argv[1]
    if kind == "tests" and len(sys.argv) == 4:
        result = select_tests(sys.argv[2], sys.argv[3])
    elif kind == "linux-build" and len(sys.argv) == 4:
        result = select_linux_build(sys.argv[2], sys.argv[3])
    elif kind in {"windows-build", "macos-build"} and len(sys.argv) == 3:
        result = select_platform_build(kind.removesuffix("-build"), sys.argv[2])
    else:
        raise ValueError(f"invalid arguments for mode: {kind}")
    print(json.dumps(result, separators=(",", ":")))


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        print(f"selection error: {error}", file=sys.stderr)
        raise SystemExit(2)

#!/usr/bin/env python3
"""Resolve dynamic QML/plugin dependencies to native runtime packages in Actions."""

import argparse
import json
import os
from pathlib import Path
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    if os.environ.get("GITHUB_ACTIONS") != "true":
        raise SystemExit("该操作需要通过 GitHub Actions 执行。")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("distribution", choices=("debian", "fedora"))
    parser.add_argument("source", type=Path)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    packages = set()
    evidence = {}

    def owner(file):
        file = file.resolve(strict=True)
        if not file.is_relative_to("/usr"):
            raise SystemExit(f"Non-system Qt runtime file: {file}")
        if args.distribution == "debian":
            lines = output("dpkg-query", "--search", str(file)).splitlines()
            names = {line.rsplit(": ", 1)[0].split(":", 1)[0] for line in lines}
        else:
            names = set(output("rpm", "--query", "--file", str(file), "--qf", "%{NAME}\n").splitlines())
        if len(names) != 1:
            raise SystemExit(f"Ambiguous Qt runtime ownership: {file}: {names}")
        name = names.pop()
        if name.endswith(("-dev", "-devel")):
            raise SystemExit(f"Qt runtime file belongs to a development package: {file}: {name}")
        packages.add(name)
        evidence[str(file)] = name

    scanner = Path((args.build / "cirrus-qmlimportscanner.txt").read_text().strip())
    scanner = scanner.resolve(strict=True)
    if not scanner.is_relative_to("/usr"):
        raise SystemExit(f"Non-system QML scanner: {scanner}")
    qml = Path(output("qtpaths6", "--query", "QT_INSTALL_QML")).resolve(strict=True)
    plugins = Path(output("qtpaths6", "--query", "QT_INSTALL_PLUGINS")).resolve(strict=True)
    imports = json.loads(output(str(scanner), "-rootPath", str(args.source / "ui"),
                                "-importPath", str(args.build / "qml"),
                                "-importPath", str(qml)))
    for module in imports:
        if module.get("type") != "module":
            continue
        path = module.get("path")
        if not path:
            if module["name"].startswith("Qt") and not module.get("optional", False):
                raise SystemExit(f"Unresolved system QML import: {module['name']}")
            continue
        directory = Path(path).resolve(strict=True)
        if directory.is_relative_to(qml):
            owner(directory / "qmldir")
        elif not directory.is_relative_to(args.build.resolve()):
            raise SystemExit(f"QML import outside system/build trees: {module['name']}: {directory}")

    # dlopen-loaded integrations are invisible to ELF dependency scanners.
    for pattern in ("sqldrivers/libqsqlite.so", "imageformats/libqwebp.so",
                    "imageformats/libqsvg.so", "iconengines/libqsvgicon.so",
                    "platforms/libqxcb.so", "platforms/*wayland*.so", "multimedia/*.so"):
        matches = sorted(plugins.glob(pattern))
        if not matches:
            raise SystemExit(f"Missing system Qt runtime integration: {pattern}")
        for file in matches:
            owner(file)
    if not packages:
        raise SystemExit("No native Qt runtime dependencies found")
    (args.build / "system-qt-runtime-evidence.json").write_text(
        json.dumps({"imports": imports, "files": evidence}, indent=2) + "\n")
    (args.build / "system-qt-runtime-packages.txt").write_text("\n".join(sorted(packages)) + "\n")
    print("\n".join(sorted(packages)))


if __name__ == "__main__":
    main()

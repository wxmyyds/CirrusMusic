#!/usr/bin/env python3
"""Resolve dynamic QML/plugin dependencies to native runtime packages in Actions."""

import argparse
import json
import os
import re
import sys
from pathlib import Path
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    if os.environ.get("GITHUB_ACTIONS") != "true":
        raise SystemExit("该操作需要通过 GitHub Actions 执行。")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("distribution", choices=("arch", "debian", "fedora"))
    parser.add_argument("source", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("--evidence", type=Path,
                        help="Write the source-bound runtime audit report for CI")
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
        elif args.distribution == "fedora":
            names = set(output("rpm", "--query", "--file", str(file), "--qf", "%{NAME}\n").splitlines())
        else:
            names = set(output("pacman", "--query", "--owns", "--quiet", str(file)).splitlines())
        if len(names) != 1:
            raise SystemExit(f"Ambiguous Qt runtime ownership: {file}: {names}")
        name = names.pop()
        if name.endswith(("-dev", "-devel")):
            raise SystemExit(f"Qt runtime file belongs to a development package: {file}: {name}")
        packages.add(name)
        evidence[str(file)] = name

    def system_tool(name):
        configured = (args.build / f"cirrus-{name}.txt").read_text().strip()
        tool = Path(configured).resolve(strict=True)
        if not tool.is_relative_to("/usr"):
            raise SystemExit(f"Non-system Qt tool: {tool}")
        return configured, tool

    configured_scanner, scanner = system_tool("qmlimportscanner")
    configured_qtpaths, qtpaths = system_tool("qtpaths")
    qml = Path(output(str(qtpaths), "--query", "QT_INSTALL_QML")).resolve(strict=True)
    plugins = Path(output(str(qtpaths), "--query", "QT_INSTALL_PLUGINS")).resolve(strict=True)
    imports = json.loads(output(str(scanner), "-rootPath", str(args.source / "ui"),
                                "-importPath", str(args.build / "qml"),
                                "-importPath", str(qml)))
    # Qt 6.8's scanner reports unavailable optional styles without carrying their
    # optional flag. Read the authoritative qmldir declarations instead. An app's
    # explicit import, or any mandatory qmldir edge, always remains required.
    required = set()
    for source in (args.source / "ui").rglob("*.qml"):
        required.update(re.findall(r"^\s*import\s+([\w.]+)", source.read_text(), re.MULTILINE))
    optional = set()
    optional_origins = {}
    for module in imports:
        if module.get("type") != "module" or not module.get("path"):
            continue
        directory = Path(module["path"]).resolve(strict=True)
        if directory.is_relative_to(qml):
            metadata = directory / "qmldir"
            owner(metadata)
            for line in metadata.read_text().splitlines():
                words = line.split()
                flags = set()
                while words and words[0] in ("optional", "default"):
                    flags.add(words.pop(0))
                if len(words) < 2 or words[0] not in ("import", "depends"):
                    continue
                name = words[1]
                if "optional" in flags and "default" not in flags:
                    optional.add(name)
                    optional_origins.setdefault(name, []).append(str(metadata))
                else:
                    required.add(name)
        elif not directory.is_relative_to(args.build.resolve()):
            raise SystemExit(f"QML import outside system/build trees: {module['name']}: {directory}")
    unavailable_optional = []
    for module in imports:
        if module.get("type") != "module" or module.get("path"):
            continue
        name = module["name"]
        if not name.startswith("Qt"):
            continue
        if name in required or name not in optional:
            raise SystemExit(f"Unresolved required system QML import: {name}")
        unavailable_optional.append({"name": name, "declared_by": optional_origins[name]})
        print(f"Qt optional import unavailable on this platform: {name}; "
              f"declared optional by {optional_origins[name]}", file=sys.stderr)

    # dlopen-loaded integrations are invisible to ELF dependency scanners.
    for pattern in ("tls/libqopensslbackend.so", "sqldrivers/libqsqlite.so", "imageformats/libqwebp.so",
                    "imageformats/libqsvg.so", "iconengines/libqsvgicon.so",
                    "platforms/libqxcb.so", "platforms/*wayland*.so", "multimedia/*.so"):
        matches = sorted(plugins.glob(pattern))
        if not matches:
            raise SystemExit(f"Missing system Qt runtime integration: {pattern}")
        for file in matches:
            owner(file)
    if not packages:
        raise SystemExit("No native Qt runtime dependencies found")
    if args.evidence is not None:
        source_sha = output("git", "-C", str(args.source), "rev-parse", "HEAD")
        if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
            raise SystemExit(f"Invalid source commit in Qt ownership evidence: {source_sha}")
        args.evidence.write_text(
            json.dumps({"source_sha": source_sha, "distribution": args.distribution,
                        "native_machine": os.uname().machine,
                        "qt_version": output("pkg-config", "--modversion", "Qt6Core"),
                        "qml_scanner": str(scanner), "qml_scanner_configured": configured_scanner,
                        "qtpaths": str(qtpaths), "qtpaths_configured": configured_qtpaths,
                        "qml_root": str(qml),
                        "plugins_root": str(plugins), "imports": imports, "files": evidence,
                        "unavailable_optional_imports": unavailable_optional}, indent=2) + "\n")
    (args.build / "system-qt-runtime-packages.txt").write_text("\n".join(sorted(packages)) + "\n")
    print("\n".join(sorted(packages)))


if __name__ == "__main__":
    main()

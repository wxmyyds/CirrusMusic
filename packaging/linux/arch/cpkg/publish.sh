#!/usr/bin/env bash
# Hand the already-built Arch binaries to arch-cloud-pkgs' canonical OCI publisher.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo '该操作需要通过 GitHub Actions 执行。' >&2; exit 1; }
root=$(cd "$(dirname "$0")/../../../.." && pwd)
repository=$(cd "${1:?missing arch-cloud-pkgs checkout}" && pwd)
dist=$(cd "${2:?missing release asset directory}" && pwd)
publish=${3:?missing publish flag}
[[ $publish == true || $publish == false ]]
publisher="$repository/.github/scripts/publish-oci-package.sh"
[[ -f $publisher && -f $repository/cpkg/packages.json ]]

# Bind both package copies to the same release inventory checked by Release.
python3 - "$dist" "$root" "$repository" <<'PY'
import hashlib, json, pathlib, subprocess, sys
assets, source, repository = map(pathlib.Path, sys.argv[1:])
manifest = json.loads((assets / 'BUILD-METADATA.json').read_text())
commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
if manifest['schema'] != 1 or manifest['commit'] != commit:
    raise SystemExit('cpkg source commit does not match the release inventory')
catalog = json.loads((repository / 'cpkg/packages.json').read_text())
if not any(p['name'] == 'cirrus' for p in catalog['packages']):
    raise SystemExit('Selected arch-cloud-pkgs source has no Cirrus catalog entry')
for architecture in ['x86_64', 'aarch64']:
    name = f"cirrus-{manifest['version']}-1-{architecture}.pkg.tar.zst"
    package = assets / name
    with package.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if digest != manifest['sha256'].get(name):
        raise SystemExit(f'cpkg package checksum mismatch: {name}')
PY
version=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$dist/BUILD-METADATA.json")
PACKAGE_NAME=cirrus PUBLISH="$publish" bash "$publisher" \
    "$dist/cirrus-$version-1-x86_64.pkg.tar.zst" \
    "$dist/cirrus-$version-1-aarch64.pkg.tar.zst"

if [[ $publish == true ]]; then
    # cpkg pulls anonymously. GHCR's first publication is private until its owner
    # changes visibility; a private upload must not be reported as installable.
    response=$(mktemp)
    trap 'rm -f "$response"' EXIT
    status=$(curl -sS -o "$response" -w '%{http_code}' \
        'https://ghcr.io/token?service=ghcr.io&scope=repository:wxmyyds/cirrus:pull')
    if [[ $status != 200 ]]; then
        echo "::error::Anonymous cpkg access denied (HTTP $status); set the Cirrus GHCR package visibility to Public."
        exit 1
    fi
    token=$(jq -er '.token' "$response")
    curl -fsS -o /dev/null -H "Authorization: Bearer $token" \
        -H 'Accept: application/vnd.oci.image.index.v1+json' \
        "https://ghcr.io/v2/wxmyyds/cirrus/manifests/v$version-1"
fi

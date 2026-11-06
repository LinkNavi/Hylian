#!/usr/bin/env bash
# Usage: lsp/release.sh <patch|minor|major|breaking>
# Tags the next lsp-vX.Y.Z, waits for CI to attach the binary, then bumps and
# pushes the Mason registry. "breaking" is an alias for major.
# Registry checkout: $HYLIAN_REGISTRY_DIR (default ../hylian-registry).
set -euo pipefail
cd "$(dirname "$0")/.."

kind="${1:-}"
[[ "$kind" == breaking ]] && kind=major
[[ "$kind" =~ ^(patch|minor|major)$ ]] || { echo "usage: $0 patch|minor|major|breaking" >&2; exit 1; }

reg="${HYLIAN_REGISTRY_DIR:-../hylian-registry}"
[[ -f "$reg/packages/hylian-lsp/package.yaml" ]] || { echo "registry not found at $reg" >&2; exit 1; }
[[ -z "$(git -C "$reg" status --porcelain)" ]] || { echo "registry has uncommitted changes" >&2; exit 1; }
[[ -z "$(git status --porcelain --untracked-files=no)" ]] || { echo "Hylian has uncommitted tracked changes" >&2; exit 1; }
[[ -f .github/workflows/lsp-release.yml ]] || { echo "lsp-release.yml missing" >&2; exit 1; }

git fetch -q --tags
git push -q origin HEAD   # the tagged commit must exist on the remote with the workflow

last="$(git tag -l 'lsp-v*' | sed 's/^lsp-v//' | sort -V | tail -1)"
IFS=. read -r ma mi pa <<<"${last:-0.0.0}"
case "$kind" in
  major) ma=$((ma+1)); mi=0; pa=0 ;;
  minor) mi=$((mi+1)); pa=0 ;;
  patch) pa=$((pa+1)) ;;
esac
tag="lsp-v$ma.$mi.$pa"
echo "${last:+lsp-v$last}${last:-none} -> $tag"

git tag "$tag"
git push -q origin "$tag"

echo "waiting for CI to attach hylian-lsp-linux-x64.tar.gz ..."
for _ in $(seq 60); do
  gh release view "$tag" --json assets -q '.assets[].name' 2>/dev/null | grep -qx hylian-lsp-linux-x64.tar.gz && break
  sleep 10
done
gh release view "$tag" --json assets -q '.assets[].name' | grep -qx hylian-lsp-linux-x64.tar.gz \
  || { echo "release asset never appeared; registry NOT bumped (tag $tag is pushed)" >&2; exit 1; }

sed -i "s|\(pkg:github/LinkNavi/Hylian@\)lsp-v[0-9.]*|\1$tag|" "$reg/packages/hylian-lsp/package.yaml"
git -C "$reg" commit -qam "hylian-lsp $tag"
git -C "$reg" push -q origin HEAD
echo "done: $tag released, registry bumped"

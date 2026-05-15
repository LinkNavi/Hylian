#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# sync-tree-sitter-repo.sh
#
# Syncs the local grammar (lsp/grammar/) into a local clone of
# tree-sitter-hylian and pushes to GitHub.
#
# Usage:
#   1. Clone the repo next to this script (or anywhere, then set REPO_DIR):
#        git clone git@github.com:LinkNavi/tree-sitter-hylian.git ~/tmp/tree-sitter-hylian
#   2. Run this script:
#        bash .dev/sync-tree-sitter-repo.sh ~/tmp/tree-sitter-hylian
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GRAMMAR_DIR="$PROJECT_ROOT/lsp/grammar"

REPO_DIR="${1:-}"
if [[ -z "$REPO_DIR" ]]; then
  echo "Usage: $0 <path-to-tree-sitter-hylian-clone>"
  echo ""
  echo "Example:"
  echo "  git clone git@github.com:LinkNavi/tree-sitter-hylian.git ~/tmp/tree-sitter-hylian"
  echo "  $0 ~/tmp/tree-sitter-hylian"
  exit 1
fi

echo "── Syncing grammar to $REPO_DIR ──"

# Copy grammar.js
cp "$GRAMMAR_DIR/grammar.js" "$REPO_DIR/grammar.js"
echo "  ✓ grammar.js"

# Copy src/
rm -rf "$REPO_DIR/src"
cp -r "$GRAMMAR_DIR/src" "$REPO_DIR/src"
echo "  ✓ src/"

# Copy queries/
rm -rf "$REPO_DIR/queries"
cp -r "$GRAMMAR_DIR/queries" "$REPO_DIR/queries"
echo "  ✓ queries/"

# Copy package.json and binding.gyp from staging
cp "$SCRIPT_DIR/tree-sitter-hylian/package.json" "$REPO_DIR/package.json"
cp "$SCRIPT_DIR/tree-sitter-hylian/binding.gyp"  "$REPO_DIR/binding.gyp"
echo "  ✓ package.json"
echo "  ✓ binding.gyp"

echo ""
echo "── Done! Now cd into the repo and push: ──"
echo ""
echo "  cd $REPO_DIR"
echo "  git add -A"
echo "  git commit -m 'sync grammar with Hylian compiler'"
echo "  git push origin master"
echo ""

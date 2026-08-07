#!/usr/bin/env bash
#
# grammar_drift.sh — check that Hylian's three independent grammars agree.
#
# There are three separate parsers for one language:
#   1. compiler/parser.y      — the real one, decides what actually compiles
#   2. lsp/src/parser_lsp.y   — what the editor underlines in red
#   3. lsp/grammar/grammar.js — tree-sitter, what the editor highlights and folds
#
# Nothing structurally keeps them in sync, and they had already drifted in both
# directions: parser_lsp.y was missing `else if` entirely (valid code shown as a
# syntax error), and the tree-sitter grammar still accepted tuple syntax that
# was removed from the language. Neither was noticeable without opening a file
# and looking at it.
#
# This runs a shared corpus through all three and fails if they disagree.
# Files in grammar_corpus/accept/ must parse in all three; files in
# grammar_corpus/reject/ must fail in all three.
#
# Usage: tests/grammar_drift.sh    (from anywhere; paths resolve to the repo)

set -u

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BOLD='\033[1m'; DIM='\033[2m'; NC='\033[0m'

PASS=0; FAIL=0; SKIP=0

HYLIAN="$ROOT/hylian"
LSPCHECK="$ROOT/lsp/lsp-syntax-check"
TS_DIR="$ROOT/lsp/grammar"

have_ts=0
if command -v tree-sitter >/dev/null 2>&1 && [ -f "$TS_DIR/grammar.js" ]; then
    have_ts=1
fi

if [ ! -x "$HYLIAN" ]; then
    echo -e "${RED}missing $HYLIAN — run ./build.sh first${NC}"
    exit 2
fi
if [ ! -x "$LSPCHECK" ]; then
    echo -e "${RED}missing $LSPCHECK — run lsp/build.sh first${NC}"
    exit 2
fi

# --- one verdict per grammar: 0 = accepted, 1 = rejected -------------------

# The compiler does far more than parse, so a nonzero exit alone would also
# catch typecheck and codegen problems. Only a parse/syntax diagnostic counts
# as "the grammar rejected this".
compiler_verdict() {
    local out
    out="$($HYLIAN "$1" -o /tmp/_grammar_drift.o 2>&1)"
    if echo "$out" | grep -qiE "syntax error|failed to parse|unexpected"; then
        return 1
    fi
    return 0
}

lsp_verdict() {
    $LSPCHECK "$1" >/dev/null 2>&1
}

treesitter_verdict() {
    ( cd "$TS_DIR" && tree-sitter parse "$1" >/dev/null 2>&1 )
}

check() {
    local file="$1" expect="$2"   # expect = accept | reject
    local name; name="$(basename "$file")"

    compiler_verdict "$file"; local c=$?
    lsp_verdict      "$file"; local l=$?
    local t="skip"
    if [ "$have_ts" = "1" ]; then
        treesitter_verdict "$file"; t=$?
    fi

    local want=0
    [ "$expect" = "reject" ] && want=1

    local bad=""
    [ "$c" != "$want" ] && bad="$bad compiler"
    [ "$l" != "$want" ] && bad="$bad lsp"
    if [ "$t" != "skip" ] && [ "$t" != "$want" ]; then bad="$bad tree-sitter"; fi

    if [ -z "$bad" ]; then
        echo -e "  ${GREEN}✓${NC} $name ${DIM}($expect)${NC}"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${NC} $name ${DIM}(expected all three to $expect)${NC}"
        echo -e "    ${DIM}disagreeing:${NC}$bad"
        FAIL=$((FAIL + 1))
    fi
}

echo -e "\n${BOLD}Grammar drift check${NC}"
if [ "$have_ts" = "0" ]; then
    echo -e "  ${YELLOW}~${NC} tree-sitter CLI not found — checking 2 of 3 grammars"
    SKIP=1
fi

echo -e "\n${BOLD}  must parse${NC}"
for f in "$ROOT"/tests/grammar_corpus/accept/*.hy; do
    [ -e "$f" ] || continue
    check "$f" accept
done

echo -e "\n${BOLD}  must be rejected${NC}"
for f in "$ROOT"/tests/grammar_corpus/reject/*.hy; do
    [ -e "$f" ] || continue
    check "$f" reject
done

rm -f /tmp/_grammar_drift.o

echo ""
if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}grammars agree${NC} ($PASS checks)"
    exit 0
fi
echo -e "${RED}${BOLD}grammars disagree${NC} ($FAIL of $((PASS + FAIL)) checks)"
exit 1

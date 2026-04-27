#!/usr/bin/env bash

# ─────────────────────────────────────────────
#  HylianLsp build script
#  Usage: ./build.sh [--clean] [--verbose|-v]
# ─────────────────────────────────────────────

cd "$(dirname "$0")"

# ── Colours ───────────────────────────────────
RESET="\033[0m"
BOLD="\033[1m"
RED="\033[1;31m"
GREEN="\033[1;32m"
YELLOW="\033[1;33m"
CYAN="\033[1;36m"
DIM="\033[2m"

# ── Flags ─────────────────────────────────────
CLEAN=0
VERBOSE=0

for arg in "$@"; do
  case "$arg" in
    --clean)        CLEAN=1 ;;
    --verbose|-v)   VERBOSE=1 ;;
    *)
      echo -e "${RED}Unknown option: $arg${RESET}"
      echo "Usage: $0 [--clean] [--verbose|-v]"
      exit 1
      ;;
  esac
done

# ── Helpers ───────────────────────────────────
info()    { echo -e "  ${CYAN}»${RESET} $*"; }
success() { echo -e "  ${GREEN}✓${RESET} $*"; }
warn()    { echo -e "  ${YELLOW}!${RESET} $*"; }
fail()    { echo -e "  ${RED}✗${RESET} $*"; }

run() {
  if [[ $VERBOSE -eq 1 ]]; then
    echo -e "  ${DIM}$ $*${RESET}"
  fi
  "$@"
}

# ── Header ────────────────────────────────────
echo ""
echo -e "${BOLD}${CYAN}╔══════════════════════════════════════╗${RESET}"
echo -e "${BOLD}${CYAN}║      HylianLsp Build System          ║${RESET}"
echo -e "${BOLD}${CYAN}╚══════════════════════════════════════╝${RESET}"
echo ""

# ── Clean ─────────────────────────────────────
if [[ $CLEAN -eq 1 ]]; then
  info "Cleaning build artifacts..."
  run rm -f hylian-lsp
  run rm -f src/parser_lsp.tab.c src/parser_lsp.tab.h
  run rm -f src/lex_lsp.yy.c
  info "Clean complete. Proceeding with build..."
  echo ""
fi

# ── Sanity checks ─────────────────────────────
for tool in bison flex gcc; do
  if ! command -v "$tool" &>/dev/null; then
    fail "Required tool not found: $tool"
    echo ""
    echo -e "${RED}${BOLD}  Build failed.${RESET}"
    echo ""
    exit 1
  fi
done

# ── Build ─────────────────────────────────────
echo -e "${BOLD}  Building LSP server...${RESET}"

(
  cd src || exit 1

  # ── Bison: parser_lsp.y → parser_lsp.tab.c / parser_lsp.tab.h ──
  info "Running bison on parser_lsp.y..."
  run bison -d -o parser_lsp.tab.c parser_lsp.y
  if [[ $? -ne 0 ]]; then
    fail "bison failed on parser_lsp.y"
    exit 1
  fi
  success "parser_lsp.tab.c generated"

  # ── Flex: lexer_lsp.l → lex_lsp.yy.c ───────────────────────────
  info "Running flex on lexer_lsp.l..."
  run flex -o lex_lsp.yy.c lexer_lsp.l
  if [[ $? -ne 0 ]]; then
    fail "flex failed on lexer_lsp.l"
    exit 1
  fi
  success "lex_lsp.yy.c generated"

  # ── Compile everything ───────────────────────────────────────────
  info "Compiling..."
  run gcc \
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
    -O2 \
    lex_lsp.yy.c \
    parser_lsp.tab.c \
    ast.c \
    lsp_diag.c \
    lsp_analysis.c \
    lsp_proto.c \
    typecheck.c \
    lsp_main.c \
    -o ../hylian-lsp

  if [[ $? -ne 0 ]]; then
    fail "gcc compilation failed"
    exit 1
  fi
)

BUILD_EXIT=$?
if [[ $BUILD_EXIT -ne 0 ]]; then
  echo ""
  echo -e "${RED}${BOLD}╔══════════════════════════════════════╗${RESET}"
  echo -e "${RED}${BOLD}║           Build  FAILED              ║${RESET}"
  echo -e "${RED}${BOLD}╚══════════════════════════════════════╝${RESET}"
  echo ""
  exit 1
fi

success "LSP server built: ./hylian-lsp"

# ── Footer ────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}╔══════════════════════════════════════╗${RESET}"
echo -e "${GREEN}${BOLD}║          Build  SUCCEEDED            ║${RESET}"
echo -e "${GREEN}${BOLD}╚══════════════════════════════════════╝${RESET}"
echo ""
echo -e "  To use with Neovim / VS Code, point your LSP client at:"
echo -e "  ${BOLD}$(pwd)/hylian-lsp${RESET}"
echo ""

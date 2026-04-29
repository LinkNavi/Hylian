#!/usr/bin/env bash

# ─────────────────────────────────────────────
#  Hylian build script
#  Usage: ./build.sh [--clean] [--verbose|-v] [--skip-runtime]
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
SKIP_RUNTIME=0

for arg in "$@"; do
  case "$arg" in
    --clean)        CLEAN=1 ;;
    --verbose|-v)   VERBOSE=1 ;;
    --skip-runtime) SKIP_RUNTIME=1 ;;
    *)
      echo -e "${RED}Unknown option: $arg${RESET}"
      echo "Usage: $0 [--clean] [--verbose|-v] [--skip-runtime]"
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
echo -e "${BOLD}${CYAN}║        Hylian Build System           ║${RESET}"
echo -e "${BOLD}${CYAN}╚══════════════════════════════════════╝${RESET}"
echo ""

# ── Clean ─────────────────────────────────────
if [[ $CLEAN -eq 1 ]]; then
  info "Cleaning build artifacts..."

  if [[ -f "./hylian" ]]; then
    run rm -f "./hylian"
    info "Removed ./hylian"
  fi

  OBJ_FILES=$(find runtime/ -name "*.o" 2>/dev/null)
  if [[ -n "$OBJ_FILES" ]]; then
    run find runtime/ -name "*.o" -delete
    info "Removed runtime .o files"
  fi

  # Also remove intermediate bison/flex outputs in src/
  run rm -f src/parser.tab.c src/parser.tab.h src/lex.yy.c

  info "Clean complete. Proceeding with build..."
  echo ""
fi

# ── Build compiler ────────────────────────────
echo -e "${BOLD}  Building compiler...${RESET}"

if [[ ! -d "src" ]]; then
  fail "src/ directory not found"
  echo ""
  echo -e "${RED}${BOLD}  Build failed.${RESET}"
  echo ""
  exit 1
fi

(
  cd src || exit 1

  run bison -d parser.y
  if [[ $? -ne 0 ]]; then
    fail "bison failed on parser.y"
    exit 1
  fi

  run flex lexer.l
  if [[ $? -ne 0 ]]; then
    fail "flex failed on lexer.l"
    exit 1
  fi

  run gcc lex.yy.c parser.tab.c ast.c ir.c lower.c opt.c codegen_asm.c typecheck.c compiler.c -o ../hylian
  if [[ $? -ne 0 ]]; then
    fail "gcc compilation failed"
    exit 1
  fi
)

COMPILER_EXIT=$?
if [[ $COMPILER_EXIT -ne 0 ]]; then
  echo ""
  echo -e "${RED}${BOLD}╔══════════════════════════════════════╗${RESET}"
  echo -e "${RED}${BOLD}║           Build  FAILED              ║${RESET}"
  echo -e "${RED}${BOLD}╚══════════════════════════════════════╝${RESET}"
  echo ""
  exit 1
fi

success "Compiler built: ./hylian"

# ── Build runtime ─────────────────────────────
RUNTIME_COUNT=0

if [[ $SKIP_RUNTIME -eq 1 ]]; then
  warn "Skipping runtime build (--skip-runtime)"
else
  echo ""
  echo -e "${BOLD}  Building runtime...${RESET}"

  if [[ ! -f "build_runtime.py" ]]; then
    fail "build_runtime.py not found"
    echo ""
    echo -e "${RED}${BOLD}╔══════════════════════════════════════╗${RESET}"
    echo -e "${RED}${BOLD}║           Build  FAILED              ║${RESET}"
    echo -e "${RED}${BOLD}╚══════════════════════════════════════╝${RESET}"
    echo ""
    exit 1
  fi

  RUNTIME_ARGS=""
  if [[ $VERBOSE -eq 1 ]]; then
    RUNTIME_ARGS="--verbose"
  fi

  run python3 build_runtime.py $RUNTIME_ARGS
  if [[ $? -ne 0 ]]; then
    fail "build_runtime.py failed"
    echo ""
    echo -e "${RED}${BOLD}╔══════════════════════════════════════╗${RESET}"
    echo -e "${RED}${BOLD}║           Build  FAILED              ║${RESET}"
    echo -e "${RED}${BOLD}╚══════════════════════════════════════╝${RESET}"
    echo ""
    exit 1
  fi

  RUNTIME_COUNT=$(find runtime/ -name "*.o" 2>/dev/null | wc -l | tr -d ' ')
  success "Runtime built: ${RUNTIME_COUNT} modules"
fi

# ── Footer ────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}╔══════════════════════════════════════╗${RESET}"
echo -e "${GREEN}${BOLD}║          Build  SUCCEEDED            ║${RESET}"
echo -e "${GREEN}${BOLD}╚══════════════════════════════════════╝${RESET}"
echo ""

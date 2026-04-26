#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install.sh — Hylian toolchain installer
#
# Installs:
#   /usr/local/bin/hylian          — compiler binary
#   /usr/local/bin/linkle          — build system
#   /usr/local/lib/hylian/std/     — pre-compiled runtime .o files
#
# Usage:
#   ./install.sh               Install (may prompt for sudo)
#   ./install.sh --uninstall   Remove everything installed by this script
#   ./install.sh --prefix DIR  Install under DIR instead of /usr/local
#   ./install.sh --no-build    Skip building; use existing ./hylian binary
#   ./install.sh --verbose     Print every command before running it
# ─────────────────────────────────────────────────────────────────────────────

set -e
cd "$(dirname "$0")"

# ── Colours ───────────────────────────────────────────────────────────────────

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    RED='\033[0;31m' GREEN='\033[0;32m' YELLOW='\033[1;33m'
    CYAN='\033[0;36m' BOLD='\033[1m' DIM='\033[2m' NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' BOLD='' DIM='' NC=''
fi

ok()   { echo -e "  ${GREEN}✓${NC}  $*"; }
fail() { echo -e "  ${RED}✗${NC}  $*" >&2; }
info() { echo -e "  ${CYAN}→${NC}  $*"; }
warn() { echo -e "  ${YELLOW}!${NC}  $*"; }
dim()  { echo -e "  ${DIM}$*${NC}"; }
step() { echo -e "\n${BOLD}${CYAN}══  $*${NC}"; }
header() {
    echo -e "${BOLD}${CYAN}"
    echo "  ██╗  ██╗██╗   ██╗██╗     ██╗ █████╗ ███╗  ██╗"
    echo "  ██║  ██║╚██╗ ██╔╝██║     ██║██╔══██╗████╗ ██║"
    echo "  ███████║ ╚████╔╝ ██║     ██║███████║██╔██╗██║"
    echo "  ██╔══██║  ╚██╔╝  ██║     ██║██╔══██║██║╚████║"
    echo "  ██║  ██║   ██║   ███████╗██║██║  ██║██║ ╚███║"
    echo "  ╚═╝  ╚═╝   ╚═╝   ╚══════╝╚═╝╚═╝  ╚═╝╚═╝  ╚══╝"
    echo -e "${NC}"
}

# ── Defaults ──────────────────────────────────────────────────────────────────

PREFIX="/usr/local"
UNINSTALL=0
NO_BUILD=0
VERBOSE=0

# ── Argument parsing ──────────────────────────────────────────────────────────

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)
            shift
            PREFIX="${1%/}"   # strip trailing slash
            ;;
        --prefix=*)
            PREFIX="${1#--prefix=}"
            PREFIX="${PREFIX%/}"
            ;;
        --uninstall)
            UNINSTALL=1
            ;;
        --no-build)
            NO_BUILD=1
            ;;
        --verbose|-v)
            VERBOSE=1
            ;;
        --help|-h)
            echo "Usage: $0 [--prefix DIR] [--uninstall] [--no-build] [--verbose]"
            exit 0
            ;;
        *)
            fail "Unknown option: $1"
            echo "  Run '$0 --help' for usage."
            exit 1
            ;;
    esac
    shift
done

BIN_DIR="${PREFIX}/bin"
LIB_DIR="${PREFIX}/lib/hylian"
STD_DIR="${LIB_DIR}/std"

# ── Sudo helper ───────────────────────────────────────────────────────────────

# We only use sudo if the destination isn't writable by the current user.
maybe_sudo() {
    local dir="$1"
    # find the first ancestor that exists
    local check="$dir"
    while [ ! -e "$check" ]; do
        check="$(dirname "$check")"
    done
    if [ -w "$check" ]; then
        "$@"
    else
        sudo "$@"
    fi
}

run() {
    if [ "$VERBOSE" -eq 1 ]; then
        dim "$ $*"
    fi
    "$@"
}

# ─────────────────────────────────────────────────────────────────────────────
# Uninstall
# ─────────────────────────────────────────────────────────────────────────────

if [ "$UNINSTALL" -eq 1 ]; then
    header
    step "Uninstalling Hylian from ${PREFIX}"

    local_removed=0
    for f in "${BIN_DIR}/hylian" "${BIN_DIR}/linkle"; do
        if [ -f "$f" ]; then
            maybe_sudo rm -f "$f"
            ok "Removed $f"
            local_removed=$((local_removed + 1))
        fi
    done

    if [ -d "$LIB_DIR" ]; then
        maybe_sudo rm -rf "$LIB_DIR"
        ok "Removed $LIB_DIR"
        local_removed=$((local_removed + 1))
    fi

    if [ "$local_removed" -eq 0 ]; then
        warn "Nothing found to uninstall under ${PREFIX}"
    else
        echo ""
        ok "Uninstall complete."
    fi
    exit 0
fi

# ─────────────────────────────────────────────────────────────────────────────
# Install
# ─────────────────────────────────────────────────────────────────────────────

header
echo -e "  ${BOLD}Installing Hylian to ${PREFIX}${NC}"

# ── Step 1: Build ─────────────────────────────────────────────────────────────

if [ "$NO_BUILD" -eq 0 ]; then
    step "Building compiler & runtime"

    build_args=""
    if [ "$VERBOSE" -eq 1 ]; then
        build_args="--verbose"
    fi

    if [ -f "./build.sh" ]; then
        bash ./build.sh $build_args
    else
        # Fallback: inline build
        info "build.sh not found — building inline"
        (
            cd src
            if [ "$VERBOSE" -eq 1 ]; then
                dim "$ bison -d parser.y"
            fi
            bison -d parser.y 2>/dev/null

            if [ "$VERBOSE" -eq 1 ]; then
                dim "$ flex lexer.l"
            fi
            flex lexer.l 2>/dev/null

            if [ "$VERBOSE" -eq 1 ]; then
                dim "$ gcc ... -o ../hylian"
            fi
            gcc lex.yy.c parser.tab.c ast.c codegen_asm.c typecheck.c compiler.c \
                -o ../hylian
        )
        if [ $? -ne 0 ]; then
            fail "Compiler build failed — aborting install."
            exit 1
        fi
        ok "Compiler built"

        info "Building runtime objects"
        python3 build_runtime.py $build_args
        ok "Runtime built"
    fi
else
    step "Skipping build (--no-build)"
    if [ ! -f "./hylian" ]; then
        fail "No ./hylian binary found and --no-build was passed."
        fail "Run ./build.sh first, or omit --no-build."
        exit 1
    fi
    ok "Using existing ./hylian binary"
fi

# ── Step 2: Verify build artifacts ────────────────────────────────────────────

step "Verifying build artifacts"

if [ ! -f "./hylian" ]; then
    fail "Compiler binary './hylian' not found."
    fail "Run ./build.sh before installing."
    exit 1
fi
ok "Compiler binary: ./hylian"

if [ ! -d "./runtime/std" ]; then
    fail "Runtime directory './runtime/std' not found."
    exit 1
fi

runtime_count=$(find ./runtime/std -name "*.o" | wc -l | tr -d ' ')
if [ "$runtime_count" -eq 0 ]; then
    fail "No runtime .o files found in ./runtime/std"
    fail "Run ./build.sh (or python3 build_runtime.py) first."
    exit 1
fi
ok "Runtime modules: ${runtime_count} .o files"

if [ ! -f "./linkle.py" ]; then
    fail "linkle.py not found."
    exit 1
fi
ok "Build system: ./linkle.py"

# ── Step 3: Create directories ────────────────────────────────────────────────

step "Creating directories"

for d in "$BIN_DIR" "$STD_DIR" \
          "$STD_DIR/system" \
          "$STD_DIR/networking"; do
    if [ ! -d "$d" ]; then
        maybe_sudo mkdir -p "$d"
        ok "Created $d"
    else
        dim "exists  $d"
    fi
done

# ── Step 4: Install compiler binary ───────────────────────────────────────────

step "Installing compiler"

maybe_sudo cp ./hylian "${BIN_DIR}/hylian"
maybe_sudo chmod 755  "${BIN_DIR}/hylian"
ok "hylian  →  ${BIN_DIR}/hylian"

# ── Step 5: Install runtime stdlib ────────────────────────────────────────────

step "Installing stdlib runtime"

# Walk runtime/std and copy every .o (and .c source alongside it)
while IFS= read -r -d '' obj; do
    # relative path under runtime/std, e.g. "networking/tcp.o"
    rel="${obj#./runtime/std/}"
    dest="${STD_DIR}/${rel}"
    dest_dir="$(dirname "$dest")"

    # Ensure sub-directory exists
    if [ ! -d "$dest_dir" ]; then
        maybe_sudo mkdir -p "$dest_dir"
    fi

    maybe_sudo cp "$obj" "$dest"
    if [ "$VERBOSE" -eq 1 ]; then
        dim "copied  $rel"
    fi
done < <(find ./runtime/std -name "*.o" -print0)

ok "Copied ${runtime_count} runtime .o files to ${STD_DIR}"

# Also copy .hyi interface files so tools can introspect the stdlib
hyi_count=0
while IFS= read -r -d '' hyi; do
    rel="${hyi#./runtime/std/}"
    dest="${STD_DIR}/${rel}"
    dest_dir="$(dirname "$dest")"
    if [ ! -d "$dest_dir" ]; then
        maybe_sudo mkdir -p "$dest_dir"
    fi
    maybe_sudo cp "$hyi" "$dest"
    hyi_count=$((hyi_count + 1))
done < <(find ./runtime/std -name "*.hyi" -print0)

if [ "$hyi_count" -gt 0 ]; then
    ok "Copied ${hyi_count} .hyi interface files to ${STD_DIR}"
fi

# ── Step 6: Install linkle ────────────────────────────────────────────────────

step "Installing linkle"

# Write a thin wrapper script to ${BIN_DIR}/linkle that invokes linkle.py
# using the installed copy, and sets HYLIAN_LIB so linkle knows where the
# stdlib lives.
LINKLE_INSTALLED="${LIB_DIR}/linkle.py"
maybe_sudo cp ./linkle.py "$LINKLE_INSTALLED"
maybe_sudo chmod 644     "$LINKLE_INSTALLED"
ok "linkle.py  →  ${LINKLE_INSTALLED}"

# Create the wrapper entry point using a temp file to avoid quoting issues
LINKLE_WRAPPER_TMP="$(mktemp)"
cat > "$LINKLE_WRAPPER_TMP" << WRAPPER_EOF
#!/usr/bin/env bash
# Auto-generated by install.sh — do not edit.
export HYLIAN_LIB="${LIB_DIR}"
exec python3 "${LINKLE_INSTALLED}" "\$@"
WRAPPER_EOF
maybe_sudo cp "$LINKLE_WRAPPER_TMP" "${BIN_DIR}/linkle"
rm -f "$LINKLE_WRAPPER_TMP"
maybe_sudo chmod 755 "${BIN_DIR}/linkle"
ok "linkle     →  ${BIN_DIR}/linkle"

# ── Step 7: Smoke test ────────────────────────────────────────────────────────

step "Smoke test"

if command -v hylian >/dev/null 2>&1; then
    hylian_ver=$(hylian --version 2>/dev/null || echo "(no --version flag)")
    ok "hylian is in PATH"
    dim "$hylian_ver"
else
    warn "hylian not found in PATH — you may need to add ${BIN_DIR} to your PATH"
fi

if command -v linkle >/dev/null 2>&1; then
    ok "linkle is in PATH"
else
    warn "linkle not found in PATH — you may need to add ${BIN_DIR} to your PATH"
fi

# ── Done ──────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}${GREEN}══════════════════════════════════════════${NC}"
echo -e "${BOLD}${GREEN}  Hylian installed successfully!${NC}"
echo -e "${BOLD}${GREEN}══════════════════════════════════════════${NC}"
echo ""
echo -e "  Compiler:  ${CYAN}${BIN_DIR}/hylian${NC}"
echo -e "  Build sys: ${CYAN}${BIN_DIR}/linkle${NC}"
echo -e "  Stdlib:    ${CYAN}${STD_DIR}${NC}"
echo ""

if ! echo "$PATH" | tr ':' '\n' | grep -qx "${BIN_DIR}"; then
    warn "${BIN_DIR} is not in your PATH."
    echo  "  Add this to your shell profile:"
    echo -e "    ${DIM}export PATH=\"\$PATH:${BIN_DIR}\"${NC}"
    echo ""
fi

echo -e "  Get started:"
echo -e "    ${DIM}linkle new myapp${NC}"
echo -e "    ${DIM}cd myapp && linkle build && linkle run${NC}"
echo ""

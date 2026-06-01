#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install.sh — Hylian toolchain installer
#
# Installs:
#   /usr/local/bin/hylian               — compiler binary
#   /usr/local/bin/linkle               — build system wrapper
#   /usr/local/lib/hylian/std/          — stdlib .o and .hyi files
#   /usr/local/lib/hylian/std/platform/ — platform .o files (linux, kernel, limine…)
#   /usr/local/lib/hylian/linkle.py     — build system source
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
        --prefix)        shift; PREFIX="${1%/}" ;;
        --prefix=*)      PREFIX="${1#--prefix=}"; PREFIX="${PREFIX%/}" ;;
        --uninstall)     UNINSTALL=1 ;;
        --no-build)      NO_BUILD=1 ;;
        --verbose|-v)    VERBOSE=1 ;;
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
PLATFORM_DIR="${STD_DIR}/platform"

# ── Sudo helper ───────────────────────────────────────────────────────────────

# maybe_sudo CHECK_PATH -- COMMAND [ARGS...]
#   Runs COMMAND with sudo if CHECK_PATH (or its first existing ancestor) is
#   not writable by the current user.  The "--" sentinel separates the check
#   path from the actual command so $1 is never passed to the command.
maybe_sudo() {
    local check="$1"; shift   # consume check path
    shift                     # consume "--" sentinel
    # walk up to first existing ancestor
    local probe="$check"
    while [ ! -e "$probe" ]; do
        probe="$(dirname "$probe")"
    done
    if [ -w "$probe" ]; then
        "$@"
    else
        sudo "$@"
    fi
}

run() {
    [ "$VERBOSE" -eq 1 ] && dim "$ $*"
    "$@"
}

# ─────────────────────────────────────────────────────────────────────────────
# Uninstall
# ─────────────────────────────────────────────────────────────────────────────

if [ "$UNINSTALL" -eq 1 ]; then
    header
    step "Uninstalling Hylian from ${PREFIX}"

    removed=0
    for f in "${BIN_DIR}/hylian" "${BIN_DIR}/linkle"; do
        if [ -f "$f" ]; then
            maybe_sudo "$(dirname "$f")" -- rm -f "$f"
            ok "Removed $f"
            removed=$((removed + 1))
        fi
    done

    if [ -d "$LIB_DIR" ]; then
        maybe_sudo "$LIB_DIR" -- rm -rf "$LIB_DIR"
        ok "Removed $LIB_DIR"
        removed=$((removed + 1))
    fi

    if [ "$removed" -eq 0 ]; then
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
    [ "$VERBOSE" -eq 1 ] && build_args="--verbose"

    if [ -f "./build.sh" ]; then
        bash ./build.sh $build_args
    else
        info "build.sh not found — building inline"
        (
            cd compiler
            bison -d parser.y 2>/dev/null
            flex lexer.l 2>/dev/null
            gcc lex.yy.c parser.tab.c ast.c ir.c lower.c opt.c codegen_asm.c codegen_termina.c typecheck.c compiler.c -o ../hylian
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

if [ ! -d "./runtime" ]; then
    fail "Runtime directory './runtime' not found."
    exit 1
fi

# Count all .o files across runtime/std/ AND runtime/platform/
std_count=$(find ./runtime/std -name "*.o" 2>/dev/null | wc -l | tr -d ' ')
plat_count=$(find ./runtime/platform -name "*.o" 2>/dev/null | wc -l | tr -d ' ')
total_count=$((std_count + plat_count))

if [ "$total_count" -eq 0 ]; then
    fail "No runtime .o files found in ./runtime/"
    fail "Run ./build.sh (or python3 build_runtime.py) first."
    exit 1
fi
ok "Stdlib modules:   ${std_count} .o files"
ok "Platform objects: ${plat_count} .o files (linux, kernel, limine…)"

if [ ! -f "./linkle.py" ]; then
    fail "linkle.py not found."
    exit 1
fi
ok "Build system: ./linkle.py"

# ── Step 3: Create directories ────────────────────────────────────────────────

step "Creating directories"

for d in \
    "$BIN_DIR" \
    "$STD_DIR" \
    "$PLATFORM_DIR" \
    "$STD_DIR/system" \
    "$STD_DIR/networking"
do
    if [ ! -d "$d" ]; then
        maybe_sudo "$PREFIX" -- mkdir -p "$d"
        ok "Created $d"
    else
        dim "exists  $d"
    fi
done

# ── Step 4: Install compiler binary ───────────────────────────────────────────

step "Installing compiler"

maybe_sudo "$BIN_DIR" -- cp ./hylian "${BIN_DIR}/hylian"
maybe_sudo "$BIN_DIR" -- chmod 755   "${BIN_DIR}/hylian"
ok "hylian  →  ${BIN_DIR}/hylian"

# ── Step 5: Install stdlib runtime (.o and .hyi) ──────────────────────────────

step "Installing stdlib runtime"

# Copy every .o from runtime/std/ → STD_DIR/, preserving subdirectory structure
std_copied=0
while IFS= read -r -d '' obj; do
    rel="${obj#./runtime/std/}"          # e.g. "io.o" or "networking/tcp.o"
    dest="${STD_DIR}/${rel}"
    dest_dir="$(dirname "$dest")"

    if [ ! -d "$dest_dir" ]; then
        maybe_sudo "$STD_DIR" -- mkdir -p "$dest_dir"
    fi

    maybe_sudo "$dest_dir" -- cp "$obj" "$dest"
    [ "$VERBOSE" -eq 1 ] && dim "std  $rel"
    std_copied=$((std_copied + 1))
done < <(find ./runtime/std -name "*.o" -print0)

ok "Copied ${std_copied} stdlib .o files → ${STD_DIR}"

# Copy every .hyi interface file
hyi_copied=0
while IFS= read -r -d '' hyi; do
    rel="${hyi#./runtime/std/}"
    dest="${STD_DIR}/${rel}"
    dest_dir="$(dirname "$dest")"

    if [ ! -d "$dest_dir" ]; then
        maybe_sudo "$STD_DIR" -- mkdir -p "$dest_dir"
    fi

    maybe_sudo "$dest_dir" -- cp "$hyi" "$dest"
    [ "$VERBOSE" -eq 1 ] && dim "hyi  $rel"
    hyi_copied=$((hyi_copied + 1))
done < <(find ./runtime/std -name "*.hyi" -print0)

ok "Copied ${hyi_copied} .hyi interface files → ${STD_DIR}"

# ── Step 6: Install platform objects ──────────────────────────────────────────
#
# Platform files live at runtime/platform/{linux,kernel,limine,macos,windows}.o
# They must be installed to STD_DIR/platform/ so that linkle can find them
# at /usr/local/lib/hylian/std/platform/<target>.o
#
# This is the bug that caused:
#   ✗ no platform file for target 'linux': /usr/local/lib/hylian/std/platform/linux.c

step "Installing platform objects"

plat_copied=0
while IFS= read -r -d '' obj; do
    fname="$(basename "$obj")"
    dest="${PLATFORM_DIR}/${fname}"

    maybe_sudo "$PLATFORM_DIR" -- cp "$obj" "$dest"
    [ "$VERBOSE" -eq 1 ] && dim "platform  $fname"
    plat_copied=$((plat_copied + 1))
done < <(find ./runtime/platform -maxdepth 1 -name "*.o" -print0)

# Also copy linker scripts (.ld files) from runtime/platform/
ld_copied=0
while IFS= read -r -d '' ld; do
    fname="$(basename "$ld")"
    dest="${PLATFORM_DIR}/${fname}"

    maybe_sudo "$PLATFORM_DIR" -- cp "$ld" "$dest"
    [ "$VERBOSE" -eq 1 ] && dim "linker script  $fname"
    ld_copied=$((ld_copied + 1))
done < <(find ./runtime/platform -maxdepth 1 -name "*.ld" -print0)

ok "Copied ${plat_copied} platform .o files → ${PLATFORM_DIR}"
[ "$ld_copied" -gt 0 ] && ok "Copied ${ld_copied} linker scripts → ${PLATFORM_DIR}"

if [ "$plat_copied" -eq 0 ]; then
    warn "No platform .o files found in ./runtime/platform/"
    warn "Run 'python3 build_runtime.py' to build them, then re-run install."
fi

# ── Step 7: Install linkle & hylian-release ──────────────────────────────────

step "Installing linkle"

LINKLE_INSTALLED="${LIB_DIR}/linkle.py"
maybe_sudo "$LIB_DIR" -- cp ./linkle.py "$LINKLE_INSTALLED"
maybe_sudo "$LIB_DIR" -- chmod 644 "$LINKLE_INSTALLED"
ok "linkle.py  →  ${LINKLE_INSTALLED}"

# Write a thin wrapper that sets HYLIAN_LIB so linkle.py knows where
# the installed runtime lives, regardless of CWD.
LINKLE_WRAPPER_TMP="$(mktemp)"
cat > "$LINKLE_WRAPPER_TMP" << WRAPPER_EOF
#!/usr/bin/env bash
# Auto-generated by install.sh — do not edit.
# Points linkle at the installed runtime directory.
export HYLIAN_LIB="${LIB_DIR}"
exec python3 "${LINKLE_INSTALLED}" "\$@"
WRAPPER_EOF
maybe_sudo "$BIN_DIR" -- cp "$LINKLE_WRAPPER_TMP" "${BIN_DIR}/linkle"
rm -f "$LINKLE_WRAPPER_TMP"
maybe_sudo "$BIN_DIR" -- chmod 755 "${BIN_DIR}/linkle"
ok "linkle     →  ${BIN_DIR}/linkle"

# Install hylian-release (only if the source file is present)
if [ -f "./hylian-release.py" ]; then
    RELEASE_INSTALLED="${LIB_DIR}/hylian-release.py"
    maybe_sudo "$LIB_DIR" -- cp ./hylian-release.py "$RELEASE_INSTALLED"
    maybe_sudo "$LIB_DIR" -- chmod 644 "$RELEASE_INSTALLED"

    RELEASE_WRAPPER_TMP="$(mktemp)"
    cat > "$RELEASE_WRAPPER_TMP" << RELEASE_EOF
#!/usr/bin/env bash
# Auto-generated by install.sh — do not edit.
exec python3 "${RELEASE_INSTALLED}" "\$@"
RELEASE_EOF
    maybe_sudo "$BIN_DIR" -- cp "$RELEASE_WRAPPER_TMP" "${BIN_DIR}/hylian-release"
    rm -f "$RELEASE_WRAPPER_TMP"
    maybe_sudo "$BIN_DIR" -- chmod 755 "${BIN_DIR}/hylian-release"
    ok "hylian-release  →  ${BIN_DIR}/hylian-release"
fi

# ── Step 8: Smoke test ────────────────────────────────────────────────────────

step "Smoke test"

if command -v hylian >/dev/null 2>&1; then
    ok "hylian is in PATH  ($(command -v hylian))"
else
    warn "hylian not found in PATH"
    warn "Add this to your shell profile:  export PATH=\"\$PATH:${BIN_DIR}\""
fi

if command -v linkle >/dev/null 2>&1; then
    ok "linkle is in PATH  ($(command -v linkle))"
else
    warn "linkle not found in PATH"
    warn "Add this to your shell profile:  export PATH=\"\$PATH:${BIN_DIR}\""
fi

# Quick sanity: make sure the platform directory was actually populated
if [ -f "${PLATFORM_DIR}/linux.o" ] || [ -f "${PLATFORM_DIR}/kernel.o" ]; then
    ok "Platform objects present at ${PLATFORM_DIR}"
else
    warn "Platform directory appears empty: ${PLATFORM_DIR}"
    warn "Re-run with: python3 build_runtime.py && ./install.sh --no-build"
fi

# ── Done ──────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}${GREEN}══════════════════════════════════════════${NC}"
echo -e "${BOLD}${GREEN}  Hylian installed successfully!${NC}"
echo -e "${BOLD}${GREEN}══════════════════════════════════════════${NC}"
echo ""
echo -e "  Compiler:   ${CYAN}${BIN_DIR}/hylian${NC}"
echo -e "  Build sys:  ${CYAN}${BIN_DIR}/linkle${NC}"
echo -e "  Stdlib:     ${CYAN}${STD_DIR}${NC}"
echo -e "  Platforms:  ${CYAN}${PLATFORM_DIR}${NC}"
echo ""

if ! echo "$PATH" | tr ':' '\n' | grep -qx "${BIN_DIR}"; then
    warn "${BIN_DIR} is not in your PATH."
    echo  "  Add this to your shell profile:"
    echo -e "    ${DIM}export PATH=\"\$PATH:${BIN_DIR}\"${NC}"
    echo ""
fi

echo -e "  Get started:"
echo -e "    ${DIM}linkle new myapp${NC}"
echo -e "    ${DIM}cd myapp && linkle run${NC}"
echo ""
echo -e "  For a workspace (kernel + userland):"
echo -e "    ${DIM}linkle new-workspace myos${NC}"
echo -e "    ${DIM}cd myos && linkle build${NC}"
echo ""

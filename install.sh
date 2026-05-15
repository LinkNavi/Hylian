#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install.sh — Hylian toolchain installer
#
# Downloads the latest stable compiler, stdlib, and Linkle from hylian.lol
# and installs them to /usr/local (or a custom prefix).
#
# Installs:
#   /usr/local/bin/hylian               — compiler binary
#   /usr/local/bin/linkle               — build system wrapper
#   /usr/local/lib/hylian/std/          — stdlib .o and .hyi files
#   /usr/local/lib/hylian/linkle.py     — build system source
#
# Usage:
#   curl -fsSL hylian.lol/install.sh | sh
#   ./install.sh
#   ./install.sh --prefix ~/.local
#   ./install.sh --channel nightly
#   ./install.sh --version 1.2.3
#   ./install.sh --uninstall
# ─────────────────────────────────────────────────────────────────────────────

set -e

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
CHANNEL="stable"
VERSION="latest"
UNINSTALL=0
REGISTRY="${HYLIAN_REGISTRY:-https://www.hylian.lol}"

# ── Argument parsing ──────────────────────────────────────────────────────────

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)        shift; PREFIX="${1%/}" ;;
        --prefix=*)      PREFIX="${1#--prefix=}"; PREFIX="${PREFIX%/}" ;;
        --channel)       shift; CHANNEL="$1" ;;
        --channel=*)     CHANNEL="${1#--channel=}" ;;
        --version)       shift; VERSION="$1" ;;
        --version=*)     VERSION="${1#--version=}" ;;
        --uninstall)     UNINSTALL=1 ;;
        --help|-h)
            echo "Usage: $0 [--prefix DIR] [--channel stable|nightly] [--version X.Y.Z] [--uninstall]"
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

maybe_sudo() {
    local check="$1"; shift
    shift  # consume "--" sentinel
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

# ── Uninstall ─────────────────────────────────────────────────────────────────

if [ "$UNINSTALL" -eq 1 ]; then
    header
    step "Uninstalling Hylian from ${PREFIX}"
    removed=0
    for f in "${BIN_DIR}/hylian" "${BIN_DIR}/linkle" "${BIN_DIR}/hylian-release"; do
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

# ── Prerequisites ─────────────────────────────────────────────────────────────

header
echo -e "  ${BOLD}Installing Hylian to ${PREFIX}${NC}"

step "Checking prerequisites"

# Need curl or wget
if command -v curl >/dev/null 2>&1; then
    DOWNLOADER="curl"
elif command -v wget >/dev/null 2>&1; then
    DOWNLOADER="wget"
else
    fail "curl or wget is required but neither was found."
    exit 1
fi
ok "Downloader: $DOWNLOADER"

# Need python3 for linkle
if ! command -v python3 >/dev/null 2>&1; then
    warn "python3 not found — Linkle may not work without it."
    warn "Install Python 3 from your package manager or https://python.org"
else
    ok "Python: $(python3 --version 2>&1)"
fi

# Need tar
if ! command -v tar >/dev/null 2>&1; then
    fail "tar is required but was not found."
    exit 1
fi
ok "tar: ok"

# ── Fetch release metadata ────────────────────────────────────────────────────

step "Fetching release info (channel: ${CHANNEL})"

META_URL="${REGISTRY}/api/release/latest?channel=${CHANNEL}"

if [ "$DOWNLOADER" = "curl" ]; then
    META=$(curl -fsSL "$META_URL" 2>&1) || { fail "Could not reach $META_URL"; exit 1; }
else
    META=$(wget -qO- "$META_URL" 2>&1)  || { fail "Could not reach $META_URL"; exit 1; }
fi

# Parse version and checksums from JSON using sed/grep (no jq required)
if [ "$VERSION" = "latest" ]; then
    VERSION=$(echo "$META" | grep -o '"version":"[^"]*"' | head -1 | sed 's/"version":"//;s/"//')
fi

if [ -z "$VERSION" ]; then
    fail "Could not determine release version from ${META_URL}"
    fail "Response: ${META}"
    exit 1
fi

ok "Version: ${VERSION}"

# Extract per-component checksums
compiler_checksum=$(echo "$META" | grep -o '"component":"compiler","checksum":"[^"]*"' | sed 's/.*"checksum":"//;s/"//')
stdlib_checksum=$(echo "$META"   | grep -o '"component":"stdlib","checksum":"[^"]*"'   | sed 's/.*"checksum":"//;s/"//')
linkle_checksum=$(echo "$META"   | grep -o '"component":"linkle","checksum":"[^"]*"'   | sed 's/.*"checksum":"//;s/"//')

# ── Download helper ───────────────────────────────────────────────────────────

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

download() {
    local url="$1"
    local dest="$2"
    if [ "$DOWNLOADER" = "curl" ]; then
        curl -fsSL --progress-bar -o "$dest" "$url"
    else
        wget -q --show-progress -O "$dest" "$url"
    fi
}

# sha256sum is sha256sum on Linux, shasum on macOS
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

verify_checksum() {
    local file="$1"
    local expected="$2"
    local name="$3"
    if [ -z "$expected" ]; then
        warn "No checksum available for ${name} — skipping verification."
        return
    fi
    local actual
    actual=$(sha256 "$file")
    if [ "$actual" = "$expected" ]; then
        ok "Checksum verified: ${name}"
    else
        fail "Checksum mismatch for ${name}!"
        fail "  expected: ${expected}"
        fail "  got:      ${actual}"
        exit 1
    fi
}

DL_BASE="${REGISTRY}/api/release/download?channel=${CHANNEL}&version=${VERSION}&component="

# ── Download compiler ─────────────────────────────────────────────────────────

step "Downloading compiler"
COMPILER_TAR="${TMP_DIR}/compiler.tar.gz"
download "${DL_BASE}compiler" "$COMPILER_TAR"
verify_checksum "$COMPILER_TAR" "$compiler_checksum" "compiler"
ok "Downloaded compiler tarball"

# ── Download stdlib ───────────────────────────────────────────────────────────

step "Downloading stdlib"
STDLIB_TAR="${TMP_DIR}/stdlib.tar.gz"
download "${DL_BASE}stdlib" "$STDLIB_TAR"
verify_checksum "$STDLIB_TAR" "$stdlib_checksum" "stdlib"
ok "Downloaded stdlib tarball"

# ── Download linkle ───────────────────────────────────────────────────────────

step "Downloading Linkle"
LINKLE_TAR="${TMP_DIR}/linkle.tar.gz"
download "${DL_BASE}linkle" "$LINKLE_TAR"
verify_checksum "$LINKLE_TAR" "$linkle_checksum" "linkle"
ok "Downloaded linkle tarball"

# ── Create directories ────────────────────────────────────────────────────────

step "Creating directories"

for d in "$BIN_DIR" "$LIB_DIR" "$STD_DIR" "$PLATFORM_DIR" \
         "$STD_DIR/system" "$STD_DIR/networking"; do
    if [ ! -d "$d" ]; then
        maybe_sudo "$PREFIX" -- mkdir -p "$d"
        ok "Created $d"
    else
        dim "exists  $d"
    fi
done

# ── Extract and install compiler ──────────────────────────────────────────────

step "Installing compiler"

COMPILER_TMP="${TMP_DIR}/compiler"
mkdir -p "$COMPILER_TMP"
tar -xzf "$COMPILER_TAR" -C "$COMPILER_TMP"

COMPILER_BIN=$(find "$COMPILER_TMP" -type f -name "hylian" | head -1)
if [ -z "$COMPILER_BIN" ]; then
    fail "Could not find 'hylian' binary in compiler tarball."
    exit 1
fi

maybe_sudo "$BIN_DIR" -- cp "$COMPILER_BIN" "${BIN_DIR}/hylian"
maybe_sudo "$BIN_DIR" -- chmod 755 "${BIN_DIR}/hylian"
ok "hylian  →  ${BIN_DIR}/hylian"

# ── Extract and install stdlib ────────────────────────────────────────────────

step "Installing stdlib"

STDLIB_TMP="${TMP_DIR}/stdlib"
mkdir -p "$STDLIB_TMP"
tar -xzf "$STDLIB_TAR" -C "$STDLIB_TMP"

# The tarball contains a runtime/ tree; copy it into STD_DIR
RUNTIME_SRC=$(find "$STDLIB_TMP" -type d -name "runtime" | head -1)
if [ -z "$RUNTIME_SRC" ]; then
    fail "Could not find 'runtime/' directory in stdlib tarball."
    exit 1
fi

std_copied=0
while IFS= read -r -d '' obj; do
    rel="${obj#${RUNTIME_SRC}/std/}"
    dest="${STD_DIR}/${rel}"
    dest_dir="$(dirname "$dest")"
    if [ ! -d "$dest_dir" ]; then
        maybe_sudo "$STD_DIR" -- mkdir -p "$dest_dir"
    fi
    maybe_sudo "$dest_dir" -- cp "$obj" "$dest"
    std_copied=$((std_copied + 1))
done < <(find "${RUNTIME_SRC}/std" -name "*.o" -print0 2>/dev/null)

hyi_copied=0
while IFS= read -r -d '' hyi; do
    rel="${hyi#${RUNTIME_SRC}/std/}"
    dest="${STD_DIR}/${rel}"
    dest_dir="$(dirname "$dest")"
    if [ ! -d "$dest_dir" ]; then
        maybe_sudo "$STD_DIR" -- mkdir -p "$dest_dir"
    fi
    maybe_sudo "$dest_dir" -- cp "$hyi" "$dest"
    hyi_copied=$((hyi_copied + 1))
done < <(find "${RUNTIME_SRC}/std" -name "*.hyi" -print0 2>/dev/null)

plat_copied=0
while IFS= read -r -d '' obj; do
    fname="$(basename "$obj")"
    maybe_sudo "$PLATFORM_DIR" -- cp "$obj" "${PLATFORM_DIR}/${fname}"
    plat_copied=$((plat_copied + 1))
done < <(find "${RUNTIME_SRC}/platform" -maxdepth 1 \( -name "*.o" -o -name "*.ld" \) -print0 2>/dev/null)

ok "Stdlib modules:   ${std_copied} .o files  +  ${hyi_copied} .hyi files"
ok "Platform objects: ${plat_copied} files"

# ── Extract and install linkle ────────────────────────────────────────────────

step "Installing Linkle"

LINKLE_TMP="${TMP_DIR}/linkle"
mkdir -p "$LINKLE_TMP"
tar -xzf "$LINKLE_TAR" -C "$LINKLE_TMP"

LINKLE_PY=$(find "$LINKLE_TMP" -type f -name "linkle.py" | head -1)
if [ -z "$LINKLE_PY" ]; then
    fail "Could not find 'linkle.py' in linkle tarball."
    exit 1
fi

LINKLE_INSTALLED="${LIB_DIR}/linkle.py"
maybe_sudo "$LIB_DIR" -- cp "$LINKLE_PY" "$LINKLE_INSTALLED"
maybe_sudo "$LIB_DIR" -- chmod 644 "$LINKLE_INSTALLED"
ok "linkle.py  →  ${LINKLE_INSTALLED}"

# Write thin wrapper script
LINKLE_WRAPPER_TMP="$(mktemp)"
cat > "$LINKLE_WRAPPER_TMP" << WRAPPER_EOF
#!/usr/bin/env bash
# Auto-generated by install.sh — do not edit.
export HYLIAN_LIB="${LIB_DIR}"
exec python3 "${LINKLE_INSTALLED}" "\$@"
WRAPPER_EOF
maybe_sudo "$BIN_DIR" -- cp "$LINKLE_WRAPPER_TMP" "${BIN_DIR}/linkle"
rm -f "$LINKLE_WRAPPER_TMP"
maybe_sudo "$BIN_DIR" -- chmod 755 "${BIN_DIR}/linkle"
ok "linkle     →  ${BIN_DIR}/linkle"

# ── Persist installed version ─────────────────────────────────────────────────

CONFIG_DIR="${HOME}/.hylian"
mkdir -p "$CONFIG_DIR"
chmod 700 "$CONFIG_DIR"

# Update channel/version in config (preserve any existing token= line)
config_file="${CONFIG_DIR}/config"
touch "$config_file"

update_config() {
    local key="$1" val="$2"
    if grep -q "^${key}=" "$config_file" 2>/dev/null; then
        sed -i "s|^${key}=.*|${key}=${val}|" "$config_file"
    else
        echo "${key}=${val}" >> "$config_file"
    fi
}

update_config "channel" "$CHANNEL"
update_config "version" "$VERSION"
chmod 600 "$config_file"
ok "Saved version info to ${config_file}"

# ── Smoke test ────────────────────────────────────────────────────────────────

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

if [ -f "${PLATFORM_DIR}/linux.o" ] || [ -f "${PLATFORM_DIR}/kernel.o" ]; then
    ok "Platform objects present at ${PLATFORM_DIR}"
else
    warn "Platform directory appears empty — the stdlib tarball may be missing platform objects."
fi

# ── Done ──────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}${GREEN}══════════════════════════════════════════${NC}"
echo -e "${BOLD}${GREEN}  Hylian ${VERSION} installed successfully!${NC}"
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
echo -e "  To update later:"
echo -e "    ${DIM}linkle update${NC}"
echo ""

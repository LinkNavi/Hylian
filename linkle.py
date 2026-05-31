#!/usr/bin/env python3
"""
Linkle — Hylian build system

Supports single-package and workspace (multi-package) projects.

Workspace layout (like Cargo):
  linkle.hy           ← workspace root  (workspace { members: ["kernel", "init"] })
  kernel/
    linkle.hy         ← member package
    src/main.hy
  init/
    linkle.hy
    src/main.hy

Single-package layout (unchanged):
  linkle.hy
  src/main.hy
"""

import os
import re
import shutil
import subprocess
import sys
import time

# ── Terminal colours ──────────────────────────────────────────────────────────

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[1;31m"
GREEN = "\033[1;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[1;36m"

NASM_FORMATS = {
    "linux": "elf64",
    "macos": "macho64",
    "windows": "win64",
    "kernel": "elf64",
    "limine": "elf64",
}

VALID_TARGETS = ("linux", "macos", "windows", "kernel", "limine")

REGISTRY_URL = os.environ.get("HYLIAN_REGISTRY", "https://hylian.lol")


# ── Semver helpers ────────────────────────────────────────────────────────────


def _parse_semver(v):
    """Parse "MAJOR.MINOR.PATCH" into a (int, int, int) tuple, or None."""
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)$", v.strip())
    if not m:
        return None
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)))


def _semver_satisfies(installed_str, required_str):
    """
    Return True when `installed_str` satisfies the `required_str` constraint.

    Supported constraint prefixes:
      "1.2.3"   — exact match
      "^1.2.3"  — compatible (same major, installed >= required)
      "~1.2.3"  — patch-level (same major+minor, installed >= required)
      ">=1.2.3" — installed >= required
      ">1.2.3"  — installed > required
      "<=1.2.3" — installed <= required
      "<1.2.3"  — installed < required

    Returns True if the constraint cannot be parsed (fail-open so unknown
    formats don't break existing projects).
    """
    installed = _parse_semver(installed_str)
    if installed is None:
        return True  # can't compare, allow

    constraint = required_str.strip()

    if constraint.startswith("^"):
        req = _parse_semver(constraint[1:])
        if req is None:
            return True
        return installed[0] == req[0] and installed >= req

    if constraint.startswith("~"):
        req = _parse_semver(constraint[1:])
        if req is None:
            return True
        return installed[0] == req[0] and installed[1] == req[1] and installed >= req

    if constraint.startswith(">="):
        req = _parse_semver(constraint[2:])
        if req is None:
            return True
        return installed >= req

    if constraint.startswith(">"):
        req = _parse_semver(constraint[1:])
        if req is None:
            return True
        return installed > req

    if constraint.startswith("<="):
        req = _parse_semver(constraint[2:])
        if req is None:
            return True
        return installed <= req

    if constraint.startswith("<"):
        req = _parse_semver(constraint[1:])
        if req is None:
            return True
        return installed < req

    # Plain version — exact match
    req = _parse_semver(constraint)
    if req is None:
        return True
    return installed == req


def _best_version(versions, constraint):
    """
    Given a list of version strings (newest-first from the registry) and a
    semver constraint string, return the newest version that satisfies the
    constraint, or None if none do.
    """
    for v in versions:
        if _semver_satisfies(v, constraint):
            return v
    return None


def _token_path():
    """Return the path of the Hylian config file that stores the API token."""
    return os.path.expanduser("~/.hylian/config")


def _read_token() -> str:
    """
    Read the API token from ~/.hylian/config.
    The file uses key=value lines; we look for token=hylian_...
    Falls back to the old ~/.linkle_token file for backwards compat.
    """
    path = _token_path()
    if not os.path.exists(path):
        old = os.path.expanduser("~/.linkle_token")
        if os.path.exists(old):
            with open(old) as fh:
                return fh.read().strip()
        return ""
    with open(path) as fh:
        content = fh.read()
    for line in content.splitlines():
        line = line.strip()
        if line.startswith("token="):
            return line[len("token=") :].strip()
    return content.strip()


def _write_config(key: str, value: str):
    """Write or update a key=value line in ~/.hylian/config."""
    config_dir = os.path.expanduser("~/.hylian")
    os.makedirs(config_dir, mode=0o700, exist_ok=True)
    path = _token_path()
    lines = []
    found = False
    if os.path.exists(path):
        with open(path) as fh:
            for line in fh:
                stripped = line.strip()
                if stripped.startswith(f"{key}="):
                    lines.append(f"{key}={value}\n")
                    found = True
                else:
                    lines.append(line if line.endswith("\n") else line + "\n")
    if not found:
        lines.append(f"{key}={value}\n")
    with open(path, "w") as fh:
        fh.writelines(lines)
    os.chmod(path, 0o600)


def _read_config(key: str) -> str:
    """Read a key from ~/.hylian/config, returning '' if not found."""
    path = _token_path()
    if not os.path.exists(path):
        return ""
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line.startswith(f"{key}="):
                return line[len(f"{key}=") :].strip()
    return ""


def _supports_color():
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()


def c(code, text):
    return f"{code}{text}{RESET}" if _supports_color() else text


def ok(msg):
    print(f"  {c(GREEN, '✓')} {msg}")


def err(msg):
    print(f"  {c(RED, '✗')} {msg}", file=sys.stderr)


def warn(msg):
    print(f"  {c(YELLOW, '!')} {msg}")


def info(msg):
    print(f"  {c(CYAN, '»')} {msg}")


def dim(msg):
    print(f"  {c(DIM, msg)}")


def step(msg):
    print(f"\n{c(BOLD, msg)}")


def detect_platform():
    import platform

    s = platform.system()
    if s == "Darwin":
        return "macos"
    if s == "Windows":
        return "windows"
    return "linux"


# ── Script root ───────────────────────────────────────────────────────────────

HYLIAN_ROOT = os.path.dirname(os.path.abspath(__file__))


# ── Config ────────────────────────────────────────────────────────────────────


class ParseError(Exception):
    pass


class LinkleConfig:
    def __init__(self):
        self.project_name = ""
        self.project_version = "0.1.0"
        self.project_author = ""
        self.project_description = ""
        self.project_repo = ""

        self.build_src = "src"
        self.build_main = "main"
        self.build_out = "build"
        self.build_bin = None  # defaults to project_name at runtime
        self.build_libs = []
        self.build_flags = []
        # optional: override the build target per-package
        self.build_target = None  # e.g. "limine", "kernel"
        # optional: linker script (.ld) for freestanding packages
        self.build_linker_script = None

        # vendors: list of {"alias": str, "path": str}
        self.vendors = []

        # packages: list of {"name": str, "version": str}
        self.packages = []

        self.targets = {}

        # workspace: list of member directory names (relative to workspace root)
        # non-None means this config IS a workspace root, not a package
        self.workspace_members = None  # None = not a workspace


def _tokenize(src):
    tokens = []
    i = 0
    line = 1
    n = len(src)

    while i < n:
        ch = src[i]

        if ch in " \t\r":
            i += 1
            continue
        if ch == "\n":
            line += 1
            i += 1
            continue
        if ch == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                i += 1
            continue

        if ch == "{":
            tokens.append(("LBRACE", "{", line))
            i += 1
            continue
        if ch == "}":
            tokens.append(("RBRACE", "}", line))
            i += 1
            continue
        if ch == "(":
            tokens.append(("LPAREN", "(", line))
            i += 1
            continue
        if ch == ")":
            tokens.append(("RPAREN", ")", line))
            i += 1
            continue
        if ch == ":":
            tokens.append(("COLON", ":", line))
            i += 1
            continue
        if ch == ",":
            tokens.append(("COMMA", ",", line))
            i += 1
            continue
        if ch == "[":
            tokens.append(("LBRACKET", "[", line))
            i += 1
            continue
        if ch == "]":
            tokens.append(("RBRACKET", "]", line))
            i += 1
            continue
        if ch == ";":
            i += 1
            continue

        if ch == '"':
            j = i + 1
            result = []
            while j < n and src[j] != '"':
                if src[j] == "\\" and j + 1 < n:
                    esc = src[j + 1]
                    result.append({"n": "\n", "t": "\t", "r": "\r"}.get(esc, esc))
                    j += 2
                else:
                    result.append(src[j])
                    j += 1
            if j >= n:
                raise ParseError(f"line {line}: unterminated string")
            tokens.append(("STRING", "".join(result), line))
            i = j + 1
            continue

        if ch.isalpha() or ch == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] in "_.-"):
                j += 1
            tokens.append(("NAME", src[i:j], line))
            i = j
            continue

        raise ParseError(f"line {line}: unexpected character '{ch}'")

    tokens.append(("EOF", "", line))
    return tokens


class _Parser:
    def __init__(self, tokens):
        self._tok = tokens
        self._pos = 0

    def peek(self):
        return self._tok[self._pos]

    def advance(self):
        t = self._tok[self._pos]
        self._pos += 1
        return t

    def expect(self, kind, value=None):
        t = self.advance()
        if t[0] != kind:
            raise ParseError(f"line {t[2]}: expected {kind}, got {t[0]} '{t[1]}'")
        if value is not None and t[1] != value:
            raise ParseError(f"line {t[2]}: expected '{value}', got '{t[1]}'")
        return t

    def at(self, kind, value=None):
        t = self.peek()
        if t[0] != kind:
            return False
        if value is not None and t[1] != value:
            return False
        return True

    def parse(self):
        cfg = LinkleConfig()
        while not self.at("EOF"):
            t = self.peek()
            if t[0] != "NAME":
                raise ParseError(f"line {t[2]}: unexpected token '{t[1]}'")
            kw = t[1]
            self.advance()
            if kw == "project":
                self._parse_project(cfg)
            elif kw == "build":
                self._parse_build(cfg)
            elif kw == "vendors":
                self._parse_vendors(cfg)
            elif kw == "target":
                self._parse_target(cfg)
            elif kw == "workspace":
                self._parse_workspace(cfg)
            elif kw == "packages":
                self._parse_packages(cfg)
            else:
                raise ParseError(f"line {t[2]}: unknown block '{kw}'")
        return cfg

    def _parse_project(self, cfg):
        self.expect("LBRACE")
        while not self.at("RBRACE"):
            key = self.expect("NAME")[1]
            self.expect("COLON")
            val = self.expect("STRING")[1]
            if self.at("COMMA"):
                self.advance()
            if key == "name":
                cfg.project_name = val
            elif key == "version":
                cfg.project_version = val
            elif key == "author":
                cfg.project_author = val
            elif key == "description":
                cfg.project_description = val
            elif key == "repo":
                cfg.project_repo = val
        self.expect("RBRACE")

    def _parse_build(self, cfg):
        self.expect("LBRACE")
        while not self.at("RBRACE"):
            key = self.expect("NAME")[1]
            self.expect("COLON")
            if self.at("LBRACKET"):
                val = self._parse_str_array()
            else:
                val = self.expect("STRING")[1]
            if self.at("COMMA"):
                self.advance()
            if key == "src":
                cfg.build_src = val
            elif key == "main":
                cfg.build_main = val
            elif key == "out":
                cfg.build_out = val
            elif key == "bin":
                cfg.build_bin = val
            elif key == "libs":
                cfg.build_libs = val if isinstance(val, list) else [val]
            elif key == "flags":
                cfg.build_flags = val if isinstance(val, list) else [val]
            elif key == "target":
                cfg.build_target = val
            elif key == "linker_script":
                cfg.build_linker_script = val
        self.expect("RBRACE")

    def _parse_vendors(self, cfg):
        self.expect("LBRACE")
        while not self.at("RBRACE"):
            alias = self.expect("NAME")[1]
            self.expect("COLON")
            path = self.expect("STRING")[1]
            if self.at("COMMA"):
                self.advance()
            cfg.vendors.append({"alias": alias, "path": path})
        self.expect("RBRACE")

    def _parse_str_array(self):
        self.expect("LBRACKET")
        items = []
        while not self.at("RBRACKET"):
            items.append(self.expect("STRING")[1])
            if self.at("COMMA"):
                self.advance()
        self.expect("RBRACKET")
        return items

    def _parse_target(self, cfg):
        name = self.expect("NAME")[1]
        self.expect("LPAREN")
        self.expect("RPAREN")
        self.expect("LBRACE")
        cmds = []
        while not self.at("RBRACE"):
            fn = self.expect("NAME")[1]
            self.expect("LPAREN")
            cmd = self.expect("STRING")[1]
            self.expect("RPAREN")
            if self.at("COLON"):
                self.advance()
            cmds.append((fn, cmd))
        self.expect("RBRACE")
        cfg.targets[name] = cmds

    def _parse_packages(self, cfg):
        self.expect("LBRACE")
        while not self.at("RBRACE"):
            name = self.expect("NAME")[1]
            self.expect("COLON")
            version = self.expect("STRING")[1]
            if self.at("COMMA"):
                self.advance()
            cfg.packages.append({"name": name, "version": version})
        self.expect("RBRACE")

    def _parse_workspace(self, cfg):
        self.expect("LBRACE")
        while not self.at("RBRACE"):
            key = self.expect("NAME")[1]
            self.expect("COLON")
            if key == "members":
                cfg.workspace_members = self._parse_str_array()
            else:
                # unknown key — consume value and skip
                if self.at("LBRACKET"):
                    self._parse_str_array()
                else:
                    self.expect("STRING")
            if self.at("COMMA"):
                self.advance()
        self.expect("RBRACE")
        if cfg.workspace_members is None:
            cfg.workspace_members = []


def parse_config(path):
    with open(path, "r") as f:
        src = f.read()
    tokens = _tokenize(src)
    return _Parser(tokens).parse()


# ── Runtime / Hylian binary resolution ───────────────────────────────────────
#
# Resolution order for the runtime directory:
#   1. $HYLIAN_LIB  (set by the linkle wrapper installed to /usr/local/bin/linkle)
#   2. <script_dir>/runtime  (running from the source tree)
#   3. /usr/local/lib/hylian/std
#   4. /usr/lib/hylian/std
#   5. ~/.hylian/std
#
# When installed, the layout is:
#   /usr/local/lib/hylian/
#     std/           ← stdlib .o files (io.o, errors.o, …)
#       platform/    ← platform .o files (linux.o, kernel.o, limine.o, …)
#     linkle.py
#
# The key fix vs the old code: "platform/" is a subdirectory of std/, NOT of
# a sibling "runtime/" directory.  The old _find_runtime_dir returned
# "/usr/local/lib/hylian/std" and then _build_platform_obj looked for
# "{runtime_dir}/platform/linux.c" — which is correct IF platform/ exists
# under std/.  The install script was the bug: it never created platform/
# under std/ nor copied the .o files there.  Both are now fixed.


def _find_runtime_dir():
    # Honour the env var set by the installed wrapper script
    env = os.environ.get("HYLIAN_LIB")
    if env:
        std = os.path.join(env, "std")
        if os.path.isdir(std):
            return std
        # Maybe HYLIAN_LIB already points at std/ directly
        if os.path.isdir(env):
            return env

    # Source-tree layout: linkle.py sits next to runtime/
    src_tree = os.path.join(HYLIAN_ROOT, "runtime")
    if os.path.isdir(src_tree):
        return src_tree

    # System-wide installs
    for candidate in (
        "/usr/local/lib/hylian/std",
        "/usr/lib/hylian/std",
        os.path.expanduser("~/.hylian/std"),
    ):
        if os.path.isdir(candidate):
            return candidate

    # Fallback — will produce a clear error later
    return src_tree


def _platform_dir(runtime_dir):
    """
    Return the directory that holds platform .o files.

    In the source tree:  runtime/platform/
    When installed:      /usr/local/lib/hylian/std/platform/
    """
    return os.path.join(runtime_dir, "platform")


def _find_hylian_bin():
    candidates = [
        os.path.join(HYLIAN_ROOT, "hylian"),
        "/usr/local/bin/hylian",
        "/usr/bin/hylian",
        shutil.which("hylian"),
    ]
    for p in candidates:
        if p and os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    raise BuildError(
        "hylian compiler not found.\n"
        "  Run ./build.sh to compile it, or ./install.sh to install system-wide."
    )


# ── Shell helpers ─────────────────────────────────────────────────────────────


def _run(cmd, verbose, label=None, cwd=None, capture=False):
    if label:
        info(label)
    if verbose:
        dim("$ " + " ".join(cmd))
    if capture:
        return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    else:
        r = subprocess.run(cmd, cwd=cwd)
        if r.returncode != 0:
            raise BuildError(f"command failed: {' '.join(cmd)}")
        return r


class BuildError(Exception):
    pass


def _is_fresh(obj, src):
    if not os.path.exists(src):
        return True
    if not os.path.exists(obj):
        return False
    return os.path.getmtime(obj) >= os.path.getmtime(src)


# ── Stdlib module registry ────────────────────────────────────────────────────

STD_MODULES = [
    # std.mem is always linked — codegen unconditionally emits extern arena_init/arena_alloc/arena_free
    {"include": "__always__", "stem": "mem", "link_libs": []},
    {"include": "std.io", "stem": "io", "link_libs": []},
    {"include": "std.errors", "stem": "errors", "link_libs": []},
    {"include": "std.strings", "stem": "strings", "link_libs": []},
    {
        "include": "std.system.filesystem",
        "stem": os.path.join("system", "filesystem"),
        "link_libs": [],
    },
    {
        "include": "std.system.env",
        "stem": os.path.join("system", "env"),
        "link_libs": [],
    },
    {"include": "std.crypto", "stem": "crypto", "link_libs": ["-lssl", "-lcrypto"]},
    {
        "include": "std.networking.tcp",
        "stem": os.path.join("networking", "tcp"),
        "link_libs": [],
    },
    {
        "include": "std.networking.udp",
        "stem": os.path.join("networking", "udp"),
        "link_libs": [],
    },
    {
        "include": "std.networking.https",
        "stem": os.path.join("networking", "https"),
        "link_libs": ["-lssl", "-lcrypto"],
    },
    # std.kernel is provided by the platform object (kernel.o / limine.o),
    # not a separate std .o — no entry needed here.
]


def _runtime_obj(stem_rel, target, verbose):
    """
    Find or build a stdlib .o from stem_rel (e.g. "io", "networking/tcp").

    Looks inside the runtime dir returned by _find_runtime_dir().
    For installed toolchains that means /usr/local/lib/hylian/std/io.o etc.
    For source-tree builds that means runtime/std/io.o etc.

    The source-tree layout has files directly under runtime/std/ while the
    installed layout has them under the std root.  We try both.
    """
    runtime_dir = _find_runtime_dir()

    # Try std/ subdirectory (source-tree layout: runtime/std/io.o)
    candidates = [
        os.path.join(runtime_dir, "std", stem_rel),
        os.path.join(runtime_dir, stem_rel),  # installed layout
    ]

    for stem in candidates:
        obj_path = stem + ".o"
        c_path = stem + ".c"
        asm_path = stem + ".asm"

        if os.path.exists(obj_path):
            if _is_fresh(obj_path, c_path) and _is_fresh(obj_path, asm_path):
                try:
                    rel = os.path.relpath(obj_path)
                except:
                    rel = obj_path
                dim(f"pre-built  {rel}")
                return obj_path

        if os.path.exists(c_path):
            try:
                rel = os.path.relpath(c_path)
            except:
                rel = c_path
            gcc_flags = [
                "gcc",
                "-O2",
                "-c",
                "-ffreestanding",
                "-nostdlib",
                "-nostdinc",
                "-fno-builtin",
            ]
            # Add kernel-specific flags for freestanding targets
            if target in ("kernel", "limine"):
                gcc_flags.extend(["-mno-sse", "-mno-mmx", "-mno-red-zone"])
            gcc_flags.extend([c_path, "-o", obj_path])
            _run(
                gcc_flags,
                verbose,
                label=f"CC   {rel}",
            )
            return obj_path

        if os.path.exists(asm_path):
            try:
                rel = os.path.relpath(asm_path)
            except:
                rel = asm_path
            fmt = NASM_FORMATS.get(target, "elf64")
            _run(
                ["nasm", "-f", fmt, "-w-label-redef-late", asm_path, "-o", obj_path],
                verbose,
                label=f"ASM  {rel}",
            )
            return obj_path

    raise BuildError(
        f"stdlib module not found: {stem_rel}\n"
        f"  Looked in: {runtime_dir}\n"
        f"  Run ./build.sh (source) or ./install.sh to populate runtime objects."
    )


def _collect_runtime_objs(includes, target, verbose):
    needed = set(includes)
    objs = []
    extra_libs = []
    for mod in STD_MODULES:
        if mod["include"] != "__always__" and mod["include"] not in needed:
            continue
        objs.append(_runtime_obj(mod["stem"], target, verbose))
        for lib in mod.get("link_libs", []):
            if lib not in extra_libs:
                extra_libs.append(lib)
    return objs, extra_libs


def _build_platform_obj(target, verbose):
    """
    Locate and return the platform .o for `target`.

    Search order:
      1. <runtime_dir>/platform/<target>.o   (both source-tree and installed)
      2. Compile from <runtime_dir>/platform/<target>.c  if .o is stale

    For freestanding targets ("kernel", "limine") the platform object is
    kernel.o or limine.o respectively — NOT linux.o.
    """
    runtime_dir = _find_runtime_dir()
    plat_dir = _platform_dir(runtime_dir)

    # Map build target → platform filename stem
    # "kernel" and "limine" each have their own dedicated platform object.
    plat_stem = {
        "linux": "linux",
        "macos": "macos",
        "windows": "windows",
        "kernel": "kernel",
        "limine": "limine",
    }.get(target, target)

    platform_obj = os.path.join(plat_dir, plat_stem + ".o")
    platform_src = os.path.join(plat_dir, plat_stem + ".c")

    if os.path.exists(platform_obj) and _is_fresh(platform_obj, platform_src):
        try:
            rel = os.path.relpath(platform_obj)
        except:
            rel = platform_obj
        dim(f"pre-built  {rel}")
        return platform_obj

    if os.path.exists(platform_src):
        try:
            rel = os.path.relpath(platform_src)
        except:
            rel = platform_src
        gcc_flags = [
            "gcc",
            "-O2",
            "-c",
            "-ffreestanding",
            "-nostdlib",
            "-nostdinc",
            "-fno-builtin",
            "-fno-stack-protector",
        ]
        # Add kernel-specific flags for freestanding targets
        if target in ("kernel", "limine"):
            gcc_flags.extend(["-mno-sse", "-mno-mmx", "-mno-red-zone"])
        gcc_flags.extend([platform_src, "-o", platform_obj])
        _run(
            gcc_flags,
            verbose,
            label=f"CC   {rel}",
        )
        return platform_obj

    # Clear, actionable error message
    raise BuildError(
        f"no platform object for target '{target}': {platform_obj}\n"
        f"  Looked in: {plat_dir}\n"
        f"  Source tree:  run  python3 build_runtime.py\n"
        f"  Installed:    run  ./install.sh  (or re-run after build_runtime.py)"
    )


# ── Include scanner ───────────────────────────────────────────────────────────


def _scan_includes(hy_file):
    """Return all include paths from a .hy file (std.* and vendors.*)."""
    includes = []
    try:
        with open(hy_file, "r") as f:
            src = f.read()
        blocks = re.findall(r"include\s*\{([^}]*)\}", src, re.DOTALL)
        for block in blocks:
            for item in re.findall(r"[\w.]+", block):
                if item.startswith("std.") or item.startswith("vendors."):
                    includes.append(item)
    except Exception:
        pass
    return includes


# ── .hyi parser ───────────────────────────────────────────────────────────────


def _pkg_config_libs(pkg_name):
    """
    Run pkg-config for the given package name and return a list of linker flags.
    Falls back to ["-l<pkg_name>"] if pkg-config is unavailable or the package
    is not found, so builds degrade gracefully rather than hard-failing.
    """
    if not shutil.which("pkg-config"):
        warn(f"pkg-config not found; falling back to -l{pkg_name} for '{pkg_name}'")
        return [f"-l{pkg_name}"]

    try:
        r = subprocess.run(
            ["pkg-config", "--libs", pkg_name],
            capture_output=True,
            text=True,
        )
        if r.returncode != 0:
            warn(
                f"pkg-config --libs {pkg_name} failed "
                f"(exit {r.returncode}); falling back to -l{pkg_name}\n"
                f"  {r.stderr.strip()}"
            )
            return [f"-l{pkg_name}"]
        flags = r.stdout.strip().split()
        return flags if flags else [f"-l{pkg_name}"]
    except Exception as exc:
        warn(f"pkg-config error for '{pkg_name}': {exc}; falling back to -l{pkg_name}")
        return [f"-l{pkg_name}"]


def _parse_hyi(path):
    result = {"module": "", "link_libs": [], "externs": [], "classes": {}}
    if not os.path.exists(path):
        return result
    with open(path, "r") as f:
        src = f.read()
    src = re.sub(r"//[^\n]*", "", src)
    lines = src.splitlines()
    i = 0
    current_class = None
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line:
            continue
        m = re.match(r"^module\s+([\w.]+)$", line)
        if m:
            result["module"] = m.group(1)
            continue
        m = re.match(r'^link\s+"([^"]+)"$', line)
        if m:
            flag = f"-l:{m.group(1)}"
            if flag not in result["link_libs"]:
                result["link_libs"].append(flag)
            continue
        m = re.match(r'^pkg\s+"([^"]+)"$', line)
        if m:
            for flag in _pkg_config_libs(m.group(1)):
                if flag not in result["link_libs"]:
                    result["link_libs"].append(flag)
            continue
        # const NAME = VALUE — bindgen emits these for enum constants
        # linkle doesn't need to emit externs for consts (they're inlined),
        # but we must not let them fall through to the fn parser.
        if re.match(r"^const\s+\w+\s*=", line):
            continue
        # class Name {  or  class Name {}  (bindgen one-liner)
        # also skip "class const Foo {}" artefacts
        m = re.match(r"^class\s+(\w+)\s*\{?", line)
        if m:
            cname = m.group(1)
            if cname != "const":  # skip "class const ..."
                current_class = cname
                if current_class not in result["classes"]:
                    result["classes"][current_class] = []
                # one-liner "class Name {}" — immediately close
                if "{}" in line:
                    current_class = None
            continue
        if line == "}" and current_class is not None:
            current_class = None
            continue
        m = re.match(r"^fn\s+(\w+)\s*\(", line)
        if m:
            fn_name = m.group(1)
            if current_class is not None:
                sym = f"{current_class}_{fn_name}"
                result["classes"][current_class].append(sym)
                if sym not in result["externs"]:
                    result["externs"].append(sym)
            else:
                if fn_name not in result["externs"]:
                    result["externs"].append(fn_name)
            continue
    return result


# ── Vendor resolution ─────────────────────────────────────────────────────────


def _resolve_vendors(cfg, project_root, obj_dir, target, verbose):
    extra_objs = []
    extra_libs = []
    vendor_externs = {}
    if not cfg.vendors:
        return extra_objs, extra_libs, vendor_externs

    # ── Alias collision check ─────────────────────────────────────────────────
    seen_aliases = {}
    for vendor in cfg.vendors:
        a = vendor["alias"]
        if a in seen_aliases:
            raise BuildError(
                f"vendor alias '{a}' is declared twice in linkle.hy:\n"
                f"  first:  {seen_aliases[a]}\n"
                f"  second: {vendor['path']}\n"
                f"  Rename one of them to a unique alias."
            )
        seen_aliases[a] = vendor["path"]

    vendor_obj_dir = os.path.join(obj_dir, "vendors")
    os.makedirs(vendor_obj_dir, exist_ok=True)
    hylian_bin = _find_hylian_bin()

    # Collect all externs across every vendor so we can warn on symbol clashes.
    all_externs = {}  # symbol_name -> alias that first declared it

    for vendor in cfg.vendors:
        alias = vendor["alias"]
        vpath = os.path.join(project_root, vendor["path"])
        hyi_file = os.path.join(vpath, alias + ".hyi")
        hy_file = os.path.join(vpath, alias + ".hy")
        vendor_obj = os.path.join(vendor_obj_dir, alias + ".o")
        vendor_asm = os.path.join(vendor_obj_dir, alias + ".asm")

        if not os.path.isdir(vpath):
            raise BuildError(
                f"vendor '{alias}': directory not found: {vpath}\n"
                f"  Create it with the .hyi file (and optionally a .hy file)."
            )

        hyi = _parse_hyi(hyi_file)

        # ── Symbol collision check ────────────────────────────────────────────
        for sym in hyi["externs"]:
            if sym in all_externs:
                warn(
                    f"symbol conflict: '{sym}' is declared in both "
                    f"vendors.{all_externs[sym]} and vendors.{alias} — "
                    f"the linker will use whichever .so is found first"
                )
            else:
                all_externs[sym] = alias

        vendor_externs[f"vendors.{alias}"] = {
            "externs": hyi["externs"],
            "classes": hyi["classes"],
        }
        for lib in hyi["link_libs"]:
            if lib not in extra_libs:
                extra_libs.append(lib)

        if os.path.exists(hy_file):
            if _is_fresh(vendor_obj, hy_file):
                try:
                    rel = os.path.relpath(vendor_obj)
                except:
                    rel = vendor_obj
                dim(f"cached     {rel}")
            else:
                r = _run(
                    [
                        hylian_bin,
                        hy_file,
                        "-o",
                        vendor_asm,
                        "--src-dir",
                        vpath,
                        "--target",
                        target,
                    ],
                    verbose,
                    capture=True,
                    cwd=project_root,
                )
                if r.returncode != 0:
                    print(r.stdout)
                    print(r.stderr, file=sys.stderr)
                    raise BuildError(f"vendor '{alias}' compilation failed")
                fmt = NASM_FORMATS.get(target, "elf64")
                _run(
                    [
                        "nasm",
                        f"-f{fmt}",
                        "-w-label-redef-late",
                        vendor_asm,
                        "-o",
                        vendor_obj,
                    ],
                    verbose,
                    label=f"Assembling vendor {alias}",
                    cwd=project_root,
                )
            extra_objs.append(vendor_obj)

        # ── C source files (header-only shims, amalgamations, etc.) ──────────
        # Any .c file in the vendor directory is compiled with gcc -c and
        # linked in directly.  This covers three cases:
        #   1. Header-only libs:  a 2-line shim that #defines the impl macro
        #   2. Single-file amalgamations (sqlite3.c, miniaudio.c, etc.)
        #   3. Multi-file C libraries distributed as source
        # The .hyi has no `link` directive in these cases — everything is
        # compiled into .o files that get linked statically.
        import glob as _glob

        c_sources = sorted(_glob.glob(os.path.join(vpath, "*.c")))
        for c_src in c_sources:
            c_stem = os.path.splitext(os.path.basename(c_src))[0]
            c_obj = os.path.join(vendor_obj_dir, f"{alias}__{c_stem}.o")
            # Skip if already fresh
            if _is_fresh(c_obj, c_src):
                try:
                    rel = os.path.relpath(c_obj)
                except:
                    rel = c_obj
                dim(f"cached     {rel}")
            else:
                info(f"Compiling  {os.path.relpath(c_src, project_root)}")
                r = _run(
                    [
                        "gcc",
                        "-O2",
                        "-c",
                        "-I",
                        vpath,  # vendor dir on include path
                        c_src,
                        "-o",
                        c_obj,
                    ],
                    verbose,
                    cwd=project_root,
                )
            extra_objs.append(c_obj)

        if (
            not os.path.exists(hy_file)
            and not c_sources
            and not os.path.exists(hyi_file)
        ):
            raise BuildError(
                f"vendor '{alias}': no .hyi, .hy, or .c files found in {vpath}"
            )
    return extra_objs, extra_libs, vendor_externs


# ── Build pipeline ────────────────────────────────────────────────────────────


def cmd_build(cfg, target, verbose, project_root):
    """Build a single package.  Returns the path to the output binary."""

    # Per-package target override (e.g. kernel package uses "limine")
    effective_target = cfg.build_target or target

    step(f"Building {cfg.project_name} v{cfg.project_version}  [{effective_target}]")

    hylian_bin = _find_hylian_bin()
    bin_name = cfg.build_bin or cfg.project_name or "out"

    src_dir = os.path.join(project_root, cfg.build_src)
    out_dir = os.path.join(project_root, cfg.build_out)
    obj_dir = os.path.join(out_dir, "obj")
    bin_dir = os.path.join(out_dir, "bin")

    main_hy = os.path.join(src_dir, cfg.build_main + ".hy")
    out_asm = os.path.join(obj_dir, cfg.build_main + ".asm")
    out_obj = os.path.join(obj_dir, cfg.build_main + ".o")

    bin_ext = ".exe" if effective_target == "windows" else ""
    bin_out = os.path.join(bin_dir, bin_name + bin_ext)

    if not os.path.exists(main_hy):
        raise BuildError(f"entry point not found: {main_hy}")

    os.makedirs(obj_dir, exist_ok=True)
    os.makedirs(bin_dir, exist_ok=True)

    t0 = time.time()

    # ── Step 1: resolve vendors ───────────────────────────────────────────────
    vendor_objs, vendor_libs, _ = _resolve_vendors(
        cfg, project_root, obj_dir, effective_target, verbose
    )

    # ── Step 2: compile Hylian → ASM ─────────────────────────────────────────
    info(
        f"Compiling  {os.path.relpath(main_hy, project_root)} → {os.path.relpath(out_asm, project_root)}"
    )
    compile_cmd = [
        hylian_bin,
        main_hy,
        "-o",
        out_asm,
        "--src-dir",
        src_dir,
        "--target",
        effective_target,
    ]
    # Freestanding targets get the --freestanding flag automatically unless
    # the build_target was overridden in which case the .hy code decides.
    if effective_target in ("kernel", "limine"):
        compile_cmd.append("--freestanding")

    r = _run(compile_cmd, verbose, capture=True, cwd=project_root)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        raise BuildError("compilation failed")
    if verbose and r.stdout.strip():
        dim(r.stdout.strip())

    # ── Step 3: assemble ASM → .o ─────────────────────────────────────────────
    fmt = NASM_FORMATS.get(effective_target, "elf64")
    _run(
        ["nasm", f"-f{fmt}", "-w-label-redef-late", out_asm, "-o", out_obj],
        verbose,
        label=f"Assembling {os.path.relpath(out_asm, project_root)}",
        cwd=project_root,
    )

    # ── Step 4: resolve stdlib runtime objects ────────────────────────────────
    includes = _scan_includes(main_hy)
    runtime_objs, runtime_libs = _collect_runtime_objs(
        includes, effective_target, verbose
    )

    # ── Step 5: build platform layer ──────────────────────────────────────────
    platform_obj = _build_platform_obj(effective_target, verbose)

    # ── Step 6: link everything ───────────────────────────────────────────────
    info(f"Linking    {os.path.relpath(bin_out, project_root)}")

    if effective_target in ("kernel", "limine"):
        # Freestanding: link with ld, not gcc
        linker_script = cfg.build_linker_script
        if linker_script is None:
            # Look for the default linker script shipped with the runtime
            runtime_dir = _find_runtime_dir()
            plat_dir = _platform_dir(runtime_dir)
            default_ld = os.path.join(plat_dir, effective_target + ".ld")
            if os.path.exists(default_ld):
                linker_script = default_ld

        link_cmd = ["ld"]
        if linker_script:
            link_cmd += ["-T", linker_script]
        link_cmd += [out_obj, platform_obj]
        link_cmd += runtime_objs
        link_cmd += vendor_objs
        link_cmd += ["-o", bin_out]
    else:
        link_cmd = ["gcc", out_obj]
        link_cmd += runtime_objs
        link_cmd += vendor_objs
        link_cmd += [platform_obj, "-o", bin_out]
        link_cmd += runtime_libs
        link_cmd += vendor_libs
        link_cmd.append("-no-pie")
        if effective_target == "macos":
            link_cmd.remove("-no-pie")
        for lib in cfg.build_libs:
            link_cmd.append(f"-l{lib}")

    _run(link_cmd, verbose, cwd=project_root)

    elapsed = time.time() - t0
    ok(f"Built {os.path.relpath(bin_out, project_root)}  ({elapsed:.2f}s)")
    return bin_out


def cmd_run(cfg, target, verbose, project_root):
    effective_target = cfg.build_target or target
    if effective_target in ("kernel", "limine"):
        bin_out = cmd_build(cfg, target, verbose, project_root)
        warn(
            f"Target '{effective_target}' produces a bare-metal ELF — cannot run directly."
        )
        info(f"Output: {bin_out}")
        info("Boot it with QEMU, or burn to a disk image.")
        return

    bin_out = cmd_build(cfg, target, verbose, project_root)
    step("Running")
    r = subprocess.run([os.path.abspath(bin_out)], cwd=project_root)
    sys.exit(r.returncode)


def cmd_clean(cfg, project_root):
    out_dir = os.path.join(project_root, cfg.build_out)
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
        ok(f"Removed {os.path.relpath(out_dir, project_root)}")
    else:
        warn(
            f"Nothing to clean ({os.path.relpath(out_dir, project_root)} doesn't exist)"
        )


def cmd_target(cfg, target_name, verbose, project_root):
    if target_name not in cfg.targets:
        err(f"No target '{target_name}' in linkle.hy")
        _print_targets(cfg)
        sys.exit(1)
    step(f"Target: {target_name}")
    for fn, cmd_str in cfg.targets[target_name]:
        if fn == "exec":
            info(f"$ {cmd_str}")
            r = subprocess.run(cmd_str, shell=True, cwd=project_root)
            if r.returncode != 0:
                raise BuildError(
                    f"target '{target_name}' failed (exit {r.returncode}): {cmd_str}"
                )
        else:
            warn(f"Unknown target function '{fn}' — skipping")


# ── Workspace build ───────────────────────────────────────────────────────────


def cmd_workspace_build(ws_cfg, ws_root, target, verbose, run_after=False):
    """
    Build all members of a workspace in declaration order.
    Each member is an independent package with its own linkle.hy.
    """
    if not ws_cfg.workspace_members:
        warn("Workspace has no members declared.")
        return

    results = []
    failures = []

    for member in ws_cfg.workspace_members:
        member_root = os.path.join(ws_root, member)
        member_config = os.path.join(member_root, "linkle.hy")

        if not os.path.isdir(member_root):
            err(f"Workspace member '{member}': directory not found: {member_root}")
            failures.append(member)
            continue

        if not os.path.exists(member_config):
            err(f"Workspace member '{member}': no linkle.hy found at {member_config}")
            failures.append(member)
            continue

        try:
            cfg = parse_config(member_config)
        except ParseError as e:
            err(f"Workspace member '{member}': failed to parse linkle.hy: {e}")
            failures.append(member)
            continue

        # A workspace member must be a package, not another workspace
        if cfg.workspace_members is not None:
            err(f"Workspace member '{member}': nested workspaces are not supported")
            failures.append(member)
            continue

        try:
            bin_out = cmd_build(cfg, target, verbose, member_root)
            results.append((member, bin_out))
        except BuildError as e:
            err(f"Member '{member}' failed: {e}")
            failures.append(member)

    print()
    if failures:
        err(
            f"Workspace build failed: {len(failures)} member(s) failed: {', '.join(failures)}"
        )
        sys.exit(1)
    else:
        ok(f"Workspace build complete — {len(results)} package(s) built")
        for name, path in results:
            try:
                rel = os.path.relpath(path, ws_root)
            except:
                rel = path
            dim(f"  {name:20s}  →  {rel}")

    if run_after:
        if len(results) == 1:
            name, bin_out = results[0]
            step(f"Running {name}")
            r = subprocess.run([os.path.abspath(bin_out)], cwd=ws_root)
            sys.exit(r.returncode)
        else:
            warn(
                "--run: multiple members built; use 'linkle run' inside a member directory."
            )


def cmd_workspace_clean(ws_cfg, ws_root):
    for member in ws_cfg.workspace_members or []:
        member_root = os.path.join(ws_root, member)
        member_config = os.path.join(member_root, "linkle.hy")
        if not os.path.exists(member_config):
            continue
        try:
            cfg = parse_config(member_config)
            cmd_clean(cfg, member_root)
        except Exception:
            pass


# ── New project scaffolding ───────────────────────────────────────────────────

_LINKLE_HY_TEMPLATE = """\
project {{
    name: "{name}",
    version: "0.1.0",
    author: "",
}}

build {{
    src: "src",
    main: "main",
    out: "build",
    bin: "{name}",
}}

// Declare native or Hylian vendor packages here.
// vendors {{
//     sdl2: "vendors/sdl2",
// }}

// Declare registry packages here.
// packages {{
//     mylib: "1.0.0",
// }}

target run() {{
    exec("./build/bin/{name}");
}}

target clean() {{
    exec("rm -rf build");
}}
"""

_LINKLE_HY_KERNEL_TEMPLATE = """\
project {{
    name: "{name}",
    version: "0.1.0",
    author: "",
}}

// Build target: "limine" compiles a Limine-bootable ELF64.
// Use "kernel" for a plain freestanding ELF (BIOS/Multiboot).
build {{
    src: "src",
    main: "main",
    out: "build",
    bin: "{name}",
    target: "limine",
}}

target clean() {{
    exec("rm -rf build");
}}
"""

_LINKLE_HY_USERLAND_TEMPLATE = """\
project {{
    name: "{name}",
    version: "0.1.0",
    author: "",
}}

build {{
    src: "src",
    main: "main",
    out: "build",
    bin: "{name}",
}}

target run() {{
    exec("./build/bin/{name}");
}}

target clean() {{
    exec("rm -rf build");
}}
"""

_WORKSPACE_TEMPLATE = """\
// Workspace root — lists all packages in this project.
// Each entry is a directory containing its own linkle.hy.
//
// Build all:    linkle build
// Clean all:    linkle clean
// Build one:    cd <member> && linkle build
workspace {{
    members: [{members}],
}}
"""

_MAIN_HY_TEMPLATE = """\
include {{
    std.io,
}}

void main() {{
    println("Hello from {name}!");
}}
"""

_KERNEL_MAIN_HY_TEMPLATE = """\
include {{
    std.kernel,
}}

// Kernel entry point — loaded by Limine at 0xFFFFFFFF80100000.
// Receives no arguments; Limine fills in protocol response pointers before
// calling _start (which the compiler maps to this function).
void main() {{
    vga_clear();
    vga_set_color(0x0F);    // bright white on black
    println("{name} kernel booting...");
    vga_set_color(0x07);    // restore default
    println("Done. Halting.");
    while (1 == 1) {{
        cli();
        halt();
    }}
}}
"""

_GITIGNORE_TEMPLATE = """\
build/
*.o
*.asm
*.elf
"""

_VENDORS_README = """\
# vendors/

Place vendor packages here. Each package lives in its own subdirectory:

```
vendors/
  sdl2/
    sdl2.hyi    ← native .so wrapper (link + extern declarations)
    sdl2.hy     ← optional pure-Hylian helpers (compiled + cached)
```

Then declare it in `linkle.hy`:

```
vendors {
    sdl2: "vendors/sdl2",
}
```

And include it in your source:

```
include {
    vendors.sdl2,
}
```
"""


def cmd_new(name):
    project_dir = os.path.join(os.getcwd(), name)
    if os.path.exists(project_dir):
        err(f"Directory '{name}' already exists")
        sys.exit(1)

    src_dir = os.path.join(project_dir, "src")
    vendors_dir = os.path.join(project_dir, "vendors")
    os.makedirs(src_dir)
    os.makedirs(vendors_dir)

    with open(os.path.join(project_dir, "linkle.hy"), "w") as f:
        f.write(_LINKLE_HY_TEMPLATE.format(name=name))
    with open(os.path.join(src_dir, "main.hy"), "w") as f:
        f.write(_MAIN_HY_TEMPLATE.format(name=name))
    with open(os.path.join(project_dir, ".gitignore"), "w") as f:
        f.write(_GITIGNORE_TEMPLATE)
    with open(os.path.join(vendors_dir, "README.md"), "w") as f:
        f.write(_VENDORS_README)

    step(f"Created project '{name}'")
    ok(f"{name}/")
    info("  linkle.hy")
    info("  src/main.hy")
    info("  vendors/")
    info("  .gitignore")
    print()
    print("  Get started:")
    print(f"    cd {name} && linkle run")
    print()


def cmd_new_workspace(name, members=None):
    """
    Scaffold a workspace with optional member packages.

    Default members are "kernel" (limine target) and "init" (linux userland).
    The user can pass custom member names via --members a,b,c.
    """
    if members is None:
        members = ["kernel", "init"]

    ws_dir = os.path.join(os.getcwd(), name)
    if os.path.exists(ws_dir):
        err(f"Directory '{name}' already exists")
        sys.exit(1)

    os.makedirs(ws_dir)

    # Workspace root linkle.hy
    members_str = ", ".join(f'"{m}"' for m in members)
    with open(os.path.join(ws_dir, "linkle.hy"), "w") as f:
        f.write(_WORKSPACE_TEMPLATE.format(members=members_str))

    with open(os.path.join(ws_dir, ".gitignore"), "w") as f:
        f.write(_GITIGNORE_TEMPLATE)

    # Scaffold each member
    for i, member in enumerate(members):
        member_dir = os.path.join(ws_dir, member)
        member_src_dir = os.path.join(member_dir, "src")
        member_vend_dir = os.path.join(member_dir, "vendors")
        os.makedirs(member_src_dir)
        os.makedirs(member_vend_dir)

        # First member defaults to kernel, rest default to userland
        is_kernel = i == 0

        tmpl_cfg = (
            _LINKLE_HY_KERNEL_TEMPLATE if is_kernel else _LINKLE_HY_USERLAND_TEMPLATE
        )
        tmpl_main = _KERNEL_MAIN_HY_TEMPLATE if is_kernel else _MAIN_HY_TEMPLATE

        with open(os.path.join(member_dir, "linkle.hy"), "w") as f:
            f.write(tmpl_cfg.format(name=member))
        with open(os.path.join(member_src_dir, "main.hy"), "w") as f:
            f.write(tmpl_main.format(name=member))
        with open(os.path.join(member_vend_dir, "README.md"), "w") as f:
            f.write(_VENDORS_README)

    step(f"Created workspace '{name}'")
    ok(f"{name}/  (workspace root)")
    ok(f"  linkle.hy")
    for i, member in enumerate(members):
        label = "kernel (limine)" if i == 0 else "userland (linux)"
        info(f"  {member}/  ← {label}")
        info(f"    linkle.hy")
        info(f"    src/main.hy")
    print()
    print("  Build all members:")
    print(f"    cd {name} && linkle build")
    print()
    print("  Build one member:")
    print(f"    cd {name}/{members[0]} && linkle build")
    print()
    print("  Add more members:")
    print(f"    mkdir {name}/myservice && linkle new myservice  # then move it in")
    print(f'    # add "myservice" to members in {name}/linkle.hy')
    print()


def cmd_vendor_new(vendor_name, project_root, cfg):
    vend_dir = os.path.join(project_root, "vendors", vendor_name)
    if os.path.exists(vend_dir):
        err(f"Vendor '{vendor_name}' already exists at {vend_dir}")
        sys.exit(1)
    os.makedirs(vend_dir)

    hyi_content = f"""\
// Hylian vendor interface — vendors.{vendor_name}
//
// Usage:
//   include {{
//       vendors.{vendor_name},
//   }}

module vendors.{vendor_name}

// link "lib{vendor_name}.so"

// fn exampleFn(arg: int) -> int
"""
    with open(os.path.join(vend_dir, vendor_name + ".hyi"), "w") as f:
        f.write(hyi_content)

    ok(f"Created vendors/{vendor_name}/")
    info(f"  vendors/{vendor_name}/{vendor_name}.hyi")
    print()
    print(f"  Add to linkle.hy:")
    print(f"    vendors {{")
    print(f'        {vendor_name}: "vendors/{vendor_name}",')
    print(f"    }}")
    print()
    print(f"  Include in your source:")
    print(f"    include {{")
    print(f"        vendors.{vendor_name},")
    print(f"    }}")


# ── Registry commands ───────────────────────────────────────────────────────


def cmd_login(token_str):
    """Save a registry API token to ~/.hylian/config."""
    token_str = token_str.strip()
    if not token_str.startswith("hylian_"):
        err("Invalid token -- must start with 'hylian_'")
        sys.exit(1)
    _write_config("token", token_str)
    ok(f"Token saved to {_token_path()}")


def _fetch_and_install_pkg(
    pkg_name, pkg_version_constraint, project_root, cfg, _visited=None
):
    """
    Core of `linkle add`: download one package, extract it, update linkle.hy,
    then recursively install any transitive dependencies declared in deps.json.

    `pkg_version_constraint` is either None (use latest), an exact version
    string like "1.2.3", or a constraint like "^1.2.0".

    `_visited` is a set of package names already processed in this run,
    used to break dependency cycles.

    Returns the resolved version string that was installed.
    """
    import io
    import json
    import tarfile
    import urllib.error
    import urllib.request

    if _visited is None:
        _visited = set()

    if pkg_name in _visited:
        dim(f"  (skipping {pkg_name} — already processed this run)")
        return None
    _visited.add(pkg_name)

    step(f"Resolving {pkg_name}")

    # ── Fetch registry metadata ───────────────────────────────────────────────
    meta_url = f"{REGISTRY_URL}/api/packages/{pkg_name}"
    try:
        with urllib.request.urlopen(meta_url) as resp:
            meta = json.loads(resp.read())
    except urllib.error.HTTPError as e:
        if e.code == 404:
            err(f"Package '{pkg_name}' not found in registry")
        else:
            err(f"Registry error: {e.code}")
        sys.exit(1)
    except Exception as e:
        err(f"Could not reach registry: {e}")
        sys.exit(1)

    available_versions = [v["version"] for v in meta.get("versions", [])]
    if not available_versions:
        err(f"Package '{pkg_name}' has no published versions")
        sys.exit(1)

    # ── Version resolution ────────────────────────────────────────────────────
    if pkg_version_constraint is None:
        # No constraint — use latest
        pkg_version = available_versions[0]
    else:
        # Try to find the best match for the constraint
        pkg_version = _best_version(available_versions, pkg_version_constraint)
        if pkg_version is None:
            err(
                f"No version of '{pkg_name}' satisfies constraint "
                f"'{pkg_version_constraint}' (available: {', '.join(available_versions)})"
            )
            sys.exit(1)

    # ── Version conflict check against what's already declared ───────────────
    existing_pkg = next((p for p in cfg.packages if p["name"] == pkg_name), None)
    if existing_pkg:
        installed_ver = existing_pkg["version"]
        if _semver_satisfies(installed_ver, pkg_version_constraint or pkg_version):
            # Already at a compatible version — skip download but still check deps
            info(
                f"{pkg_name} {installed_ver} already satisfies '{pkg_version_constraint or pkg_version}'"
            )
            pkg_version = installed_ver
        elif pkg_version_constraint is not None:
            # User explicitly requested a version — allow upgrade, warn and proceed
            warn(f"Upgrading {pkg_name}: {installed_ver} -> {pkg_version}")
        else:
            # Transitive dep conflict — block it
            err(
                f"Version conflict for '{pkg_name}':\n"
                f"  already declared: {installed_ver}\n"
                f"  required:         {pkg_version}\n"
                f"  Run: linkle add {pkg_name}@{pkg_version} to upgrade explicitly."
            )
            sys.exit(1)

    info(f"Downloading {pkg_name} v{pkg_version}")

    # ── Download tarball ──────────────────────────────────────────────────────
    dl_url = f"{REGISTRY_URL}/api/packages/{pkg_name}/{pkg_version}/download"
    try:
        with urllib.request.urlopen(dl_url) as resp:
            tarball_bytes = resp.read()
    except urllib.error.HTTPError as e:
        err(f"Download failed: {e.code}")
        sys.exit(1)
    except Exception as e:
        err(f"Could not reach registry: {e}")
        sys.exit(1)

    # ── Extract tarball ───────────────────────────────────────────────────────
    vendors_dir = os.path.join(project_root, "vendors", pkg_name)
    os.makedirs(vendors_dir, exist_ok=True)

    sub_vendor_aliases = []  # non-empty => multi-vendor package
    transitive_deps = {}  # {name: constraint} from deps.json

    with tarfile.open(fileobj=io.BytesIO(tarball_bytes), mode="r:gz") as tar:
        # Pass 1 — read manifests (vendors.list and deps.json)
        for member in tar.getmembers():
            parts = member.name.split("/", 1)
            inner = parts[1] if len(parts) > 1 else parts[0]
            if inner == "vendors.list":
                f = tar.extractfile(member)
                if f:
                    sub_vendor_aliases = [
                        line.strip()
                        for line in f.read().decode().splitlines()
                        if line.strip()
                    ]
            elif inner == "deps.json":
                f = tar.extractfile(member)
                if f:
                    try:
                        transitive_deps = json.loads(f.read().decode())
                    except Exception:
                        warn(
                            f"{pkg_name}: could not parse deps.json — skipping transitive deps"
                        )

        # Pass 2 — extract real source files
        for member in tar.getmembers():
            parts = member.name.split("/", 1)
            inner = parts[1] if len(parts) > 1 and parts[1] else parts[0]
            if inner in ("vendors.list", "deps.json") or not inner:
                continue
            member.name = inner
            tar.extract(member, vendors_dir)

    ok(f"Extracted to vendors/{pkg_name}/")

    # ── Alias collision check ─────────────────────────────────────────────────
    existing_aliases = {v["alias"]: v["path"] for v in cfg.vendors}

    # Paths this package owns
    pkg_owned_prefixes = (
        f"vendors/{pkg_name}/",
        f"vendors/{pkg_name}",
    )

    if sub_vendor_aliases:
        collisions = []
        for alias in sub_vendor_aliases:
            if alias in existing_aliases:
                existing_path = existing_aliases[alias]
                expected_path = f"vendors/{pkg_name}/{alias}"
                if existing_path != expected_path and not any(
                    existing_path.startswith(p) for p in pkg_owned_prefixes
                ):
                    collisions.append((alias, existing_path))

        if collisions:
            err(f"Alias collision(s) installing '{pkg_name}':")
            for alias, existing_path in collisions:
                err(f"  alias '{alias}' is already used by: {existing_path}")
            err(
                f"  Rename the conflicting entry in your vendors {{}} block to a "
                f"unique alias before adding this package."
            )
            sys.exit(1)
    else:
        if pkg_name in existing_aliases:
            existing_path = existing_aliases[pkg_name]
            expected_path = f"vendors/{pkg_name}"
            if existing_path != expected_path and not any(
                existing_path.startswith(p) for p in pkg_owned_prefixes
            ):
                err(
                    f"Alias collision: '{pkg_name}' is already used by: {existing_path}\n"
                    f"  Rename that entry in your vendors {{}} block to a unique alias."
                )
                sys.exit(1)

    # ── Update linkle.hy ──────────────────────────────────────────────────────
    config_path = os.path.join(project_root, "linkle.hy")
    with open(config_path, "r") as f:
        src = f.read()

    # packages block
    already_in_packages = any(p["name"] == pkg_name for p in cfg.packages)
    if not already_in_packages:
        new_pkg_entry = f'    {pkg_name}: "{pkg_version}",\n'
        if re.search(r"packages\s*\{", src):
            src = re.sub(
                r"(packages\s*\{[^}]*?)(\})",
                lambda m: m.group(1) + new_pkg_entry + m.group(2),
                src,
                count=1,
                flags=re.DOTALL,
            )
        else:
            src += f"\npackages {{\n{new_pkg_entry}}}\n"
    else:
        # Update version in-place if upgrading
        src = re.sub(
            rf'(\b{re.escape(pkg_name)}\s*:\s*")[^"]*(")',
            lambda m: m.group(1) + pkg_version + m.group(2),
            src,
            count=1,
        )

    # vendors block
    added_aliases = []
    if sub_vendor_aliases:
        new_vend_entries = ""
        for alias in sub_vendor_aliases:
            expected_path = f"vendors/{pkg_name}/{alias}"
            if alias not in existing_aliases:
                new_vend_entries += f'    {alias}: "{expected_path}",\n'
                added_aliases.append(alias)
            elif existing_aliases[alias] != expected_path:
                # Update path in-place (e.g. upgrading from direct to sub-vendor layout)
                src = re.sub(
                    rf'(\b{re.escape(alias)}\s*:\s*")[^"]*(")',
                    lambda m, p=expected_path: m.group(1) + p + m.group(2),
                    src,
                    count=1,
                )
                added_aliases.append(alias)
            else:
                added_aliases.append(alias)
        if new_vend_entries:
            if re.search(r"vendors\s*\{", src):
                src = re.sub(
                    r"(vendors\s*\{[^}]*?)(\})",
                    lambda m: m.group(1) + new_vend_entries + m.group(2),
                    src,
                    count=1,
                    flags=re.DOTALL,
                )
            else:
                src += f"\nvendors {{\n{new_vend_entries}}}\n"
    else:
        if pkg_name not in existing_aliases:
            vend_entry = f'    {pkg_name}: "vendors/{pkg_name}",\n'
            if re.search(r"vendors\s*\{", src):
                src = re.sub(
                    r"(vendors\s*\{[^}]*?)(\})",
                    lambda m: m.group(1) + vend_entry + m.group(2),
                    src,
                    count=1,
                    flags=re.DOTALL,
                )
            else:
                src += f"\nvendors {{\n{vend_entry}}}\n"
        added_aliases = [pkg_name]

    with open(config_path, "w") as f:
        f.write(src)

    if not already_in_packages:
        ok(f"Added {pkg_name} v{pkg_version} to linkle.hy")
    else:
        info(f"{pkg_name} already in packages, updated vendors entries")

    for alias in added_aliases:
        ok(f"Done — include it with: include {{ vendors.{alias}, }}")

    # ── Transitive dependencies ───────────────────────────────────────────────
    if transitive_deps:
        info(f"Resolving {len(transitive_deps)} transitive dep(s) for {pkg_name}…")
        # Re-read cfg so each recursive call sees the latest linkle.hy state
        updated_cfg = parse_config(config_path)
        for dep_name, dep_constraint in transitive_deps.items():
            if not re.match(r"^[a-zA-Z][a-zA-Z0-9_\-.]{1,63}$", dep_name):
                warn(
                    f"  skipping invalid dep name '{dep_name}' from {pkg_name}/deps.json"
                )
                continue
            _fetch_and_install_pkg(
                dep_name,
                dep_constraint or None,
                project_root,
                updated_cfg,
                _visited,
            )
            # Refresh cfg again after each dep so alias/package sets stay current
            updated_cfg = parse_config(config_path)

    return pkg_version


def cmd_add(pkg_spec, project_root, cfg):
    """
    Download a package from the registry and add it to linkle.hy.

    pkg_spec is either "name" (fetches latest) or "name@1.2.3".
    """
    if "@" in pkg_spec:
        pkg_name, pkg_version_constraint = pkg_spec.split("@", 1)
    else:
        pkg_name = pkg_spec
        pkg_version_constraint = None

    # Validate name
    if not re.match(r"^[a-zA-Z][a-zA-Z0-9_\-.]{1,63}$", pkg_name):
        err(f"Invalid package name: {pkg_name}")
        sys.exit(1)

    _fetch_and_install_pkg(pkg_name, pkg_version_constraint, project_root, cfg)


def cmd_publish(project_root, cfg):
    """
    Package the current project and publish it to the registry.

    The tarball contains:
      <name>/          <- top-level directory named after the package
        *.hyi
        *.hy
        *.c
        *.h
    """
    import io
    import json
    import tarfile
    import urllib.error
    import urllib.request

    # Load token
    token = _read_token()
    if not token:
        err("Not logged in. Run: linkle login <token>")
        err("Get a token at https://www.hylian.lol/account")
        sys.exit(1)

    name = cfg.project_name
    version = cfg.project_version
    desc = cfg.project_description
    repo = cfg.project_repo

    if not name:
        err("project.name is empty in linkle.hy")
        sys.exit(1)
    if not version:
        err("project.version is empty in linkle.hy")
        sys.exit(1)

    step(f"Publishing {name} v{version}")

    # Collect files to publish.
    #
    # Two modes:
    #   1. Vendor/library project: has a `vendors {}` block in linkle.hy.
    #      Pack every declared sub-vendor directory (vendors/<alias>/) so
    #      consumers get all .hyi / .hy / .c / .h files they need.
    #      Also include a "vendors.list" manifest so cmd_add knows the aliases.
    #
    #   2. Plain application/library: pack src/ + any root-level .hyi/.h files
    #      (original behaviour).
    src_dir = os.path.join(project_root, cfg.build_src)
    files = []  # list of (filesystem_path, arcname_inside_tarball)
    vendor_aliases = []  # populated in mode 1
    deps_bytes = None  # populated in mode 1 if the package has deps

    if cfg.vendors:
        # Mode 1 — vendor package: ship each declared sub-vendor directory.
        for vend in cfg.vendors:
            alias = vend["alias"]
            vpath = os.path.join(project_root, vend["path"])
            if not os.path.isdir(vpath):
                warn(f"vendor '{alias}' path not found, skipping: {vpath}")
                continue
            vendor_aliases.append(alias)
            for root, _, fnames in os.walk(vpath):
                for fn in fnames:
                    if fn.endswith((".hy", ".hyi", ".c", ".h")):
                        fspath = os.path.join(root, fn)
                        # arcname: <pkg_name>/<alias>/...
                        rel = os.path.relpath(fspath, vpath)
                        files.append((fspath, os.path.join(name, alias, rel)))

        if not files:
            err("No vendor source files found to publish (check your vendors {} block)")
            sys.exit(1)

        # Embed a deps.json if this package itself depends on registry packages.
        # Format: { "pkgname": "^1.0.0", ... }
        if cfg.packages:
            import json as _json

            deps_dict = {p["name"]: p["version"] for p in cfg.packages}
            deps_bytes = _json.dumps(deps_dict, indent=2).encode()
            # Will be added to the tarball below alongside vendors.list
        else:
            deps_bytes = None
    else:
        # Mode 2 — plain project: pack src/ + root .hyi/.h files.
        for root, _, fnames in os.walk(src_dir):
            for fn in fnames:
                if fn.endswith((".hy", ".hyi", ".c", ".h")):
                    fspath = os.path.join(root, fn)
                    files.append(
                        (
                            fspath,
                            os.path.join(name, os.path.relpath(fspath, project_root)),
                        )
                    )

        for fn in os.listdir(project_root):
            fpath = os.path.join(project_root, fn)
            if os.path.isfile(fpath) and fn.endswith((".hyi", ".h")):
                files.append((fpath, os.path.join(name, fn)))

        if not files:
            err("No source files found to publish")
            sys.exit(1)

    info(f"Packing {len(files)} file(s)")

    # Build tarball in memory
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for fspath, arcname in files:
            tar.add(fspath, arcname=arcname)

        # If this is a vendor package, embed a small manifest listing the
        # sub-vendor aliases so cmd_add can register them all without having
        # to parse any .hyi files.
        if vendor_aliases:
            manifest = "\n".join(vendor_aliases) + "\n"
            manifest_bytes = manifest.encode()
            info_obj = tarfile.TarInfo(name=f"{name}/vendors.list")
            info_obj.size = len(manifest_bytes)
            tar.addfile(info_obj, io.BytesIO(manifest_bytes))

        # Embed deps.json if this package has registry dependencies.
        if vendor_aliases and deps_bytes:
            deps_info = tarfile.TarInfo(name=f"{name}/deps.json")
            deps_info.size = len(deps_bytes)
            tar.addfile(deps_info, io.BytesIO(deps_bytes))

    tarball_bytes = buf.getvalue()

    info(f"Tarball size: {len(tarball_bytes)} bytes")

    # Build multipart body manually (no external deps)
    boundary = "----HylianBoundary" + os.urandom(8).hex()
    body_parts = []
    for field, value in [
        ("name", name),
        ("version", version),
        ("description", desc),
        ("repo", repo),
    ]:
        body_parts.append(
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="{field}"\r\n\r\n'
            f"{value}\r\n"
        )
    # tarball part
    body_parts.append(
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="tarball"; filename="{name}-{version}.tar.gz"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    )
    body = b"".join(p.encode() if isinstance(p, str) else p for p in body_parts)
    body += tarball_bytes
    body += f"\r\n--{boundary}--\r\n".encode()

    req = urllib.request.Request(
        f"{REGISTRY_URL}/api/packages",
        data=body,
        headers={
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Authorization": f"Bearer {token}",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read())
        ok(f"Published {data['name']} v{data['version']}")
    except urllib.error.HTTPError as e:
        body_bytes = e.read()
        try:
            msg = json.loads(body_bytes).get("error", body_bytes.decode())
        except Exception:
            msg = body_bytes.decode()
        err(f"Publish failed ({e.code}): {msg}")
        sys.exit(1)
    except Exception as e:
        err(f"Could not reach registry: {e}")
        sys.exit(1)


# ── Help ──────────────────────────────────────────────────────────────────────


def cmd_update(channel: str = "stable"):
    """
    Pull the latest toolchain release from hylian.lol and install it.
    channel: 'stable' (default) or 'nightly'
    Nightly requires explicit opt-in -- it is never applied automatically.
    """
    import hashlib
    import io
    import json as _json
    import shutil
    import tarfile
    import urllib.error
    import urllib.request

    if channel not in ("stable", "nightly"):
        err(f"Unknown channel '{channel}'. Use: stable | nightly")
        sys.exit(1)

    step(f"Checking for updates ({channel} channel)")

    meta_url = f"{REGISTRY_URL}/api/release/latest?channel={channel}"
    try:
        with urllib.request.urlopen(meta_url) as resp:
            meta = _json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        if e.code == 404:
            err(f"No {channel} release found on the server.")
        else:
            err(f"Could not reach registry ({e.code}): {e.reason}")
        sys.exit(1)
    except Exception as ex:
        err(f"Could not reach registry: {ex}")
        sys.exit(1)

    remote_version = meta["version"]
    info(f"Latest {channel}: {remote_version}")

    local_version = _read_config("version") or ""
    if local_version == remote_version:
        ok(f"Already up-to-date ({remote_version})")
        return

    if local_version:
        info(f"Updating  {local_version}  ->  {remote_version}")
    else:
        info(f"Installing {remote_version}")

    checksums = {c_["component"]: c_["checksum"] for c_ in meta.get("components", [])}
    hylian_lib = os.environ.get("HYLIAN_LIB", "/usr/local/lib/hylian")
    bin_dir = os.path.dirname(shutil.which("linkle") or "/usr/local/bin/linkle")

    def _dl(component):
        dl_url = (
            f"{REGISTRY_URL}/api/release/download"
            f"?channel={channel}&version={remote_version}&component={component}"
        )
        info(f"Downloading {component}...")
        try:
            with urllib.request.urlopen(dl_url) as r:
                data = r.read()
        except Exception as ex:
            err(f"Download failed for {component}: {ex}")
            sys.exit(1)
        expected = checksums.get(component)
        if expected:
            actual = hashlib.sha256(data).hexdigest()
            if actual != expected:
                err(
                    f"Checksum mismatch for {component}: expected {expected}, got {actual}"
                )
                sys.exit(1)
            ok(f"{component} checksum OK")
        return data

    def _ex(data, dest_dir):
        import io as _io

        buf = _io.BytesIO(data)
        with tarfile.open(fileobj=buf, mode="r:gz") as tar:
            tar.extractall(dest_dir)

    import tempfile

    with tempfile.TemporaryDirectory(prefix="hylian-update-") as tmp:
        step("Updating compiler")
        compiler_data = _dl("compiler")
        _ex(compiler_data, tmp)
        compiler_bin = None
        for root, _, files in os.walk(tmp):
            for fn in files:
                if fn in ("hylian", "hylian.exe"):
                    compiler_bin = os.path.join(root, fn)
                    break
            if compiler_bin:
                break
        if not compiler_bin:
            err("Compiler binary not found in tarball.")
            sys.exit(1)
        dest_bin = os.path.join(bin_dir, os.path.basename(compiler_bin))
        shutil.copy2(compiler_bin, dest_bin)
        os.chmod(dest_bin, 0o755)
        ok(f"Compiler -> {dest_bin}")

        step("Updating stdlib")
        stdlib_data = _dl("stdlib")
        _ex(stdlib_data, tmp)
        runtime_src = os.path.join(tmp, "runtime")
        if os.path.isdir(runtime_src):
            std_dest = os.path.join(hylian_lib, "std")
            for root, dirs, files in os.walk(runtime_src):
                rel_root = os.path.relpath(root, runtime_src)
                dest_root = os.path.join(std_dest, rel_root)
                os.makedirs(dest_root, exist_ok=True)
                for fn in files:
                    shutil.copy2(os.path.join(root, fn), os.path.join(dest_root, fn))
            ok(f"Stdlib -> {std_dest}")
        else:
            warn("runtime/ not found in stdlib tarball -- skipping.")

        step("Updating Linkle")
        linkle_data = _dl("linkle")
        _ex(linkle_data, tmp)
        linkle_py_src = None
        for root, _, files in os.walk(tmp):
            if "linkle.py" in files:
                linkle_py_src = os.path.join(root, "linkle.py")
                break
        if linkle_py_src:
            dest_py = os.path.join(hylian_lib, "linkle.py")
            shutil.copy2(linkle_py_src, dest_py)
            ok(f"Linkle -> {dest_py}")
        else:
            warn("linkle.py not found in tarball -- skipping.")

    _write_config("version", remote_version)
    _write_config("channel", channel)
    print()
    ok(f"Hylian {remote_version} installed")


def cmd_use(version: str):
    """
    Pin the stable toolchain to a specific previously-published version.
    Usage: linkle use <version>  e.g. 1.2.3
    """
    if not re.match(r"^\d+\.\d+\.\d+$", version):
        err(f"'{version}' is not a valid semver version.")
        err("Usage: linkle use <version>  e.g. 1.2.3")
        sys.exit(1)
    step(f"Switching to stable {version}")

    import hashlib
    import io as _io
    import json as _json
    import shutil
    import tarfile
    import urllib.error
    import urllib.request

    hylian_lib = os.environ.get("HYLIAN_LIB", "/usr/local/lib/hylian")
    bin_dir = os.path.dirname(shutil.which("linkle") or "/usr/local/bin/linkle")

    def _fetch(component):
        url = (
            f"{REGISTRY_URL}/api/release/download"
            f"?channel=stable&version={version}&component={component}"
        )
        info(f"Downloading {component} {version}...")
        try:
            with urllib.request.urlopen(url) as r:
                data = r.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                err(
                    f"Version {version} not found (only last 5 stable builds are kept)."
                )
            else:
                err(f"Download failed ({e.code}): {e.reason}")
            sys.exit(1)
        return data

    def _ex(data, dest):
        buf = _io.BytesIO(data)
        with tarfile.open(fileobj=buf, mode="r:gz") as tar:
            tar.extractall(dest)

    import tempfile

    with tempfile.TemporaryDirectory(prefix=f"hylian-use-{version}-") as tmp:
        cdata = _fetch("compiler")
        _ex(cdata, tmp)
        for root, _, files in os.walk(tmp):
            for fn in files:
                if fn in ("hylian", "hylian.exe"):
                    dest = os.path.join(bin_dir, fn)
                    shutil.copy2(os.path.join(root, fn), dest)
                    os.chmod(dest, 0o755)
                    ok(f"compiler -> {dest}")

        stdlib_data = _fetch("stdlib")
        _ex(stdlib_data, tmp)
        runtime_src = os.path.join(tmp, "runtime")
        if os.path.isdir(runtime_src):
            std_dest = os.path.join(hylian_lib, "std")
            for root, dirs, files in os.walk(runtime_src):
                rel = os.path.relpath(root, runtime_src)
                os.makedirs(os.path.join(std_dest, rel), exist_ok=True)
                for fn in files:
                    shutil.copy2(
                        os.path.join(root, fn), os.path.join(std_dest, rel, fn)
                    )
            ok(f"stdlib -> {std_dest}")

        ldata = _fetch("linkle")
        _ex(ldata, tmp)
        for root, _, files in os.walk(tmp):
            if "linkle.py" in files:
                dest_py = os.path.join(hylian_lib, "linkle.py")
                shutil.copy2(os.path.join(root, "linkle.py"), dest_py)
                ok(f"linkle -> {dest_py}")
                break

    _write_config("version", version)
    _write_config("channel", "stable")
    print()
    ok(f"Pinned to Hylian {version}")


def _print_usage(cfg=None, is_workspace=False):
    ws_note = " (workspace root)" if is_workspace else ""
    print(f"""
{c(BOLD, "Linkle")} — Hylian build system{ws_note}

{c(BOLD, "USAGE:")}
  linkle <command> [options]

{c(BOLD, "COMMANDS:")}
  {c(GREEN, "build")}                       Compile and link the project (or all workspace members)
  {c(GREEN, "run")}                         Build then run the binary  (single package only)
  {c(GREEN, "clean")}                       Remove build output
  {c(GREEN, "new <name>")}                  Scaffold a new single-package project
  {c(GREEN, "new-workspace <name>")}        Scaffold a new workspace with kernel + init members
  {c(GREEN, "vendor new <name>")}           Scaffold a new vendor package
  {c(GREEN, "add <name[@ver]>")}              Download a package from the registry
  {c(GREEN, "publish")}                       Publish this package to the registry
  {c(GREEN, "login <token>")}                 Save a registry API token (~/.hylian/config)
  {c(GREEN, "update")}                        Update toolchain to latest stable
  {c(GREEN, "update --channel nightly")}      Opt in to nightly builds
  {c(GREEN, "use <version>")}                 Pin to a specific stable version
  {c(GREEN, "help")}                        Show this message""")

    if cfg and cfg.targets:
        print(f"\n{c(BOLD, 'PROJECT TARGETS')} (from linkle.hy):")
        for tname in cfg.targets:
            print(f"  {c(CYAN, tname)}")

    if cfg and cfg.workspace_members:
        print(f"\n{c(BOLD, 'WORKSPACE MEMBERS')}:")
        for m in cfg.workspace_members:
            print(f"  {c(CYAN, m)}")

    print(f"""
{c(BOLD, "BUILD OUTPUT:")}
  build/
    bin/   ← compiled executable (or .elf for kernel/limine targets)
    obj/   ← intermediate .asm and .o files

{c(BOLD, "OPTIONS:")}
  --target <platform>   linux | macos | windows | kernel | limine  (default: auto-detect)
  --verbose, -v         Print every command
  --help, -h            Show this message

{c(BOLD, "EXAMPLES:")}
  linkle new myapp && cd myapp && linkle run
  linkle new-workspace myos && cd myos && linkle build
""")


def _print_targets(cfg):
    if cfg.targets:
        print("  Available targets:", ", ".join(cfg.targets.keys()))


# ── Project root search ───────────────────────────────────────────────────────


def find_project_root():
    """Walk up from CWD looking for linkle.hy."""
    d = os.getcwd()
    while True:
        if os.path.exists(os.path.join(d, "linkle.hy")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


# ── Entry point ───────────────────────────────────────────────────────────────


def main():
    args = sys.argv[1:]
    verbose = False
    target = detect_platform()
    command = None
    rest = []
    ws_members_override = None  # for new-workspace --members a,b,c

    i = 0
    while i < len(args):
        a = args[i]
        if a in ("--verbose", "-v"):
            verbose = True
        elif a in ("--help", "-h"):
            command = "help"
        elif a == "--target" and i + 1 < len(args):
            i += 1
            target = args[i]
            if target not in VALID_TARGETS:
                err(
                    f"Unknown target '{target}' — must be one of: {', '.join(VALID_TARGETS)}"
                )
                sys.exit(1)
        elif a.startswith("--target="):
            target = a.split("=", 1)[1]
            if target not in VALID_TARGETS:
                err(
                    f"Unknown target '{target}' — must be one of: {', '.join(VALID_TARGETS)}"
                )
                sys.exit(1)
        elif a == "--members" and i + 1 < len(args):
            i += 1
            ws_members_override = [m.strip() for m in args[i].split(",") if m.strip()]
        elif command is None and not a.startswith("-"):
            command = a
        else:
            rest.append(a)
        i += 1

    if command is None:
        command = "help"

    # ── Commands that don't need a project root ───────────────────────────────

    if command == "new":
        if not rest:
            err("Usage: linkle new <project-name>")
            sys.exit(1)
        cmd_new(rest[0])
        return

    if command in ("new-workspace", "new_workspace"):
        if not rest:
            err("Usage: linkle new-workspace <workspace-name>")
            sys.exit(1)
        cmd_new_workspace(rest[0], members=ws_members_override)
        return

    if command == "help":
        project_root = find_project_root()
        cfg = None
        is_ws = False
        if project_root:
            try:
                cfg = parse_config(os.path.join(project_root, "linkle.hy"))
                is_ws = cfg.workspace_members is not None
            except ParseError:
                pass
        _print_usage(cfg, is_workspace=is_ws)
        return

    if command == "login":
        if not rest:
            err("Usage: linkle login <token>")
            sys.exit(1)
        cmd_login(rest[0])
        return

    if command == "update":
        channel = "stable"
        i2 = 0
        while i2 < len(rest):
            if rest[i2] == "--channel" and i2 + 1 < len(rest):
                channel = rest[i2 + 1]
                i2 += 1
            elif rest[i2].startswith("--channel="):
                channel = rest[i2].split("=", 1)[1]
            i2 += 1
        cmd_update(channel)
        return

    if command == "use":
        if not rest:
            err("Usage: linkle use <version>  e.g. 1.2.3")
            sys.exit(1)
        cmd_use(rest[0])
        return

    # ── Commands that need a project root ─────────────────────────────────────

    project_root = find_project_root()
    if project_root is None:
        err("No linkle.hy found in current directory or any parent directory")
        err("Run 'linkle new <name>' to create a new project")
        err("Run 'linkle new-workspace <name>' to create a workspace")
        sys.exit(1)

    config_path = os.path.join(project_root, "linkle.hy")
    try:
        cfg = parse_config(config_path)
    except ParseError as e:
        err(f"Failed to parse linkle.hy: {e}")
        sys.exit(1)

    is_workspace = cfg.workspace_members is not None

    try:
        if command == "build":
            if is_workspace:
                cmd_workspace_build(cfg, project_root, target, verbose)
            else:
                cmd_build(cfg, target, verbose, project_root)

        elif command == "run":
            if is_workspace:
                # "linkle run" from workspace root: build all, try to run the
                # one non-kernel member.  If ambiguous, tell the user to cd in.
                runnable = []
                for member in cfg.workspace_members or []:
                    mcfg_path = os.path.join(project_root, member, "linkle.hy")
                    if os.path.exists(mcfg_path):
                        try:
                            mcfg = parse_config(mcfg_path)
                            eff = mcfg.build_target or target
                            if eff not in ("kernel", "limine"):
                                runnable.append(member)
                        except ParseError:
                            pass
                if len(runnable) == 1:
                    m = runnable[0]
                    mdir = os.path.join(project_root, m)
                    mcfg = parse_config(os.path.join(mdir, "linkle.hy"))
                    cmd_run(mcfg, target, verbose, mdir)
                elif len(runnable) == 0:
                    warn(
                        "All workspace members are kernel/freestanding targets — cannot run directly."
                    )
                    info("Use 'linkle build' then boot the ELF with QEMU.")
                else:
                    warn(
                        "Multiple runnable members found. Run from inside a member directory:"
                    )
                    for m in runnable:
                        info(f"  cd {m} && linkle run")
            else:
                cmd_run(cfg, target, verbose, project_root)

        elif command == "clean":
            if is_workspace:
                cmd_workspace_clean(cfg, project_root)
            else:
                cmd_clean(cfg, project_root)

        elif command == "vendor":
            if is_workspace:
                err(
                    "Run 'linkle vendor new <name>' from inside a member directory, not the workspace root."
                )
                sys.exit(1)
            sub = rest[0] if rest else None
            if sub == "new":
                if len(rest) < 2:
                    err("Usage: linkle vendor new <name>")
                    sys.exit(1)
                cmd_vendor_new(rest[1], project_root, cfg)
            else:
                err(f"Unknown vendor sub-command '{sub}'")
                err("Available: linkle vendor new <name>")
                sys.exit(1)

        elif command == "add":
            if not rest:
                err("Usage: linkle add <name[@version]>")
                sys.exit(1)
            cmd_add(rest[0], project_root, cfg)

        elif command == "publish":
            cmd_publish(project_root, cfg)

        elif command in cfg.targets:
            cmd_target(cfg, command, verbose, project_root)

        else:
            err(f"Unknown command '{command}'")
            _print_usage(cfg, is_workspace=is_workspace)
            sys.exit(1)

    except BuildError as e:
        err(str(e))
        sys.exit(1)
    except KeyboardInterrupt:
        print()
        warn("Interrupted")
        sys.exit(130)


if __name__ == "__main__":
    main()

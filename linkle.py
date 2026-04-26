#!/usr/bin/env python3
"""
linkle.py — Hylian build system

Usage:
  python3 linkle.py <command> [options]
  linkle <command> [options]          (if installed)

Commands:
  build               Compile and link the project
  run                 Build then run the binary
  clean               Remove build artifacts
  new <name>          Scaffold a new Hylian project
  <target>            Run a named target from linkle.hy

Options:
  --target <platform> linux | macos | windows  (default: auto-detect)
  --verbose, -v       Show every command before running it
  --release           Build with -O2 (default in release mode)
  --help, -h          Show this message
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import time

# ── ANSI colours ──────────────────────────────────────────────────────────────

RESET = "\033[0m"
BOLD = "\033[1m"
RED = "\033[0;31m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[0;36m"
DIM = "\033[2m"


def _supports_color():
    return sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


USE_COLOR = _supports_color()


def c(color, text):
    return f"{color}{text}{RESET}" if USE_COLOR else text


def ok(msg):
    print(c(GREEN, f"  ✓ {msg}"))


def err(msg):
    print(c(RED, f"  ✗ {msg}"), file=sys.stderr)


def warn(msg):
    print(c(YELLOW, f"  ! {msg}"))


def info(msg):
    print(c(CYAN, f"    {msg}"))


def dim(msg):
    print(c(DIM, f"    {msg}"))


def step(msg):
    print(c(BOLD, f"\n=== {msg} ==="))


# ── Platform detection ────────────────────────────────────────────────────────


def detect_platform():
    s = platform.system().lower()
    if s == "darwin":
        return "macos"
    if s == "windows":
        return "windows"
    return "linux"


NASM_FORMATS = {
    "linux": "elf64",
    "macos": "macho64",
    "windows": "win64",
}

# ── linkle.hy parser ──────────────────────────────────────────────────────────


class ParseError(Exception):
    pass


class LinkleConfig:
    def __init__(self):
        self.project_name = ""
        self.project_version = "0.1.0"
        self.project_author = ""

        self.build_src = "src"
        self.build_main = "main"
        self.build_out = "build"
        self.build_bin = "out"
        self.build_std = "c++17"  # unused in ASM backend, kept for compat
        self.build_flags = []
        self.build_libs = []

        self.targets = {}  # name -> [cmd, ...]


def _tokenize(src):
    """
    Tiny hand-written tokenizer for linkle.hy config syntax.
    Returns list of (kind, value, line) triples.
    kind: NAME | STRING | LBRACE | RBRACE | LPAREN | RPAREN | COLON | COMMA | EOF
    """
    tokens = []
    i = 0
    line = 1
    n = len(src)

    while i < n:
        ch = src[i]

        # whitespace
        if ch in " \t\r":
            i += 1
            continue
        if ch == "\n":
            line += 1
            i += 1
            continue

        # line comment
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
        if ch == ";":
            i += 1
            continue  # optional trailing semicolons

        # string literal
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

        # identifier / keyword
        if ch.isalpha() or ch == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] in "_.-"):
                j += 1
            tokens.append(("NAME", src[i:j], line))
            i = j
            continue

        # bracket arrays
        if ch == "[":
            tokens.append(("LBRACKET", "[", line))
            i += 1
            continue
        if ch == "]":
            tokens.append(("RBRACKET", "]", line))
            i += 1
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
            if t[0] == "NAME" and t[1] == "project":
                self.advance()
                self._parse_project(cfg)
            elif t[0] == "NAME" and t[1] == "build":
                self.advance()
                self._parse_build(cfg)
            elif t[0] == "NAME" and t[1] == "target":
                self.advance()
                self._parse_target(cfg)
            else:
                raise ParseError(f"line {t[2]}: unexpected token '{t[1]}'")
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
            elif key == "std":
                cfg.build_std = val
            elif key == "flags":
                cfg.build_flags = val
            elif key == "libs":
                cfg.build_libs = val
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
            # exec("some shell command");
            fn = self.expect("NAME")[1]
            self.expect("LPAREN")
            cmd = self.expect("STRING")[1]
            self.expect("RPAREN")
            if self.at("COLON"):
                self.advance()  # trailing ;
            if self.at("NAME", ";"):
                self.advance()
            # handle real semicolons already stripped in tokenizer
            cmds.append((fn, cmd))
        self.expect("RBRACE")
        cfg.targets[name] = cmds


def parse_config(path):
    try:
        with open(path, "r") as f:
            src = f.read()
    except FileNotFoundError:
        raise ParseError(f"config file not found: {path}")
    tokens = _tokenize(src)
    return _Parser(tokens).parse()


# ── Runtime resolution ────────────────────────────────────────────────────────

HYLIAN_ROOT = os.path.dirname(os.path.abspath(__file__))
RUNTIME_DIR = os.path.join(HYLIAN_ROOT, "runtime", "std")


def _runtime_obj(stem_rel, target, verbose):
    """
    Given a stem relative to RUNTIME_DIR (e.g. "io_linux"),
    return a path to a usable .o file, building it if necessary.
    Raises RuntimeError if nothing can be found.
    """
    stem = os.path.join(RUNTIME_DIR, stem_rel)

    obj_path = stem + ".o"
    c_path = stem + ".c"
    asm_path = stem + ".asm"

    # Pre-built .o
    if os.path.exists(obj_path):
        if _is_fresh(obj_path, c_path) and _is_fresh(obj_path, asm_path):
            dim(f"pre-built  {os.path.relpath(obj_path)}")
            return obj_path

    # Compile from C
    if os.path.exists(c_path):
        _run(
            ["gcc", "-O2", "-c", c_path, "-o", obj_path],
            verbose,
            label=f"CC   {os.path.relpath(c_path)}",
        )
        return obj_path

    # Assemble from ASM
    if os.path.exists(asm_path):
        fmt = NASM_FORMATS.get(target, "elf64")
        _run(
            ["nasm", "-f", fmt, asm_path, "-o", obj_path],
            verbose,
            label=f"ASM  {os.path.relpath(asm_path)}",
        )
        return obj_path

    raise RuntimeError(f"runtime module not found: {stem_rel} (.o / .c / .asm)")


def _is_fresh(obj, src):
    """True if obj is newer than src (or src doesn't exist)."""
    if not os.path.exists(src):
        return True
    if not os.path.exists(obj):
        return False
    return os.path.getmtime(obj) >= os.path.getmtime(src)


# ── Shell helpers ─────────────────────────────────────────────────────────────


def _run(cmd, verbose, label=None, cwd=None, capture=False):
    if label:
        info(label)
    if verbose:
        dim("$ " + " ".join(cmd))
    if capture:
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
        return r
    else:
        r = subprocess.run(cmd, cwd=cwd)
        if r.returncode != 0:
            raise BuildError(f"command failed: {' '.join(cmd)}")
        return r


class BuildError(Exception):
    pass


# ── Build pipeline ────────────────────────────────────────────────────────────


# ── Stdlib module registry ────────────────────────────────────────────────────
# To add a new stdlib module, add ONE entry here.
# Keys:
#   include   — the "std.xxx" string users write in include {}
#   stems     — dict of target -> stem path under runtime/std/ (no extension)
#               use key "default" as fallback for missing targets
#   is_c      — True = compile with gcc -c, False = assemble with nasm
#
# The resolver (linkle.py) and codegen (codegen_asm.c) both read from their
# own copy of this table.  Keep them in sync when adding a module.
# ─────────────────────────────────────────────────────────────────────────────

STD_MODULES = [
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
]


def _collect_runtime_objs(includes, target, verbose):
    """
    Given the list of std.* includes found in the source, return a tuple of
    (list of resolved .o paths, list of extra linker flags) for all needed
    runtime modules.
    """
    needed = set(includes)
    objs = []
    extra_libs = []
    for mod in STD_MODULES:
        if mod["include"] not in needed:
            continue
        stem = mod["stem"]
        objs.append(_runtime_obj(stem, target, verbose))
        for lib in mod.get("link_libs", []):
            if lib not in extra_libs:
                extra_libs.append(lib)
    return objs, extra_libs


def _scan_includes(hy_file):
    """
    Quick regex scan of a .hy file to find std.* includes without running
    the full compiler. Used to decide which runtime modules to link.
    """
    includes = []
    try:
        with open(hy_file, "r") as f:
            src = f.read()
        # match include { std.io, std.errors, ... }
        blocks = re.findall(r"include\s*\{([^}]*)\}", src, re.DOTALL)
        for block in blocks:
            for item in re.findall(r"[\w.]+", block):
                if item.startswith("std."):
                    includes.append(item)
    except Exception:
        pass
    return includes


def cmd_build(cfg, target, verbose, project_root):
    step(f"Building {cfg.project_name} v{cfg.project_version}")

    hylian_bin = os.path.join(HYLIAN_ROOT, "hylian")
    if not os.path.exists(hylian_bin):
        raise BuildError(
            f"hylian compiler not found at {hylian_bin}\nRun: cd src && bison -d parser.y && flex lexer.l && gcc lex.yy.c parser.tab.c ast.c codegen_asm.c compiler.c -o ../hylian"
        )

    src_dir = os.path.join(project_root, cfg.build_src)
    out_dir = os.path.join(project_root, cfg.build_out)
    main_hy = os.path.join(src_dir, cfg.build_main + ".hy")
    out_asm = os.path.join(out_dir, cfg.build_main + ".asm")
    out_obj = os.path.join(out_dir, cfg.build_main + ".o")
    bin_ext = ".exe" if target == "windows" else ""
    bin_out = os.path.join(out_dir, cfg.build_bin + bin_ext)

    if not os.path.exists(main_hy):
        raise BuildError(f"entry point not found: {main_hy}")

    os.makedirs(out_dir, exist_ok=True)

    t0 = time.time()

    # ── Step 1: compile Hylian → ASM ─────────────────────────────────────────
    info(f"Compiling  {os.path.relpath(main_hy)} → {os.path.relpath(out_asm)}")
    r = _run(
        [hylian_bin, main_hy, "-o", out_asm, "--src-dir", src_dir, "--target", target],
        verbose,
        capture=True,
        cwd=project_root,
    )
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        raise BuildError("compilation failed")
    if verbose and r.stdout.strip():
        dim(r.stdout.strip())

    # ── Step 2: assemble ASM → .o ─────────────────────────────────────────────
    fmt = NASM_FORMATS.get(target, "elf64")
    _run(
        ["nasm", f"-f{fmt}", out_asm, "-o", out_obj],
        verbose,
        label=f"Assembling {os.path.relpath(out_asm)}",
        cwd=project_root,
    )

    # ── Step 3: resolve runtime objects ───────────────────────────────────────
    includes = _scan_includes(main_hy)
    runtime_objs, extra_libs = _collect_runtime_objs(includes, target, verbose)

    # ── Step 4: link ──────────────────────────────────────────────────────────
    info(f"Linking    {os.path.relpath(bin_out)}")
    link_cmd = ["gcc", out_obj] + runtime_objs + ["-o", bin_out]
    link_cmd += extra_libs

    if target != "windows":
        link_cmd.append("-no-pie")

    for lib in cfg.build_libs:
        link_cmd.append(f"-l{lib}")

    _run(link_cmd, verbose, cwd=project_root)

    elapsed = time.time() - t0
    ok(f"Built {os.path.relpath(bin_out)}  ({elapsed:.2f}s)")
    return bin_out


def cmd_run(cfg, target, verbose, project_root):
    bin_out = cmd_build(cfg, target, verbose, project_root)
    step("Running")
    bin_abs = os.path.abspath(bin_out)
    r = subprocess.run([bin_abs], cwd=project_root)
    sys.exit(r.returncode)


def cmd_clean(cfg, project_root):
    out_dir = os.path.join(project_root, cfg.build_out)
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
        ok(f"Removed {os.path.relpath(out_dir)}")
    else:
        warn(f"Nothing to clean ({os.path.relpath(out_dir)} doesn't exist)")


def cmd_target(cfg, target_name, verbose, project_root):
    if target_name not in cfg.targets:
        err(f"No target '{target_name}' in linkle.hy")
        _print_targets(cfg)
        sys.exit(1)

    step(f"Target: {target_name}")
    cmds = cfg.targets[target_name]
    for fn, cmd in cmds:
        if fn == "exec":
            info(f"$ {cmd}")
            if verbose:
                dim(cmd)
            r = subprocess.run(cmd, shell=True, cwd=project_root)
            if r.returncode != 0:
                raise BuildError(
                    f"target '{target_name}' failed (exit {r.returncode}): {cmd}"
                )
        else:
            warn(f"Unknown target function '{fn}' — skipping")


# ── New project scaffold ──────────────────────────────────────────────────────

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

target run() {{
    exec("./build/{name}");
}}

target clean() {{
    exec("rm -rf build");
}}
"""

_MAIN_HY_TEMPLATE = """\
include {{
    std.io,
}}

Error? main() {{
    println("Hello from {name}!");
    return nil;
}}
"""


def cmd_new(name):
    project_dir = os.path.join(os.getcwd(), name)
    if os.path.exists(project_dir):
        err(f"Directory '{name}' already exists")
        sys.exit(1)

    src_dir = os.path.join(project_dir, "src")
    os.makedirs(src_dir)

    with open(os.path.join(project_dir, "linkle.hy"), "w") as f:
        f.write(_LINKLE_HY_TEMPLATE.format(name=name))

    with open(os.path.join(src_dir, "main.hy"), "w") as f:
        f.write(_MAIN_HY_TEMPLATE.format(name=name))

    gitignore = os.path.join(project_dir, ".gitignore")
    with open(gitignore, "w") as f:
        f.write("build/\n*.o\n*.asm\n")

    step(f"Created project '{name}'")
    ok(f"{name}/")
    info(f"  linkle.hy")
    info(f"  src/main.hy")
    info(f"  .gitignore")
    print()
    print(f"  Get started:")
    print(f"    cd {name}")
    print(f"    linkle build")
    print(f"    linkle run")


# ── Help ──────────────────────────────────────────────────────────────────────


def _print_usage(cfg=None):
    print(f"""
{c(BOLD, "Linkle")} — Hylian build system

{c(BOLD, "USAGE:")}
  linkle <command> [options]

{c(BOLD, "COMMANDS:")}
  {c(GREEN, "build")}              Compile and link the project
  {c(GREEN, "run")}                Build then run the binary
  {c(GREEN, "clean")}              Remove build output directory
  {c(GREEN, "new <name>")}         Scaffold a new Hylian project
  {c(GREEN, "help")}               Show this message""")

    if cfg and cfg.targets:
        print(f"\n{c(BOLD, 'PROJECT TARGETS')} (from linkle.hy):")
        for name in cfg.targets:
            print(f"  {c(CYAN, name)}")

    print(f"""
{c(BOLD, "OPTIONS:")}
  --target <platform>   linux | macos | windows  (default: auto-detect)
  --verbose, -v         Print every command
  --help, -h            Show this message
""")


def _print_targets(cfg):
    if cfg.targets:
        print("  Available targets:", ", ".join(cfg.targets.keys()))


# ── Entry point ───────────────────────────────────────────────────────────────


def find_project_root():
    """Walk up from cwd looking for linkle.hy."""
    d = os.getcwd()
    while True:
        if os.path.exists(os.path.join(d, "linkle.hy")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def main():
    # ── Peel off global flags before positional commands ─────────────────────
    args = sys.argv[1:]

    verbose = False
    target = detect_platform()
    command = None
    rest = []

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
            if target not in ("linux", "macos", "windows"):
                err(f"Unknown target '{target}' — must be linux, macos, or windows")
                sys.exit(1)
        elif a.startswith("--target="):
            target = a.split("=", 1)[1]
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

    if command == "help":
        project_root = find_project_root()
        cfg = None
        if project_root:
            try:
                cfg = parse_config(os.path.join(project_root, "linkle.hy"))
            except ParseError:
                pass
        _print_usage(cfg)
        return

    # ── Commands that need a project root ─────────────────────────────────────
    project_root = find_project_root()
    if project_root is None:
        err("No linkle.hy found in current directory or any parent directory")
        err("Run 'linkle new <name>' to create a new project")
        sys.exit(1)

    config_path = os.path.join(project_root, "linkle.hy")
    try:
        cfg = parse_config(config_path)
    except ParseError as e:
        err(f"Failed to parse linkle.hy: {e}")
        sys.exit(1)

    try:
        if command == "build":
            cmd_build(cfg, target, verbose, project_root)

        elif command == "run":
            cmd_run(cfg, target, verbose, project_root)

        elif command == "clean":
            cmd_clean(cfg, project_root)

        elif command in cfg.targets:
            cmd_target(cfg, command, verbose, project_root)

        else:
            err(f"Unknown command '{command}'")
            _print_usage(cfg)
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

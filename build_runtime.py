#!/usr/bin/env python3
"""
build_runtime.py — Hylian runtime build script

Scans the runtime/ directory for:
  - .c  files -> compiles to .o via gcc -O2 -c
  - .asm files -> assembles to .o via nasm (if no .o already exists)

If a .c and .asm file share the same stem, the .c takes priority.

Usage:
  python3 build_runtime.py [--target linux|macos|windows] [--force] [--verbose]

Options:
  --target   Platform to build for (default: linux)
  --force    Recompile all files even if .o is up to date
  --verbose  Print each command before running it
  --clean    Remove all generated .o files
"""

import argparse
import os
import platform
import subprocess
import sys

# ── Format mappings ───────────────────────────────────────────────────────────

NASM_FORMATS = {
    "linux": "elf64",
    "macos": "macho64",
    "windows": "win64",
}

GCC_FLAGS = ["-O2", "-c"]

RUNTIME_DIR = os.path.join(os.path.dirname(__file__), "runtime")

# ── Helpers ───────────────────────────────────────────────────────────────────


def run(cmd, verbose):
    if verbose:
        print("  $", " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ERROR: {' '.join(cmd)}")
        if result.stdout.strip():
            print(result.stdout.strip())
        if result.stderr.strip():
            print(result.stderr.strip())
        return False
    return True


def is_stale(src, out):
    """Returns True if out doesn't exist or src is newer than out."""
    if not os.path.exists(out):
        return True
    return os.path.getmtime(src) > os.path.getmtime(out)


def obj_path(src):
    """Return the .o path next to the source file."""
    base, _ = os.path.splitext(src)
    return base + ".o"


# ── Discovery ─────────────────────────────────────────────────────────────────


def find_sources(runtime_dir):
    """
    Walk the runtime directory and return two dicts:
      c_files   : { stem_path -> full .c path }
      asm_files : { stem_path -> full .asm path }

    stem_path is the path without extension, used to detect conflicts.
    """
    c_files = {}
    asm_files = {}

    for dirpath, _, filenames in os.walk(runtime_dir):
        for fname in filenames:
            full = os.path.join(dirpath, fname)
            stem, ext = os.path.splitext(full)
            if ext == ".c":
                c_files[stem] = full
            elif ext == ".asm":
                asm_files[stem] = full

    return c_files, asm_files


# ── Build ─────────────────────────────────────────────────────────────────────


def build_c(src, force, verbose):
    out = obj_path(src)
    if not force and not is_stale(src, out):
        if verbose:
            print(f"  up-to-date: {os.path.relpath(out)}")
        return True, False  # (success, did_work)

    print(f"  CC  {os.path.relpath(src)}")
    cmd = ["gcc"] + GCC_FLAGS + [src, "-o", out]
    ok = run(cmd, verbose)
    return ok, True


def build_asm(src, target, force, verbose):
    out = obj_path(src)
    if not force and not is_stale(src, out):
        if verbose:
            print(f"  up-to-date: {os.path.relpath(out)}")
        return True, False

    fmt = NASM_FORMATS.get(target, "elf64")
    print(f"  ASM {os.path.relpath(src)}")
    cmd = ["nasm", "-f", fmt, src, "-o", out]
    ok = run(cmd, verbose)
    return ok, True


# ── Clean ─────────────────────────────────────────────────────────────────────


def clean(runtime_dir, verbose):
    removed = 0
    for dirpath, _, filenames in os.walk(runtime_dir):
        for fname in filenames:
            if fname.endswith(".o"):
                full = os.path.join(dirpath, fname)
                if verbose:
                    print(f"  RM  {os.path.relpath(full)}")
                os.remove(full)
                removed += 1
    print(f"Removed {removed} object file(s).")


# ── Main ──────────────────────────────────────────────────────────────────────


def detect_target():
    s = platform.system().lower()
    if s == "darwin":
        return "macos"
    if s == "windows":
        return "windows"
    return "linux"


def main():
    parser = argparse.ArgumentParser(description="Build Hylian runtime objects")
    parser.add_argument(
        "--target",
        choices=["linux", "macos", "windows"],
        default=detect_target(),
        help="Target platform (default: auto-detected)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Recompile everything even if up to date",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Print each command before running it",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove all generated .o files",
    )
    args = parser.parse_args()

    if not os.path.isdir(RUNTIME_DIR):
        print(f"error: runtime directory not found: {RUNTIME_DIR}", file=sys.stderr)
        sys.exit(1)

    if args.clean:
        clean(RUNTIME_DIR, args.verbose)
        return

    print(f"Building Hylian runtime (target={args.target})")

    c_files, asm_files = find_sources(RUNTIME_DIR)

    # C files that have a .o already pre-built should be shown as skipped
    # unless --force is set.

    errors = 0
    compiled = 0
    skipped = 0

    # ── Compile C files first ─────────────────────────────────────────────────
    for stem, src in sorted(c_files.items()):
        ok, did_work = build_c(src, args.force, args.verbose)
        if not ok:
            errors += 1
        elif did_work:
            compiled += 1
        else:
            skipped += 1

    # ── Assemble .asm files — skip if a .c (and thus .o) already covers it ────
    for stem, src in sorted(asm_files.items()):
        if stem in c_files:
            # C file takes priority — its .o is already built above
            if args.verbose:
                print(f"  skip (C override): {os.path.relpath(src)}")
            continue

        ok, did_work = build_asm(src, args.target, args.force, args.verbose)
        if not ok:
            errors += 1
        elif did_work:
            compiled += 1
        else:
            skipped += 1

    # ── Summary ───────────────────────────────────────────────────────────────
    print()
    if errors:
        print(f"Done: {compiled} compiled, {skipped} up-to-date, {errors} FAILED")
        sys.exit(1)
    else:
        print(f"Done: {compiled} compiled, {skipped} up-to-date")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
build_runtime.py — Hylian runtime build script

Scans the runtime/ directory tree for .c files and compiles each one to a
corresponding .o object file using gcc.

Usage:
  python3 build_runtime.py [--force] [--verbose] [--clean]

Options:
  --force    Recompile all files even if .o is up to date
  --verbose  Print each command before running it
  --clean    Remove all generated .o files
"""

import argparse
import os
import subprocess
import sys

# ── Build flags ───────────────────────────────────────────────────────────────

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


def find_c_sources(runtime_dir):
    """
    Walk the runtime directory and return a list of all .c source paths,
    sorted alphabetically.
    """
    sources = []
    for dirpath, _, filenames in os.walk(runtime_dir):
        for fname in sorted(filenames):
            if fname.endswith(".c"):
                sources.append(os.path.join(dirpath, fname))
    return sources


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


def main():
    parser = argparse.ArgumentParser(description="Build Hylian runtime objects")
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

    print("Building Hylian runtime")

    sources = find_c_sources(RUNTIME_DIR)

    errors = 0
    compiled = 0
    skipped = 0

    for src in sources:
        ok, did_work = build_c(src, args.force, args.verbose)
        if not ok:
            errors += 1
        elif did_work:
            compiled += 1
        else:
            skipped += 1

    print()
    if errors:
        print(f"Done: {compiled} compiled, {skipped} up-to-date, {errors} FAILED")
        sys.exit(1)
    else:
        print(f"Done: {compiled} compiled, {skipped} up-to-date")


if __name__ == "__main__":
    main()

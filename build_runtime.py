#!/usr/bin/env python3
"""
build_runtime.py — Hylian runtime build script

Scans the runtime/ directory tree for .c files and compiles each one to a
corresponding .o object file using gcc.

Usage:
  python3 build_runtime.py [--force] [--verbose] [--clean] [--target <platform>]

Options:
  --force            Recompile all files even if .o is up to date
  --verbose          Print each command before running it
  --clean            Remove all generated .o files
  --target <platform>  linux | macos | windows (default: auto-detect)
"""

import argparse
import os
import platform
import subprocess
import sys

# ── Build flags ───────────────────────────────────────────────────────────────

GCC_FLAGS = [
    "-O2",
    "-c",
    "-ffreestanding",
    "-nostdlib",
    "-nostdinc",
    "-fno-builtin",
    "-fno-stack-protector",
]

RUNTIME_DIR = os.path.join(os.path.dirname(__file__), "runtime")
PLATFORM_DIR = os.path.join(os.path.dirname(__file__), "runtime", "platform")

# Files in the platform/ subdirectory are built separately with their own
# flags — skip them during the normal std scan.
SKIP_DIRS = {os.path.normpath(PLATFORM_DIR)}


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


def detect_platform():
    s = platform.system().lower()
    if s == "darwin":
        return "macos"
    if s == "windows":
        return "windows"
    return "linux"


# ── Platform build ────────────────────────────────────────────────────────────


def build_platform(target, verbose):
    src = os.path.join(PLATFORM_DIR, f"{target}.c")
    out = os.path.join(PLATFORM_DIR, f"{target}.o")
    if not os.path.exists(src):
        print(f"  ERROR: no platform file for target '{target}': {src}")
        return None
    # skip windows.c on non-windows hosts — it needs MSVC/mingw
    current = detect_platform()
    if target == "windows" and current != "windows":
        print(f"  SKIP  runtime/platform/windows.c (cross-compile not supported)")
        return None
    if not is_stale(src, out):
        if verbose:
            print(f"  up-to-date: {os.path.relpath(out)}")
        return out
    print(f"  CC  {os.path.relpath(src)}")
    cmd = [
        "gcc",
        "-O2",
        "-c",
        "-ffreestanding",
        "-nostdlib",
        "-nostdinc",
        "-fno-builtin",
        "-fno-stack-protector",
        "-mno-red-zone",
        src,
        "-o",
        out,
    ]
    ok = run(cmd, verbose)
    return out if ok else None


def build_freestanding_platforms(verbose):
    """Build kernel.c and limine.c — always freestanding, always built."""
    targets = ["kernel", "limine"]
    results = []
    for name in targets:
        src = os.path.join(PLATFORM_DIR, f"{name}.c")
        out = os.path.join(PLATFORM_DIR, f"{name}.o")
        if not os.path.exists(src):
            continue
        if not is_stale(src, out):
            if verbose:
                print(f"  up-to-date: {os.path.relpath(out)}")
            results.append((out, False))
            continue
        print(f"  CC  {os.path.relpath(src)}")
        cmd = [
            "gcc",
            "-O2",
            "-c",
            "-ffreestanding",
            "-nostdlib",
            "-nostdinc",
            "-fno-builtin",
            "-fno-stack-protector",
            "-mno-red-zone",
            src,
            "-o",
            out,
        ]
        ok = run(cmd, verbose)
        results.append((out if ok else None, True))
    return results


# ── Discovery ─────────────────────────────────────────────────────────────────


def find_c_sources(runtime_dir):
    """
    Walk the runtime/std directory and return all .c source paths,
    sorted alphabetically. Skips the platform/ subdirectory.
    """
    sources = []
    for dirpath, dirnames, filenames in os.walk(runtime_dir):
        # Skip platform dir — built separately
        if os.path.normpath(dirpath) in SKIP_DIRS:
            dirnames.clear()
            continue
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
    success = run(cmd, verbose)
    return success, True


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
    parser.add_argument(
        "--target",
        default=detect_platform(),
        choices=["linux", "macos", "windows"],
        help="Platform to build the platform layer for (default: auto-detect)",
    )
    args = parser.parse_args()

    if not os.path.isdir(RUNTIME_DIR):
        print(f"error: runtime directory not found: {RUNTIME_DIR}", file=sys.stderr)
        sys.exit(1)

    if args.clean:
        clean(RUNTIME_DIR, args.verbose)
        return

    print("Building Hylian runtime")

    errors = 0
    compiled = 0
    skipped = 0

    # ── Build stdlib sources ──────────────────────────────────────────────────
    sources = find_c_sources(RUNTIME_DIR)
    for src in sources:
        success, did_work = build_c(src, args.force, args.verbose)
        if not success:
            errors += 1
        elif did_work:
            compiled += 1
        else:
            skipped += 1

    # ── Build platform layer ──────────────────────────────────────────────────
    print(f"\nBuilding platform layer ({args.target})")
    result = build_platform(args.target, args.verbose)
    if result is None and args.target == detect_platform():
        errors += 1
    elif result is not None:
        compiled += 1

    # ── Build freestanding kernel platforms ───────────────────────────────────
    for out, did_work in build_freestanding_platforms(args.verbose):
        if out is None:
            errors += 1
        elif did_work:
            compiled += 1
        else:
            skipped += 1

    print()
    total_mods = compiled + skipped
    if errors:
        print(f"Done: {compiled} compiled, {skipped} up-to-date, {errors} FAILED")
        sys.exit(1)
    else:
        print(f"Done: {compiled} compiled, {skipped} up-to-date")


if __name__ == "__main__":
    main()

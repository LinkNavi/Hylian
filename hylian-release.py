#!/usr/bin/env python3
"""
hylian-release — Release management tool for the Hylian toolchain.

Publishes compiler, stdlib, and Linkle updates to hylian.lol.

Commands:
  hylian-release publish stable       — build & publish a stable release
  hylian-release publish nightly      — build & publish a nightly release
  hylian-release rollback 0.x.x       — mark a previous stable as current

Authentication:
  Token is read from ~/.hylian/config
  Only the user "Link" may publish releases
"""

import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request

# ── Terminal colours ──────────────────────────────────────────────────────────

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[1;31m"
GREEN = "\033[1;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[1;36m"


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


def header():
    print(
        c(
            CYAN + BOLD,
            """
  ██╗  ██╗██╗   ██╗██╗     ██╗ █████╗ ███╗  ██╗
  ██║  ██║╚██╗ ██╔╝██║     ██║██╔══██╗████╗ ██║
  ███████║ ╚████╔╝ ██║     ██║███████║██╔██╗██║
  ██╔══██║  ╚██╔╝  ██║     ██║██╔══██║██║╚████║
  ██║  ██║   ██║   ███████╗██║██║  ██║██║ ╚███║
  ╚═╝  ╚═╝   ╚═╝   ╚══════╝╚═╝╚═╝  ╚═╝╚═╝  ╚══╝
""",
        )
    )
    print(f"  {c(BOLD, 'hylian-release')}  — toolchain release manager\n")


# ── Config ────────────────────────────────────────────────────────────────────

REGISTRY_URL = os.environ.get("HYLIAN_REGISTRY", "https://www.hylian.lol")
HYLIAN_ROOT = os.path.dirname(os.path.abspath(__file__))


def _config_path() -> str:
    return os.path.expanduser("~/.hylian/config")


def _read_token() -> str:
    path = _config_path()
    if not os.path.exists(path):
        err(f"No config found at {path}")
        err("Run:  linkle login  (or create ~/.hylian/config with token=hylian_…)")
        sys.exit(1)
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("token="):
                tok = line[len("token=") :].strip()
                if tok:
                    return tok
    err(f"No token= entry found in {path}")
    sys.exit(1)


# ── Checksums ─────────────────────────────────────────────────────────────────


def _sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# ── Tarball helpers ───────────────────────────────────────────────────────────


def _make_tarball(root_dir: str, include_paths: list[str]) -> bytes:
    """
    Create an in-memory .tar.gz containing the given paths (relative to
    root_dir).  Each path may be a file or directory (recursive).
    """
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for rel in include_paths:
            abs_path = os.path.join(root_dir, rel)
            if not os.path.exists(abs_path):
                raise FileNotFoundError(f"Missing path for tarball: {abs_path}")
            tar.add(abs_path, arcname=rel)
    return buf.getvalue()


# ── HTTP helpers ──────────────────────────────────────────────────────────────


def _api(method: str, path: str, token: str, *, json_body=None, files=None) -> dict:
    """
    Perform a JSON API call.  `files` is a dict mapping field names to
    (filename, bytes, content_type) tuples for multipart/form-data uploads.
    """
    url = f"{REGISTRY_URL}{path}"

    if files is not None:
        body, content_type = _encode_multipart(json_body or {}, files)
    elif json_body is not None:
        body = json.dumps(json_body).encode()
        content_type = "application/json"
    else:
        body = None
        content_type = None

    req = urllib.request.Request(url, data=body, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    if content_type:
        req.add_header("Content-Type", content_type)

    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        body_text = e.read().decode(errors="replace")
        try:
            detail = json.loads(body_text).get("error", body_text)
        except Exception:
            detail = body_text
        err(f"HTTP {e.code} from {url}: {detail}")
        sys.exit(1)


def _encode_multipart(fields: dict, files: dict) -> tuple[bytes, str]:
    boundary = "----HylianReleaseBoundary" + hashlib.md5(os.urandom(16)).hexdigest()
    parts = []

    for name, value in fields.items():
        parts.append(
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
            f"{value}\r\n"
        )

    for name, (filename, data, ct) in files.items():
        parts.append(
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
            f"Content-Type: {ct}\r\n\r\n"
        )
        parts_bytes = "".join(parts).encode() + data + b"\r\n"
        parts = [parts_bytes.decode(errors="surrogateescape")]

    body = (
        "".join(parts).encode(errors="surrogateescape") + f"--{boundary}--\r\n".encode()
    )
    return body, f"multipart/form-data; boundary={boundary}"


# ── Build helpers ─────────────────────────────────────────────────────────────


def _run(cmd: list[str], cwd: str = None, check: bool = True):
    dim("$ " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=cwd or HYLIAN_ROOT, capture_output=False)
    if check and result.returncode != 0:
        err(f"Command failed: {' '.join(cmd)}")
        sys.exit(result.returncode)
    return result


def _build_toolchain():
    """Run the full build: compiler + runtime."""
    step("Building compiler")
    _run(["bash", "build.sh"], cwd=HYLIAN_ROOT)

    step("Building stdlib / runtime")
    _run(["python3", "build_runtime.py"], cwd=HYLIAN_ROOT)


# ── Version helpers ───────────────────────────────────────────────────────────


def _git_sha() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=HYLIAN_ROOT,
            capture_output=True,
            text=True,
        )
        return result.stdout.strip()
    except Exception:
        return "unknown"


def _next_nightly_version() -> str:
    ts = time.strftime("%Y%m%d%H%M", time.gmtime())
    sha = _git_sha()
    return f"nightly-{ts}-{sha}"


def _prompt_stable_version() -> str:
    try:
        current = _api("GET", "/api/release/latest?channel=stable", "")
        last = current.get("version", "0.0.0")
        info(f"Last stable release: {last}")
    except SystemExit:
        last = "0.0.0"

    while True:
        try:
            version = input(f"  {c(CYAN, '?')} New stable version (semver): ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(0)
        if re.match(r"^\d+\.\d+\.\d+$", version):
            return version
        warn("Please enter a semver version like 1.2.3")


# ── Tarballs to publish ───────────────────────────────────────────────────────
#
# We publish three separate tarballs so the server (and clients) can update
# components individually without re-downloading everything.
#
#   compiler  — just the `hylian` binary
#   stdlib    — runtime/std/**  +  runtime/platform/**
#   linkle    — linkle.py


def _assemble_tarballs() -> dict[str, bytes]:
    step("Assembling release tarballs")

    compiler_bin = os.path.join(HYLIAN_ROOT, "hylian")
    if not os.path.exists(compiler_bin):
        err("Compiler binary './hylian' not found — run the build first.")
        sys.exit(1)

    runtime_std = os.path.join(HYLIAN_ROOT, "runtime")
    if not os.path.isdir(runtime_std):
        err("Runtime directory './runtime' not found.")
        sys.exit(1)

    linkle_src = os.path.join(HYLIAN_ROOT, "linkle.py")
    if not os.path.exists(linkle_src):
        err("linkle.py not found.")
        sys.exit(1)

    compiler_tar = _make_tarball(HYLIAN_ROOT, ["hylian"])
    ok(f"compiler tarball  {len(compiler_tar):,} bytes")

    stdlib_tar = _make_tarball(HYLIAN_ROOT, ["runtime"])
    ok(f"stdlib tarball    {len(stdlib_tar):,} bytes")

    linkle_tar = _make_tarball(HYLIAN_ROOT, ["linkle.py"])
    ok(f"linkle tarball    {len(linkle_tar):,} bytes")

    return {
        "compiler": compiler_tar,
        "stdlib": stdlib_tar,
        "linkle": linkle_tar,
    }


# ── Publish ───────────────────────────────────────────────────────────────────


def _publish(channel: str, version: str, token: str, tarballs: dict[str, bytes]):
    step(f"Publishing {channel} {version}")

    for component, data in tarballs.items():
        checksum = _sha256_bytes(data)
        info(f"Uploading {component} ({len(data):,} bytes, sha256={checksum[:16]}…)")

        filename = f"{component}-{version}.tar.gz"
        fields = {
            "channel": channel,
            "version": version,
            "component": component,
            "checksum": checksum,
        }
        file_fields = {
            "tarball": (filename, data, "application/gzip"),
        }

        resp = _api(
            "POST",
            "/api/release/publish",
            token,
            json_body=fields,
            files=file_fields,
        )
        ok(f"{component} published — id={resp.get('id')}")


# ── Commands ──────────────────────────────────────────────────────────────────


def cmd_publish(channel: str):
    if channel not in ("stable", "nightly"):
        err(f"Unknown channel '{channel}'. Use: stable | nightly")
        sys.exit(1)

    header()
    token = _read_token()

    if channel == "nightly":
        version = _next_nightly_version()
        info(f"Nightly version: {version}")
        # Build is expected to have already run in CI; but do it locally too
        # if the binary is missing.
        if not os.path.exists(os.path.join(HYLIAN_ROOT, "hylian")):
            warn("Compiler binary missing — building now…")
            _build_toolchain()
    else:
        # Stable: confirm version, then (re)build
        version = _prompt_stable_version()
        _build_toolchain()

    tarballs = _assemble_tarballs()
    _publish(channel, version, token, tarballs)

    print()
    ok(f"Release {c(BOLD, version)} published to {c(CYAN, channel)} ✓")
    if channel == "stable":
        info(f"Users can install with:  linkle use {version}")
        info("To rollback:             hylian-release rollback <version>")


def cmd_rollback(version: str):
    header()
    if not re.match(r"^\d+\.\d+\.\d+$", version):
        err(f"'{version}' doesn't look like a semver stable version.")
        err("Only stable releases can be rolled back.")
        sys.exit(1)

    token = _read_token()
    step(f"Rolling back stable to {version}")

    resp = _api(
        "POST",
        "/api/release/rollback",
        token,
        json_body={"version": version},
    )

    print()
    ok(f"Stable channel now points to {c(BOLD, resp.get('version', version))} ✓")
    info("Users on stable will receive this version on their next update.")


# ── Entry point ───────────────────────────────────────────────────────────────


def usage():
    print(f"""
{c(BOLD, "hylian-release")} — Hylian toolchain release manager

{c(BOLD, "USAGE")}
  hylian-release publish stable       Publish a new stable release
  hylian-release publish nightly      Publish a nightly snapshot
  hylian-release rollback 0.x.x       Roll stable back to a previous version

{c(BOLD, "AUTHENTICATION")}
  Token is read from ~/.hylian/config (token=hylian_…)
  Only the user "Link" can publish releases.

{c(BOLD, "ENVIRONMENT")}
  HYLIAN_REGISTRY   Override the registry URL (default: https://www.hylian.lol)
""")


def main():
    args = sys.argv[1:]

    if not args or args[0] in ("-h", "--help"):
        usage()
        sys.exit(0)

    if args[0] == "publish":
        if len(args) < 2:
            err("Usage: hylian-release publish <stable|nightly>")
            sys.exit(1)
        cmd_publish(args[1])

    elif args[0] == "rollback":
        if len(args) < 2:
            err("Usage: hylian-release rollback <version>")
            sys.exit(1)
        cmd_rollback(args[1])

    else:
        err(f"Unknown command: {args[0]}")
        usage()
        sys.exit(1)


if __name__ == "__main__":
    main()

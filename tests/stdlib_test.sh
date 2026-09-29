#!/bin/bash
# Tests for the pure-Hylian stdlib modules that need no C runtime: string
# extensions, map, os.fs list_dir, inflate/gzip, tar, dns, http, https/tls.
#
# Each tests/stdlib/<name>.hy is compiled with ./hylian (--src-dir stdlib),
# linked against the always-linked runtime modules, run inside a scratch
# directory of generated fixtures, and its output compared with
# tests/stdlib/<name>.expected. The network tests talk only to throwaway
# servers on 127.0.0.1 (fake DNS, HTTP, TLS) started by this script, so no
# real network is needed. Needs python3; the https tests also need the
# openssl CLI and libssl (skipped otherwise).
#
# Usage: ./tests/stdlib_test.sh          (from anywhere; builds nothing itself -
#        run ./build.sh first)

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HY="$ROOT/hylian"
SRC="$ROOT/tests/stdlib"
WORK="$(mktemp -d)"
PIDS=()
PASS=0; FAIL=0; SKIP=0

cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; rm -rf "$WORK"; }
trap cleanup EXIT

[ -x "$HY" ] || { echo "compiler not built: run ./build.sh first"; exit 1; }
command -v python3 >/dev/null || { echo "python3 required"; exit 1; }

export HYLIAN_LIB="$ROOT/stdlib"

# ── runtime objects ──────────────────────────────────────────────────────────
for m in mem runtime array; do
    "$HY" "$ROOT/stdlib/$m.hy" -o "$WORK/$m.o" >/dev/null 2>&1 || { echo "failed to build $m"; exit 1; }
done

# ── fixtures ─────────────────────────────────────────────────────────────────
cd "$WORK"
python3 - <<'PY'
import gzip, os, random
random.seed(1)
text = b"".join(b"line %d: the quick brown fox jumps over the lazy dog\n" % i for i in range(3000))
data = text + bytes(random.getrandbits(8) for _ in range(5000)) + text[:1000]
open("data.bin", "wb").write(data)
open("data.gz", "wb").write(gzip.compress(data, 9))
open("data0.bin", "wb").write(data[:20000])
open("data0.gz", "wb").write(gzip.compress(data[:20000], 0))
open("small.bin", "wb").write(b"hello hello hello hello\n")
open("small.gz", "wb").write(gzip.compress(b"hello hello hello hello\n"))
PY
mkdir -p listdir/sub && touch listdir/a listdir/b.txt
mkdir -p pkgsrc/mylib-1.0/src && echo 'int f() { return 1; }' > pkgsrc/mylib-1.0/src/lib.hy
echo "name = mylib" > pkgsrc/mylib-1.0/pkg.toml
head -c 3000 /dev/urandom > pkgsrc/mylib-1.0/blob.bin
python3 -c "open('pkgsrc/mylib-1.0/src/' + 'x'*120 + '.txt', 'w').write('long name')"
tar -C pkgsrc -czf pkg.tar.gz mylib-1.0
python3 - <<'PY'
import tarfile, io
t = tarfile.open("evil.tar", "w")
i = tarfile.TarInfo("../escape.txt"); i.size = 3
t.addfile(i, io.BytesIO(b"bad")); t.close()
PY
cp "$SRC/fake_dns.py" "$SRC/fake_http.py" .
python3 fake_dns.py & PIDS+=($!)
python3 fake_http.py & PIDS+=($!)

HAVE_TLS=0
if command -v openssl >/dev/null && ldconfig -p 2>/dev/null | grep -q "libssl.so"; then
    if openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 2 \
        -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" >/dev/null 2>&1; then
        HAVE_TLS=1
        python3 fake_http.py tls & PIDS+=($!)
    fi
fi
sleep 1

# ── runner ───────────────────────────────────────────────────────────────────
run() {
    local name="$1"; shift
    local libs="$1"; shift
    if ! "$HY" "$SRC/$name.hy" --src-dir "$ROOT/stdlib" -o "$WORK/$name.o" >"$WORK/$name.cerr" 2>&1 \
       || grep -q "error" "$WORK/$name.cerr"; then
        echo "  ✗ FAIL  $name (compile): $(grep error "$WORK/$name.cerr" | head -3)"; FAIL=$((FAIL+1)); return
    fi
    if ! gcc "$WORK/$name.o" "$WORK"/mem.o "$WORK"/runtime.o "$WORK"/array.o -o "$WORK/$name.bin" -no-pie $libs 2>"$WORK/$name.lerr"; then
        echo "  ✗ FAIL  $name (link): $(head -3 "$WORK/$name.lerr")"; FAIL=$((FAIL+1)); return
    fi
    local actual
    actual=$(env "$@" timeout 60 "$WORK/$name.bin" 2>&1)
    if [ "$actual" == "$(cat "$SRC/$name.expected")" ]; then
        echo "  ✓ PASS  $name"; PASS=$((PASS+1))
    else
        echo "  ✗ FAIL  $name"; diff <(echo "$actual") "$SRC/$name.expected" | head -10; FAIL=$((FAIL+1))
    fi
}

run string_ext ""
run map ""
run fs_list ""
run gzip_tar ""
run net_stack ""
if [ "$HAVE_TLS" == 1 ]; then
    run https "-lssl -lcrypto" SSL_CERT_FILE="$WORK/cert.pem"
    run https_untrusted "-lssl -lcrypto"
else
    echo "  ~ SKIP  https, https_untrusted (need openssl CLI + libssl)"; SKIP=$((SKIP+2))
fi

echo ""
echo "stdlib tests: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -eq 0 ]

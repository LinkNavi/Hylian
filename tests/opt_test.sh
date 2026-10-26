#!/bin/bash
# Optimizer / backend regression tests.
#
#   1. tests/opt/*.hy with their .expected output, compiled with the optimizer
#      on AND off (HYLIAN_OPT=0) - edge cases the fuzzer avoids on purpose
#      (NaN compares, int64 extremes through the magic-number division, ...).
#   2. the differential fuzzer (tests/opt_fuzz.py) on a fixed set of seeds,
#      including seeds that once exposed real bugs (11275: an unconverged
#      copy-propagation dataflow on a very large function).
#
# Usage: ./tests/opt_test.sh [fuzz-count]      (needs ./hylian: run ./build.sh first)

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HY="$ROOT/hylian"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
[ -x "$HY" ] || { echo "compiler not built: run ./build.sh first"; exit 1; }
PASS=0; FAIL=0

for m in mem runtime array; do
    "$HY" "$ROOT/stdlib/$m.hy" --src-dir "$ROOT/stdlib" -o "$WORK/$m.o" >/dev/null 2>&1 || { echo "failed to build $m"; exit 1; }
done

for src in "$ROOT"/tests/opt/*.hy; do
    name=$(basename "$src" .hy)
    for mode in opt noopt; do
        if [ $mode = noopt ]; then export HYLIAN_OPT=0; else unset HYLIAN_OPT; fi
        if "$HY" "$src" --src-dir "$ROOT/stdlib" -o "$WORK/$name.o" >/dev/null 2>"$WORK/err" &&
           gcc "$WORK/$name.o" "$WORK"/mem.o "$WORK"/runtime.o "$WORK"/array.o -o "$WORK/$name" -no-pie 2>>"$WORK/err" &&
           diff <("$WORK/$name") "$ROOT/tests/opt/$name.expected" >"$WORK/diff"; then
            echo "  ✓ PASS  $name ($mode)"; PASS=$((PASS+1))
        else
            echo "  ✗ FAIL  $name ($mode)"; head -5 "$WORK/err" "$WORK/diff"; FAIL=$((FAIL+1))
        fi
    done
done
unset HYLIAN_OPT

COUNT="${1:-150}"
if python3 "$ROOT/tests/opt_fuzz.py" "$COUNT" 1 >"$WORK/fuzz1" 2>&1 &&
   python3 "$ROOT/tests/opt_fuzz.py" 1 11275 >"$WORK/fuzz2" 2>&1; then
    echo "  ✓ PASS  fuzz ($COUNT seeds + regression seeds)"; PASS=$((PASS+1))
else
    echo "  ✗ FAIL  fuzz"; tail -12 "$WORK/fuzz1" "$WORK/fuzz2"; FAIL=$((FAIL+1))
fi

echo "opt tests: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]

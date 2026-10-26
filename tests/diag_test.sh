#!/bin/bash
# Compiler diagnostics: exact line:col, did-you-mean hints, and that an error
# stops the build (no object file, non-zero exit).
#
# Usage: tests/diag_test.sh   (after ./build.sh)

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HY="$ROOT/hylian"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export HYLIAN_LIB="$ROOT/stdlib"
PASS=0; FAIL=0

# expect NAME 'source' 'substring that must appear in the plain-text diagnostics'
expect() {
    local name="$1" src="$2" want="$3"
    printf '%s' "$src" > "$WORK/t.hy"
    rm -f "$WORK/t.o"
    local out rc
    timeout 10 "$HY" "$WORK/t.hy" -o "$WORK/t.o" --src-dir "$ROOT/stdlib" > "$WORK/out" 2>&1
    rc=$?
    out="$(sed 's/\x1b\[[0-9;]*m//g' "$WORK/out")"
    if [[ "$out" == *"$want"* ]] && [ "$rc" -ne 0 ] && [ ! -f "$WORK/t.o" ]; then
        echo "  ✓ PASS  $name"; PASS=$((PASS+1))
    else
        echo "  ✗ FAIL  $name (exit $rc, object written: $([ -f "$WORK/t.o" ] && echo yes || echo no))"
        echo "      wanted: $want"; echo "      got: $out" | head -6
        FAIL=$((FAIL+1))
    fi
}

expect "missing ';' is reported at the end of the previous line" \
$'int main() {\n    int y = 3\n    return y;\n}\n' \
"t.hy:2:14: error: syntax error: expected ';' at the end of this statement"

expect "unexpected token names the token and points at it" \
$'int main() {\n    int x = (1 + ;\n    return 0;\n}\n' \
"t.hy:2:18: error: syntax error: unexpected ';'"

expect "unknown type in a declaration suggests the class" \
$'struct Point { int x; int y; }\nint main() {\n    Pont p;\n    return 0;\n}\n' \
"unknown type 'Pont'"

expect "assigning to a field that does not exist is an error" \
$'struct Point { int x; int y; }\nint main() {\n    Point p;\n    p.z = 3;\n    return 0;\n}\n' \
"no field 'z' on type 'Point'"

expect "address-of needs a variable" \
$'int main() {\n    int a = 3;\n    if (a > &1) { return 1; }\n    return 0;\n}\n' \
"can only take the address of a variable"

expect "a class defined twice" \
$'struct P { int x; }\nstruct P { int y; }\nint main() { return 0; }\n' \
"class 'P' is defined more than once"


expect "undefined variable suggests the closest name" \
$'int main() {\n    int total = 1;\n    return totl;\n}\n' \
"hint: did you mean 'total'?"

expect "undefined variable is underlined exactly" \
$'int main() {\n    return zzz;\n}\n' \
"t.hy:2:12: error: undefined variable 'zzz'"

expect "undefined function suggests the closest function" \
$'int compute(int a) { return a; }\nint main() {\n    return compue(1);\n}\n' \
"hint: did you mean 'compute'?"

expect "missing field suggests the closest field" \
$'class P { int count; }\nint main() {\n    P p = new P();\n    return p.cout;\n}\n' \
"hint: did you mean 'count'?"

expect "a function used as a value gets a hint" \
$'int f(int a) { return a; }\nint main() {\n    return f;\n}\n' \
"is a function: call it as f(...)"

expect "line numbers survive a multi-line block comment" \
$'/* a\n   b\n   c */\nint main() {\n    return zzz;\n}\n' \
"t.hy:5:12: error: undefined variable 'zzz'"

expect "an unterminated block comment is an error, not a hang" \
$'int main() { return 0; }\n/* never closed\n' \
"unterminated /* comment"

expect "a caret line is printed under the source" \
$'int main() {\n    return zzz;\n}\n' \
"    return zzz;"

echo
echo "diagnostics tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

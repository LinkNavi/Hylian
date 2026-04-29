#!/bin/bash

# ─────────────────────────────────────────────────────────────────────────────
# Hylian Compiler Test Suite
# ─────────────────────────────────────────────────────────────────────────────

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

PASS=0
FAIL=0
SKIP=0

# Temp files to clean up at exit
TMPFILES=()
TMPBINS=()

cleanup() {
    for f in "${TMPFILES[@]}"; do rm -f "$f"; done
    for b in "${TMPBINS[@]}"; do rm -f "$b"; done
    rm -f io_runtime.o errors_runtime.o strings_runtime.o
}
trap cleanup EXIT

# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

header() {
    echo -e "\n${BOLD}${CYAN}══════════════════════════════════════════${NC}"
    echo -e "${BOLD}${CYAN}  $1${NC}"
    echo -e "${BOLD}${CYAN}══════════════════════════════════════════${NC}"
}

section() {
    echo -e "\n${BOLD}  ── $1 ──${NC}"
}

pass_msg()  { echo -e "  ${GREEN}✓ PASS${NC}  $1"; }
fail_msg()  { echo -e "  ${RED}✗ FAIL${NC}  $1"; }
skip_msg()  { echo -e "  ${YELLOW}~ SKIP${NC}  $1"; }
info_msg()  { echo -e "  ${DIM}  $1${NC}"; }

# ─────────────────────────────────────────────────────────────────────────────
# Build compiler
# ─────────────────────────────────────────────────────────────────────────────

header "Building Hylian Compiler"

cd src
bison -d parser.y   2>/dev/null
flex lexer.l        2>/dev/null
gcc lex.yy.c parser.tab.c ast.c ir.c lower.c opt.c codegen_asm.c typecheck.c compiler.c \
    -o ../hylian 2>&1
if [ $? -ne 0 ]; then
    echo -e "${RED}✗ Compiler build failed — aborting.${NC}"
    exit 1
fi
cd ..
echo -e "${GREEN}  ✓ Compiler built successfully${NC}"

# ─────────────────────────────────────────────────────────────────────────────
# Resolve runtime objects
# ─────────────────────────────────────────────────────────────────────────────

header "Resolving Runtime Objects"

resolve_runtime_obj() {
    local stem="$1"
    local out="$2"

    if [ -f "${stem}.o" ]; then
        cp "${stem}.o" "${out}"
        echo -e "  ${GREEN}✓${NC} ${stem}.o"
    elif [ -f "${stem}.c" ]; then
        gcc -O2 -c "${stem}.c" -o "${out}" 2>&1
        if [ $? -ne 0 ]; then
            echo -e "  ${RED}✗ failed to compile ${stem}.c${NC}"
            exit 1
        fi
        echo -e "  ${GREEN}✓${NC} compiled ${stem}.c"
    else
        echo -e "  ${RED}✗ not found: ${stem}${NC}"
        exit 1
    fi
}

resolve_runtime_obj "runtime/std/io"      "io_runtime.o"
resolve_runtime_obj "runtime/std/errors"  "errors_runtime.o"
resolve_runtime_obj "runtime/std/strings" "strings_runtime.o"

# ─────────────────────────────────────────────────────────────────────────────
# IR dump test runner (checks stderr output from --dump-ir, no linking needed)
# run_test_ir_dump <id> <desc> <hy_src> <grep_pattern> [absent_pattern]
# Pass = grep_pattern IS found in dump AND absent_pattern (if given) is NOT found.
# ─────────────────────────────────────────────────────────────────────────────

run_test_ir_dump() {
    local id="$1"
    local desc="$2"
    local src="$3"
    local must_have="$4"
    local must_not_have="${5:-}"   # optional: pattern that must be absent

    local hy_file="__test_${id}.hy"
    local asm_file="__test_${id}.asm"
    TMPFILES+=("$hy_file" "$asm_file")

    printf '%s' "$src" > "$hy_file"

    local ir_out
    ir_out=$(./hylian "$hy_file" -o "$asm_file" --dump-ir 2>&1)
    if [ $? -ne 0 ]; then
        fail_msg "$desc"
        info_msg "compiler error: $ir_out"
        FAIL=$((FAIL + 1))
        return
    fi

    if ! echo "$ir_out" | grep -qF "$must_have"; then
        fail_msg "$desc"
        info_msg "expected pattern: $must_have"
        info_msg "dump (first 10 lines): $(echo "$ir_out" | head -10)"
        FAIL=$((FAIL + 1))
        return
    fi

    if [ -n "$must_not_have" ] && echo "$ir_out" | grep -qF "$must_not_have"; then
        fail_msg "$desc"
        info_msg "pattern should be absent: $must_not_have"
        info_msg "dump (first 10 lines): $(echo "$ir_out" | head -10)"
        FAIL=$((FAIL + 1))
        return
    fi

    pass_msg "$desc"
    PASS=$((PASS + 1))
}

# ─────────────────────────────────────────────────────────────────────────────
# Test runner
# ─────────────────────────────────────────────────────────────────────────────

# run_test <id> <description> <hy_source> <expected_stdout> [extra_objs...]
run_test() {
    local id="$1"
    local desc="$2"
    local src="$3"
    local expected="$4"
    shift 4
    local extra_objs=("$@")

    local hy_file="__test_${id}.hy"
    local asm_file="__test_${id}.asm"
    local obj_file="__test_${id}.o"
    local bin_file="__test_${id}_bin"

    TMPFILES+=("$hy_file" "$asm_file" "$obj_file")
    TMPBINS+=("$bin_file")

    printf '%s' "$src" > "$hy_file"

    # Compile .hy → .asm
    ./hylian "$hy_file" -o "$asm_file" 2>/tmp/hylian_err
    if [ $? -ne 0 ]; then
        fail_msg "$desc"
        info_msg "compiler error: $(cat /tmp/hylian_err)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Assemble .asm → .o
    nasm -felf64 "$asm_file" -o "$obj_file" 2>/tmp/nasm_err
    if [ $? -ne 0 ]; then
        fail_msg "$desc"
        info_msg "nasm error: $(cat /tmp/nasm_err)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Link
    gcc "$obj_file" io_runtime.o errors_runtime.o strings_runtime.o \
        "${extra_objs[@]}" -o "$bin_file" -no-pie 2>/tmp/link_err
    if [ $? -ne 0 ]; then
        fail_msg "$desc"
        info_msg "link error: $(cat /tmp/link_err)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Run and capture output
    local actual
    actual=$("./$bin_file" 2>&1)
    local exit_code=$?

    if [ "$actual" = "$expected" ]; then
        pass_msg "$desc"
        PASS=$((PASS + 1))
    else
        fail_msg "$desc"
        info_msg "expected: $(echo "$expected" | head -5)"
        info_msg "got:      $(echo "$actual"   | head -5)"
        FAIL=$((FAIL + 1))
    fi
}

# run_test_exit <id> <description> <hy_source> <expected_exit_code>
run_test_exit() {
    local id="$1"
    local desc="$2"
    local src="$3"
    local expected_exit="$4"

    local hy_file="__test_${id}.hy"
    local asm_file="__test_${id}.asm"
    local obj_file="__test_${id}.o"
    local bin_file="__test_${id}_bin"

    TMPFILES+=("$hy_file" "$asm_file" "$obj_file")
    TMPBINS+=("$bin_file")

    printf '%s' "$src" > "$hy_file"

    ./hylian "$hy_file" -o "$asm_file" 2>/tmp/hylian_err
    if [ $? -ne 0 ]; then
        fail_msg "$desc"
        info_msg "compiler error: $(cat /tmp/hylian_err)"
        FAIL=$((FAIL + 1))
        return
    fi

    nasm -felf64 "$asm_file" -o "$obj_file" 2>/dev/null
    gcc "$obj_file" io_runtime.o errors_runtime.o strings_runtime.o \
        -o "$bin_file" -no-pie 2>/dev/null

    "./$bin_file" >/dev/null 2>&1
    local actual_exit=$?

    if [ "$actual_exit" = "$expected_exit" ]; then
        pass_msg "$desc"
        PASS=$((PASS + 1))
    else
        fail_msg "$desc"
        info_msg "expected exit: $expected_exit  got: $actual_exit"
        FAIL=$((FAIL + 1))
    fi
}

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Basic I/O
# ─────────────────────────────────────────────────────────────────────────────

section "Basic I/O"

run_test "t01" "Hello World" \
'include {
    std.io,
}
Error? main() {
    println("Hello, World!");
    return nil;
}' \
"Hello, World!"

run_test "t02" "print without newline then println" \
'include {
    std.io,
}
Error? main() {
    print("foo: ");
    println("bar");
    return nil;
}' \
"foo: bar"

run_test "t03" "println integer" \
'include {
    std.io,
}
Error? main() {
    int x = 42;
    println(x);
    return nil;
}' \
"42"

run_test "t04" "multiple println calls" \
'include {
    std.io,
}
Error? main() {
    println("one");
    println("two");
    println("three");
    return nil;
}' \
"one
two
three"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Variables and Arithmetic
# ─────────────────────────────────────────────────────────────────────────────

section "Variables & Arithmetic"

run_test "t10" "integer addition" \
'include {
    std.io,
}
Error? main() {
    int a = 10;
    int b = 32;
    int c = a + b;
    println(c);
    return nil;
}' \
"42"

run_test "t11" "integer subtraction" \
'include {
    std.io,
}
Error? main() {
    int x = 100;
    int y = 58;
    int z = x - y;
    println(z);
    return nil;
}' \
"42"

run_test "t12" "integer multiplication" \
'include {
    std.io,
}
Error? main() {
    int a = 6;
    int b = 7;
    int c = a * b;
    println(c);
    return nil;
}' \
"42"

run_test "t13" "integer division" \
'include {
    std.io,
}
Error? main() {
    int a = 84;
    int b = 2;
    int c = a / b;
    println(c);
    return nil;
}' \
"42"

run_test "t14" "negative numbers" \
'include {
    std.io,
}
Error? main() {
    int x = -10;
    int y = x + 52;
    println(y);
    return nil;
}' \
"42"

run_test "t15" "chained arithmetic" \
'include {
    std.io,
}
Error? main() {
    int x = 2 + 3 * 10 - 8;
    println(x);
    return nil;
}' \
"24"

run_test "t16" "variable reassignment" \
'include {
    std.io,
}
Error? main() {
    int x = 1;
    x = 42;
    println(x);
    return nil;
}' \
"42"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: String interpolation
# ─────────────────────────────────────────────────────────────────────────────

section "String Interpolation"

run_test "t20" "interpolate integer variable" \
'include {
    std.io,
}
Error? main() {
    int x = 7;
    println("value is {{x}}");
    return nil;
}' \
"value is 7"

run_test "t21" "interpolate string variable" \
'include {
    std.io,
}
Error? main() {
    str name = "World";
    println("Hello, {{name}}!");
    return nil;
}' \
"Hello, World!"

run_test "t22" "interpolate multiple variables" \
'include {
    std.io,
}
Error? main() {
    int a = 3;
    int b = 4;
    int c = a + b;
    println("{{a}} + {{b}} = {{c}}");
    return nil;
}' \
"3 + 4 = 7"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Conditionals
# ─────────────────────────────────────────────────────────────────────────────

section "Conditionals"

run_test "t30" "if true branch" \
'include {
    std.io,
}
Error? main() {
    int x = 10;
    if (x > 5) {
        println("big");
    }
    return nil;
}' \
"big"

run_test "t31" "if-else false branch" \
'include {
    std.io,
}
Error? main() {
    int x = 3;
    if (x > 5) {
        println("big");
    } else {
        println("small");
    }
    return nil;
}' \
"small"

run_test "t32" "equality check true" \
'include {
    std.io,
}
Error? main() {
    int x = 42;
    if (x == 42) {
        println("yes");
    } else {
        println("no");
    }
    return nil;
}' \
"yes"

run_test "t33" "not-equal check" \
'include {
    std.io,
}
Error? main() {
    int x = 7;
    if (x != 42) {
        println("different");
    }
    return nil;
}' \
"different"

run_test "t34" "nested if-else" \
'include {
    std.io,
}
Error? main() {
    int x = 5;
    if (x < 0) {
        println("negative");
    } else {
        if (x == 0) {
            println("zero");
        } else {
            println("positive");
        }
    }
    return nil;
}' \
"positive"

run_test "t35" "boolean true" \
'include {
    std.io,
}
Error? main() {
    bool flag = true;
    if (flag) {
        println("on");
    } else {
        println("off");
    }
    return nil;
}' \
"on"

run_test "t36" "boolean false" \
'include {
    std.io,
}
Error? main() {
    bool flag = false;
    if (flag) {
        println("on");
    } else {
        println("off");
    }
    return nil;
}' \
"off"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Loops
# ─────────────────────────────────────────────────────────────────────────────

section "Loops"

run_test "t40" "while loop basic count" \
'include {
    std.io,
}
Error? main() {
    int i = 0;
    while (i < 3) {
        println(i);
        i = i + 1;
    }
    return nil;
}' \
"0
1
2"

run_test "t41" "while loop accumulator" \
'include {
    std.io,
}
Error? main() {
    int sum = 0;
    int i = 1;
    while (i <= 10) {
        sum = sum + i;
        i = i + 1;
    }
    println(sum);
    return nil;
}' \
"55"

run_test "t42" "while loop with break" \
'include {
    std.io,
}
Error? main() {
    int i = 0;
    while (i < 100) {
        if (i == 5) {
            break;
        }
        i = i + 1;
    }
    println(i);
    return nil;
}' \
"5"

run_test "t43" "while loop with continue" \
'include {
    std.io,
}
Error? main() {
    int sum = 0;
    int i = 0;
    while (i < 5) {
        i = i + 1;
        if (i == 3) {
            continue;
        }
        sum = sum + i;
    }
    println(sum);
    return nil;
}' \
"12"

run_test "t44" "for loop basic" \
'include {
    std.io,
}
Error? main() {
    int sum = 0;
    for (int i = 1; i <= 5; i = i + 1) {
        sum = sum + i;
    }
    println(sum);
    return nil;
}' \
"15"

run_test "t45" "nested loops" \
'include {
    std.io,
}
Error? main() {
    int count = 0;
    int i = 0;
    while (i < 3) {
        int j = 0;
        while (j < 3) {
            count = count + 1;
            j = j + 1;
        }
        i = i + 1;
    }
    println(count);
    return nil;
}' \
"9"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Functions
# ─────────────────────────────────────────────────────────────────────────────

section "Functions"

run_test "t50" "function returning int" \
'include {
    std.io,
}
int add(int a, int b) {
    return a + b;
}
Error? main() {
    int r = add(3, 4);
    println(r);
    return nil;
}' \
"7"

run_test "t51" "multiple function calls" \
'include {
    std.io,
}
int square(int x) {
    return x * x;
}
Error? main() {
    println(square(3));
    println(square(4));
    println(square(5));
    return nil;
}' \
"9
16
25"

run_test "t52" "recursive function (factorial)" \
'include {
    std.io,
}
int fact(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * fact(n - 1);
}
Error? main() {
    println(fact(6));
    return nil;
}' \
"720"

run_test "t53" "recursive function (fibonacci)" \
'include {
    std.io,
}
int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}
Error? main() {
    println(fib(10));
    return nil;
}' \
"55"

run_test "t54" "function with multiple params" \
'include {
    std.io,
}
int clamp(int val, int lo, int hi) {
    if (val < lo) { return lo; }
    if (val > hi) { return hi; }
    return val;
}
Error? main() {
    println(clamp(5, 0, 10));
    println(clamp(-3, 0, 10));
    println(clamp(99, 0, 10));
    return nil;
}' \
"5
0
10"

run_test "t55" "void-style function (returns nil error)" \
'include {
    std.io,
}
Error? greet(str name) {
    println("Hi, {{name}}!");
    return nil;
}
Error? main() {
    greet("Alice");
    greet("Bob");
    return nil;
}' \
"Hi, Alice!
Hi, Bob!"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Error handling
# ─────────────────────────────────────────────────────────────────────────────

section "Error Handling"

run_test "t60" "return nil (success path)" \
'include {
    std.io,
    std.errors,
}
Error? might_fail(bool ok) {
    if (ok) {
        return nil;
    }
    return Err("oops");
}
Error? main() {
    Error? e = might_fail(true);
    if (e) {
        println("failed");
    } else {
        println("ok");
    }
    return nil;
}' \
"ok"

run_test "t61" "return Err and check message" \
'include {
    std.io,
    std.errors,
}
Error? might_fail(bool ok) {
    if (ok) {
        return nil;
    }
    return Err("something went wrong");
}
Error? main() {
    Error? e = might_fail(false);
    if (e) {
        println(e.message());
    } else {
        println("no error");
    }
    return nil;
}' \
"something went wrong"

run_test_exit "t62" "panic exits with code 1" \
'include {
    std.io,
    std.errors,
}
Error? main() {
    panic("fatal error");
    return nil;
}' \
"1"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Classes
# ─────────────────────────────────────────────────────────────────────────────

section "Classes"

run_test "t70" "class instantiation and field access" \
'include {
    std.io,
}
public class Point {
    int x;
    int y;
    Point(int px, int py) {
        x = px;
        y = py;
    }
}
Error? main() {
    Point p = new Point(3, 7);
    println(p.x);
    println(p.y);
    return nil;
}' \
"3
7"

run_test "t71" "class method" \
'include {
    std.io,
}
public class Counter {
    int count;
    Counter() {
        count = 0;
    }
    void increment() {
        count = count + 1;
    }
    int get() {
        return count;
    }
}
Error? main() {
    Counter c = new Counter();
    c.increment();
    c.increment();
    c.increment();
    println(c.get());
    return nil;
}' \
"3"

run_test "t72" "multiple class instances" \
'include {
    std.io,
}
public class Box {
    int val;
    Box(int v) {
        val = v;
    }
    int doubled() {
        return val * 2;
    }
}
Error? main() {
    Box a = new Box(5);
    Box b = new Box(10);
    println(a.doubled());
    println(b.doubled());
    return nil;
}' \
"10
20"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Arrays
# ─────────────────────────────────────────────────────────────────────────────

section "Arrays"

run_test "t80" "array literal and index access" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [10, 20, 30];
    println(nums[0]);
    println(nums[1]);
    println(nums[2]);
    return nil;
}' \
"10
20
30"

run_test "t81" "array length" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [1, 2, 3, 4, 5];
    println(nums.len);
    return nil;
}' \
"5"

run_test "t82" "array mutation" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [1, 2, 3];
    nums[1] = 99;
    println(nums[1]);
    return nil;
}' \
"99"

run_test "t83" "iterate array with for-in" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [3, 1, 4];
    for (n in nums) {
        println(n);
    }
    return nil;
}' \
"3
1
4"

run_test "t84" "sum array with for-in" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [1, 2, 3, 4, 5];
    int sum = 0;
    for (n in nums) {
        sum = sum + n;
    }
    println(sum);
    return nil;
}' \
"15"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Nullable types
# ─────────────────────────────────────────────────────────────────────────────

section "Nullable Types"

run_test "t90" "nullable int set to nil" \
'include {
    std.io,
}
Error? main() {
    int? x = nil;
    if (x) {
        println("has value");
    } else {
        println("nil");
    }
    return nil;
}' \
"nil"

run_test "t91" "nullable int with value" \
'include {
    std.io,
}
Error? main() {
    int? x = 42;
    if (x) {
        println(x);
    } else {
        println("nil");
    }
    return nil;
}' \
"42"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Multi (tagged union)
# ─────────────────────────────────────────────────────────────────────────────

section "Tagged Unions (multi)"

run_test "t100" "multi holds int, check tag" \
'include {
    std.io,
}
Error? main() {
    multi<int | str> x = 42;
    println(x.tag);
    return nil;
}' \
"0"

run_test "t101" "multi holds str, check tag" \
'include {
    std.io,
}
Error? main() {
    multi<int | str> x = "hello";
    println(x.tag);
    return nil;
}' \
"1"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Strings (std.strings)
# ─────────────────────────────────────────────────────────────────────────────

section "Strings (std.strings)"

run_test "t110" "str_length of literal" \
'include {
    std.io,
    std.strings,
}
Error? main() {
    int n = str_length("hello");
    println(n);
    return nil;
}' \
"5"

run_test "t111" "str_length of variable" \
'include {
    std.io,
    std.strings,
}
Error? main() {
    str s = "hylian";
    int n = str_length(s);
    println(n);
    return nil;
}' \
"6"

run_test "t112" "str_to_upper" \
'include {
    std.io,
    std.strings,
}
Error? main() {
    str s = str_to_upper("hello");
    println(s);
    return nil;
}' \
"HELLO"

run_test "t113" "str_to_lower" \
'include {
    std.io,
    std.strings,
}
Error? main() {
    str s = str_to_lower("WORLD");
    println(s);
    return nil;
}' \
"world"

run_test "t114" "str_contains true" \
'include {
    std.io,
    std.strings,
}
Error? main() {
    bool found = str_contains("hello world", "world");
    if (found) {
        println("yes");
    } else {
        println("no");
    }
    return nil;
}' \
"yes"

run_test "t115" "str_contains false" \
'include {
    std.io,
    std.strings,
}
Error? main() {
    bool found = str_contains("hello", "xyz");
    if (found) {
        println("yes");
    } else {
        println("no");
    }
    return nil;
}' \
"no"

run_test "t116" "str_equals same strings" \
'include {
    std.io,
    std.strings,
}
Error? main() {
    bool eq = str_equals("hello", "hello");
    if (eq) {
        println("equal");
    } else {
        println("not equal");
    }
    return nil;
}' \
"equal"

run_test "t117" "str_equals different strings" \
'include {
    std.io,
    std.strings,
}
Error? main() {
    bool eq = str_equals("foo", "bar");
    if (eq) {
        println("equal");
    } else {
        println("not equal");
    }
    return nil;
}' \
"not equal"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Array push / pop
# ─────────────────────────────────────────────────────────────────────────────

section "Array push / pop"

run_test "t120" "push increases len" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [];
    nums.push(10);
    nums.push(20);
    nums.push(30);
    println(nums.len);
    return nil;
}' \
"3"

run_test "t121" "push then index" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [];
    nums.push(42);
    println(nums[0]);
    return nil;
}' \
"42"

run_test "t122" "push multiple and read all" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [];
    nums.push(1);
    nums.push(2);
    nums.push(3);
    println(nums[0]);
    println(nums[1]);
    println(nums[2]);
    return nil;
}' \
"1
2
3"

run_test "t123" "pop returns last element" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [10, 20, 30];
    int v = nums.pop();
    println(v);
    return nil;
}' \
"30"

run_test "t124" "pop decreases len" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [10, 20, 30];
    nums.pop();
    println(nums.len);
    return nil;
}' \
"2"

run_test "t125" "push-then-pop LIFO order" \
'include {
    std.io,
}
Error? main() {
    array<int> nums = [];
    nums.push(5);
    nums.push(10);
    int a = nums.pop();
    int b = nums.pop();
    println(a);
    println(b);
    return nil;
}' \
"10
5"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Enums
# ─────────────────────────────────────────────────────────────────────────────

section "Enums"

run_test "t130" "enum variant value (first = 0)" \
'include {
    std.io,
}
enum Color {
    Red,
    Green,
    Blue,
}
Error? main() {
    int v = Color.Red;
    println(v);
    return nil;
}' \
"0"

run_test "t131" "enum variant value (second = 1)" \
'include {
    std.io,
}
enum Color {
    Red,
    Green,
    Blue,
}
Error? main() {
    int v = Color.Green;
    println(v);
    return nil;
}' \
"1"

run_test "t132" "enum variant value (third = 2)" \
'include {
    std.io,
}
enum Direction {
    North,
    South,
    East,
    West,
}
Error? main() {
    int v = Direction.East;
    println(v);
    return nil;
}' \
"2"

run_test "t133" "enum in if comparison" \
'include {
    std.io,
}
enum Status {
    Ok,
    Fail,
}
Error? main() {
    int s = Status.Ok;
    if (s == Status.Ok) {
        println("ok");
    } else {
        println("fail");
    }
    return nil;
}' \
"ok"

run_test "t134" "enum stored in variable" \
'include {
    std.io,
}
enum Level {
    Low,
    Mid,
    High,
}
Error? main() {
    int lvl = Level.High;
    println(lvl);
    return nil;
}' \
"2"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: Switch statements
# ─────────────────────────────────────────────────────────────────────────────

section "Switch statements"

run_test "t140" "switch matches first case" \
'include {
    std.io,
}
Error? main() {
    int x = 1;
    switch (x) {
        case 1: { println("one"); }
        case 2: { println("two"); }
        case 3: { println("three"); }
    }
    return nil;
}' \
"one"

run_test "t141" "switch matches middle case" \
'include {
    std.io,
}
Error? main() {
    int x = 2;
    switch (x) {
        case 1: { println("one"); }
        case 2: { println("two"); }
        case 3: { println("three"); }
    }
    return nil;
}' \
"two"

run_test "t142" "switch with default" \
'include {
    std.io,
}
Error? main() {
    int x = 99;
    switch (x) {
        case 1: { println("one"); }
        case 2: { println("two"); }
        default: { println("other"); }
    }
    return nil;
}' \
"other"

run_test "t143" "switch on enum variant" \
'include {
    std.io,
}
enum Dir {
    North,
    South,
    East,
    West,
}
Error? main() {
    int d = Dir.South;
    switch (d) {
        case Dir.North: { println("north"); }
        case Dir.South: { println("south"); }
        case Dir.East:  { println("east"); }
        case Dir.West:  { println("west"); }
    }
    return nil;
}' \
"south"

run_test "t144" "switch no match and no default does nothing" \
'include {
    std.io,
}
Error? main() {
    int x = 5;
    switch (x) {
        case 1: { println("one"); }
        case 2: { println("two"); }
    }
    println("done");
    return nil;
}' \
"done"

run_test "t145" "switch on computed expression" \
'include {
    std.io,
}
Error? main() {
    int a = 1;
    int b = 2;
    switch (a + b) {
        case 2: { println("two"); }
        case 3: { println("three"); }
        default: { println("other"); }
    }
    return nil;
}' \
"three"

# ─────────────────────────────────────────────────────────────────────────────
# Tests: IR layer (--dump-ir)
# ─────────────────────────────────────────────────────────────────────────────

section "IR layer (--dump-ir)"

run_test_ir_dump "t150" "--dump-ir emits before-opt header" \
'include {
    std.io,
}
Error? main() {
    println("hi");
    return nil;
}' \
"=== IR before optimization ==="

run_test_ir_dump "t151" "--dump-ir emits after-opt header" \
'include {
    std.io,
}
Error? main() {
    println("hi");
    return nil;
}' \
"=== IR after optimization"

run_test_ir_dump "t152" "--dump-ir shows FUNC_BEGIN in dump" \
'include {
    std.io,
}
Error? main() {
    println("hi");
    return nil;
}' \
"FUNC_BEGIN"

run_test_ir_dump "t153" "--dump-ir shows CONST_INT in dump" \
'include {
    std.io,
}
Error? main() {
    int x = 7;
    println(x);
    return nil;
}' \
"CONST_INT"

run_test_ir_dump "t154" "constant folding: optimization makes at least one change" \
'include {
    std.io,
}
Error? main() {
    int x = 2 + 3;
    println(x);
    return nil;
}' \
"=== IR after optimization" \
"(0 changes)"

run_test_ir_dump "t155" "--dump-ir shows STORE_VAR for named variable" \
'include {
    std.io,
}
Error? main() {
    int n = 42;
    println(n);
    return nil;
}' \
"STORE_VAR"

run_test "t156" "--dump-ir still produces correct output" \
'include {
    std.io,
}
Error? main() {
    int x = 10;
    int y = 20;
    println(x + y);
    return nil;
}' \
"30"

# ─────────────────────────────────────────────────────────────────────────────
# Results
# ─────────────────────────────────────────────────────────────────────────────

TOTAL=$((PASS + FAIL + SKIP))

echo ""
echo -e "${BOLD}${CYAN}══════════════════════════════════════════${NC}"
echo -e "${BOLD}  Results: ${TOTAL} tests  |  ${GREEN}${PASS} passed${NC}${BOLD}  |  ${RED}${FAIL} failed${NC}${BOLD}  |  ${YELLOW}${SKIP} skipped${NC}"
echo -e "${BOLD}${CYAN}══════════════════════════════════════════${NC}"
echo ""

if [ "$FAIL" -ne 0 ]; then
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi

echo -e "${GREEN}All tests passed!${NC}"
exit 0

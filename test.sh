#!/bin/bash

# Hylian Compiler Test Suite (ASM Backend)

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

echo "=== Building Hylian Compiler ==="
cd src
bison -d parser.y
flex lexer.l
gcc lex.yy.c parser.tab.c ast.c codegen_asm.c compiler.c -o ../hylian
cd ..
echo -e "${GREEN}✓ Build successful${NC}\n"

# ── Runtime object resolution ─────────────────────────────────────────────────
# For each runtime module, prefer a pre-built .o file over assembling .asm.
# This mirrors what the build system (build_runtime.py) produces.

resolve_runtime_obj() {
    local stem="$1"     # e.g. runtime/std/io_linux
    local out="$2"      # destination .o name, e.g. io_runtime.o
    local fmt="elf64"

    if [ -f "${stem}.o" ]; then
        cp "${stem}.o" "${out}"
        echo -e "  ${GREEN}✓ using pre-built ${stem}.o${NC}"
    elif [ -f "${stem}.c" ]; then
        gcc -O2 -c "${stem}.c" -o "${out}"
        echo -e "  ${GREEN}✓ compiled ${stem}.c${NC}"
    elif [ -f "${stem}.asm" ]; then
        nasm -f ${fmt} "${stem}.asm" -o "${out}"
        echo -e "  ${GREEN}✓ assembled ${stem}.asm${NC}"
    else
        echo -e "  ${RED}✗ runtime module not found: ${stem}${NC}"
        exit 1
    fi
}

echo "=== Resolving IO Runtime ==="
resolve_runtime_obj "runtime/std/io_linux" "io_runtime.o"
echo ""

PASS=0
FAIL=0

run_test() {
    local test_num="$1"
    local desc="$2"
    local hy_file="$3"
    local expected="$4"
    # optional: extra .o files to link (e.g. errors_runtime.o)
    local extra_objs="${5:-}"

    echo "=== Test ${test_num}: ${desc} ==="

    ./hylian "${hy_file}" -o "test_${test_num}.asm"
    nasm -f elf64 "test_${test_num}.asm" -o "test_${test_num}.o"
    gcc "test_${test_num}.o" io_runtime.o ${extra_objs} -o "test_${test_num}_bin" -no-pie

    actual=$("./test_${test_num}_bin" 2>&1)

    if [ "$actual" = "$expected" ]; then
        echo -e "${GREEN}✓ Test ${test_num} passed${NC}"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}✗ Test ${test_num} failed${NC}"
        echo "  Expected: $expected"
        echo "  Got:      $actual"
        FAIL=$((FAIL + 1))
    fi
    echo ""
}

# Test 1: Hello World
cat > tests/hello_asm.hy << 'EOF'
include {
    std.io,
}

Error? main() {
    println("Hello, World!");
    return nil;
}
EOF

run_test 1 "Hello World" "tests/hello_asm.hy" "Hello, World!"

# Test 2: println with integer
cat > test_2.hy << 'EOF'
include {
    std.io,
}

Error? main() {
    int x = 42;
    println(x);
    return nil;
}
EOF

run_test 2 "println with integer" "test_2.hy" "42"

# Test 3: while loop with break
cat > test_3.hy << 'EOF'
include {
    std.io,
}

Error? main() {
    int i = 0;
    while (i < 10) {
        if (i == 5) {
            break;
        }
        i = i + 1;
    }
    println(i);
    return nil;
}
EOF

run_test 3 "while loop with break" "test_3.hy" "5"

# Test 4: for loop with continue
cat > test_4.hy << 'EOF'
include {
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
}
EOF

run_test 4 "for loop with continue" "test_4.hy" "12"

# Test 5: String interpolation
cat > test_5.hy << 'EOF'
include {
    std.io,
}

Error? main() {
    int x = 7;
    println("value is {{x}}");
    return nil;
}
EOF

run_test 5 "String interpolation" "test_5.hy" "value is 7"

# Test 6: Function call
cat > test_6.hy << 'EOF'
include {
    std.io,
}

int add(int a, int b) {
    int result = a + b;
    return result;
}

Error? main() {
    int r = add(3, 4);
    println(r);
    return nil;
}
EOF

run_test 6 "Function call" "test_6.hy" "7"

# Cleanup
rm -f test_2.hy test_3.hy test_4.hy test_5.hy test_6.hy
rm -f test_1.asm test_2.asm test_3.asm test_4.asm test_5.asm test_6.asm
rm -f test_1.o test_2.o test_3.o test_4.o test_5.o test_6.o
rm -f test_1_bin test_2_bin test_3_bin test_4_bin test_5_bin test_6_bin
rm -f io_runtime.o errors_runtime.o filesystem_runtime.o env_runtime.o

echo "=========================="
echo -e "Results: ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}"
echo "=========================="

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi

echo -e "${GREEN}=== All Tests Passed! ===${NC}"

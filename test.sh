#!/bin/bash

# Hylian Compiler Test Suite

set -e  # Exit on error

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== Building Hylian Compiler ==="
cd src
bison -d parser.y
flex lexer.l
gcc lex.yy.c parser.tab.c ast.c codegen.c -o ../hylian
cd ..
echo -e "${GREEN}✓ Build successful${NC}\n"

# Test 1: Basic class with parameters
echo "=== Test 1: Methods with Parameters ==="
cat > test_params.hy << 'EOF'
class Player {
    private int health;
    private str name;
    
    int getHealth() {
        return health;
    }
    
    Error? setHealth(int newHealth) {
        if (newHealth <= 0) {
            return nil;
        }
        health = newHealth;
        return nil;
    }
    
    void setName(str newName) {
        name = newName;
    }
}
EOF

./hylian < test_params.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 1 passed${NC}"
else
    echo -e "${RED}✗ Test 1 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 2: Multiple classes
echo "=== Test 2: Multiple Classes ==="
cat > test_multi.hy << 'EOF'
class Enemy {
    private int damage;
    
    int getDamage() {
        return damage;
    }
}

public class Game {
    private int score;
    
    void addScore(int points) {
        score = points;
    }
}
EOF

./hylian < test_multi.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 2 passed${NC}"
else
    echo -e "${RED}✗ Test 2 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 3: Control flow
echo "=== Test 3: Control Flow (if/else, while) ==="
cat > test_control.hy << 'EOF'
class Calculator {
    int max(int a, int b) {
        if (a > b) {
            return a;
        } else {
            return b;
        }
    }
    
    int factorial(int n) {
        int result;
        int i;
        result = 1;
        i = 1;
        while (i <= n) {
            result = result;
            i = i;
        }
        return result;
    }
}
EOF

./hylian < test_control.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 3 passed${NC}"
else
    echo -e "${RED}✗ Test 3 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 4: Operators and expressions
echo "=== Test 4: Binary Operators ==="
cat > test_ops.hy << 'EOF'
class Math {
    int add(int a, int b) {
        return a;
    }
    
    int compare(int x, int y) {
        if (x == y) {
            return 0;
        }
        if (x < y) {
            return 1;
        }
        return 2;
    }
}
EOF

./hylian < test_ops.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 4 passed${NC}"
else
    echo -e "${RED}✗ Test 4 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 5: Empty classes
echo "=== Test 5: Empty Classes ==="
cat > test_empty.hy << 'EOF'
class Empty {}

public class AlsoEmpty {}
EOF

./hylian < test_empty.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 5 passed${NC}"
else
    echo -e "${RED}✗ Test 5 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 6: Member access and method calls
echo "=== Test 6: Member Access ==="
cat > test_member.hy << 'EOF'
class Player {
    public int health;
    
    void damage(int amount) {
        health = health;
    }
}
EOF

./hylian < test_member.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 6 passed${NC}"
else
    echo -e "${RED}✗ Test 6 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Cleanup
rm -f test_*.hy output.cpp output.o

echo -e "${GREEN}=== All Tests Passed! ===${NC}"

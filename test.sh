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

# Test 7: Function call arguments
echo "=== Test 7: Function Call Arguments ==="
cat > test_args.hy << 'EOF'
class Player {
    private int health;
    private str name;

    Error? setHealth(int newHealth) {
        if (newHealth < 0) {
            return nil;
        }
        health = newHealth;
        return nil;
    }

    void setName(str newName) {
        name = newName;
    }

    int getHealth() {
        return health;
    }
}

Error? main() {
    Player p = new Player();
    p.setHealth(100);
    p.setName("Alice");
    return nil;
}
EOF

./hylian < test_args.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 7 passed${NC}"
else
    echo -e "${RED}✗ Test 7 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 8: Constructors with parameters
echo "=== Test 8: Constructors with Parameters ==="
cat > test_ctor.hy << 'EOF'
public class Enemy {
    Enemy(int dmg, str tag) {
        damage = dmg;
        name = tag;
    }
    private int damage;
    private str name;

    int getDamage() {
        return damage;
    }
}

Error? main() {
    Enemy e = new Enemy(10, "Goomba");
    return nil;
}
EOF

./hylian < test_ctor.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 8 passed${NC}"
else
    echo -e "${RED}✗ Test 8 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 9: Error? main() compiles to int main()
echo "=== Test 9: Error? main() entry point ==="
cat > test_main.hy << 'EOF'
class Greeter {
    void greet(str who) {
        name = who;
    }
    private str name;
}

Error? main() {
    Greeter g = new Greeter();
    g.greet("World");
    return nil;
}
EOF

./hylian < test_main.hy > /dev/null
# Must fully link (not just -c) since main must return int
if g++ -std=c++17 output.cpp -o test_main_bin 2>/dev/null; then
    echo -e "${GREEN}✓ Test 9 passed${NC}"
else
    echo -e "${RED}✗ Test 9 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 10: := declare-assign (auto type inference)
echo "=== Test 10: := Declare-Assign ==="
cat > test_decl.hy << 'EOF'
class Calc {
    int add(int a, int b) {
        result := a + b;
        return result;
    }
}

Error? main() {
    x := 42;
    return nil;
}
EOF

./hylian < test_decl.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 10 passed${NC}"
else
    echo -e "${RED}✗ Test 10 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 11: String interpolation - basic variables
echo "=== Test 11: String Interpolation (variables) ==="
cat > test_interp1.hy << 'EOF'
Error? main() {
    str name = "Alice";
    int score = 42;
    str msg = "hello {{name}}, your score is {{score}}!";
    str plain = "no interpolation here";
    return nil;
}
EOF

./hylian < test_interp1.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 11 passed${NC}"
else
    echo -e "${RED}✗ Test 11 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 12: String interpolation - method calls and expressions
echo "=== Test 12: String Interpolation (expressions) ==="
cat > test_interp2.hy << 'EOF'
public class Player {
    Player(str n, int h) {
        name = n;
        health = h;
    }
    private str name;
    private int health;

    int getHealth() { return health; }
    str getName() { return name; }
}

Error? main() {
    Player p = new Player("Bob", 75);
    str status = "Player {{p.getName()}} has {{p.getHealth()}} HP";
    str math = "2 + 2 = {{2 + 2}}";
    return nil;
}
EOF

./hylian < test_interp2.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 12 passed${NC}"
else
    echo -e "${RED}✗ Test 12 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 13: String interpolation - runtime correctness
echo "=== Test 13: String Interpolation (runtime output) ==="
cat > test_interp3.hy << 'EOF'
Error? main() {
    str name = "World";
    int x = 7;
    str result = "Hello {{name}}, {{x}} times {{x}} is {{x * x}}";
    return nil;
}
EOF

./hylian < test_interp3.hy > /dev/null
sed 's/return 0;/printf("%s\\n", result.c_str()); return 0;/' output.cpp > test_interp3_run.cpp
if g++ -std=c++17 test_interp3_run.cpp -o test_interp3_bin 2>/dev/null; then
    output=$(./test_interp3_bin)
    expected="Hello World, 7 times 7 is 49"
    if [ "$output" = "$expected" ]; then
        echo -e "${GREEN}✓ Test 13 passed${NC}"
    else
        echo -e "${RED}✗ Test 13 failed: got '$output', expected '$expected'${NC}"
        exit 1
    fi
else
    echo -e "${RED}✗ Test 13 failed to compile${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 14: array<T> flexible
echo "=== Test 14: array<T> flexible ==="
cat > test_array_flex.hy << 'EOF'
Error? main() {
    array<int> nums = [1, 2, 3, 4, 5];
    int x = nums[0];
    nums[1] = 99;
    return nil;
}
EOF

./hylian < test_array_flex.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 14 passed${NC}"
else
    echo -e "${RED}✗ Test 14 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 15: array<T, N> fixed
echo "=== Test 15: array<T, N> fixed ==="
cat > test_array_fixed.hy << 'EOF'
Error? main() {
    array<int, 3> nums = [10, 20, 30];
    int x = nums[2];
    return nil;
}
EOF

./hylian < test_array_fixed.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 15 passed${NC}"
else
    echo -e "${RED}✗ Test 15 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 16: multi<A | B> flexible
echo "=== Test 16: multi<A | B> flexible ==="
cat > test_multi_union.hy << 'EOF'
Error? main() {
    multi<str | int> mixed = ["hello", 42, "world"];
    return nil;
}
EOF

./hylian < test_multi_union.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 16 passed${NC}"
else
    echo -e "${RED}✗ Test 16 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Test 17: multi<any> flexible
echo "=== Test 17: multi<any> flexible ==="
cat > test_multi_any.hy << 'EOF'
Error? main() {
    multi<any> stuff = ["hello", 42, true];
    return nil;
}
EOF

./hylian < test_multi_any.hy > /dev/null
if g++ -std=c++17 -c output.cpp -o output.o 2>/dev/null; then
    echo -e "${GREEN}✓ Test 17 passed${NC}"
else
    echo -e "${RED}✗ Test 17 failed${NC}"
    cat output.cpp
    exit 1
fi
echo ""

# Cleanup
rm -f test_*.hy output.cpp output.o test_main_bin test_interp3_run.cpp test_interp3_bin

echo -e "${GREEN}=== All Tests Passed! ===${NC}"

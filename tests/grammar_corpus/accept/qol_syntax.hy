/* block comment
   over two lines */

typedef int myint;
typedef uint8 octet;

int counter = 10;
float ratio = 2.5;

enum Level { Low, Mid = 5, High }

int add(int a, int b = 5) {
    return a + b;
}

void features() {
    int s = 0;
    for (int i = 0; i < 5; i++) { s += i; }
    for (int j = 10; j > 0; j -= 3) { s += 1; }
    for (k in 0..4) { s += k; }
    for (m in 1..=3) { s += m; }

    int t = s > 2 ? 10 : 20;
    int u = s > 5 ? 1 : s > 2 ? 2 : 3;

    int n = 0;
    do { n += 1; } while (n < 3);

    int a = 'a' + '\n' + '\x41' + 0xFF + 0b1010 + 0o17 + 1_000;
    int z = sizeof(int) + sizeof(uint8);
    float f = 1.5e3 + 2.0e-2;

    int c = 6;
    c *= 2; c %= 4; c <<= 2; c >>= 1; c &= 7; c |= 1; c ^= 2;
    array<int> v = [1, 2, 3];
    v[1] += 10;
    array<array<int>> grid = [];
    int buf[16];
    buf[0] = 1;
    str name = "value {{s + 1}} and {{f}}";
    defer cleanup();
    defer { cleanup(); n += 1; }
}

void cleanup() {
}

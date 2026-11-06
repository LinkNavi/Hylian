include { std.io, }

// fixed-size array, indices provably in range / one past the end / escaping via push
int in_range() {
    int a[100];
    int s = 0;
    for (int i = 0; i < 10; i++) { for (int j = 0; j < 10; j++) { a[i * 10 + j] = i + j; } }
    for (int i = 0; i < 10; i++) { for (int j = 0; j < 10; j++) { s += a[i * 10 + j] * (j + 1); } }
    return s;
}
int past_end() {
    int a[99];
    int s = 0;
    for (int i = 0; i < 10; i++) { for (int j = 0; j < 10; j++) { a[i * 10 + j] = 1; s += a[i * 10 + j]; } }
    return s;
}
int grows() {
    int a[4];
    for (int i = 0; i < 8; i++) { a.push(i); }
    int s = 0;
    for (int i = 0; i < 12; i++) { s += a[i]; }
    return s * 100 + a.len;
}
int neg_start() {
    int a[10];
    int s = 0;
    for (int i = 0; i < 10; i++) { a[i] = i + 1; }
    for (int i = 0; i < 10; i++) { s += a[i - 1] + a[i + 1]; }
    return s;
}
// early-return function with a recursive slow path (shrink-wrapped prologue)
int fib(int n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
int pick(int a, int b) {
    if (a < b) { return a; }
    int t = pick(b, a - 1) + pick(a - 2, b);
    return t + 1;
}
// x / 2^k only inside the arm where x % 2^k == 0, and also outside it
int halve(int x) {
    int r = 0;
    if (x % 4 == 0) { r = x / 4 + x / 2; } else { r = x / 4 - x / 2; }
    if (x % 2 != 0) { r += 100; } else { r += x / 2; }
    return r;
}
Error? main() {
    println(in_range());
    println(past_end());
    println(grows());
    println(neg_start());
    println(fib(20));
    println(pick(9, 3));
    for (int x = -9; x <= 9; x++) { println(halve(x)); }
    int best = 0;
    for (int n = 1; n < 3000; n++) {
        int x = n; int st = 0;
        while (x != 1) { if (x % 2 == 0) { x = x / 2; } else { x = 3 * x + 1; } st += 1; }
        if (st > best) { best = st; }
    }
    println(best);
    return nil;
}

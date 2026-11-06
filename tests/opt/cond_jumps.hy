include { std.io, }
int calls = 0;
bool t(int x) { calls += 1; return x > 0; }
bool f(int x) { calls += 1; return x < 0; }
int classify(int a, int b, int c) {
    int r = 0;
    if (a > 0 && b > 0) { r += 1; } else { r += 10; }
    if (a > 0 || b > 0) { r += 100; }
    if (a > 0 && (b > 0 || c > 0)) { r += 1000; }
    if ((a > 0 || b > 0) && (c > 0 || a < -5)) { r += 10000; }
    if (a > 0 && b > 0 && c > 0) { r += 100000; }
    if (!(a > 0) || c == 3) { r += 1000000; }
    return r;
}
Error? main() {
    for (int a = -7; a <= 3; a += 5) { for (int b = -1; b <= 1; b++) { for (int c = 0; c <= 3; c += 3) { println(classify(a, b, c)); } } }
    // evaluation order and short-circuit side effects
    if (t(1) && f(1) && t(1)) { println(1); }
    if (f(1) || t(1) || f(1)) { println(2); }
    if (t(1) || f(1)) { println(3); }
    if (f(1) && t(1)) { println(4); }
    println(calls);
    int i = 0; int n = 0;
    while (i < 50 && n < 20 || i == 7) { i += 1; n += 2; }
    println(i * 100 + n);
    for (int k = 0; k < 10 && k * k < 30; k++) { n += k; }
    println(n);
    return nil;
}

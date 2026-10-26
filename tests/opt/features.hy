include { std.io, std.string, }
enum Color { Red, Green = 5, Blue }
int sw(int x) {
    switch (x) {
        case 1: { return 10; }
        case 2: { return 20; }
        default: { return 99; }
    }
    return 0;
}
int dfr(int n) { int r = 0; defer { r = 5; } if (n > 1) { return n * 2; } return n; }
uint8 wrap8(uint8 a) { return a + 250; }
int32 wrap32(int32 a) { return a * 65536 * 65536; }
float32 f32(float32 a) { return a * 2.0; }
str greet(str n) { return "hi " + n; }
int fact(int n) { if (n <= 1) { return 1; } return n * fact(n - 1); }
int fibi(int n) { int a = 0; int b = 1; for (int i = 0; i < n; i++) { int t = a + b; a = b; b = t; } return a; }
Error? main() {
    println(sw(1) + sw(2) + sw(7));
    println(dfr(1) * 100 + dfr(4));
    println(wrap8(10));
    println(wrap32(3));
    println(cast<int>(f32(2.25) * 100.0));
    println(greet("bob"));
    println(fact(10));
    println(fibi(50));
    println(Color.Blue + Color.Green);
    int s = 0;
    for (i in 0..10) { if (i % 2 == 0) { continue; } if (i > 7) { break; } s += i; }
    println(s);
    str w = "hello world";
    println(length(w));
    println(index_of(w, "wor"));
    println(to_upper(slice(w, 0, 5)));
    int i = 0;
    do { i += 3; } while (i < 10);
    println(i);
    int k = 5;
    println(k > 3 ? (k > 4 ? 1 : 2) : 3);
    println("v={{k * 2}} {{w}}");
    return nil;
}

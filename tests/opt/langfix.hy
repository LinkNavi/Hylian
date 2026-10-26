include { std.io, }
// Regression tests for language bugs found while optimizing: struct copy /
// assignment / return, array<float> reads, empty constructors, `new` on structs.
struct P { int a; int b; int c; }
P mk(int x) { P r; r.a = x; r.b = x + 1; r.c = x + 2; return r; }
class Box { public int a; public int b; public int c; Box() { } }
class Counter { public int n; Counter(int start) { n = start; } int bump() { n += 1; return n; } }
struct Pt { int x; int y; }
int dirty() { Box x = new Box(); x.a = 111; x.b = 222; x.c = 333; return x.a; }
int clean() { Box y = new Box(); return y.a + y.b + y.c; }
float half(array<float> f, int i) { return f[i] / 2.0; }
Error? main() {
    P x; x.a = 1; x.b = 2; x.c = 3;
    P y = x;
    y.a = 10;
    println(y.a + y.b * 10 + y.c * 100);
    P z; z = x;
    println(z.a + z.b * 10 + z.c * 100);
    P w = mk(5);
    println(w.a + w.b * 10 + w.c * 100);
    P v; v = mk(7);
    println(v.a + v.b * 10 + v.c * 100);
    array<float> f = [];
    f.push(1.5); f.push(2.5);
    float q = f[1];
    println(cast<int>(q * 10.0));
    println(cast<int>(f[0] * 10.0));
    println(cast<int>(half(f, 1) * 100.0));
    float sf = 0.0;
    for (e in f) { sf += e; }
    println(cast<int>(sf * 10.0));
    println(cast<int>(f.pop() * 10.0));
    int t = 0;
    for (int i = 0; i < 100; i++) { t += dirty(); t += clean(); }
    println(t);
    Counter c = new Counter(5);
    c.bump(); c.bump();
    println(c.n);
    Pt p = new Pt();
    p.x = 4; p.y = 5;
    println(p.x * 10 + p.y);
    Pt r = new Pt();
    println(r.x + r.y);
    return nil;
}

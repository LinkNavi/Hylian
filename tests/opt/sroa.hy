include { std.io, }
struct P { int a; int b; float f; }
int sum(P p) { return p.a * 3 + p.b; }
float fsum(P p) { return p.f * 2.0 + p.a; }
int rs(P p, int n) {
    if (n == 0) { return p.a + p.b; }
    P q;
    q.a = p.b;
    q.b = p.a + n;
    q.f = p.f;
    return rs(q, n - 1);
}
Error? main() {
    P x; x.a = 1; x.b = 2; x.f = 1.5;
    P y; y.a = x.a; y.b = x.b; y.f = x.f;
    y.a = 10;
    println(sum(x));
    println(sum(y));
    println(cast<int>(fsum(x) * 10.0));
    println(cast<int>(fsum(y) * 10.0));
    int t = 0;
    for (int i = 0; i < 100; i++) {
        x.a += i;
        P z; z.a = x.a; z.b = x.b; z.f = x.f;
        z.b = z.b + 1;
        t += sum(z) + cast<int>(z.f);
        if (i % 10 == 0) { x.a = y.a; x.b = y.b; x.f = y.f; }
    }
    println(t);
    println(x.a);
    println(rs(x, 5));
    return nil;
}

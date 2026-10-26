include { std.io, }
struct V3 { float x; float y; float z; }
struct Small { uint8 a; uint8 b; int32 c; }
V3 vadd(V3 a, V3 b) { V3 r; r.x = a.x + b.x; r.y = a.y + b.y; r.z = a.z + b.z; return r; }
V3 vscale(V3 a, float k = 2.0) { V3 r; r.x = a.x * k; r.y = a.y * k; r.z = a.z * k; return r; }
V3 rec(V3 a, int n) { if (n == 0) { return a; } return rec(vadd(a, a), n - 1); }
float vlen2(V3 a) { return a.x * a.x + a.y * a.y + a.z * a.z; }
Small mk(int a) { Small s; s.a = a; s.b = a + 1; s.c = a * 1000; return s; }
int sm(Small s) { return s.a + s.b + s.c; }
class Acc {
    int total;
    Acc() { total = 0; }
    void add(int n) { total += n; }
    int get() { return total; }
}
Error? main() {
    V3 a; a.x = 1.0; a.y = 2.0; a.z = 3.0;
    V3 b = vadd(a, vscale(a));
    println(cast<int>(vlen2(b)));
    V3 c = rec(a, 3);
    println(cast<int>(c.x + c.y + c.z));
    Small s = mk(7);
    println(sm(s));
    Small t = s;
    t.c = 1;
    println(sm(t) * 10000 + sm(s) % 10000);
    Acc acc = new Acc();
    for (int i = 0; i < 10; i++) { acc.add(i); }
    println(acc.get());
    array<int> arr = [5, 3, 8];
    arr[1] += 10;
    arr[2] <<= 1;
    println(arr[0] + arr[1] * 10 + arr[2] * 100);
    return nil;
}

include { std.io, }
struct Seg { (int, int) a; (int, int) b; int id; }
int sum((int, int) p, int k) { return p.0 + p.1 + k; }
(int, int) mk(int a) { return (a, a * 2); }
int main() {
    (int, int) t = mk(5);
    println("{{sum(t, 100)}}");
    Seg s;
    s.a = mk(1); s.b = mk(10); s.id = 3;
    println("{{s.a.0}} {{s.a.1}} {{s.b.0}} {{s.b.1}} {{s.id}} {{sum(s.b, 0)}}");
    println("{{sum((7, 8), 1)}}");
    return 0;
}

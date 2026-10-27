include { std.io, }

struct Pt { int x; int y; int z; }

Pt make(int a, int b) { Pt p = Pt { x: a, y: b, z: a + b }; return p; }
Pt flip(int a, int b) { Pt p = Pt { x: b, y: a, z: 0 }; return p; }

Pt build(fn(int, int) -> Pt f, int a, int b) { return f(a, b); }

int main() {
    fn(int, int) -> Pt f = addrof_fn(make);
    Pt p = f(3, 4);
    println("{{p.x}} {{p.y}} {{p.z}}");
    Pt q = build(addrof_fn(flip), 1, 2);
    println("{{q.x}} {{q.y}} {{q.z}}");
    return 0;
}

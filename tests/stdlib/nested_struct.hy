include { std.io, }

struct P { int x; int y; }
struct L { P a; P b; int n; }

L make(int k) {
    P q; q.x = k; q.y = k + 1;
    return L { a: q, b: P { x: k * 2, y: k * 3 }, n: k };
}

int main() {
    L l = L { a: P { x: 1, y: 2 }, b: P { x: 3, y: 4 }, n: 5 };
    println("{{l.a.x}} {{l.a.y}} {{l.b.x}} {{l.b.y}} {{l.n}}");
    L m = make(7);
    println("{{m.a.x}} {{m.a.y}} {{m.b.x}} {{m.b.y}} {{m.n}}");
    return 0;
}

include { std.io, }
struct Pt { int x; int y; }
struct Line { Pt a; Pt b; }
Pt mkp(int x, int y) { Pt p; p.x = x; p.y = y; return p; }
Line mkl() { Line l; l.a = mkp(1, 2); l.b = mkp(3, 4); return l; }
int len2(Line l) { int dx = l.b.x - l.a.x; int dy = l.b.y - l.a.y; return dx * dx + dy * dy; }
void bump(Pt p) { p.x = 99; }
Pt swap(Pt p) { Pt r; r.x = p.y; r.y = p.x; return r; }
Error? main() {
    Line l = mkl();
    println(l.a.x * 1000 + l.a.y * 100 + l.b.x * 10 + l.b.y);
    println(len2(l));
    Pt p = mkp(7, 8);
    bump(p);
    println(p.x);
    Pt q = swap(p);
    println(q.x * 10 + q.y);
    Pt r = swap(swap(p));
    println(r.x * 10 + r.y);
    l.b = p;
    println(l.b.x * 10 + l.b.y);
    Line m = l;
    m.a.x = 50;
    println(l.a.x * 100 + m.a.x);
    return nil;
}

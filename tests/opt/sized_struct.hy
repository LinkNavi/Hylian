include { std.io, }
struct Small { uint8 a; uint8 b; int32 c; }
int sm(Small s) { return s.a + s.b + s.c; }
Small mk(int a) { Small s; s.a = a; s.b = a + 1; s.c = a * 1000; return s; }
Error? main() {
    Small s; s.a = 7; s.b = 8; s.c = 7000;
    println(s.a); println(s.b); println(s.c);
    println(sm(s));
    Small t = mk(7);
    println(t.a); println(t.b); println(t.c);
    Small u = s;
    println(u.c);
    return nil;
}

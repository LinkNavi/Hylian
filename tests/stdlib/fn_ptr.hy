include { std.io, }

int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
float half(float x) { return x / 2.0; }
float scale(float x, int n) { return x * n; }

int apply(fn(int, int) -> int f, int a, int b) { return f(a, b); }

int inc(int x) { return x + 1; }
int dbl(int x) { return x * 2; }
struct Handler { int id; fn(int) -> int run; }

int main() {
    fn(int, int) -> int op = addrof_fn(add);
    println("add: {{op(3, 4)}}");
    op = addrof_fn(mul);
    println("mul: {{op(3, 4)}}");
    println("apply: {{apply(addrof_fn(add), 10, 20)}}");
    h := addrof_fn(half);
    float r = h(5.0);
    println("half: {{r}}");
    fn(float, int) -> float sc = addrof_fn(scale);
    println("scale: {{sc(1.5, 4)}}");
    return dispatch_main();
}

int dispatch_main() {
    array<fn(int) -> int> tbl = [addrof_fn(inc), addrof_fn(dbl)];
    f := tbl[1];
    println("tbl[1](5) = {{f(5)}}");
    Handler h = Handler { id: 1, run: addrof_fn(inc) };
    g := h.run;
    println("h.run(5) = {{g(5)}}");
    println("direct = {{h.run(5)}}");
    return 0;
}

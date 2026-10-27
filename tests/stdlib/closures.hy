include { std.io, std.string, }

int twice(int x) { return x * 2; }

int apply(fn(int) -> int f, int v) { return f(v); }

fn(int) -> int make_adder(int n) {
    return fn(int x) -> int { return x + n; };
}

int main() {
    int base = 10;
    // only called here: lives on the stack
    fn(int) -> int add = fn(int x) -> int { return x + base; };
    println("stack: {{add(5)}}");

    // passed to a function: heap
    int r = apply(fn(int x) -> int { return x * base; }, 4);
    println("arg: {{r}}");

    // returned: outlives make_adder
    fn(int) -> int add7 = make_adder(7);
    fn(int) -> int add9 = make_adder(9);
    println("returned: {{add7(1)}} {{add9(1)}}");

    // a bare function and a closure share the type
    fn(int) -> int f = addrof_fn(twice);
    println("bare: {{f(21)}} {{apply(addrof_fn(twice), 8)}}");
    f = add7;
    println("reassigned: {{f(100)}}");

    // capture is by value
    int n = 1;
    fn(int) -> int snap = fn(int x) -> int { return x + n; };
    n = 50;
    println("by value: {{snap(0)}}");

    // a closure with no captures
    println("plain: {{apply(fn(int x) -> int { return x - 1; }, 10)}}");
    return 0;
}

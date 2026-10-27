include { std.io, }

(int, Error?) parse(str s) {
    if (length(s) == 0) { return (0, Err("empty", 1)); }
    return (42, nil);
}

(int, Error?) twice(str s) {
    int v = try parse(s);
    return (v * 2, nil);
}

int apply(fn(int, int) -> int f, int a, int b) { return f(a, b); }
struct Handler { int id; fn(int) -> int run; }

int main() {
    v, err := twice("7");
    _, e2 := parse("");
    t := (1, 2);
    int first = t.0;
    (int, Error?) r = parse("1");
    fn(int) -> int g = addrof_fn(main);
    array<fn(int) -> int> table = [];
    return 0;
}

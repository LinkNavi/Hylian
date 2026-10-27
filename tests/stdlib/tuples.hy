include { std.io, }

(int, Error?) parse_digit(str s) {
    if (length(s) == 0) { return (0, Err("empty", 1)); }
    if (s == "x") { return (0, Err("bad digit", 2)); }
    return (7, nil);
}

(int, Error?) twice(str s) {
    int v = try parse_digit(s);
    return (v * 2, nil);
}

Error? check(str s) {
    try parse_digit(s);
    println("check ok");
    return nil;
}

(float, int) pair() { return (2.5, 4); }

(int, str, Error?) parts(int n) {
    if (n < 0) { return (0, "", Err("negative", 9)); }
    return (n, "n={{n}}", nil);
}
(int, Error?) total(int n) {
    t := try parts(n);           // 3 elements: t is (int, str)
    return (t.0 * 10, nil);
}

int main() {
    a3, e31 := total(4);
    println("total {{a3}} {{e31 == nil}}");
    b3, e32 := total(-1);
    if (e32 != nil) { println("total failed: {{e32.message()}}"); }
    v, err := twice("5");
    if (err != nil) { println("unexpected"); }
    println("v = {{v}}");

    w, e2 := twice("x");
    if (e2 != nil) { println("error: {{e2.message()}} code {{e2.code()}}"); }

    Error? e3 = check("");
    if (e3 != nil) { println("check failed: {{e3.message()}}"); }
    Error? e4 = check("1");

    a, b := pair();
    println("pair {{a}} {{b}}");
    t := (1, 2);
    println("t {{t._0}} {{t._1}}");
    return 0;
}

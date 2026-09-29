include {
    std.io,
}
int fact(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * fact(n - 1);
}
Error? main() {
    println(fact(6));
    return nil;
}
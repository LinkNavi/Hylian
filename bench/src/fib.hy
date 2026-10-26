include {
    std.io,
}
int fib(int n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
Error? main() {
    println(fib(32));
    return nil;
}

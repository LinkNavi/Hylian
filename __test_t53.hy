include {
    std.io,
}
int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}
Error? main() {
    println(fib(10));
    return nil;
}
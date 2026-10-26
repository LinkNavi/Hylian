include {
    std.io,
}
static int arr[1000];
int fib(int n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
Error? main() {
    float x = 0.0;
    for (int i = 0; i < 10; i++) { x = x + 0.5 * 2.0; arr[i] = i * 2; }
    println(fib(20));
    println(arr[9]);
    println(cast<int>(x));
    return nil;
}

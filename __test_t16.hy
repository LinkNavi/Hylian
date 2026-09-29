include {
    std.io,
}
Error? main() {
    int x = 1;
    x = 42;
    println(x);
    return nil;
}
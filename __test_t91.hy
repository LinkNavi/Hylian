include {
    std.io,
}
Error? main() {
    int? x = 42;
    if (x) {
        println(x);
    } else {
        println("nil");
    }
    return nil;
}
include {
    std.io,
}
Error? main() {
    int? x = nil;
    if (x) {
        println("has value");
    } else {
        println("nil");
    }
    return nil;
}
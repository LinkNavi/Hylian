include {
    std.io,
}
Error? main() {
    int x = 7;
    if (x != 42) {
        println("different");
    }
    return nil;
}
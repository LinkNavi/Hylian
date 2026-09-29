include {
    std.io,
}
Error? main() {
    int x = 42;
    if (x == 42) {
        println("yes");
    } else {
        println("no");
    }
    return nil;
}
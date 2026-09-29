include {
    std.io,
}
Error? main() {
    int x = 99;
    switch (x) {
        case 1: { println("one"); }
        case 2: { println("two"); }
        default: { println("other"); }
    }
    return nil;
}
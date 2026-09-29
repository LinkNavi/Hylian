include {
    std.io,
}
Error? main() {
    int x = 1;
    switch (x) {
        case 1: { println("one"); }
        case 2: { println("two"); }
        case 3: { println("three"); }
    }
    return nil;
}
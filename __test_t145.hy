include {
    std.io,
}
Error? main() {
    int a = 1;
    int b = 2;
    switch (a + b) {
        case 2: { println("two"); }
        case 3: { println("three"); }
        default: { println("other"); }
    }
    return nil;
}
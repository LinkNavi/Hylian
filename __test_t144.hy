include {
    std.io,
}
Error? main() {
    int x = 5;
    switch (x) {
        case 1: { println("one"); }
        case 2: { println("two"); }
    }
    println("done");
    return nil;
}
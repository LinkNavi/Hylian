include {
    std.io,
}
Error? main() {
    int x = 3;
    if (x > 5) {
        println("big");
    } else {
        println("small");
    }
    return nil;
}
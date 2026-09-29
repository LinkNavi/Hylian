include {
    std.io,
}
Error? main() {
    int i = 0;
    while (i < 3) {
        println(i);
        i = i + 1;
    }
    return nil;
}
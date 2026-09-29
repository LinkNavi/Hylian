include {
    std.io,
}
Error? main() {
    int i = 0;
    while (i < 100) {
        if (i == 5) {
            break;
        }
        i = i + 1;
    }
    println(i);
    return nil;
}
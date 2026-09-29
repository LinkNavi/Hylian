include {
    std.io,
    std.strings,
}
Error? main() {
    bool found = str_contains("hello", "xyz");
    if (found) {
        println("yes");
    } else {
        println("no");
    }
    return nil;
}
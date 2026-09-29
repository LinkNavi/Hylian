include {
    std.io,
    std.strings,
}
Error? main() {
    bool found = str_contains("hello world", "world");
    if (found) {
        println("yes");
    } else {
        println("no");
    }
    return nil;
}
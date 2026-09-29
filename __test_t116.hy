include {
    std.io,
    std.strings,
}
Error? main() {
    bool eq = str_equals("hello", "hello");
    if (eq) {
        println("equal");
    } else {
        println("not equal");
    }
    return nil;
}
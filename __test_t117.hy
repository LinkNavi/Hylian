include {
    std.io,
    std.strings,
}
Error? main() {
    bool eq = str_equals("foo", "bar");
    if (eq) {
        println("equal");
    } else {
        println("not equal");
    }
    return nil;
}
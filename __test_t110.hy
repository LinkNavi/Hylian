include {
    std.io,
    std.strings,
}
Error? main() {
    int n = str_length("hello");
    println(n);
    return nil;
}
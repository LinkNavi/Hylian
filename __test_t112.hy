include {
    std.io,
    std.strings,
}
Error? main() {
    str s = str_to_upper("hello");
    println(s);
    return nil;
}
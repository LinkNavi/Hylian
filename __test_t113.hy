include {
    std.io,
    std.strings,
}
Error? main() {
    str s = str_to_lower("WORLD");
    println(s);
    return nil;
}
include {
    std.io,
    std.strings,
}
Error? main() {
    str s = "hylian";
    int n = str_length(s);
    println(n);
    return nil;
}
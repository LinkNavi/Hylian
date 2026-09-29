include {
    std.io,
}
Error? main() {
    multi<int | str> x = 42;
    println(x.tag);
    return nil;
}
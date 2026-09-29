include {
    std.io,
}
Error? main() {
    multi<int | str> x = "hello";
    println(x.tag);
    return nil;
}
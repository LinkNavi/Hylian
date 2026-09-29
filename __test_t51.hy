include {
    std.io,
}
int square(int x) {
    return x * x;
}
Error? main() {
    println(square(3));
    println(square(4));
    println(square(5));
    return nil;
}
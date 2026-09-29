include {
    std.io,
}
enum Color {
    Red,
    Green,
    Blue,
}
Error? main() {
    int v = Color.Red;
    println(v);
    return nil;
}
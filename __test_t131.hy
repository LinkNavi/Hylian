include {
    std.io,
}
enum Color {
    Red,
    Green,
    Blue,
}
Error? main() {
    int v = Color.Green;
    println(v);
    return nil;
}
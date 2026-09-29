include {
    std.io,
}
Error? main() {
    int a = 3;
    int b = 4;
    int c = a + b;
    println("{{a}} + {{b}} = {{c}}");
    return nil;
}
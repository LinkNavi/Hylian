include {
    std.io,
}
int add(int a, int b) {
    return a + b;
}
Error? main() {
    int r = add(3, 4);
    println(r);
    return nil;
}
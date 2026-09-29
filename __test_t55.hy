include {
    std.io,
}
Error? greet(str name) {
    println("Hi, {{name}}!");
    return nil;
}
Error? main() {
    greet("Alice");
    greet("Bob");
    return nil;
}
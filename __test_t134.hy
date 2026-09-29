include {
    std.io,
}
enum Level {
    Low,
    Mid,
    High,
}
Error? main() {
    int lvl = Level.High;
    println(lvl);
    return nil;
}
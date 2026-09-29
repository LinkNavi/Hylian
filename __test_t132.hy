include {
    std.io,
}
enum Direction {
    North,
    South,
    East,
    West,
}
Error? main() {
    int v = Direction.East;
    println(v);
    return nil;
}
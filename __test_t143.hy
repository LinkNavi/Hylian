include {
    std.io,
}
enum Dir {
    North,
    South,
    East,
    West,
}
Error? main() {
    int d = Dir.South;
    switch (d) {
        case Dir.North: { println("north"); }
        case Dir.South: { println("south"); }
        case Dir.East:  { println("east"); }
        case Dir.West:  { println("west"); }
    }
    return nil;
}
include {
    std.io,
}
public class Point {
    int x;
    int y;
    Point(int px, int py) {
        x = px;
        y = py;
    }
}
Error? main() {
    Point p = new Point(3, 7);
    println(p.x);
    println(p.y);
    return nil;
}
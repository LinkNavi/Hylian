include {
    std.io,
}
public class Box {
    int val;
    Box(int v) {
        val = v;
    }
    int doubled() {
        return val * 2;
    }
}
Error? main() {
    Box a = new Box(5);
    Box b = new Box(10);
    println(a.doubled());
    println(b.doubled());
    return nil;
}
include {
    std.io,
}
int clamp(int val, int lo, int hi) {
    if (val < lo) { return lo; }
    if (val > hi) { return hi; }
    return val;
}
Error? main() {
    println(clamp(5, 0, 10));
    println(clamp(-3, 0, 10));
    println(clamp(99, 0, 10));
    return nil;
}
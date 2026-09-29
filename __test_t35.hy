include {
    std.io,
}
Error? main() {
    bool flag = true;
    if (flag) {
        println("on");
    } else {
        println("off");
    }
    return nil;
}
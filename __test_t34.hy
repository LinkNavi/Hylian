include {
    std.io,
}
Error? main() {
    int x = 5;
    if (x < 0) {
        println("negative");
    } else {
        if (x == 0) {
            println("zero");
        } else {
            println("positive");
        }
    }
    return nil;
}
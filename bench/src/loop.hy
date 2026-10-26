include {
    std.io,
}
Error? main() {
    int s = 0;
    for (int i = 0; i < 12000; i++) {
        for (int j = 0; j < 5000; j++) {
            s = (s * 31 + (i ^ j)) % 1000003;
        }
    }
    println(s);
    return nil;
}

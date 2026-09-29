include {
    std.io,
}
Error? main() {
    int sum = 0;
    for (int i = 1; i <= 5; i = i + 1) {
        sum = sum + i;
    }
    println(sum);
    return nil;
}
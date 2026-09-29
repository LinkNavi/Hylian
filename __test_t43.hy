include {
    std.io,
}
Error? main() {
    int sum = 0;
    int i = 0;
    while (i < 5) {
        i = i + 1;
        if (i == 3) {
            continue;
        }
        sum = sum + i;
    }
    println(sum);
    return nil;
}
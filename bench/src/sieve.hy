include {
    std.io,
}
Error? main() {
    int n = 10000000;
    int flags[10000001];
    int count = 0;
    for (int i = 2; i <= n; i++) {
        if (flags[i] == 0) {
            count += 1;
            int j = i * 2;
            while (j <= n) {
                flags[j] = 1;
                j += i;
            }
        }
    }
    println(count);
    return nil;
}

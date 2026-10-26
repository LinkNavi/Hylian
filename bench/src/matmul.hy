include {
    std.io,
}
Error? main() {
    int n = 250;
    int a[62500];
    int b[62500];
    int c[62500];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            a[i * n + j] = (i + j) % 10;
            b[i * n + j] = (i * j) % 10;
        }
    }
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int s = 0;
            for (int k = 0; k < n; k++) {
                s += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = s;
        }
    }
    int sum = 0;
    for (int i = 0; i < n * n; i++) { sum += c[i]; }
    println(sum);
    return nil;
}

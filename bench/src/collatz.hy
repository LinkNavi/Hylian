include {
    std.io,
}
Error? main() {
    int best = 0;
    int bestn = 0;
    for (int n = 1; n < 1000000; n++) {
        int x = n;
        int steps = 0;
        while (x != 1) {
            if (x % 2 == 0) { x = x / 2; } else { x = 3 * x + 1; }
            steps += 1;
        }
        if (steps > best) { best = steps; bestn = n; }
    }
    println(bestn);
    return nil;
}

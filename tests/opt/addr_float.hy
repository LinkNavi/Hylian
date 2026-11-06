include { std.io, }
// a[i*n + k] with a runtime n: index splitting must keep the same bounds behaviour
int mm(int n) {
    int a[64]; int b[64]; int c[64];
    for (int i = 0; i < n; i++) { for (int j = 0; j < n; j++) { a[i * n + j] = i + j; b[i * n + j] = i - j; } }
    for (int i = 0; i < n; i++) { for (int j = 0; j < n; j++) {
        int s = 0;
        for (int k = 0; k < n; k++) { s += a[i * n + k] * b[k * n + j]; }
        c[i * n + j] = s;
    } }
    int t = 0;
    for (int i = 0; i < 64; i++) { t = t * 31 + c[i]; }
    return t;
}
// the same float expression on both sides of a branch (cross-block CSE) incl. NaN
float f(float zr, float zi, float c) {
    float r = 0.0;
    int it = 0;
    while (it < 20 && zr * zr + zi * zi < 4.0) {
        float t = zr * zr - zi * zi + c;
        zi = 2.0 * zr * zi + c;
        zr = t;
        it += 1;
        r += zr * zr;
    }
    return r + it;
}
Error? main() {
    println(mm(8)); println(mm(7)); println(mm(9)); println(mm(5));
    println(f(0.1, 0.2, 0.3)); println(f(1.5, 1.5, 0.1)); println(f(0.0, 0.0, -1.0));
    float nan = 0.0 / 0.0;
    println(f(nan, 1.0, 0.0));
    return nil;
}

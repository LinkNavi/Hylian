include { std.io, }

int sum_plain(array<int> a) {
    int s = 0;
    for (int i = 0; i < a.len; i++) { s += a[i]; }
    return s;
}
// the array shrinks while we iterate: every access must stay checked
int sum_shrinking(array<int> a) {
    int s = 0;
    for (int i = 0; i < a.len; i++) {
        s = s * 3 + a[i];
        if (i % 2 == 0) { a.pop(); }
    }
    return s * 1000 + a.len;
}
// bounded by another (longer) array's length
int cross(array<int> a, array<int> b) {
    int s = 0;
    for (int i = 0; i < b.len; i++) { s = s * 5 + a[i]; }
    return s;
}
int stride(array<int> a) {
    int s = 0;
    for (int i = 1; i < a.len; i += 2) { s = s * 7 + a[i] + a[i - 1]; }
    return s;
}
int offset_read(array<int> a) {
    int s = 0;
    for (int i = 0; i < a.len; i++) { s = s * 11 + a[i + 1]; }   // i+1 runs one past the end
    return s;
}
int from_negative(array<int> a) {
    int s = 0;
    for (int i = 0 - 2; i < a.len; i++) { s = s * 13 + a[i]; }
    return s;
}
int nil_array() {
    array<int> z = nil;
    int s = 0;
    for (int i = 0; i < 4; i++) { s += z[i]; }
    return s + z.len;
}
Error? main() {
    array<int> a = [];
    array<int> b = [];
    for (int i = 0; i < 10; i++) { a.push(i * i + 1); }
    for (int i = 0; i < 14; i++) { b.push(i); }
    println(sum_plain(a));
    println(sum_shrinking(a));
    println(a.len);
    println(cross(a, b));
    array<int> c = [];
    for (int i = 0; i < 9; i++) { c.push(i + 3); }
    println(stride(c));
    println(offset_read(c));
    println(from_negative(c));
    println(nil_array());
    return nil;
}

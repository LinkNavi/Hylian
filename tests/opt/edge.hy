include { std.io, }

// float compares with NaN (all false except !=), fused into branches and not
int fcmps(float a, float b) {
    int r = 0;
    if (a < b) { r += 1; }
    if (a <= b) { r += 2; }
    if (a > b) { r += 4; }
    if (a >= b) { r += 8; }
    if (a == b) { r += 16; }
    if (a != b) { r += 32; }
    int k = (a < b) ? 100 : 0;
    k += (a == b) ? 200 : 0;
    k += (a != b) ? 400 : 0;
    return r + k;
}

int sdivs(int x) {
    return x / 3 + x / 7 * 10 + x / 10 * 100 + x / 1000003 + x / 8 + x / (0 - 7) + x / (0 - 8) + x % 7 + x % 8 + x % (0 - 9) + x % 1000003;
}

usize udivs(usize x) {
    return x / 3 + x / 7 + x / 10 + x % 641 + x % 1000003 + x / 65537 + x % 12;
}

int arr_oob() {
    int a[4];
    for (int i = 0; i < 4; i++) { a[i] = i + 10; }
    int s = 0;
    for (int i = 0 - 3; i < 8; i++) { s = s * 3 + a[i]; a[i] = 99; }
    return s;
}

Error? main() {
    float nan = 0.0 / 0.0;
    float one = 1.0;
    println(fcmps(nan, one));
    println(fcmps(one, nan));
    println(fcmps(nan, nan));
    println(fcmps(one, one));
    println(fcmps(one, 2.0));
    println(fcmps(2.0, one));

    int mn = 0 - 9223372036854775807 - 1;
    int mx = 9223372036854775807;
    println(sdivs(0));
    println(sdivs(1));
    println(sdivs(0 - 1));
    println(sdivs(mx));
    println(sdivs(mn));
    println(sdivs(mn + 1));
    println(sdivs(123456789012345));
    println(sdivs(0 - 987654321987));
    println(cast<int>(udivs(cast<usize>(0))));
    println(cast<int>(udivs(cast<usize>(mx))));
    println(cast<int>(udivs(cast<usize>(mn))));
    println(cast<int>(udivs(cast<usize>(0 - 1))));
    println(cast<int>(udivs(cast<usize>(123456789012345))));
    println(arr_oob());
    return nil;
}

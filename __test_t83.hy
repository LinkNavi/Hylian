include {
    std.io,
}
Error? main() {
    array<int> nums = [3, 1, 4];
    for (n in nums) {
        println(n);
    }
    return nil;
}
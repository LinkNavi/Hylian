include {
    std.io,
}
Error? main() {
    array<int> nums = [1, 2, 3, 4, 5];
    int sum = 0;
    for (n in nums) {
        sum = sum + n;
    }
    println(sum);
    return nil;
}
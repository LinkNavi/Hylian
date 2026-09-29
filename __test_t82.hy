include {
    std.io,
}
Error? main() {
    array<int> nums = [1, 2, 3];
    nums[1] = 99;
    println(nums[1]);
    return nil;
}
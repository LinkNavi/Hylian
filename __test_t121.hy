include {
    std.io,
}
Error? main() {
    array<int> nums = [];
    nums.push(42);
    println(nums[0]);
    return nil;
}
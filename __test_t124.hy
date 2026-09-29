include {
    std.io,
}
Error? main() {
    array<int> nums = [10, 20, 30];
    nums.pop();
    println(nums.len);
    return nil;
}
include {
    std.io,
}
Error? main() {
    array<int> nums = [];
    nums.push(10);
    nums.push(20);
    nums.push(30);
    println(nums.len);
    return nil;
}
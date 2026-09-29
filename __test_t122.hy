include {
    std.io,
}
Error? main() {
    array<int> nums = [];
    nums.push(1);
    nums.push(2);
    nums.push(3);
    println(nums[0]);
    println(nums[1]);
    println(nums[2]);
    return nil;
}
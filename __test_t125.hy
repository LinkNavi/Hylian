include {
    std.io,
}
Error? main() {
    array<int> nums = [];
    nums.push(5);
    nums.push(10);
    int a = nums.pop();
    int b = nums.pop();
    println(a);
    println(b);
    return nil;
}
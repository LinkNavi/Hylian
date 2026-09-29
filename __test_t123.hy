include {
    std.io,
}
Error? main() {
    array<int> nums = [10, 20, 30];
    int v = nums.pop();
    println(v);
    return nil;
}
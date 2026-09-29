include { std.io, std.assert, }
int main() {
    assert(1 == 1, "fine");
    assert_eq(2 + 2, 4, "sum");
    println("before");
    assert_eq(2 + 2, 5, "sum is wrong");
    println("unreachable");
    return 0;
}

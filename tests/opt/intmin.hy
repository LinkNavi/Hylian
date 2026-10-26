include { std.io, }
Error? main() {
    int mn = 0 - 9223372036854775807 - 1;
    println(int_to_str(mn));
    println("v={{mn}}");
    println(int_to_str(0 - 45));
    println(int_to_str(0));
    println(mn);
    return nil;
}

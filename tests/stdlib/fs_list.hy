include { std.io, std.string, std.os.fs, }
int main() {
    array<str> n = list_dir("listdir");
    println(int_to_str(n.len));
    // readdir order is filesystem-defined; check membership instead
    if (contains(join(n, ","), "a") && contains(join(n, ","), "b.txt") && contains(join(n, ","), "sub")) { println("names ok"); }
    if (list_dir("nonexistent") == nil) { println("nil ok"); }
    mkdir_p("listdir/x/y/z");
    if (is_dir("listdir/x/y/z")) { println("mkdir_p ok"); }
    return 0;
}

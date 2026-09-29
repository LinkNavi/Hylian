include { std.io, std.string, }
int main() {
    array<str> p = split("a,b,,c", ",");
    println(int_to_str(p.len));
    println(join(p, "-"));
    println(replace("hello world world", "world", "X"));
    println(repeat("ab", 3));
    println(format("GET {} HTTP/{} {}", ["/x", "1.1"]));
    println(int_to_str(parse_int("-123", 0)));
    println(int_to_str(parse_int("12x", 7)));
    println(int_to_str(parse_hex("1aF", 0)));
    println(int_to_str(last_index_of("abcabc", "bc")));
    println(int_to_str(split("", ",").len));
    println(int_to_str(length(int_to_str(12345))));
    return 0;
}

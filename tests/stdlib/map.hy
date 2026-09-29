include { std.io, std.string, std.map, }
int main() {
    usize m = map_new();
    map_put(m, "name", "hylian");
    map_put_int(m, "count", 3);
    int i = 0;
    while (i < 100) { map_put_int(m, format("k{}", [int_to_str(i)]), i * 2); i += 1; }
    println(map_get(m, "name", "?"));
    println(int_to_str(map_get_int(m, "count", -1)));
    println(int_to_str(map_get_int(m, "k77", -1)));
    println(int_to_str(map_len(m)));
    map_put_int(m, "count", 9);
    println(int_to_str(map_get_int(m, "count", -1)));
    println(int_to_str(map_len(m)));
    map_remove(m, "name");
    println(map_get(m, "name", "gone"));
    println(int_to_str(map_get_int(m, "k99", -1)));
    println(map_key_at(m, 0));
    return 0;
}

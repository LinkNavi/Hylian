include { std.io, std.string, std.hashmap, }

Error? run(HashMap<int> m) {
    int v = try m.get("count");
    println("must count = {{v}}");
    int w = try m.get("absent");
    println("unreachable {{w}}");
    return nil;
}

int main() {
    HashMap<int> m = new HashMap<int>();
    m.put("count", 3);
    int i = 0;
    while (i < 100) { m.put("k" + int_to_str(i), i * 2); i += 1; }
    println("len {{m.len()}}");
    v, err := m.get("k77");
    println("k77 {{v}} {{err == nil}}");
    w, e2 := m.get("nope");
    if (e2 != nil) { println("miss: {{e2.message()}} ({{e2.code()}})"); }
    int orv = m.get_or("nope", -1);
    println("or {{orv}}");
    m.put("count", 9);
    int cv = m.get_or("count", 0);
    println("count {{cv}} len {{m.len()}}");
    m.remove("k5");
    bool h5 = m.has("k5");
    bool h6 = m.has("k6");
    println("has k5 {{h5}} k6 {{h6}} len {{m.len()}}");
    println("first {{m.key_at(0)}}");
    Error? e = run(m);
    if (e != nil) { println("run failed: {{e.message()}}"); }

    HashMap<str> names = new HashMap<str>();
    names.put("a", "alpha");
    names.put("b", "beta");
    str nb = names.get_or("b", "?");
    println("names {{nb}}");
    HashMap<float> fs = new HashMap<float>();
    fs.put("pi", 3.25);
    float pi = fs.get_or("pi", 0.0);
    println("float {{pi}}");
    return 0;
}

include { std.io, std.string, std.https, }
int main() {
    r1, e1 := https_get("https://localhost:18443/hello");
    if (e1 != nil) { println(e1.message()); return 1; }
    println(int_to_str(r1.status));
    print(http_body(r1));
    r2, e2 := fetch("https://127.0.0.1:18443/chunked");
    println(int_to_str(r2.body_len));
    r3, e3 := fetch("http://127.0.0.1:18099/hello");
    print(http_body(r3));
    return 0;
}

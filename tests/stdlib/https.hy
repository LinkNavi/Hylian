include { std.io, std.string, std.https, }
int main() {
    usize r = https_get("https://localhost:18443/hello");
    if (r == 0) { println(https_error()); return 1; }
    println(int_to_str(http_status(r)));
    print(http_body(r));
    r = fetch("https://127.0.0.1:18443/chunked");
    println(int_to_str(http_body_len(r)));
    r = fetch("http://127.0.0.1:18099/hello");
    print(http_body(r));
    return 0;
}

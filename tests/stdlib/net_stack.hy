include { std.io, std.string, std.dns, std.http, }
int main() {
    println(dns_query_port("127.0.0.1", 15353, "pkg.example.com"));
    if (dns_query_port("127.0.0.1", 15353, "nx.test") == nil) { println("nxdomain nil"); }
    println(dns_resolve("1.2.3.4"));
    println(dns_resolve("localhost"));

    r1, e1 := http_get("http://localhost:18099/hello");
    println(int_to_str(r1.status));
    println(http_header(r1, "X-TEST"));
    print(http_body(r1));
    r2, e2 := http_get("http://127.0.0.1:18099/redir");
    print(http_body(r2));
    r3, e3 := http_get("http://127.0.0.1:18099/chunked");
    println(int_to_str(r3.body_len));
    r4, e4 := http_get("http://127.0.0.1:18099/bin");
    println(int_to_str(r4.body_len));
    r5, e5 := http_get("http://127.0.0.1:18099/nope");
    println(int_to_str(r5.status));
    r6, e6 := http_post("http://127.0.0.1:18099/x", "text/plain", "payload");
    println(http_body(r6));
    r7, e7 := http_get("https://127.0.0.1:18099/x");
    if (e7 != nil) { println("https refused"); }
    r8, e8 := http_get("http://127.0.0.1:1/x");
    if (e8 != nil) { println(e8.message()); }
    return 0;
}

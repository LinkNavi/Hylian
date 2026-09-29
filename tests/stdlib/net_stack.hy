include { std.io, std.string, std.dns, std.http, }
int main() {
    println(dns_query_port("127.0.0.1", 15353, "pkg.example.com"));
    if (dns_query_port("127.0.0.1", 15353, "nx.test") == nil) { println("nxdomain nil"); }
    println(dns_resolve("1.2.3.4"));
    println(dns_resolve("localhost"));

    usize r = http_get("http://localhost:18099/hello");
    println(int_to_str(http_status(r)));
    println(http_header(r, "X-TEST"));
    print(http_body(r));
    r = http_get("http://127.0.0.1:18099/redir");
    print(http_body(r));
    r = http_get("http://127.0.0.1:18099/chunked");
    println(int_to_str(http_body_len(r)));
    r = http_get("http://127.0.0.1:18099/bin");
    println(int_to_str(http_body_len(r)));
    r = http_get("http://127.0.0.1:18099/nope");
    println(int_to_str(http_status(r)));
    r = http_post("http://127.0.0.1:18099/x", "text/plain", "payload");
    println(http_body(r));
    if (http_get("https://127.0.0.1:18099/x") == 0) { println("https refused"); }
    if (http_get("http://127.0.0.1:1/x") == 0) { println(http_error()); }
    return 0;
}

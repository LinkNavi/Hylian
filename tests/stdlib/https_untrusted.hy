include { std.io, std.string, std.https, }
int main() {
    usize r = https_get("https://localhost:18443/hello");
    if (r == 0) { println(https_error()); }
    return 0;
}

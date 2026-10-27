include { std.io, std.string, std.https, }
int main() {
    r, err := https_get("https://localhost:18443/hello");
    if (err != nil) { println(err.message()); }
    return 0;
}

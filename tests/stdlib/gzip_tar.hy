include { std.io, std.string, std.bytes, std.os.fs, std.inflate, std.tar, }

static int same(str gz, str raw) {
    str g = read_all(gz);
    str expect = read_all(raw);
    int elen = file_size(raw);
    usize out = gunzip(cast<usize>(g), file_size(gz));
    if (out == 0) { println(concat("FAIL ", inflate_error())); return 0; }
    if (buf_len(out) != elen) { println("FAIL len"); return 0; }
    int i = 0;
    while (i < elen) {
        if (get8(buf_addr(out), i) != get8(cast<usize>(expect), i)) { println("FAIL byte"); return 0; }
        i += 1;
    }
    println(concat("ok ", gz));
    return 1;
}

int main() {
    same("data.gz", "data.bin");          // dynamic Huffman
    same("data0.gz", "data0.bin");        // stored blocks
    same("small.gz", "small.bin");        // fixed Huffman

    str g = read_all("data.gz");
    int n = file_size("data.gz");
    set8(cast<usize>(g), n / 2, get8(cast<usize>(g), n / 2) ^ 255);
    if (gunzip(cast<usize>(g), n) == 0) { println("corrupt rejected"); }

    str t = read_all("pkg.tar.gz");
    usize raw = gunzip(cast<usize>(t), file_size("pkg.tar.gz"));
    println(int_to_str(tar_list(buf_addr(raw), buf_len(raw)).len));
    println(int_to_str(tar_extract(buf_addr(raw), buf_len(raw), "installed", 1)));

    str e = read_all("evil.tar");
    println(int_to_str(tar_extract(cast<usize>(e), file_size("evil.tar"), "evilout", 0)));
    println(tar_error());
    return 0;
}

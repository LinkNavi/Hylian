include {
    std.io,
    std.errors,
}
Error? might_fail(bool ok) {
    if (ok) {
        return nil;
    }
    return Err("something went wrong");
}
Error? main() {
    Error? e = might_fail(false);
    if (e) {
        println(e.message());
    } else {
        println("no error");
    }
    return nil;
}
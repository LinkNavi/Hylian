include {
    std.io,
    std.errors,
}
Error? might_fail(bool ok) {
    if (ok) {
        return nil;
    }
    return Err("oops");
}
Error? main() {
    Error? e = might_fail(true);
    if (e) {
        println("failed");
    } else {
        println("ok");
    }
    return nil;
}
include {
    std.io,
}
enum Status {
    Ok,
    Fail,
}
Error? main() {
    int s = Status.Ok;
    if (s == Status.Ok) {
        println("ok");
    } else {
        println("fail");
    }
    return nil;
}
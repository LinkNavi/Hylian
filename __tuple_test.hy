include {
    std.io,
}

(int, int) divmod(int a, int b) {
    return a / b, a % b;
}

Error? main() {
    (int, int) result = divmod(17, 5);
    int quotient  = result.0;
    int remainder = result.1;
    println(quotient);
    println(remainder);
    return nil;
}

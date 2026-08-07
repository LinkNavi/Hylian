naked usize raw(int n) {
    return cast<usize>(syscall(9, 0, n, 3, 34, -1, 0));
}

void ptr_ops() {
    int x = 5;
    unsafe {
        *int p = &x;
        *p = 7;
    }
    usize buf = raw(16);
    unsafe {
        *uint8 b = cast<*uint8>(buf);
        *b = cast<uint8>(65);
    }
}

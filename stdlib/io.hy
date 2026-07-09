// io — basic terminal input/output over raw read()/write() syscalls.

include {
    platform.linux_x86_64,
    string,
}

static int STDIN  = 0;
static int STDOUT = 1;
static int STDERR = 2;

// print: write a string to stdout, no trailing newline.
void print(str msg) {
    syscall(NR_WRITE, STDOUT, cast<int>(msg), length(msg));
}

// eprint: write a string to stderr, no trailing newline.
void eprint(str msg) {
    syscall(NR_WRITE, STDERR, cast<int>(msg), length(msg));
}

// println: write a string to stdout followed by a newline.
void println(str msg) {
    print(msg);
    print("\n");
}

// eprintln: write a string to stderr followed by a newline.
void eprintln(str msg) {
    eprint(msg);
    eprint("\n");
}

// int_to_str: decimal string representation of n (handles negatives).
str int_to_str(int n) {
    if (n == 0) { return "0"; }

    bool neg = n < 0;
    int v = n;
    if (neg) { v = -v; }

    // enough for a 64-bit value's digits plus a sign
    usize buf = raw_alloc(32);
    int pos = 31;
    unsafe { *uint8 tp = cast<*uint8>(buf + cast<usize>(pos)); *tp = 0; }
    pos -= 1;

    while (v > 0) {
        int digit = v % 10;
        usize p = buf + cast<usize>(pos);
        unsafe { *uint8 dp = cast<*uint8>(p); *dp = cast<uint8>(48 + digit); }
        pos -= 1;
        v = v / 10;
    }
    if (neg) {
        usize p = buf + cast<usize>(pos);
        unsafe { *uint8 dp = cast<*uint8>(p); *dp = cast<uint8>(45); } // '-'
        pos -= 1;
    }

    return slice(cast<str>(buf), pos + 1, 31);
}

// str_to_int: parse a decimal string (optional leading whitespace, optional
// +/- sign). Returns 0 for empty or whitespace-only input.
int str_to_int(str s) {
    int n = length(s);
    int i = 0;
    while (i < n) {
        usize p = cast<usize>(s) + cast<usize>(i);
        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        int ci = cast<int>(c);
        if (ci != 32 && ci != 9) { break; }
        i += 1;
    }

    bool neg = false;
    if (i < n) {
        usize p = cast<usize>(s) + cast<usize>(i);
        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        if (cast<int>(c) == 45) { neg = true; i += 1; }
        else if (cast<int>(c) == 43) { i += 1; }
    }

    int result = 0;
    while (i < n) {
        usize p = cast<usize>(s) + cast<usize>(i);
        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        int ci = cast<int>(c);
        if (ci < 48 || ci > 57) { break; }
        result = result * 10 + (ci - 48);
        i += 1;
    }

    if (neg) { return -result; }
    return result;
}

// read_line: read one line from stdin, without the trailing newline.
// Returns an empty string on EOF. Reads one byte at a time - fine for an
// interactive terminal, not meant for bulk input (use fs.read for that).
str read_line() {
    usize buf = raw_alloc(4096);
    int pos = 0;

    while (pos < 4095) {
        usize p = buf + cast<usize>(pos);
        int n = syscall(NR_READ, STDIN, cast<int>(p), 1);
        if (n <= 0) { break; }

        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        if (cast<int>(c) == 10) { break; } // '\n'
        pos += 1;
    }

    usize term = buf + cast<usize>(pos);
    unsafe { *uint8 tp = cast<*uint8>(term); *tp = 0; }
    return cast<str>(buf);
}

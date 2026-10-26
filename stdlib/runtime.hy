// runtime — low-level primitives the compiler calls directly for the
// print/println *syntax* (not the std.io print()/println() functions,
// which are a separate, ordinary stdlib layer built on top of syscall()
// and never actually invoked by print/println source - see
// compiler/ir_to_mir.c's IR_PRINT/IR_PRINTLN case, which lowers straight
// to calls named exactly "hylian_print"/"hylian_println" regardless of
// whether std.io is even included).
//
// Always-linked, same reasoning and same constraints as mem.hy: needed by
// every compiled program unconditionally, so it's compiled standalone and
// linked in separately (see linkle.py's STD_MODULES __always__ handling),
// and deliberately does NOT `include platform.linux_x86_64` to avoid a
// multiple-definition clash with whatever the program's own translation
// unit independently compiles from that file.
//
// `strlen` (used by IR_PRINTLN's PRINT_ARG_STR_PTR path, for printing a
// runtime str variable rather than a literal) isn't defined here - it
// isn't needed to be: Hylian binaries currently always link against gcc's
// default (non-freestanding) startup, which pulls in glibc, and glibc
// already provides a standard `strlen`.
//
// hylian_println still writes its trailing newline from a 1-byte local
// rather than a "\n" literal. That started as a workaround for the compiler
// never decoding backslash escapes (so "\n" really was '\' followed by 'n');
// escapes work now, but the local-byte version is kept because it needs no
// relocation against .rodata at all, which matters for a function that is
// linked into every single binary including freestanding ones.
// It works because Hylian locals are 8-byte slots and x86 is little-endian,
// so the value's low byte lands at the start of the slot's address.

void hylian_print(usize str_ptr, usize len) {
    syscall(1, 1, cast<int>(str_ptr), cast<int>(len));
}

void hylian_println(usize str_ptr, usize len) {
    syscall(1, 1, cast<int>(str_ptr), cast<int>(len));
    usize newline = cast<usize>(10);
    usize newline_addr = cast<usize>(&newline);
    syscall(1, 1, cast<int>(newline_addr), 1);
}

// hylian_int_to_str: render `val` as decimal into `buf` (capacity `buflen`),
// returning the number of bytes written. Also always-linked and also called
// directly by the compiler, not by user code: print/println of an integer
// lowers to a MIR_ALLOCA_LOCAL scratch buffer + a call to exactly this name
// (see compiler/ir_to_mir.c's PRINT_ARG_INT case), so without a definition
// here every `println(someInt)` was an undefined reference at link time.
//
// Digits are generated least-significant-first, which means they come out
// backwards. Rather than needing a second buffer, they're written to the END
// of the caller's buffer and then slid down to the front — the source and
// destination ranges can overlap, but the copy runs front-to-back and the
// destination index is always <= the source index, so a byte is only ever
// overwritten after it has already been read.
int hylian_int_to_str(int val, usize buf, int buflen) {
    if (buflen <= 0) {
        return 0;
    }

    if (val == 0) {
        unsafe {
            *uint8 zp = cast<*uint8>(buf);
            *zp = cast<uint8>(48);
        }
        return 1;
    }

    // Digits are taken from the value with its own sign (truncating division
    // leaves a negative remainder for a negative number), so the most negative
    // int, which has no positive counterpart to negate into, prints correctly.
    bool neg = val < 0;
    int v = val;

    int pos = buflen;
    while (v != 0) {
        pos = pos - 1;
        if (pos < 0) {
            return 0;
        }
        int digit = v % 10;
        if (neg) {
            digit = 0 - digit;
        }
        usize p = buf + cast<usize>(pos);
        unsafe {
            *uint8 dp = cast<*uint8>(p);
            *dp = cast<uint8>(48 + digit);
        }
        v = v / 10;
    }

    if (neg) {
        pos = pos - 1;
        if (pos < 0) {
            return 0;
        }
        usize sp = buf + cast<usize>(pos);
        unsafe {
            *uint8 mp = cast<*uint8>(sp);
            *mp = cast<uint8>(45);
        }
    }

    int len = buflen - pos;
    int i = 0;
    while (i < len) {
        usize from = buf + cast<usize>(pos + i);
        usize to = buf + cast<usize>(i);
        uint8 c;
        unsafe {
            *uint8 fp = cast<*uint8>(from);
            c = *fp;
        }
        unsafe {
            *uint8 tp = cast<*uint8>(to);
            *tp = c;
        }
        i = i + 1;
    }
    return len;
}

// ── string building helpers for interpolation and float printing ────────────
//
// The compiler calls these directly (like hylian_int_to_str above): string
// interpolation `"n = {{n}}"` lowers to a chain of hylian_str_concat calls
// over hylian_str_from_* conversions, and print/println of a float goes
// through hylian_float_to_str. Also always-linked, so also on the
// is_arena_bootstrap_fn allowlist in compiler/lower.c (no auto arena).

// hylian_str_alloc: n zeroed bytes for a string. Strings are small and
// immutable, so rather than an mmap (a whole page) per string they're carved
// out of 64 KiB chunks that are never freed - same "leaks by design, but
// predictable" trade-off as multi values in mem.hy. Because they're never
// freed, an interpolated string is safe to return from a function.
static int _str_pool_ptr = 0;
static int _str_pool_end = 0;

usize hylian_str_alloc(int n) {
    int need = (n + 8) / 8 * 8;
    if (need > 32768) {
        // big strings get their own mapping
        return cast<usize>(syscall(9, 0, need, 3, 34, -1, 0));
    }
    if (_str_pool_ptr == 0 || _str_pool_ptr + need > _str_pool_end) {
        int chunk = 65536;
        int base = syscall(9, 0, chunk, 3, 34, -1, 0);
        _str_pool_ptr = base;
        _str_pool_end = base + chunk;
    }
    usize p = cast<usize>(_str_pool_ptr);
    _str_pool_ptr = _str_pool_ptr + need;
    return p;
}

int _rt_strlen(usize s) {
    if (s == cast<usize>(0)) { return 0; }
    int n = 0;
    while (true) {
        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(s + cast<usize>(n)); c = *bp; }
        if (cast<int>(c) == 0) { return n; }
        n = n + 1;
    }
    return n;
}

// hylian_str_concat: new string a + b. nil counts as "".
usize hylian_str_concat(usize a, usize b) {
    int la = _rt_strlen(a);
    int lb = _rt_strlen(b);
    usize out = hylian_str_alloc(la + lb + 1);
    int i = 0;
    while (i < la) {
        uint8 c;
        unsafe {
            *uint8 sp = cast<*uint8>(a + cast<usize>(i)); c = *sp;
            *uint8 dp = cast<*uint8>(out + cast<usize>(i)); *dp = c;
        }
        i = i + 1;
    }
    i = 0;
    while (i < lb) {
        uint8 c;
        unsafe {
            *uint8 sp = cast<*uint8>(b + cast<usize>(i)); c = *sp;
            *uint8 dp = cast<*uint8>(out + cast<usize>(la + i)); *dp = c;
        }
        i = i + 1;
    }
    return out;
}

// hylian_str_eq: byte-for-byte `a == b` on two strings (what the compiler
// lowers `==`/`!=` on str to). Lives here, in the always-linked runtime, so
// comparing strings never depends on the program including std.string.
// nil equals only nil.
int hylian_str_eq(usize a, usize b) {
    if (a == b) { return 1; }
    if (a == cast<usize>(0)) { return 0; }
    if (b == cast<usize>(0)) { return 0; }
    int i = 0;
    while (true) {
        uint8 ca; uint8 cb;
        unsafe {
            *uint8 pa = cast<*uint8>(a + cast<usize>(i)); ca = *pa;
            *uint8 pb = cast<*uint8>(b + cast<usize>(i)); cb = *pb;
        }
        if (ca != cb) { return 0; }
        if (cast<int>(ca) == 0) { return 1; }
        i = i + 1;
    }
    return 1;
}

usize hylian_str_from_int(int v) {
    usize out = hylian_str_alloc(32);
    int n = hylian_int_to_str(v, out, 31);
    unsafe { *uint8 tp = cast<*uint8>(out + cast<usize>(n)); *tp = cast<uint8>(0); }
    return out;
}

usize hylian_str_from_bool(int v) {
    if (v != 0) { return cast<usize>("true"); }
    return cast<usize>("false");
}

void _rt_put(usize buf, int pos, int c) {
    unsafe { *uint8 p = cast<*uint8>(buf + cast<usize>(pos)); *p = cast<uint8>(c); }
}

// hylian_float_to_str: render x into buf (buflen >= 40) as plain decimal with
// up to 6 fractional digits and trailing zeros trimmed to at least one
// ("2.0", "3.14159"); values >= 1e15 switch to "d.dddddde+N"; "nan"/"inf"/
// "-inf" as expected. Returns the byte count (no NUL is written).
int hylian_float_to_str(float x, usize buf, int buflen) {
    if (buflen < 40) { return 0; }
    int pos = 0;
    if (x != x) {
        _rt_put(buf, 0, 110); _rt_put(buf, 1, 97); _rt_put(buf, 2, 110);
        return 3;
    }
    if (x < 0.0) {
        _rt_put(buf, pos, 45);
        pos = pos + 1;
        x = 0.0 - x;
    }
    if (x > 1.0e308) {
        _rt_put(buf, pos, 105); _rt_put(buf, pos + 1, 110); _rt_put(buf, pos + 2, 102);
        return pos + 3;
    }
    int exp10 = 0;
    if (x >= 1.0e15) {
        // scientific notation: normalise the mantissa into [1, 10)
        while (x >= 10.0) {
            x = x / 10.0;
            exp10 = exp10 + 1;
        }
    }
    int ip = cast<int>(x);
    float frac = x - cast<float>(ip);
    int scaled = cast<int>(frac * 1000000.0 + 0.5);
    if (scaled >= 1000000) {
        ip = ip + 1;
        scaled = scaled - 1000000;
    }
    pos = pos + hylian_int_to_str(ip, buf + cast<usize>(pos), buflen - pos);
    _rt_put(buf, pos, 46);
    pos = pos + 1;
    // up to six fractional digits, trailing zeros dropped but at least one
    // digit kept: strip zeros arithmetically, then print what's left
    // zero-padded on the left to its digit count
    int nd = 6;
    int tz = scaled;
    while (nd > 1 && tz % 10 == 0) {
        tz = tz / 10;
        nd = nd - 1;
    }
    int div = 1;
    int k = 1;
    while (k < nd) {
        div = div * 10;
        k = k + 1;
    }
    while (div > 0) {
        int digit = tz / div;
        tz = tz - digit * div;
        _rt_put(buf, pos, 48 + digit);
        pos = pos + 1;
        div = div / 10;
    }
    if (exp10 > 0) {
        _rt_put(buf, pos, 101);
        _rt_put(buf, pos + 1, 43);
        pos = pos + 2;
        pos = pos + hylian_int_to_str(exp10, buf + cast<usize>(pos), buflen - pos);
    }
    return pos;
}

usize hylian_str_from_float(float x) {
    usize out = hylian_str_alloc(48);
    int n = hylian_float_to_str(x, out, 47);
    _rt_put(out, n, 0);
    return out;
}

// hylian_panic: what `panic("msg")` compiles to. Writes "panic: msg" plus a
// newline to stderr and terminates the process with exit status 1 (no
// unwinding, no cleanup - like abort() with a message). Always linked.
void hylian_panic(usize msg) {
    int n = _rt_strlen(msg);
    usize prefix = cast<usize>("panic: ");
    syscall(1, 2, cast<int>(prefix), 7);
    if (n > 0) { syscall(1, 2, cast<int>(msg), n); }
    usize newline = cast<usize>(10);
    usize newline_addr = cast<usize>(&newline);
    syscall(1, 2, cast<int>(newline_addr), 1);
    syscall(231, 1);
}

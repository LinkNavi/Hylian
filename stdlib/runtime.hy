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

naked void hylian_print(usize str_ptr, usize len) {
    syscall(1, 1, cast<int>(str_ptr), cast<int>(len));
}

naked void hylian_println(usize str_ptr, usize len) {
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
naked int hylian_int_to_str(int val, usize buf, int buflen) {
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

    bool neg = val < 0;
    int v = val;
    if (neg) {
        v = -v;
    }

    int pos = buflen;
    while (v > 0) {
        pos = pos - 1;
        if (pos < 0) {
            return 0;
        }
        int digit = v % 10;
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

// bytes — raw, length-carrying byte buffers.
//
// `str` is NUL-terminated, so it can't hold arbitrary binary data (a DNS
// packet, a gzip stream, a tar header all contain 0 bytes). Everything in
// here works on a `usize` base address plus an explicit length instead, and
// is what net/dns/http/inflate/tar are built on. Offsets are byte offsets
// from `base`; nothing is bounds-checked - callers own that.

include {
    platform.linux_x86_64,
    string,
}

// bytes_alloc: zero-filled buffer of n bytes (n is bumped to at least 1).
usize bytes_alloc(int n) {
    if (n < 1) { n = 1; }
    return raw_alloc(n);
}

// bytes_free: release a buffer obtained from bytes_alloc.
void bytes_free(usize base, int n) {
    if (n < 1) { n = 1; }
    raw_free(base, n);
}

// get8 / set8: one byte as an int 0-255.
int get8(usize base, int off) {
    uint8 c;
    unsafe { *uint8 p = cast<*uint8>(base + cast<usize>(off)); c = *p; }
    return cast<int>(c);
}

void set8(usize base, int off, int v) {
    unsafe { *uint8 p = cast<*uint8>(base + cast<usize>(off)); *p = cast<uint8>(v & 255); }
}

// Big-endian (network order) 16/32-bit accessors.
int get16be(usize base, int off) {
    return get8(base, off) * 256 + get8(base, off + 1);
}

void set16be(usize base, int off, int v) {
    set8(base, off, v / 256);
    set8(base, off + 1, v);
}

int get32be(usize base, int off) {
    return get16be(base, off) * 65536 + get16be(base, off + 2);
}

void set32be(usize base, int off, int v) {
    set16be(base, off, v / 65536);
    set16be(base, off + 2, v);
}

// get64 / set64: one native-endian 64-bit word (used for handle blocks).
int get64(usize base, int off) {
    int v;
    unsafe { *int p = cast<*int>(base + cast<usize>(off)); v = *p; }
    return v;
}

void set64(usize base, int off, int v) {
    unsafe { *int p = cast<*int>(base + cast<usize>(off)); *p = v; }
}

// bytes_copy: copy n bytes from src+soff to dst+doff (regions must not overlap).
void bytes_copy(usize dst, int doff, usize src, int soff, int n) {
    int i = 0;
    while (i < n) {
        set8(dst, doff + i, get8(src, soff + i));
        i += 1;
    }
}

// bytes_from_str: address of a str's bytes, for use with the functions here.
usize bytes_of(str s) {
    return cast<usize>(s);
}

// bytes_put_str: copy the characters of s (without the NUL) to dst+doff.
// Returns the offset just past what was written.
int bytes_put_str(usize dst, int doff, str s) {
    int n = length(s);
    bytes_copy(dst, doff, cast<usize>(s), 0, n);
    return doff + n;
}

// bytes_to_str: a fresh NUL-terminated str holding n bytes from base+off.
// Any NUL inside the range will of course end the string early for
// str-consuming functions; the trailing NUL is always added.
str bytes_to_str(usize base, int off, int n) {
    usize buf = raw_alloc(n + 1);
    bytes_copy(buf, 0, base, off, n);
    set8(buf, n, 0);
    return cast<str>(buf);
}

// bytes_grow: return a buffer of new_cap bytes holding the first `used`
// bytes of `base`, and free the old one (old_cap bytes).
usize bytes_grow(usize base, int used, int old_cap, int new_cap) {
    usize nb = raw_alloc(new_cap);
    bytes_copy(nb, 0, base, 0, used);
    raw_free(base, old_cap);
    return nb;
}

// ── growable buffer ──────────────────────────────────────────────────────────
//
// A Buffer is an opaque `usize` handle to {address, used, capacity}; append
// data as it arrives (e.g. from recv) and read it back with buf_addr/buf_len.

// buf_new: empty buffer.
usize buf_new() {
    usize b = raw_alloc(24);
    set64(b, 0, cast<int>(bytes_alloc(4096)));
    set64(b, 8, 0);
    set64(b, 16, 4096);
    return b;
}

// buf_addr / buf_len: current contents.
usize buf_addr(usize b) { return cast<usize>(get64(b, 0)); }
int buf_len(usize b) { return get64(b, 8); }

// buf_reserve: make room for at least n more bytes and return the address
// where they should be written (buf_addr + buf_len). Follow with buf_commit.
usize buf_reserve(usize b, int n) {
    int used = get64(b, 8);
    int cap = get64(b, 16);
    if (used + n > cap) {
        int ncap = cap * 2;
        while (ncap < used + n) { ncap = ncap * 2; }
        usize nb = bytes_grow(cast<usize>(get64(b, 0)), used, cap, ncap);
        set64(b, 0, cast<int>(nb));
        set64(b, 16, ncap);
    }
    return cast<usize>(get64(b, 0)) + cast<usize>(used);
}

// buf_commit: record that n bytes were written at the reserved address.
void buf_commit(usize b, int n) {
    set64(b, 8, get64(b, 8) + n);
}

// buf_append: append n bytes from src+soff.
void buf_append(usize b, usize src, int soff, int n) {
    usize dst = buf_reserve(b, n);
    bytes_copy(dst, 0, src, soff, n);
    buf_commit(b, n);
}

// buf_append_str: append a str's characters (no NUL).
void buf_append_str(usize b, str s) {
    buf_append(b, cast<usize>(s), 0, length(s));
}

// buf_free: release the buffer and its handle.
void buf_free(usize b) {
    bytes_free(cast<usize>(get64(b, 0)), get64(b, 16));
    raw_free(b, 24);
}

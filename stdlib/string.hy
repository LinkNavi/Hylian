// string — string utility functions.
//
// Written as plain functions (trim(s), not s.trim()) since Hylian doesn't
// have method-call syntax yet. Once it does, `s.trim()` can become sugar
// for `trim(s)` (UFCS) without anything in this file changing - every
// function here already takes the string as its first parameter for
// exactly that reason.

include {
    platform.linux_x86_64,
}

// length: number of bytes before the terminating nul. nil counts as 0 —
// is_empty() below already relied on that being true, by checking `s == nil`
// itself before ever calling length(); this makes it true unconditionally so
// any other nil `str` (e.g. read_all()'s failure return) is safe to measure
// too, instead of dereferencing a null pointer.
int length(str s) {
    if (s == nil) { return 0; }
    int n = 0;
    while (true) {
        usize p = cast<usize>(s) + cast<usize>(n);
        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        if (cast<int>(c) == 0) { return n; }
        n += 1;
    }
    return n;
}

// is_empty: true if s is nil or has zero length.
bool is_empty(str s) {
    if (s == nil) { return true; }
    return length(s) == 0;
}

// equals: byte-for-byte comparison.
bool equals(str a, str b) {
    int la = length(a);
    int lb = length(b);
    if (la != lb) { return false; }
    int i = 0;
    while (i < la) {
        usize pa = cast<usize>(a) + cast<usize>(i);
        usize pb = cast<usize>(b) + cast<usize>(i);
        uint8 ca; uint8 cb;
        unsafe {
            *uint8 bpa = cast<*uint8>(pa); ca = *bpa;
            *uint8 bpb = cast<*uint8>(pb); cb = *bpb;
        }
        if (ca != cb) { return false; }
        i += 1;
    }
    return true;
}

// index_of: index of the first occurrence of needle in s, or -1.
int index_of(str s, str needle) {
    int ls = length(s);
    int ln = length(needle);
    if (ln == 0) { return 0; }
    if (ln > ls) { return -1; }

    int i = 0;
    while (i <= ls - ln) {
        int j = 0;
        bool matched = true;
        while (j < ln) {
            usize ps = cast<usize>(s) + cast<usize>(i + j);
            usize pn = cast<usize>(needle) + cast<usize>(j);
            uint8 cs; uint8 cn;
            unsafe {
                *uint8 bps = cast<*uint8>(ps); cs = *bps;
                *uint8 bpn = cast<*uint8>(pn); cn = *bpn;
            }
            if (cs != cn) { matched = false; }
            j += 1;
        }
        if (matched) { return i; }
        i += 1;
    }
    return -1;
}

// contains: true if s contains needle anywhere.
bool contains(str s, str needle) {
    return index_of(s, needle) >= 0;
}

// starts_with: true if s begins with prefix.
bool starts_with(str s, str prefix) {
    int lp = length(prefix);
    if (lp > length(s)) { return false; }
    int i = 0;
    while (i < lp) {
        usize ps = cast<usize>(s) + cast<usize>(i);
        usize pp = cast<usize>(prefix) + cast<usize>(i);
        uint8 cs; uint8 cp;
        unsafe {
            *uint8 bps = cast<*uint8>(ps); cs = *bps;
            *uint8 bpp = cast<*uint8>(pp); cp = *bpp;
        }
        if (cs != cp) { return false; }
        i += 1;
    }
    return true;
}

// ends_with: true if s ends with suffix.
bool ends_with(str s, str suffix) {
    int ls = length(s);
    int lf = length(suffix);
    if (lf > ls) { return false; }
    int offset = ls - lf;
    int i = 0;
    while (i < lf) {
        usize ps = cast<usize>(s) + cast<usize>(offset + i);
        usize pf = cast<usize>(suffix) + cast<usize>(i);
        uint8 cs; uint8 cf;
        unsafe {
            *uint8 bps = cast<*uint8>(ps); cs = *bps;
            *uint8 bpf = cast<*uint8>(pf); cf = *bpf;
        }
        if (cs != cf) { return false; }
        i += 1;
    }
    return true;
}

// slice: substring from start (inclusive) to end (exclusive).
// Out-of-range indices are clamped rather than erroring.
str slice(str s, int start, int end) {
    int ls = length(s);
    if (start < 0) { start = 0; }
    if (end > ls) { end = ls; }
    if (end < start) { end = start; }
    int n = end - start;

    usize buf = raw_alloc(n + 1);
    int i = 0;
    while (i < n) {
        usize sp = cast<usize>(s) + cast<usize>(start + i);
        usize dp = buf + cast<usize>(i);
        uint8 c;
        unsafe {
            *uint8 sbp = cast<*uint8>(sp); c = *sbp;
            *uint8 dbp = cast<*uint8>(dp); *dbp = c;
        }
        i += 1;
    }
    usize term = buf + cast<usize>(n);
    unsafe { *uint8 tp = cast<*uint8>(term); *tp = 0; }
    return cast<str>(buf);
}

static bool _is_space(uint8 c) {
    int ci = cast<int>(c);
    return ci == 32 || ci == 9 || ci == 10 || ci == 13;
}

// trim_start: remove leading whitespace.
str trim_start(str s) {
    int ls = length(s);
    int i = 0;
    while (i < ls) {
        usize p = cast<usize>(s) + cast<usize>(i);
        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        if (!_is_space(c)) { break; }
        i += 1;
    }
    return slice(s, i, ls);
}

// trim_end: remove trailing whitespace.
str trim_end(str s) {
    int ls = length(s);
    int i = ls;
    while (i > 0) {
        usize p = cast<usize>(s) + cast<usize>(i - 1);
        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        if (!_is_space(c)) { break; }
        i -= 1;
    }
    return slice(s, 0, i);
}

// trim: remove leading and trailing whitespace.
str trim(str s) {
    return trim_end(trim_start(s));
}

static uint8 _to_upper_byte(uint8 c) {
    int ci = cast<int>(c);
    if (ci >= 97 && ci <= 122) { return cast<uint8>(ci - 32); }
    return c;
}

static uint8 _to_lower_byte(uint8 c) {
    int ci = cast<int>(c);
    if (ci >= 65 && ci <= 90) { return cast<uint8>(ci + 32); }
    return c;
}

// to_upper: ASCII-only uppercase conversion, returns a new string.
str to_upper(str s) {
    int n = length(s);
    usize buf = raw_alloc(n + 1);
    int i = 0;
    while (i < n) {
        usize sp = cast<usize>(s) + cast<usize>(i);
        usize dp = buf + cast<usize>(i);
        uint8 c;
        unsafe {
            *uint8 sbp = cast<*uint8>(sp); c = *sbp;
            *uint8 dbp = cast<*uint8>(dp); *dbp = _to_upper_byte(c);
        }
        i += 1;
    }
    usize term = buf + cast<usize>(n);
    unsafe { *uint8 tp = cast<*uint8>(term); *tp = 0; }
    return cast<str>(buf);
}

// to_lower: ASCII-only lowercase conversion, returns a new string.
str to_lower(str s) {
    int n = length(s);
    usize buf = raw_alloc(n + 1);
    int i = 0;
    while (i < n) {
        usize sp = cast<usize>(s) + cast<usize>(i);
        usize dp = buf + cast<usize>(i);
        uint8 c;
        unsafe {
            *uint8 sbp = cast<*uint8>(sp); c = *sbp;
            *uint8 dbp = cast<*uint8>(dp); *dbp = _to_lower_byte(c);
        }
        i += 1;
    }
    usize term = buf + cast<usize>(n);
    unsafe { *uint8 tp = cast<*uint8>(term); *tp = 0; }
    return cast<str>(buf);
}

// concat: returns a new string containing a followed by b.
str concat(str a, str b) {
    int la = length(a);
    int lb = length(b);
    usize buf = raw_alloc(la + lb + 1);
    int i = 0;
    while (i < la) {
        usize sp = cast<usize>(a) + cast<usize>(i);
        usize dp = buf + cast<usize>(i);
        uint8 c;
        unsafe {
            *uint8 sbp = cast<*uint8>(sp); c = *sbp;
            *uint8 dbp = cast<*uint8>(dp); *dbp = c;
        }
        i += 1;
    }
    i = 0;
    while (i < lb) {
        usize sp = cast<usize>(b) + cast<usize>(i);
        usize dp = buf + cast<usize>(la + i);
        uint8 c;
        unsafe {
            *uint8 sbp = cast<*uint8>(sp); c = *sbp;
            *uint8 dbp = cast<*uint8>(dp); *dbp = c;
        }
        i += 1;
    }
    usize term = buf + cast<usize>(la + lb);
    unsafe { *uint8 tp = cast<*uint8>(term); *tp = 0; }
    return cast<str>(buf);
}

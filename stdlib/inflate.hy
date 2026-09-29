// inflate — DEFLATE (RFC 1951) and gzip (RFC 1952) decompression.
//
//   usize out = gunzip(data, len);          // Buffer handle, or 0 on error
//   if (out == 0) { eprintln(inflate_error()); }
//   // buf_addr(out) / buf_len(out) hold the decompressed bytes
//
//   usize raw = inflate(data, len);         // for a bare deflate stream
//
// gunzip checks the gzip header, the CRC-32 and the size recorded in the
// trailer; a corrupt archive fails rather than yielding garbage. Only the
// first gzip member is decoded. Decompression only - there is no compressor.
//
// The decoder is the classic canonical-Huffman design (as in zlib's "puff"):
// simple and small rather than fast. `data` is a raw byte address + length
// (see `bytes`), so the input can be a file read or an HTTP body_addr.

include {
    string,
    bytes,
}

static str _inflate_err = "";

// inflate_error: why the last inflate/gunzip call failed.
str inflate_error() {
    return _inflate_err;
}

// Decoder state, a block of 64-bit words:
//   0 src  8 len  16 pos  24 bitbuf  32 bitcnt  40 failed  48 out Buffer
//   56 length-base  64 length-extra  72 dist-base  80 dist-extra  (arrays)
static int _S_SRC = 0;
static int _S_LEN = 8;
static int _S_POS = 16;
static int _S_BUF = 24;
static int _S_CNT = 32;
static int _S_ERR = 40;
static int _S_OUT = 48;
static int _S_LB  = 56;
static int _S_LE  = 64;
static int _S_DB  = 72;
static int _S_DE  = 80;

static bool _failed(usize st, str why) {
    if (get64(st, _S_ERR) == 0) {
        set64(st, _S_ERR, 1);
        _inflate_err = why;
    }
    return false;
}

// _bits: read `need` bits (LSB first), 0 <= need <= 16.
static int _bits(usize st, int need) {
    int buf = get64(st, _S_BUF);
    int cnt = get64(st, _S_CNT);
    while (cnt < need) {
        int pos = get64(st, _S_POS);
        if (pos >= get64(st, _S_LEN)) {
            _failed(st, "unexpected end of compressed data");
            return 0;
        }
        buf = buf | (get8(cast<usize>(get64(st, _S_SRC)), pos) << cnt);
        set64(st, _S_POS, pos + 1);
        cnt += 8;
    }
    int v = buf & ((1 << need) - 1);
    set64(st, _S_BUF, buf >> need);
    set64(st, _S_CNT, cnt - need);
    return v;
}

static void _put(usize st, int byte) {
    usize out = cast<usize>(get64(st, _S_OUT));
    usize p = buf_reserve(out, 1);
    set8(p, 0, byte);
    buf_commit(out, 1);
}

// _build: canonical Huffman table from code lengths[off .. off+n).
// counts[len] = number of codes of that length; symbols = symbols ordered by
// (length, value). Returns false for an over-subscribed set of lengths.
static bool _build(array<int> lengths, int off, int n, array<int> counts, array<int> symbols) {
    int i = 0;
    while (i < 16) { counts[i] = 0; i += 1; }
    i = 0;
    while (i < n) {
        int l = lengths[off + i];
        counts[l] = counts[l] + 1;
        i += 1;
    }
    int left = 1;
    i = 1;
    while (i < 16) {
        left = left * 2 - counts[i];
        if (left < 0) { return false; }
        i += 1;
    }
    array<int> offs = [];
    i = 0;
    while (i < 16) { offs.push(0); i += 1; }
    i = 1;
    while (i < 15) {
        offs[i + 1] = offs[i] + counts[i];
        i += 1;
    }
    i = 0;
    while (i < n) {
        int l = lengths[off + i];
        if (l != 0) {
            symbols[offs[l]] = i;
            offs[l] = offs[l] + 1;
        }
        i += 1;
    }
    return true;
}

// _decode: next symbol using a table from _build, or -1 on bad code / EOF.
static int _decode(usize st, array<int> counts, array<int> symbols) {
    int code = 0;
    int first = 0;
    int index = 0;
    int len = 1;
    while (len < 16) {
        code = code | _bits(st, 1);
        if (get64(st, _S_ERR) != 0) { return -1; }
        int count = counts[len];
        if (code - count < first) { return symbols[index + (code - first)]; }
        index += count;
        first += count;
        first = first << 1;
        code = code << 1;
        len += 1;
    }
    _failed(st, "invalid Huffman code");
    return -1;
}

static array<int> _zeros(int n) {
    array<int> a = [];
    int i = 0;
    while (i < n) { a.push(0); i += 1; }
    return a;
}

// _codes: decode literal/length + distance symbols until end-of-block.
static bool _codes(usize st, array<int> lc, array<int> ls, array<int> dc, array<int> ds) {
    array<int> lbase = cast<array<int> >(cast<usize>(get64(st, _S_LB)));
    array<int> lext = cast<array<int> >(cast<usize>(get64(st, _S_LE)));
    array<int> dbase = cast<array<int> >(cast<usize>(get64(st, _S_DB)));
    array<int> dext = cast<array<int> >(cast<usize>(get64(st, _S_DE)));
    usize out = cast<usize>(get64(st, _S_OUT));
    while (true) {
        int sym = _decode(st, lc, ls);
        if (sym < 0) { return false; }
        if (sym < 256) {
            _put(st, sym);
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) { return _failed(st, "invalid length code"); }
            int len = lbase[sym] + _bits(st, lext[sym]);
            int ds_ = _decode(st, dc, ds);
            if (ds_ < 0) { return false; }
            if (ds_ >= 30) { return _failed(st, "invalid distance code"); }
            int dist = dbase[ds_] + _bits(st, dext[ds_]);
            if (get64(st, _S_ERR) != 0) { return false; }
            if (dist > buf_len(out)) { return _failed(st, "distance reaches before start of output"); }
            int k = 0;
            while (k < len) {
                int b = get8(buf_addr(out), buf_len(out) - dist);
                _put(st, b);
                k += 1;
            }
        }
    }
    return true;
}

static bool _stored(usize st) {
    set64(st, _S_BUF, 0);
    set64(st, _S_CNT, 0);
    int pos = get64(st, _S_POS);
    int len = get64(st, _S_LEN);
    usize src = cast<usize>(get64(st, _S_SRC));
    if (pos + 4 > len) { return _failed(st, "truncated stored block"); }
    int n = get8(src, pos) + get8(src, pos + 1) * 256;
    int nn = get8(src, pos + 2) + get8(src, pos + 3) * 256;
    if (n != 65535 - nn) { return _failed(st, "stored block length check failed"); }
    pos += 4;
    if (pos + n > len) { return _failed(st, "truncated stored block"); }
    buf_append(cast<usize>(get64(st, _S_OUT)), src, pos, n);
    set64(st, _S_POS, pos + n);
    return true;
}

static bool _fixed(usize st) {
    array<int> lengths = _zeros(320);
    int i = 0;
    while (i < 144) { lengths[i] = 8; i += 1; }
    while (i < 256) { lengths[i] = 9; i += 1; }
    while (i < 280) { lengths[i] = 7; i += 1; }
    while (i < 288) { lengths[i] = 8; i += 1; }
    i = 0;
    while (i < 30) { lengths[288 + i] = 5; i += 1; }
    array<int> lc = _zeros(16); array<int> ls = _zeros(288);
    array<int> dc = _zeros(16); array<int> ds = _zeros(30);
    _build(lengths, 0, 288, lc, ls);
    _build(lengths, 288, 30, dc, ds);
    return _codes(st, lc, ls, dc, ds);
}

static bool _dynamic(usize st) {
    int nlen = _bits(st, 5) + 257;
    int ndist = _bits(st, 5) + 1;
    int ncode = _bits(st, 4) + 4;
    if (get64(st, _S_ERR) != 0) { return false; }
    if (nlen > 286 || ndist > 30) { return _failed(st, "too many length or distance codes"); }

    array<int> order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];
    array<int> lengths = _zeros(320);
    int i = 0;
    while (i < ncode) {
        lengths[order[i]] = _bits(st, 3);
        i += 1;
    }
    array<int> cc = _zeros(16); array<int> cs = _zeros(19);
    if (!_build(lengths, 0, 19, cc, cs)) { return _failed(st, "bad code-length code"); }

    array<int> lens = _zeros(320);
    int idx = 0;
    while (idx < nlen + ndist) {
        int sym = _decode(st, cc, cs);
        if (sym < 0) { return false; }
        if (sym < 16) {
            lens[idx] = sym;
            idx += 1;
        } else {
            int rep = 0;
            int val = 0;
            if (sym == 16) {
                if (idx == 0) { return _failed(st, "repeat with no previous length"); }
                val = lens[idx - 1];
                rep = 3 + _bits(st, 2);
            } else if (sym == 17) {
                rep = 3 + _bits(st, 3);
            } else {
                rep = 11 + _bits(st, 7);
            }
            if (idx + rep > nlen + ndist) { return _failed(st, "too many code lengths"); }
            while (rep > 0) { lens[idx] = val; idx += 1; rep -= 1; }
        }
    }
    if (lens[256] == 0) { return _failed(st, "missing end-of-block code"); }
    array<int> lc = _zeros(16); array<int> ls = _zeros(288);
    array<int> dc = _zeros(16); array<int> ds = _zeros(30);
    if (!_build(lens, 0, nlen, lc, ls)) { return _failed(st, "bad literal/length code"); }
    if (!_build(lens, nlen, ndist, dc, ds)) { return _failed(st, "bad distance code"); }
    return _codes(st, lc, ls, dc, ds);
}

// _run: inflate the stream at src..src+len into a new Buffer. Sets *consumed
// (written into the word at `used_out`) to the number of input bytes read.
static usize _run(usize src, int len, usize used_out) {
    usize st = bytes_alloc(96);
    set64(st, _S_SRC, cast<int>(src));
    set64(st, _S_LEN, len);
    usize out = buf_new();
    set64(st, _S_OUT, cast<int>(out));
    array<int> lb = [3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258];
    array<int> le = [0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0];
    array<int> db = [1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577];
    array<int> de = [0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13];
    set64(st, _S_LB, cast<int>(cast<usize>(lb)));
    set64(st, _S_LE, cast<int>(cast<usize>(le)));
    set64(st, _S_DB, cast<int>(cast<usize>(db)));
    set64(st, _S_DE, cast<int>(cast<usize>(de)));

    bool last = false;
    bool ok = true;
    while (ok && !last) {
        last = _bits(st, 1) == 1;
        int kind = _bits(st, 2);
        if (get64(st, _S_ERR) != 0) { ok = false; }
        else if (kind == 0) { ok = _stored(st); }
        else if (kind == 1) { ok = _fixed(st); }
        else if (kind == 2) { ok = _dynamic(st); }
        else { ok = _failed(st, "invalid block type"); }
    }
    set64(used_out, 0, get64(st, _S_POS));
    bool bad = get64(st, _S_ERR) != 0;
    bytes_free(st, 96);
    if (bad || !ok) {
        buf_free(out);
        return cast<usize>(0);
    }
    return out;
}

// inflate: decompress a raw DEFLATE stream. Returns a Buffer handle holding
// the output, or 0 (see inflate_error()).
usize inflate(usize data, int len) {
    _inflate_err = "";
    usize used = bytes_alloc(8);
    usize out = _run(data, len, used);
    bytes_free(used, 8);
    return out;
}

// ── gzip ─────────────────────────────────────────────────────────────────────

static int _crc_table = 0;

// crc32: CRC-32 (IEEE, as used by gzip/zip/png) of len bytes at data.
int crc32(usize data, int len) {
    if (_crc_table == 0) {
        usize t = bytes_alloc(256 * 8);
        int n = 0;
        while (n < 256) {
            int c = n;
            int k = 0;
            while (k < 8) {
                if ((c & 1) == 1) { c = 3988292384 ^ (c >> 1); } else { c = c >> 1; }
                k += 1;
            }
            set64(t, n * 8, c);
            n += 1;
        }
        _crc_table = cast<int>(t);
    }
    usize t = cast<usize>(_crc_table);
    int crc = 4294967295;
    int i = 0;
    while (i < len) {
        crc = get64(t, ((crc ^ get8(data, i)) & 255) * 8) ^ (crc >> 8);
        i += 1;
    }
    return crc ^ 4294967295;
}

static int _le32(usize p, int off) {
    return get8(p, off) + get8(p, off + 1) * 256 + get8(p, off + 2) * 65536 + get8(p, off + 3) * 16777216;
}

// is_gzip: true if the data starts with the gzip magic bytes 1f 8b.
bool is_gzip(usize data, int len) {
    return len >= 2 && get8(data, 0) == 31 && get8(data, 1) == 139;
}

// gunzip: decompress a gzip file image. Returns a Buffer handle with the
// original bytes, or 0 (see inflate_error()) if the header is invalid, the
// deflate stream is corrupt, or the CRC-32 / size in the trailer don't match.
usize gunzip(usize data, int len) {
    _inflate_err = "";
    if (len < 18 || !is_gzip(data, len)) {
        _inflate_err = "not a gzip stream";
        return cast<usize>(0);
    }
    if (get8(data, 2) != 8) {
        _inflate_err = "unsupported gzip compression method";
        return cast<usize>(0);
    }
    int flags = get8(data, 3);
    int pos = 10;
    if ((flags & 4) != 0) { // FEXTRA
        pos += 2 + get8(data, pos) + get8(data, pos + 1) * 256;
    }
    if ((flags & 8) != 0) { // FNAME
        while (pos < len && get8(data, pos) != 0) { pos += 1; }
        pos += 1;
    }
    if ((flags & 16) != 0) { // FCOMMENT
        while (pos < len && get8(data, pos) != 0) { pos += 1; }
        pos += 1;
    }
    if ((flags & 2) != 0) { pos += 2; } // FHCRC
    if (pos + 8 > len) {
        _inflate_err = "truncated gzip header";
        return cast<usize>(0);
    }

    usize used = bytes_alloc(8);
    usize out = _run(data + cast<usize>(pos), len - pos - 8, used);
    int consumed = get64(used, 0);
    bytes_free(used, 8);
    if (out == 0) { return out; }

    int tail = pos + consumed;
    if (tail + 8 > len) {
        buf_free(out);
        _inflate_err = "truncated gzip trailer";
        return cast<usize>(0);
    }
    int want_crc = _le32(data, tail);
    int want_size = _le32(data, tail + 4);
    if (want_size != (buf_len(out) & 4294967295)) {
        buf_free(out);
        _inflate_err = "gzip size mismatch";
        return cast<usize>(0);
    }
    if (crc32(buf_addr(out), buf_len(out)) != want_crc) {
        buf_free(out);
        _inflate_err = "gzip CRC-32 mismatch";
        return cast<usize>(0);
    }
    return out;
}

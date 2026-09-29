// tar — read and extract tar archives (ustar, GNU long names, PAX paths).
//
//   usize raw = gunzip(data, len);                        // see `inflate`
//   int n = tar_extract(buf_addr(raw), buf_len(raw), "out", 1);
//   if (n < 0) { eprintln(tar_error()); }
//
//   array<str> names = tar_list(addr, len);               // just the paths
//
// `strip` drops that many leading path components from every entry, like
// `tar --strip-components` (registries usually wrap a package in one
// top-level directory: strip 1).
//
// Safety - archives are untrusted input, so extraction refuses to write
// outside `dest`: an entry whose path is absolute or contains a ".."
// component makes tar_extract fail (before writing that entry), and symlinks
// and hard links are skipped entirely rather than created (a symlink could
// otherwise redirect a later entry outside dest). Device nodes and other
// special files are skipped too. Files get their archived permission bits
// masked to 0755-style (no setuid/setgid/sticky).

include {
    platform.linux_x86_64,
    string,
    bytes,
    os.fs,
}

static str _tar_err = "";

// tar_error: why the last tar call failed.
str tar_error() {
    return _tar_err;
}

// _octal: numeric value of the NUL/space padded octal field at data+off.
static int _octal(usize data, int off, int n) {
    int i = 0;
    while (i < n && get8(data, off + i) == 32) { i += 1; }
    int v = 0;
    while (i < n) {
        int c = get8(data, off + i);
        if (c < 48 || c > 55) { break; }
        v = v * 8 + (c - 48);
        i += 1;
    }
    return v;
}

// _field: NUL-terminated text of the fixed-width field at data+off.
static str _field(usize data, int off, int n) {
    int len = 0;
    while (len < n && get8(data, off + len) != 0) { len += 1; }
    return bytes_to_str(data, off, len);
}

static bool _is_zero_block(usize data, int off) {
    int i = 0;
    while (i < 512) {
        if (get8(data, off + i) != 0) { return false; }
        i += 1;
    }
    return true;
}

static bool _checksum_ok(usize data, int off) {
    int sum = 0;
    int i = 0;
    while (i < 512) {
        if (i >= 148 && i < 156) { sum += 32; } else { sum += get8(data, off + i); }
        i += 1;
    }
    return sum == _octal(data, off + 148, 8);
}

// _pax_path: the "path" record from a PAX extended header body, or nil.
// Records look like "<len> key=value\n".
static str _pax_path(usize data, int off, int size) {
    str text = bytes_to_str(data, off, size);
    array<str> lines = split(text, "\n");
    int i = 0;
    while (i < lines.len) {
        int sp = index_of(lines[i], " ");
        if (sp > 0) {
            str rec = slice(lines[i], sp + 1, length(lines[i]));
            if (starts_with(rec, "path=")) { return slice(rec, 5, length(rec)); }
        }
        i += 1;
    }
    return nil;
}

// _strip: drop `n` leading components from path ("a/b/c", 1 -> "b/c").
// Returns "" if the path has n or fewer components.
static str _strip(str path, int n) {
    int i = 0;
    while (i < n) {
        int slash = index_of(path, "/");
        if (slash < 0) { return ""; }
        path = slice(path, slash + 1, length(path));
        i += 1;
    }
    return path;
}

// _safe_path: false for absolute paths or any ".." component.
static bool _safe_path(str path) {
    if (starts_with(path, "/")) { return false; }
    array<str> parts = split(path, "/");
    int i = 0;
    while (i < parts.len) {
        if (equals(parts[i], "..")) { return false; }
        i += 1;
    }
    return true;
}

// _dirname: everything before the last "/" ("" if none).
static str _dirname(str path) {
    int slash = last_index_of(path, "/");
    if (slash < 0) { return ""; }
    return slice(path, 0, slash);
}

static bool _tar_fail(str msg) {
    _tar_err = msg;
    return false;
}

// _walk: shared driver. mode 0 = collect names into `names`, mode 1 = extract
// into dest. Returns the number of regular files seen/extracted, or -1.
static int _walk(usize data, int len, str dest, int strip, int mode, array<str> names) {
    _tar_err = "";
    int off = 0;
    int count = 0;
    str long_name = nil;

    if (mode == 1) {
        if (!mkdir_p(dest)) { _tar_fail(concat("cannot create ", dest)); return -1; }
    }

    while (off + 512 <= len) {
        if (_is_zero_block(data, off)) { return count; }
        if (!_checksum_ok(data, off)) { _tar_fail("bad tar header checksum"); return -1; }

        int size = _octal(data, off + 124, 12);
        int type = get8(data, off + 156);
        int body = off + 512;
        int next = body + ((size + 511) / 512) * 512;
        if (body + size > len) { _tar_fail("truncated tar archive"); return -1; }

        if (type == 76) { // 'L': GNU long name applies to the next entry
            long_name = bytes_to_str(data, body, size);
            off = next;
        } else if (type == 120) { // 'x': PAX per-entry header
            str p = _pax_path(data, body, size);
            if (p != nil) { long_name = p; }
            off = next;
        } else if (type == 103) { // 'g': PAX global header, ignore
            off = next;
        } else {
            str name = long_name;
            long_name = nil;
            if (name == nil) {
                name = _field(data, off, 100);
                if (get8(data, off + 257) == 117 && get8(data, off + 258) == 115) { // "us(tar)"
                    str prefix = _field(data, off + 345, 155);
                    if (length(prefix) > 0) { name = concat(concat(prefix, "/"), name); }
                }
            }
            if (!_safe_path(name)) { _tar_fail(concat("unsafe path in archive: ", name)); return -1; }
            while (ends_with(name, "/") && length(name) > 0) { name = slice(name, 0, length(name) - 1); }
            str rel = _strip(name, strip);

            bool is_file = type == 48 || type == 0 || type == 55;
            bool is_dir = type == 53;
            if (length(rel) > 0 && (is_file || is_dir)) {
                if (mode == 0) {
                    if (is_file) { names.push(rel); count += 1; }
                } else {
                    str target = concat(concat(dest, "/"), rel);
                    if (is_dir) {
                        mkdir_p(target);
                    } else {
                        mkdir_p(_dirname(target));
                        int perm = _octal(data, off + 100, 8) & 493; // 0755
                        if (perm == 0) { perm = 420; }
                        int fd = open(target, O_WRONLY + O_CREAT + O_TRUNC, perm);
                        if (fd < 0) { _tar_fail(concat("cannot write ", target)); return -1; }
                        int done = 0;
                        while (done < size) {
                            int n = write(fd, cast<str>(data + cast<usize>(body + done)), size - done);
                            if (n <= 0) { break; }
                            done += n;
                        }
                        close(fd);
                        if (done != size) { _tar_fail(concat("short write to ", target)); return -1; }
                        count += 1;
                    }
                }
            }
            off = next;
        }
    }
    return count;
}

// tar_list: archived paths of the regular files in the archive (no component
// stripping). Returns nil if the archive is malformed (see tar_error()).
array<str> tar_list(usize data, int len) {
    array<str> names = [];
    if (_walk(data, len, "", 0, 0, names) < 0) { return nil; }
    return names;
}

// tar_extract: extract the archive at data..data+len into directory `dest`
// (created if needed), dropping `strip` leading path components. Returns the
// number of regular files written, or -1 on error (see tar_error()).
int tar_extract(usize data, int len, str dest, int strip) {
    array<str> unused = [];
    return _walk(data, len, dest, strip, 1, unused);
}

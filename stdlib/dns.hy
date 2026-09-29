// dns — hostname -> IPv4 address.
//
//   str ip = dns_resolve("example.com");   // "93.184.216.34", or nil
//
// dns_resolve tries, in order:
//   1. the name is already a dotted-quad IPv4 address -> returned as is
//   2. /etc/hosts (so "localhost" and any local overrides work)
//   3. a real DNS query (A record, UDP port 53) to each `nameserver` listed
//      in /etc/resolv.conf, with a timeout and one retry per server
//
// IPv4 only: AAAA records, search domains, TCP fallback for truncated
// replies and DNSSEC are not implemented. Results are not cached.
//
// dns_query lets a caller pick the server explicitly (also what the tests
// use to talk to a fake local server).

include {
    platform.linux_x86_64,
    string,
    bytes,
    net,
}

static int DNS_TIMEOUT_SECS = 3;
static int DNS_TYPE_A       = 1;
static int _dns_next_id     = 0;

// _slurp: whole contents of a small text file, or nil if it can't be opened.
// (Local syscall-based reader: os.fs also defines close(), which would clash
// with net's if both were included.)
static str _slurp(str path) {
    int fd = syscall(NR_OPEN, cast<int>(path), 0, 0);
    if (fd < 0) { return nil; }
    int cap = 4096;
    usize buf = bytes_alloc(cap);
    int total = 0;
    while (true) {
        if (total >= cap - 1) {
            buf = bytes_grow(buf, total, cap, cap * 2);
            cap = cap * 2;
        }
        int n = syscall(NR_READ, fd, cast<int>(buf + cast<usize>(total)), cap - 1 - total);
        if (n <= 0) { break; }
        total += n;
    }
    syscall(NR_CLOSE, fd);
    set8(buf, total, 0);
    return cast<str>(buf);
}

// _tokens: whitespace-separated words of a line, with any "# comment" removed.
static array<str> _tokens(str line) {
    int hash = index_of(line, "#");
    if (hash >= 0) { line = slice(line, 0, hash); }
    line = replace(line, "\t", " ");
    line = replace(line, "\r", " ");
    array<str> out = [];
    array<str> raw = split(line, " ");
    int i = 0;
    while (i < raw.len) {
        if (length(raw[i]) > 0) { out.push(raw[i]); }
        i += 1;
    }
    return out;
}

// _hosts_lookup: first /etc/hosts entry for name (case-insensitive), or nil.
static str _hosts_lookup(str name) {
    str text = _slurp("/etc/hosts");
    if (text == nil) { return nil; }
    str want = to_lower(name);
    array<str> lines = split(text, "\n");
    int i = 0;
    while (i < lines.len) {
        array<str> t = _tokens(lines[i]);
        if (t.len >= 2 && _parse_ipv4_valid(t[0])) {
            int j = 1;
            while (j < t.len) {
                if (equals(to_lower(t[j]), want)) { return t[0]; }
                j += 1;
            }
        }
        i += 1;
    }
    return nil;
}

// dns_nameservers: IPv4 nameservers from /etc/resolv.conf, in file order.
array<str> dns_nameservers() {
    array<str> out = [];
    str text = _slurp("/etc/resolv.conf");
    if (text == nil) { return out; }
    array<str> lines = split(text, "\n");
    int i = 0;
    while (i < lines.len) {
        array<str> t = _tokens(lines[i]);
        if (t.len >= 2 && equals(t[0], "nameserver") && _parse_ipv4_valid(t[1])) {
            out.push(t[1]);
        }
        i += 1;
    }
    return out;
}

// _skip_name: offset just past a (possibly compressed) domain name at off.
static int _skip_name(usize pkt, int off, int len) {
    while (off < len) {
        int b = get8(pkt, off);
        if (b == 0) { return off + 1; }
        if ((b & 192) == 192) { return off + 2; } // compression pointer ends the name
        off += b + 1;
    }
    return len;
}

// _query_len / _build_query: wire size of, and a new buffer holding, an
// A-record query for name (12-byte header, labels, type + class).
static int _query_len(str name) {
    return 12 + length(name) + 2 + 4;
}

static usize _build_query(str name, int id) {
    int n = length(name);
    usize q = bytes_alloc(_query_len(name));
    set16be(q, 0, id);
    set16be(q, 2, 256);   // flags: recursion desired
    set16be(q, 4, 1);     // one question
    int off = 12;
    int start = 0;
    while (start <= n) {
        int dot = index_of_from(name, ".", start);
        int end = dot;
        if (dot < 0) { end = n; }
        int lablen = end - start;
        set8(q, off, lablen);
        off += 1;
        bytes_copy(q, off, cast<usize>(name), start, lablen);
        off += lablen;
        start = end + 1;
    }
    set8(q, off, 0);
    off += 1;
    set16be(q, off, DNS_TYPE_A);
    set16be(q, off + 2, 1); // class IN
    return q;
}

// _parse_reply: the first A record's address as a dotted-quad string, or nil
// (wrong id, error rcode, or no A record in the answers).
static str _parse_reply(usize pkt, int len, int id) {
    if (len < 12) { return nil; }
    if (get16be(pkt, 0) != id) { return nil; }
    int flags = get16be(pkt, 2);
    if ((flags & 32768) == 0) { return nil; } // not a response
    if ((flags & 15) != 0) { return nil; }    // NXDOMAIN, SERVFAIL, ...
    int qd = get16be(pkt, 4);
    int an = get16be(pkt, 6);
    int off = 12;
    int i = 0;
    while (i < qd) {
        off = _skip_name(pkt, off, len) + 4;
        i += 1;
    }
    i = 0;
    while (i < an) {
        off = _skip_name(pkt, off, len);
        if (off + 10 > len) { return nil; }
        int rtype = get16be(pkt, off);
        int rdlen = get16be(pkt, off + 8);
        off += 10;
        if (off + rdlen > len) { return nil; }
        if (rtype == DNS_TYPE_A && rdlen == 4) {
            array<str> parts = [];
            int k = 0;
            while (k < 4) {
                parts.push(int_to_str_dec(get8(pkt, off + k)));
                k += 1;
            }
            return join(parts, ".");
        }
        off += rdlen;
        i += 1;
    }
    return nil;
}

// int_to_str_dec: decimal text of a small non-negative int (local so dns
// doesn't depend on io).
static str int_to_str_dec(int v) {
    if (v == 0) { return "0"; }
    array<str> digits = [];
    str tbl = "0123456789";
    while (v > 0) {
        digits.push(slice(tbl, v % 10, v % 10 + 1));
        v = v / 10;
    }
    // digits were collected least-significant first
    array<str> rev = [];
    int i = digits.len - 1;
    while (i >= 0) { rev.push(digits[i]); i -= 1; }
    return join(rev, "");
}

// dns_query: ask one specific server (port 53) for name's A record. Returns
// the address as a dotted-quad string, or nil on timeout / error / no A record.
str dns_query(str server_ip, str name) {
    return dns_query_port(server_ip, 53, name);
}

// dns_query_port: dns_query against a non-standard port.
str dns_query_port(str server_ip, int port, str name) {
    int fd = udp_socket();
    if (fd < 0) { return nil; }
    set_timeout(fd, DNS_TIMEOUT_SECS);

    _dns_next_id += 1;
    int id = (syscall(NR_GETPID) * 31 + _dns_next_id * 7919) % 65536;
    usize q = _build_query(name, id);
    int qlen = _query_len(name);
    usize reply = bytes_alloc(1500);

    str result = nil;
    int attempt = 0;
    while (attempt < 2 && result == nil) {
        if (sendto(fd, q, qlen, server_ip, port) == qlen) {
            int n = recvfrom(fd, reply, 1500);
            if (n > 0) {
                result = _parse_reply(reply, n, id);
                if (result == nil) { attempt = 2; } // definitive failure, don't retry
            }
        }
        attempt += 1;
    }
    bytes_free(q, qlen);
    bytes_free(reply, 1500);
    close(fd);
    return result;
}

// dns_resolve: see the header comment. Returns a dotted-quad IPv4 string or nil.
str dns_resolve(str name) {
    if (length(name) == 0) { return nil; }
    if (_parse_ipv4_valid(name)) { return name; }
    str host = _hosts_lookup(name);
    if (host != nil) { return host; }
    array<str> servers = dns_nameservers();
    int i = 0;
    while (i < servers.len) {
        str ip = dns_query(servers[i], name);
        if (ip != nil) { return ip; }
        i += 1;
    }
    return nil;
}

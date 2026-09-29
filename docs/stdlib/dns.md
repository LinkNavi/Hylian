# `dns`

```hylian
include { dns, }
```

Source: `stdlib/dns.hy`. Hostname → IPv4 address.

| Function | Signature | Description |
|---|---|---|
| `dns_resolve` | `str dns_resolve(str name)` | Dotted-quad IPv4 string, or `nil`. |
| `dns_query` | `str dns_query(str server_ip, str name)` | Ask one specific server (port 53) for an A record. |
| `dns_query_port` | `str dns_query_port(str server_ip, int port, str name)` | Same, non-standard port. |
| `dns_nameservers` | `array<str> dns_nameservers()` | IPv4 `nameserver` entries from `/etc/resolv.conf`. |

`dns_resolve` tries, in order: the name is already an IPv4 literal → `/etc/hosts` →
a real DNS query (A record over UDP) to each nameserver in `/etc/resolv.conf` (3 s
timeout, one retry each). CNAME chains are followed implicitly (the first A record in
the answer is used).

**Not implemented:** IPv6/AAAA, search domains, TCP fallback for truncated replies,
caching, DNSSEC.

```hylian
include { io, dns, }

int main() {
    str ip = dns_resolve("example.com");
    if (ip == nil) { return 1; }
    println(ip);
    return 0;
}
```

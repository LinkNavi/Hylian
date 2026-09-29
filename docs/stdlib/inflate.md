# `inflate`

```hylian
include { inflate, }
```

Source: `stdlib/inflate.hy`. DEFLATE (RFC 1951) and gzip (RFC 1952) **decompression**
(there is no compressor). Input is a raw byte address + length (see [`bytes`](bytes.md));
output is a Buffer handle (`buf_addr`/`buf_len`), or `0` on error.

| Function | Signature | Description |
|---|---|---|
| `gunzip` | `usize gunzip(usize data, int len)` | Decompress a gzip image. Verifies the header, CRC-32 and size trailer; a corrupt file fails rather than yielding garbage. Only the first member is decoded. |
| `inflate` | `usize inflate(usize data, int len)` | Decompress a bare DEFLATE stream. |
| `is_gzip` | `bool is_gzip(usize data, int len)` | Starts with the `1f 8b` magic? |
| `crc32` | `int crc32(usize data, int len)` | CRC-32 (IEEE). |
| `inflate_error` | `str inflate_error()` | Why the last call failed. |

The decoder is a small canonical-Huffman design (stored, fixed and dynamic blocks); it
favours simplicity over speed.

```hylian
include { io, bytes, os.fs, inflate, }

int main() {
    str gz = read_all("data.gz");
    usize out = gunzip(cast<usize>(gz), file_size("data.gz"));
    if (out == 0) { eprintln(inflate_error()); return 1; }
    println(bytes_to_str(buf_addr(out), 0, buf_len(out)));
    return 0;
}
```

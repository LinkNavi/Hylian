# `map`

```hylian
include { map, }
```

Source: `stdlib/map.hy`. A string-keyed hash map. The language has no `map<K,V>` type, so a
map is an opaque `usize` handle. Keys are copied on insert; `str` values are stored by
reference. Entries keep **insertion order**, which is also the iteration order.

| Function | Signature | Description |
|---|---|---|
| `map_new` | `usize map_new()` | Create an empty map. |
| `map_put` / `map_put_int` | `void map_put(usize m, str key, str value)` | Set a key to a `str` / `int` value (replacing any existing value). |
| `map_get` / `map_get_int` | `str map_get(usize m, str key, str fallback)` | Look up a key; `fallback` if absent. |
| `map_has` | `bool map_has(usize m, str key)` | Is the key present? |
| `map_remove` | `bool map_remove(usize m, str key)` | Delete a key (O(n); preserves order). `true` if it was present. |
| `map_len` | `int map_len(usize m)` | Number of entries. |
| `map_key_at` / `map_val_at` / `map_int_at` | `str map_key_at(usize m, int i)` | The `i`-th entry in insertion order. |

A key should always be read back the way it was written (`map_get` for `map_put`,
`map_get_int` for `map_put_int`) — values are one raw word each.

```hylian
include { io, map, }

int main() {
    usize deps = map_new();
    map_put(deps, "http", "1.0.0");
    map_put_int(deps, "retries", 3);
    println(map_get(deps, "http", "?"));       // 1.0.0
    int i = 0;
    while (i < map_len(deps)) {                // iterate in insertion order
        println(map_key_at(deps, i));
        i += 1;
    }
    return 0;
}
```

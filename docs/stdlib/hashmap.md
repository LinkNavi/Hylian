# hashmap

A typed, string-keyed hash map, built on [generics](../language/syntax.md#generics). Unlike
[map](map.md) (an opaque `usize` handle holding raw words) the value type is part of the type.

```
include { std.hashmap, }

HashMap<int> m = new HashMap<int>();
m.put("count", 3);
v, err := m.get("count");          // (V, Error?): err is set when the key is missing
int c = m.get_or("nope", -1);
int n = try m.get("count");        // from a function returning Error? or (T, Error?)
```

| Method | |
|---|---|
| `put(str key, V val)` | insert or overwrite |
| `get(str key) -> (V, Error?)` | error code 404 for a missing key |
| `get_or(str key, V fallback) -> V` | |
| `has(str key) -> bool`, `len() -> int` | |
| `key_at(int i)`, `val_at(int i)` | iterate in insertion order |
| `remove(str key)` | keeps the order of the rest |

`V` can be `int`, `str`, `float`, or any one-word value. Quotes inside `{{ }}` interpolation
must be escaped, so bind `m.get_or("k", 0)` to a local before printing it.

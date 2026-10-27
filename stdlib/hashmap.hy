// hashmap — a typed, string-keyed hash map built on generics.
//
//   HashMap<int> m = new HashMap<int>();
//   m.put("count", 3);
//   v, err := m.get("count");        // (V, Error?) - err is set for a missing key
//   int c = m.get_or("nope", -1);
//   int n = try m.get("count");      // inside a function returning Error?/(T, Error?)
//
// Unlike map.hy (an opaque usize handle holding raw words) the value type is
// part of the type: HashMap<str>, HashMap<float>, ... Keys are strings.
// Entries keep insertion order (key_at / val_at), and removal keeps that order.
// Lookup uses an open-addressed index of (entry + 1), rebuilt when more than
// half full or after a removal.

include {
    string,
    bytes,
}

static int _hm_hash(str key) {
    int h = 5381;
    int n = length(key);
    int i = 0;
    while (i < n) {
        h = h * 33 + get8(cast<usize>(key), i);
        i += 1;
    }
    if (h < 0) { h = 0 - (h + 1); }
    return h;
}

class HashMap<V> {
    array<str> keys;
    array<V> vals;
    array<int> index;
    int cap;

    HashMap() {
        keys = [];
        vals = [];
        index = [];
        cap = 0;
        self.rebuild(16);
    }

    void rebuild(int newcap) {
        index = [];
        int i = 0;
        while (i < newcap) { index.push(0); i += 1; }
        cap = newcap;
        i = 0;
        while (i < keys.len) {
            int slot = _hm_hash(keys[i]) % cap;
            while (index[slot] != 0) { slot = (slot + 1) % cap; }
            index[slot] = i + 1;
            i += 1;
        }
    }

    // entry number of `key`, or -1
    int find(str key) {
        int slot = _hm_hash(key) % cap;
        while (index[slot] != 0) {
            int e = index[slot] - 1;
            if (keys[e] == key) { return e; }
            slot = (slot + 1) % cap;
        }
        return -1;
    }

    int len() { return keys.len; }

    bool has(str key) { return self.find(key) >= 0; }

    void put(str key, V val) {
        int e = self.find(key);
        if (e >= 0) { vals[e] = val; return; }
        keys.push(key);
        vals.push(val);
        if (keys.len * 2 > cap) { self.rebuild(cap * 2); return; }
        int slot = _hm_hash(key) % cap;
        while (index[slot] != 0) { slot = (slot + 1) % cap; }
        index[slot] = keys.len;
    }

    (V, Error?) get(str key) {
        int e = self.find(key);
        if (e < 0) { return (vals[0], Err("key not found: " + key, 404)); }
        return (vals[e], nil);
    }

    V get_or(str key, V fallback) {
        int e = self.find(key);
        if (e < 0) { return fallback; }
        return vals[e];
    }

    str key_at(int i) { return keys[i]; }
    V val_at(int i) { return vals[i]; }

    void remove(str key) {
        int e = self.find(key);
        if (e < 0) { return; }
        array<str> nk = [];
        array<V> nv = [];
        int i = 0;
        while (i < keys.len) {
            if (i != e) { nk.push(keys[i]); nv.push(vals[i]); }
            i += 1;
        }
        keys = nk;
        vals = nv;
        self.rebuild(cap);
    }
}

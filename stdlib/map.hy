// map — string-keyed hash map.
//
//   usize m = map_new();
//   map_put(m, "name", "hylian");
//   str v = map_get(m, "name", "?");     // "hylian"
//   map_put_int(m, "count", 3);          // ints share the same table
//   int c = map_get_int(m, "count", 0);
//
// A map is an opaque `usize` handle (there is no map<K,V> type in the
// language, and a class value can't be handed around by pointer), pointing at
// a small raw block. Keys are copied on insert; str values are stored by
// reference. Entries keep insertion order, so map_key_at / map_val_at give a
// stable iteration order:
//
//   int i = 0;
//   while (i < map_len(m)) { ... map_key_at(m, i) ...; i += 1; }
//
// Handle layout (5 words): keys array, vals array (array<int> of raw words),
// index table address, index capacity, [count is keys.len]. The index is an
// open-addressed table of (entry index + 1), 0 meaning empty; it is rebuilt
// whenever it gets more than half full or an entry is removed.
//
// Values are one machine word each, so str and int values can coexist in a
// map - but a given key should always be read back the way it was written.

include {
    string,
    bytes,
}

static int _MAP_KEYS = 0;
static int _MAP_VALS = 8;
static int _MAP_IDX  = 16;
static int _MAP_CAP  = 24;

static int _map_hash(str key) {
    int h = 5381;
    int n = length(key);
    int i = 0;
    while (i < n) {
        h = h * 33 + get8(cast<usize>(key), i);
        i += 1;
    }
    if (h < 0) { h = 0 - (h + 1); } // non-negative without relying on abs
    return h;
}

static array<str> _map_keys(usize m) {
    return cast<array<str> >(cast<usize>(get64(m, _MAP_KEYS)));
}

static array<int> _map_vals(usize m) {
    return cast<array<int> >(cast<usize>(get64(m, _MAP_VALS)));
}

// _map_rebuild: recreate the index table with capacity `cap` (power of two).
static void _map_rebuild(usize m, int cap) {
    int old = get64(m, _MAP_IDX);
    int old_cap = get64(m, _MAP_CAP);
    if (old != 0) { raw_free(cast<usize>(old), old_cap * 8); }

    usize idx = raw_alloc(cap * 8);
    array<str> keys = _map_keys(m);
    int i = 0;
    while (i < keys.len) {
        int slot = _map_hash(keys[i]) % cap;
        while (get64(idx, slot * 8) != 0) { slot = (slot + 1) % cap; }
        set64(idx, slot * 8, i + 1);
        i += 1;
    }
    set64(m, _MAP_IDX, cast<int>(idx));
    set64(m, _MAP_CAP, cap);
}

// map_new: create an empty map.
usize map_new() {
    usize m = raw_alloc(32);
    array<str> keys = [];
    array<int> vals = [];
    set64(m, _MAP_KEYS, cast<int>(cast<usize>(keys)));
    set64(m, _MAP_VALS, cast<int>(cast<usize>(vals)));
    set64(m, _MAP_IDX, 0);
    _map_rebuild(m, 16);
    return m;
}

// map_len: number of entries.
int map_len(usize m) {
    array<str> keys = _map_keys(m);
    return keys.len;
}

// _map_find: entry index of key, or -1.
static int _map_find(usize m, str key) {
    usize idx = cast<usize>(get64(m, _MAP_IDX));
    int cap = get64(m, _MAP_CAP);
    array<str> keys = _map_keys(m);
    int slot = _map_hash(key) % cap;
    while (true) {
        int e = get64(idx, slot * 8);
        if (e == 0) { return -1; }
        if (equals(keys[e - 1], key)) { return e - 1; }
        slot = (slot + 1) % cap;
    }
    return -1;
}

// map_has: true if key is present.
bool map_has(usize m, str key) {
    return _map_find(m, key) >= 0;
}

static void _map_put_word(usize m, str key, int word) {
    array<int> vals = _map_vals(m);
    int e = _map_find(m, key);
    if (e >= 0) {
        vals[e] = word;
        return;
    }
    array<str> keys = _map_keys(m);
    keys.push(slice(key, 0, length(key)));
    vals.push(word);
    int cap = get64(m, _MAP_CAP);
    if (keys.len * 2 > cap) {
        _map_rebuild(m, cap * 2);
    } else {
        usize idx = cast<usize>(get64(m, _MAP_IDX));
        int slot = _map_hash(key) % cap;
        while (get64(idx, slot * 8) != 0) { slot = (slot + 1) % cap; }
        set64(idx, slot * 8, keys.len);
    }
}

// map_put: set key to a str value (replacing any existing value).
void map_put(usize m, str key, str value) {
    _map_put_word(m, key, cast<int>(cast<usize>(value)));
}

// map_put_int: set key to an int value.
void map_put_int(usize m, str key, int value) {
    _map_put_word(m, key, value);
}

// map_get: the str stored under key, or `fallback` if absent.
str map_get(usize m, str key, str fallback) {
    int e = _map_find(m, key);
    if (e < 0) { return fallback; }
    array<int> vals = _map_vals(m);
    return cast<str>(cast<usize>(vals[e]));
}

// map_get_int: the int stored under key, or `fallback` if absent.
int map_get_int(usize m, str key, int fallback) {
    int e = _map_find(m, key);
    if (e < 0) { return fallback; }
    array<int> vals = _map_vals(m);
    return vals[e];
}

// map_key_at / map_val_at / map_int_at: the i-th entry in insertion order.
str map_key_at(usize m, int i) {
    array<str> keys = _map_keys(m);
    return keys[i];
}

str map_val_at(usize m, int i) {
    array<int> vals = _map_vals(m);
    return cast<str>(cast<usize>(vals[i]));
}

int map_int_at(usize m, int i) {
    array<int> vals = _map_vals(m);
    return vals[i];
}

// map_remove: delete key. Returns true if it was present. Later entries
// shift down by one (insertion order is preserved) and the index is rebuilt.
bool map_remove(usize m, str key) {
    int e = _map_find(m, key);
    if (e < 0) { return false; }
    array<str> keys = _map_keys(m);
    array<int> vals = _map_vals(m);
    int i = e;
    while (i < keys.len - 1) {
        keys[i] = keys[i + 1];
        vals[i] = vals[i + 1];
        i += 1;
    }
    keys.pop();
    vals.pop();
    _map_rebuild(m, get64(m, _MAP_CAP));
    return true;
}

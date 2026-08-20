// array — the runtime backing `array<T>`.
//
// The compiler lowers array syntax to calls to the functions in this file:
// `[1, 2, 3]` becomes hylian_array_alloc + repeated hylian_array_push,
// `a[i]` becomes hylian_array_get, `a[i] = v` hylian_array_set, `a.push(v)`
// and `a.pop()` the obvious ones, and `a.len` / `a.cap` are read DIRECTLY as
// words at offsets 0 and 8 of the array pointer (see IR_ARRAY_LEN /
// IR_ARRAY_CAP in compiler/ir_to_mir.c). That last part is why the header
// layout below is not free to change on this side alone.
//
// None of these existed before. Arrays parsed, typechecked and generated code,
// and then every program that used one failed to link with "undefined
// reference to hylian_array_alloc" — so arrays were entirely unusable, which
// is why the rest of the stdlib passes arguments as raw pointer blocks.
//
// Header layout (24 bytes), then a separately allocated data block:
//   [0..8)   len   — number of elements in use     (read directly by codegen)
//   [8..16)  cap   — elements the data block holds (read directly by codegen)
//   [16..24) data  — pointer to the element storage
//
// The data block is separate rather than inline so that growing an array does
// not move its header. If the header moved, every variable already holding the
// array pointer would be left dangling — and the compiler stores that pointer
// in an ordinary local, so there would be no way to find and update them all.
//
// Every element is 8 bytes. Hylian's array element types are int, str,
// pointers and class references, all of which are pointer-width.
//
// Like mem.hy and runtime.hy this is always linked into every binary, so it is
// compiled standalone and deliberately does NOT include platform.linux_x86_64:
// user code includes that file too, and two translation units both defining
// raw_alloc/raw_free would collide at link time. Hence the private mmap/munmap
// wrappers with the syscall numbers inlined (9 = mmap, 11 = munmap), the same
// deliberate exception mem.hy makes for the same reason.

static int ARRAY_HEADER_SIZE = 24;
static int ARRAY_ELEM_SIZE   = 8;
static int ARRAY_MIN_CAP     = 8;

usize _arr_raw_alloc(int size) {
    return cast<usize>(syscall(9, 0, size, 3, 34, -1, 0));
}

void _arr_raw_free(usize ptr, int size) {
    syscall(11, cast<int>(ptr), size, 0);
}

usize _arr_load(usize addr) {
    usize v;
    unsafe {
        *usize p = cast<*usize>(addr);
        v = *p;
    }
    return v;
}

void _arr_store(usize addr, usize value) {
    unsafe {
        *usize p = cast<*usize>(addr);
        *p = value;
    }
}

// hylian_array_alloc: new empty array with room for at least `hint` elements.
usize hylian_array_alloc(int hint) {
    int cap = hint;
    if (cap < ARRAY_MIN_CAP) { cap = ARRAY_MIN_CAP; }

    usize header = _arr_raw_alloc(ARRAY_HEADER_SIZE);
    usize data = _arr_raw_alloc(cap * ARRAY_ELEM_SIZE);

    _arr_store(header, cast<usize>(0));
    _arr_store(header + cast<usize>(8), cast<usize>(cap));
    _arr_store(header + cast<usize>(16), data);
    return header;
}

// hylian_array_free: release an array's storage. Not emitted by the compiler
// (arrays currently live for the life of the process), but needed by any code
// that builds a lot of short-lived arrays and wants the memory back.
void hylian_array_free(usize arr) {
    if (arr == cast<usize>(0)) { return; }
    usize cap = _arr_load(arr + cast<usize>(8));
    usize data = _arr_load(arr + cast<usize>(16));
    _arr_raw_free(data, cast<int>(cap) * ARRAY_ELEM_SIZE);
    _arr_raw_free(arr, ARRAY_HEADER_SIZE);
}

// _arr_grow: make room for at least `needed` elements, doubling each time so
// repeated pushes stay amortised O(1) rather than copying on every append.
void _arr_grow(usize arr, int needed) {
    usize cap = _arr_load(arr + cast<usize>(8));
    if (cast<int>(cap) >= needed) { return; }

    int new_cap = cast<int>(cap);
    if (new_cap < ARRAY_MIN_CAP) { new_cap = ARRAY_MIN_CAP; }
    while (new_cap < needed) {
        new_cap = new_cap * 2;
    }

    usize old_data = _arr_load(arr + cast<usize>(16));
    usize new_data = _arr_raw_alloc(new_cap * ARRAY_ELEM_SIZE);

    usize len = _arr_load(arr);
    int i = 0;
    while (i < cast<int>(len)) {
        usize off = cast<usize>(i * ARRAY_ELEM_SIZE);
        _arr_store(new_data + off, _arr_load(old_data + off));
        i += 1;
    }

    _arr_raw_free(old_data, cast<int>(cap) * ARRAY_ELEM_SIZE);
    _arr_store(arr + cast<usize>(16), new_data);
    _arr_store(arr + cast<usize>(8), cast<usize>(new_cap));
}

// hylian_array_push: append one element.
void hylian_array_push(usize arr, usize value) {
    if (arr == cast<usize>(0)) { return; }
    usize len = _arr_load(arr);
    _arr_grow(arr, cast<int>(len) + 1);

    usize data = _arr_load(arr + cast<usize>(16));
    _arr_store(data + len * cast<usize>(ARRAY_ELEM_SIZE), value);
    _arr_store(arr, len + cast<usize>(1));
}

// hylian_array_pop: remove and return the last element, or 0 if empty.
usize hylian_array_pop(usize arr) {
    if (arr == cast<usize>(0)) { return cast<usize>(0); }
    usize len = _arr_load(arr);
    if (len == cast<usize>(0)) { return cast<usize>(0); }

    usize new_len = len - cast<usize>(1);
    usize data = _arr_load(arr + cast<usize>(16));
    usize value = _arr_load(data + new_len * cast<usize>(ARRAY_ELEM_SIZE));
    _arr_store(arr, new_len);
    return value;
}

// hylian_array_get: element at `idx`, or 0 if out of range.
//
// Returning 0 rather than reading past the end is deliberate: Hylian has no
// exceptions and no panic-on-index yet, so the alternative to a bounds check
// here is an out-of-bounds read that corrupts or crashes somewhere unrelated.
usize hylian_array_get(usize arr, int idx) {
    if (arr == cast<usize>(0)) { return cast<usize>(0); }
    if (idx < 0) { return cast<usize>(0); }
    usize len = _arr_load(arr);
    if (idx >= cast<int>(len)) { return cast<usize>(0); }

    usize data = _arr_load(arr + cast<usize>(16));
    return _arr_load(data + cast<usize>(idx * ARRAY_ELEM_SIZE));
}

// hylian_array_set: overwrite the element at `idx`. Out-of-range writes are
// ignored rather than corrupting memory, for the same reason as get.
void hylian_array_set(usize arr, int idx, usize value) {
    if (arr == cast<usize>(0)) { return; }
    if (idx < 0) { return; }
    usize len = _arr_load(arr);
    if (idx >= cast<int>(len)) { return; }

    usize data = _arr_load(arr + cast<usize>(16));
    _arr_store(data + cast<usize>(idx * ARRAY_ELEM_SIZE), value);
}

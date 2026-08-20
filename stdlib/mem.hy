// mem — arena allocator backing `new` expressions.
//
// Every non-naked function gets a hidden `__arena__` local (a single
// pointer-sized slot) that the compiler threads through arena_init /
// arena_alloc / arena_free automatically (see compiler/lower.c) — user
// code never calls these directly. The slot just holds the address of
// the *current* arena block; arena_alloc bump-allocates from it and,
// when a block fills up, grabs a new (larger) block and chains it.
//
// These three functions must be `naked`: a non-naked function gets its
// own arena via a call to arena_init, so if arena_init itself weren't
// naked, calling it would try to set up an arena for itself first —
// infinite recursion before anything is initialized.
//
// This replaces the old C runtime/std/mem.c now that arena_alloc's
// calling convention (arena pointer + size, both args) is fixed in
// compiler/ir_to_mir.c — previously the pointer arg was silently dropped.
//
// Deliberately does NOT `include platform.linux_x86_64` and share its
// raw_alloc/raw_free: this file is compiled standalone and always linked
// into every program (see linkle.py's __always__ handling), same as
// user code, which independently includes platform.linux_x86_64 too —
// two separate translation units both defining raw_alloc/raw_free would
// collide at link time ("multiple definition"). So this has its own
// tiny private mmap/munmap wrapper instead, with the Linux/x86-64
// syscall numbers inlined directly (9 = mmap, 11 = munmap) — the one
// deliberate exception to "no raw numbers outside platform files",
// justified by needing to stay a self-contained translation unit.

// Block layout (raw bytes — no struct type, since `new`/classes are exactly
// what this file has to work without):
//   [0..8)   next block pointer (0 = none, oldest-to-newest linked list)
//   [8..16)  bytes used so far in this block's data area
//   [16..24) capacity of this block's data area
//   [24..)   data
static int ARENA_HEADER_SIZE = 24;
static int ARENA_BLOCK_SIZE  = 65536;

// Backing arena for `multi` values (see hylian_multi_alloc at the bottom of
// this file). It lives here rather than in its own always-linked module so it
// can reuse the block machinery above instead of duplicating it.
static usize MULTI_ARENA = 0;

usize _mem_raw_alloc(int size) {
    return cast<usize>(syscall(9, 0, size, 3, 34, -1, 0));
}

void _mem_raw_free(usize ptr, int size) {
    syscall(11, cast<int>(ptr), size, 0);
}

usize _arena_new_block(usize min_size) {
    usize sz = min_size;
    if (sz < cast<usize>(ARENA_BLOCK_SIZE)) {
        sz = cast<usize>(ARENA_BLOCK_SIZE);
    }
    usize total = sz + cast<usize>(ARENA_HEADER_SIZE);
    usize block = _mem_raw_alloc(cast<int>(total));

    unsafe {
        *cast<*usize>(block) = cast<usize>(0);
        *cast<*usize>(block + cast<usize>(8)) = cast<usize>(0);
        *cast<*usize>(block + cast<usize>(16)) = sz;
    }
    return block;
}

void arena_init(usize slot) {
    usize block = _arena_new_block(cast<usize>(ARENA_BLOCK_SIZE));
    unsafe {
        *cast<*usize>(slot) = block;
    }
}

usize arena_alloc(usize slot, usize size) {
    usize aligned = ((size + cast<usize>(7)) / cast<usize>(8)) * cast<usize>(8);

    usize block;
    unsafe {
        block = *cast<*usize>(slot);
    }

    usize used;
    usize cap;
    unsafe {
        used = *cast<*usize>(block + cast<usize>(8));
        cap  = *cast<*usize>(block + cast<usize>(16));
    }

    if (used + aligned > cap) {
        usize new_block = _arena_new_block(aligned);
        unsafe {
            *cast<*usize>(new_block) = block;
        }
        block = new_block;
        used = cast<usize>(0);
        unsafe {
            *cast<*usize>(slot) = block;
        }
    }

    usize ptr = block + cast<usize>(ARENA_HEADER_SIZE) + used;
    usize new_used = used + aligned;
    unsafe {
        *cast<*usize>(block + cast<usize>(8)) = new_used;
    }
    return ptr;
}

// hylian_multi_alloc: box one `multi<A | B | ...>` value.
//
// Called directly by the compiler, never by user code: a `multi` variable
// declaration lowers to IR_MULTI_ALLOC, which becomes a call to exactly this
// name (see compiler/ir_to_mir.c). Until this existed there was no definition
// of it anywhere in the tree, so every program that declared a multi failed at
// link time with "undefined reference to hylian_multi_alloc" — the feature
// parsed, typechecked and generated code, and then couldn't be linked.
//
// The layout is two 8-byte words and is fixed by the compiler side:
//   [0..8)   tag   — index into the multi's declared type list
//   [8..16)  value — the payload, or a pointer to it for non-scalars
// IR_MULTI_TAG and IR_MULTI_VALUE load from exactly those two offsets, so this
// layout can't be changed on one side alone.
//
// Storage comes from a private, process-lifetime arena rather than the
// caller's per-function one: a multi routinely outlives the function that
// created it (that's the point of returning one), and the caller's arena is
// freed on return. Nothing frees MULTI_ARENA — multis leak by design for now,
// which is a real limitation but a predictable one, and far better than the
// use-after-free that arena-allocating them would produce.
usize hylian_multi_alloc(usize tag, usize value) {
    usize slot = cast<usize>(&MULTI_ARENA);
    if (MULTI_ARENA == cast<usize>(0)) {
        arena_init(slot);
    }

    usize p = arena_alloc(slot, cast<usize>(16));
    unsafe {
        *cast<*usize>(p) = tag;
        *cast<*usize>(p + cast<usize>(8)) = value;
    }
    return p;
}

// ── Error values ─────────────────────────────────────────────────────────────
//
// `Err("boom")` lowers to a call to hylian_make_err, and `e.message()` to a
// call to Error_message. Neither had a definition anywhere, so ANY program
// using the Error type failed to link — which made the language's whole error
// story unusable despite `Error?` return types parsing and typechecking fine.
//
// An Error is deliberately just one word: a pointer to its NUL-terminated
// message. `nil` (a null pointer) is the no-error value, which is why
// `if (e != nil)` is the idiomatic check and why no tag byte is needed.
//
// Allocated from the same process-lifetime arena as multi values: an Error is
// routinely returned upwards out of the function that created it, so it cannot
// live in that function's own arena, which is freed on return.
usize hylian_make_err(usize msg_ptr) {
    usize slot = cast<usize>(&MULTI_ARENA);
    if (MULTI_ARENA == cast<usize>(0)) {
        arena_init(slot);
    }
    usize e = arena_alloc(slot, cast<usize>(8));
    unsafe {
        *cast<*usize>(e) = msg_ptr;
    }
    return e;
}

// Error_message: the message an Error carries. Returns nil for a nil Error
// rather than dereferencing it, so `e.message()` on a success value is safe.
usize Error_message(usize err) {
    if (err == cast<usize>(0)) { return cast<usize>(0); }
    usize msg;
    unsafe {
        msg = *cast<*usize>(err);
    }
    return msg;
}

void arena_free(usize slot) {
    usize block;
    unsafe {
        block = *cast<*usize>(slot);
    }

    while (block != cast<usize>(0)) {
        usize next;
        usize cap;
        unsafe {
            next = *cast<*usize>(block);
            cap  = *cast<*usize>(block + cast<usize>(16));
        }
        _mem_raw_free(block, cast<int>(cap + cast<usize>(ARENA_HEADER_SIZE)));
        block = next;
    }

    unsafe {
        *cast<*usize>(slot) = cast<usize>(0);
    }
}

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

naked usize _mem_raw_alloc(int size) {
    return cast<usize>(syscall(9, 0, size, 3, 34, -1, 0));
}

naked void _mem_raw_free(usize ptr, int size) {
    syscall(11, cast<int>(ptr), size, 0);
}

naked usize _arena_new_block(usize min_size) {
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

naked void arena_init(usize slot) {
    usize block = _arena_new_block(cast<usize>(ARENA_BLOCK_SIZE));
    unsafe {
        *cast<*usize>(slot) = block;
    }
}

naked usize arena_alloc(usize slot, usize size) {
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

naked void arena_free(usize slot) {
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

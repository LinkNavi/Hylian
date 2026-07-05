include {
    std.sys,
}

// ── simple bump allocator ─────────────────────────────────────────────────────
// Used when no arena is available. Falls back to mmap per-block.
// For arena-based allocation, use the arena_* functions (still in C via mem.hyi).

static usize _heap_ptr  = 0;
static usize _heap_end  = 0;
static int   _BLOCK     = 65536;

str hy_alloc(int size) {
    // align to 8
    size = (size + 7) & ~7;
    if (_heap_ptr + cast<usize>(size) > _heap_end) {
        int block = size > _BLOCK ? size : _BLOCK;
        usize new_region = sys_mmap(block);
        _heap_ptr = new_region;
        _heap_end = new_region + cast<usize>(block);
    }
    usize p = _heap_ptr;
    _heap_ptr += cast<usize>(size);
    return cast<str>(p);
}

void hy_free(str ptr, int size) {
    // bump allocator — no individual frees; munmap only for large blocks
    if (size >= _BLOCK) {
        sys_munmap(cast<int>(ptr), size);
    }
}

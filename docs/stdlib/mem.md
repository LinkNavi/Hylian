# `mem`

Source: `stdlib/mem.hy`. **Always linked** into every compiled program — you don't
`include { mem, }` yourself; the compiler unconditionally emits calls to the functions
below for every `new` expression.

This is the arena allocator backing `new`. Every non-`naked` function gets a hidden
`__arena__` local (a single pointer-sized slot) that the compiler threads through
`arena_init`/`arena_alloc`/`arena_free` automatically (`compiler/lower.c`) — user code
never calls these directly.

| Function | Signature | Description |
|---|---|---|
| `arena_init` | `naked void arena_init(usize slot)` | Allocates the arena's first block and stores it in `slot`. |
| `arena_alloc` | `naked usize arena_alloc(usize slot, usize size)` | Bump-allocates `size` bytes (8-byte aligned) from the arena at `slot`, grabbing a new, chained block if the current one is full. |
| `arena_free` | `naked void arena_free(usize slot)` | Frees every block in the arena's chain. |

All three are `naked`: a non-`naked` function gets its own arena via a call to
`arena_init`, so if `arena_init` itself weren't `naked`, calling it would try to set up
an arena for itself first — infinite recursion before anything is initialized.

Each arena block is a raw byte layout with no struct type (deliberately — `new` and
classes are exactly what this file has to work without):

```
[0..8)   next block pointer (0 = none; oldest-to-newest linked list)
[8..16)  bytes used so far in this block's data area
[16..24) capacity of this block's data area
[24..)   data
```

Blocks are allocated in units of at least 65536 bytes (`ARENA_BLOCK_SIZE`), or larger
if a single allocation needs more than that. `mem.hy` deliberately does not
`include platform.linux_x86_64` and share its `raw_alloc`/`raw_free`: it's compiled
standalone and always linked into every program, same as user code (which
independently includes `platform.linux_x86_64` too) — two translation units both
defining `raw_alloc`/`raw_free` would collide at link time. Instead it has its own
private `mmap`/`munmap` wrapper with the Linux/x86-64 syscall numbers inlined directly
(`9` = `mmap`, `11` = `munmap`) — the one deliberate exception to "no raw syscall
numbers outside `platform.*`" in this codebase.

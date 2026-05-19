#include "../platform/platform.h"

#define ARENA_BLOCK_SIZE (64 * 1024)  /* 64 KB default block */

typedef unsigned long arena_size_t;

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    arena_size_t used;
    arena_size_t cap;
} ArenaBlock;

typedef struct {
    ArenaBlock *head;
} Arena;

static ArenaBlock *block_new(arena_size_t min_size) {
    arena_size_t sz = min_size > ARENA_BLOCK_SIZE ? min_size : ARENA_BLOCK_SIZE;
    ArenaBlock *b = (ArenaBlock *)hy_alloc(sizeof(ArenaBlock) + sz);
    if (!b) return (ArenaBlock *)0;
    b->next = (ArenaBlock *)0;
    b->used = 0;
    b->cap  = sz;
    return b;
}

void arena_init(Arena *a) {
    if (!a) return;
    a->head = block_new(ARENA_BLOCK_SIZE);
}

void *arena_alloc(Arena *a, arena_size_t size) {
    if (!a || !a->head) return (void *)0;
    /* align to 8 bytes */
    size = (size + 7) & ~(arena_size_t)7;
    if (a->head->used + size > a->head->cap) {
        ArenaBlock *b = block_new(size);
        if (!b) return (void *)0;
        b->next = a->head;
        a->head = b;
    }
    void *p = (char *)(a->head + 1) + a->head->used;
    a->head->used += size;
    return p;
}

void arena_free(Arena *a) {
    if (!a) return;
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        hy_free(b, sizeof(ArenaBlock) + b->cap);
        b = next;
    }
    a->head = (ArenaBlock *)0;
}

/* Heap-allocated Arena — for explicit arena passing across function boundaries */
Arena *arena_new(void) {
    Arena *a = (Arena *)hy_alloc(sizeof(Arena));
    if (!a) return (Arena *)0;
    arena_init(a);
    return a;
}

void arena_delete(Arena *a) {
    if (!a) return;
    arena_free(a);
    hy_free(a, sizeof(Arena));
}
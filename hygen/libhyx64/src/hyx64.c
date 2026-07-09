#include "hyx64.h"
#include <stdlib.h>
#include <string.h>

/* ---- buffer ---- */

void x64_buf_init(X64Buf *b) {
    b->cap = 256;
    b->data = malloc(b->cap);
    b->len = 0;
}

void x64_buf_free(X64Buf *b) {
    free(b->data);
    b->data = NULL; b->len = b->cap = 0;
}

static void ensure(X64Buf *b, size_t extra) {
    if (b->len + extra > b->cap) {
        while (b->len + extra > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
}

void x64_buf_push(X64Buf *b, uint8_t byte) {
    ensure(b, 1);
    b->data[b->len++] = byte;
}

void x64_buf_write(X64Buf *b, const void *data, size_t n) {
    ensure(b, n);
    memcpy(b->data + b->len, data, n);
    b->len += n;
}

void x64_buf_write32(X64Buf *b, uint32_t v) { x64_buf_write(b, &v, 4); }
void x64_buf_write64(X64Buf *b, uint64_t v) { x64_buf_write(b, &v, 8); }

/* ---- relocations ---- */

void x64_reloc_list_init(X64RelocList *r) {
    r->cap = 8; r->count = 0;
    r->relocs = malloc(r->cap * sizeof(X64Reloc));
}

void x64_reloc_list_free(X64RelocList *r) {
    free(r->relocs); r->relocs = NULL; r->count = r->cap = 0;
}

void x64_reloc_add(X64RelocList *r, size_t offset, const char *symbol, X64RelocKind kind) {
    if (r->count == r->cap) {
        r->cap *= 2;
        r->relocs = realloc(r->relocs, r->cap * sizeof(X64Reloc));
    }
    r->relocs[r->count].offset = offset;
    r->relocs[r->count].symbol = symbol;
    r->relocs[r->count].kind = kind;
    r->count++;
}

/* ---- register pool: order matters (roughly caller-saved-first so regalloc's
   "lowest free id wins" preference lines up with cheaper calling-convention
   behavior; not load-bearing for correctness either way) ---- */
const X64Reg x64_int_reg_pool[X64_NUM_ALLOCATABLE_INT_REGS] = {
    X64_RBX, X64_RSI, X64_RDI,
    X64_R8, X64_R9, X64_R10, X64_R12, X64_R13, X64_R14, X64_R15,
};

const int x64_float_reg_pool[X64_NUM_ALLOCATABLE_FLOAT_REGS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
};

const X64Reg x64_sysv_arg_regs[6] = {
    X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9,
};

const X64Reg x64_syscall_arg_regs[6] = {
    X64_RDI, X64_RSI, X64_RDX, X64_R10, X64_R8, X64_R9,
};

#ifndef HYX64_H
#define HYX64_H

#include "hymir.h"
#include "hyregalloc.h"
#include <stdint.h>
#include <stddef.h>

/* ---- growable byte buffer ---- */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} X64Buf;

void x64_buf_init(X64Buf *b);
void x64_buf_free(X64Buf *b);
void x64_buf_push(X64Buf *b, uint8_t byte);
void x64_buf_write(X64Buf *b, const void *data, size_t n);
void x64_buf_write32(X64Buf *b, uint32_t v);
void x64_buf_write64(X64Buf *b, uint64_t v);

/* ---- relocations: patched by libhyobj once section addresses are known ---- */
typedef enum {
    X64_RELOC_REL32,  /* call/jmp to external symbol, PC-relative */
    X64_RELOC_RIP32,  /* lea reg, [rip+sym] - PC-relative to data/rodata */
} X64RelocKind;

typedef struct {
    size_t       offset;   /* byte offset in the code buffer to patch */
    const char  *symbol;
    X64RelocKind kind;
} X64Reloc;

typedef struct {
    X64Reloc *relocs;
    int       count, cap;
} X64RelocList;

void x64_reloc_list_init(X64RelocList *r);
void x64_reloc_list_free(X64RelocList *r);
void x64_reloc_add(X64RelocList *r, size_t offset, const char *symbol, X64RelocKind kind);

/* ---- physical GPRs. target owns this mapping - regalloc only ever sees
   0..num_int_regs-1; this is where those ids become real machine registers.
   RSP/RBP reserved for the frame, not included in the allocatable set. */
/* RAX/RDX/RCX/R11 are reserved as fixed scratch registers, never handed to
   regalloc: RAX/RDX for mul/div results and general scratch, RCX because
   shift counts must live in CL, R11 as a second scratch for two-operand
   ops. Keeping them out of the allocatable pool means operand materialization
   can always use fixed registers without risking clobbering a live vreg. */
/* Values MUST match the real x86-64 ISA register encoding numbers exactly -
   x64_rex()/x64_modrm() do raw bit arithmetic (reg & 7, reg >> 3) on these,
   they aren't just convenient IDs. */
typedef enum {
    X64_RAX = 0, X64_RCX = 1, X64_RDX = 2, X64_RBX = 3,
    X64_RSP = 4, X64_RBP = 5, X64_RSI = 6, X64_RDI = 7,
    X64_R8 = 8, X64_R9 = 9, X64_R10 = 10, X64_R11 = 11,
    X64_R12 = 12, X64_R13 = 13, X64_R14 = 14, X64_R15 = 15,
} X64Reg;

#define X64_NUM_ALLOCATABLE_INT_REGS 10
extern const X64Reg x64_int_reg_pool[X64_NUM_ALLOCATABLE_INT_REGS];

/* xmm0-13 allocatable, xmm14/xmm15 reserved as fixed scratch for operand
   materialization - same role RAX/RDX/RCX/R11 play for the int side. No
   ISA constraints on xmm registers (no CL-style fixed-position requirement
   like shifts have for GPRs), so this is a plain identity mapping. */
#define X64_NUM_ALLOCATABLE_FLOAT_REGS 14
extern const int x64_float_reg_pool[X64_NUM_ALLOCATABLE_FLOAT_REGS];
#define X64_XMM_SCRATCH_A 14
#define X64_XMM_SCRATCH_B 15

/* SysV x86-64 integer argument registers, in order */
extern const X64Reg x64_sysv_arg_regs[6];
#define X64_SYSV_ARG_REG_COUNT 6

/* Linux syscall ABI argument registers - NOT the same as the SysV call ABI:
   4th arg is r10, not rcx (rcx gets clobbered by the `syscall` instruction
   itself to hold the return address for sysret) */
extern const X64Reg x64_syscall_arg_regs[6];
#define X64_SYSCALL_ARG_REG_COUNT 6

/* ---- top-level: lower one MIR function (post-regalloc) straight to bytes.
   Frame size (locals + int spills + float spills, 16-byte aligned) is
   computed internally from fn->local_count and ra's spill counts - the
   caller no longer supplies it, removing a footgun where a wrong external
   frame_size could silently corrupt local variables or spilled values. */
void x64_lower_func(const MIRFunc *fn, const RegAllocResult *ra,
                    X64Buf *out_code, X64RelocList *out_relocs);

#endif

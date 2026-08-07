#ifndef HYREGALLOC_H
#define HYREGALLOC_H

#include "hymir.h"

/* Target owns the register set - regalloc knows nothing about x86 specifically.
   num_int_regs/num_float_regs are just "how many usable slots", the target
   (libhyx64) maps ids 0..num_int_regs-1 to actual physical registers. */
/* num_callee_saved_int_regs / num_callee_saved_float_regs: how many of the
   *first* ids in each class survive a call. The target must order its pool so
   the callee-saved registers come first (ids 0..n-1). A vreg whose live range
   spans a call instruction can only be given one of those; anything else is
   spilled instead, because the call would otherwise destroy it. Set to 0 for
   a class with no callee-saved registers (SysV has no callee-saved xmm). */
typedef struct {
    int num_int_regs;
    int num_float_regs;
    int num_callee_saved_int_regs;
    int num_callee_saved_float_regs;
} RegAllocTarget;

typedef enum { MCLOC_REG, MCLOC_SPILL } MCLocKind;

typedef struct {
    MCLocKind kind;
    int is_float;    /* which class this vreg belongs to */
    union {
        int reg_id;      /* class-relative index, target maps to physical reg */
        int spill_slot;  /* class-relative stack slot index */
    };
} MCLoc;

typedef struct {
    MCLoc *vreg_locs;         /* indexed by vreg id, size = vreg_count */
    int    vreg_count;
    int    num_int_spill_slots;
    int    num_float_spill_slots;
    /* Highest callee-saved reg id actually handed out in each class, +1 (0 if
       none). The target uses this to save/restore exactly the registers it
       has to in the prologue/epilogue, rather than all of them. */
    int    callee_saved_int_used;
    int    callee_saved_float_used;
} RegAllocResult;

RegAllocResult regalloc_run(const MIRFunc *fn, const RegAllocTarget *target);
void regalloc_result_free(RegAllocResult *res);

#endif

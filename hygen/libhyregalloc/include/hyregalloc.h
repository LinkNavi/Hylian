#ifndef HYREGALLOC_H
#define HYREGALLOC_H

#include "hymir.h"

/* Target owns the register set - regalloc knows nothing about x86 specifically.
   num_int_regs/num_float_regs are just "how many usable slots", the target
   (libhyx64) maps ids 0..num_int_regs-1 to actual physical registers. */
typedef struct {
    int num_int_regs;
    int num_float_regs;
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
} RegAllocResult;

RegAllocResult regalloc_run(const MIRFunc *fn, const RegAllocTarget *target);
void regalloc_result_free(RegAllocResult *res);

#endif

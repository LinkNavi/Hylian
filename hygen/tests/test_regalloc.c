#include "hymir.h"
#include "hyregalloc.h"
#include <stdio.h>
#include <assert.h>

/* builds:
   sum = 0
   i = 0
   L0:
     t = i < 10
     jmp_unless t, L1
     sum = sum + i
     i = i + 1
     jmp L0
   L1:
     ret sum

   sum(v0) and i(v1) are both live across the loop back-edge (L0 -> L0),
   which only a real dataflow liveness pass (not a linear instruction scan)
   gets right. Then we force a spill by giving the target only 1 int reg.
*/
int main(void) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = mir_func_new(mod, "loop_sum");

    int v_sum = mir_new_vreg(fn);
    int v_i   = mir_new_vreg(fn);
    int v_t   = mir_new_vreg(fn);

    MIRInstr *m;

    m = mir_emit(fn, MIR_MOV);
    m->dest = mir_vreg(v_sum, MIR_I64); m->src1 = mir_imm_int(0, MIR_I64);

    m = mir_emit(fn, MIR_MOV);
    m->dest = mir_vreg(v_i, MIR_I64); m->src1 = mir_imm_int(0, MIR_I64);

    m = mir_emit(fn, MIR_LABEL);
    m->dest = mir_label(0);

    m = mir_emit(fn, MIR_CMP_LT);
    m->dest = mir_vreg(v_t, MIR_I64);
    m->src1 = mir_vreg(v_i, MIR_I64);
    m->src2 = mir_imm_int(10, MIR_I64);

    m = mir_emit(fn, MIR_JMP_UNLESS);
    m->src1 = mir_vreg(v_t, MIR_I64);
    m->dest = mir_label(1);

    m = mir_emit(fn, MIR_ADD);
    m->dest = mir_vreg(v_sum, MIR_I64);
    m->src1 = mir_vreg(v_sum, MIR_I64);
    m->src2 = mir_vreg(v_i, MIR_I64);

    m = mir_emit(fn, MIR_ADD);
    m->dest = mir_vreg(v_i, MIR_I64);
    m->src1 = mir_vreg(v_i, MIR_I64);
    m->src2 = mir_imm_int(1, MIR_I64);

    m = mir_emit(fn, MIR_JMP);
    m->dest = mir_label(0);

    m = mir_emit(fn, MIR_LABEL);
    m->dest = mir_label(1);

    m = mir_emit(fn, MIR_RET);
    m->src1 = mir_vreg(v_sum, MIR_I64);

    MIRVerifyResult vr = mir_verify(mod);
    if (!vr.ok) { fprintf(stderr, "verify failed: %s\n", vr.msg); assert(0); }

    /* only 1 int register available -> sum and i can't both live in registers
       across the loop body, so one of them MUST spill */
    RegAllocTarget target = { .num_int_regs = 1, .num_float_regs = 4 };
    RegAllocResult res = regalloc_run(fn, &target);

    printf("vreg locations:\n");
    for (int v = 0; v < res.vreg_count; v++) {
        MCLoc *l = &res.vreg_locs[v];
        if (l->kind == MCLOC_REG)
            printf("  v%d -> reg%d (%s)\n", v, l->reg_id, l->is_float ? "xmm" : "gpr");
        else
            printf("  v%d -> spill slot %d (%s)\n", v, l->spill_slot, l->is_float ? "xmm" : "gpr");
    }
    printf("int spill slots used: %d\n", res.num_int_spill_slots);

    assert(res.num_int_spill_slots >= 1 && "expected at least one spill with only 1 register");

    /* with 4 registers there should be no spilling at all for this tiny function */
    RegAllocTarget roomy = { .num_int_regs = 4, .num_float_regs = 4 };
    RegAllocResult res2 = regalloc_run(fn, &roomy);
    assert(res2.num_int_spill_slots == 0 && "shouldn't need to spill with plenty of registers");

    regalloc_result_free(&res);
    regalloc_result_free(&res2);
    mir_module_free(mod);
    printf("\nOK\n");
    return 0;
}

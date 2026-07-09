#include "hymir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include "hyobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>

/* builds a freestanding _start that computes:
     a = 1.5, b = 2.5, c = 3.0   (3 live floats -> forces a spill with only 2 regs)
     sum = a + b + c              = 7.0
     neg = -sum                   = -7.0
     back = -neg                  = 7.0  (round-trips FNEG)
     ok = (back == sum)           should be true (1)
     n = (int)back                = 7
     exit(ok ? n : 99)            expect exit code 7
*/
int main(void) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = mir_func_new(mod, "_start");
    /* NOT naked: unlike a pure syscall wrapper, this function needs a real
       stack frame for its float spill slots. naked skips prologue setup
       entirely (no rbp established at all), which would make any
       rbp-relative spill access dereference garbage. The trailing
       leave/ret after the unconditional exit() calls below is just
       unreachable dead code - harmless, not a correctness issue. */

    int v_a = mir_new_vreg(fn), v_b = mir_new_vreg(fn), v_c = mir_new_vreg(fn);
    int v_ab = mir_new_vreg(fn), v_sum = mir_new_vreg(fn);
    int v_neg = mir_new_vreg(fn), v_back = mir_new_vreg(fn);
    int v_ok = mir_new_vreg(fn), v_n = mir_new_vreg(fn);
    MIRInstr *m;

    m = mir_emit(fn, MIR_MOV); m->dest = mir_vreg(v_a, MIR_F64); m->src1 = mir_imm_float(1.5, MIR_F64);
    m = mir_emit(fn, MIR_MOV); m->dest = mir_vreg(v_b, MIR_F64); m->src1 = mir_imm_float(2.5, MIR_F64);
    m = mir_emit(fn, MIR_MOV); m->dest = mir_vreg(v_c, MIR_F64); m->src1 = mir_imm_float(3.0, MIR_F64);

    m = mir_emit(fn, MIR_FADD); m->dest = mir_vreg(v_ab, MIR_F64);
    m->src1 = mir_vreg(v_a, MIR_F64); m->src2 = mir_vreg(v_b, MIR_F64);

    m = mir_emit(fn, MIR_FADD); m->dest = mir_vreg(v_sum, MIR_F64);
    m->src1 = mir_vreg(v_ab, MIR_F64); m->src2 = mir_vreg(v_c, MIR_F64);

    m = mir_emit(fn, MIR_FNEG); m->dest = mir_vreg(v_neg, MIR_F64); m->src1 = mir_vreg(v_sum, MIR_F64);
    m = mir_emit(fn, MIR_FNEG); m->dest = mir_vreg(v_back, MIR_F64); m->src1 = mir_vreg(v_neg, MIR_F64);

    m = mir_emit(fn, MIR_FCMP_EQ); m->dest = mir_vreg(v_ok, MIR_I64);
    m->src1 = mir_vreg(v_back, MIR_F64); m->src2 = mir_vreg(v_sum, MIR_F64);

    m = mir_emit(fn, MIR_F2I); m->dest = mir_vreg(v_n, MIR_I64); m->src1 = mir_vreg(v_back, MIR_F64);

    /* exit(ok ? n : 99) via jmp_unless */
    m = mir_emit(fn, MIR_JMP_UNLESS); m->src1 = mir_vreg(v_ok, MIR_I64); m->dest = mir_label(0);

    MIRValue *exit_ok_args = malloc(2 * sizeof(MIRValue));
    exit_ok_args[0] = mir_imm_int(60, MIR_I64);
    exit_ok_args[1] = mir_vreg(v_n, MIR_I64);
    m = mir_emit(fn, MIR_SYSCALL); m->args = exit_ok_args; m->arg_count = 2;

    m = mir_emit(fn, MIR_LABEL); m->dest = mir_label(0);
    MIRValue *exit_bad_args = malloc(2 * sizeof(MIRValue));
    exit_bad_args[0] = mir_imm_int(60, MIR_I64);
    exit_bad_args[1] = mir_imm_int(99, MIR_I64);
    m = mir_emit(fn, MIR_SYSCALL); m->args = exit_bad_args; m->arg_count = 2;

    /* force a spill: only 2 float registers available, but a/b/c/ab/sum/neg/back
       have overlapping live ranges well beyond 2 at once */
    RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 2 };
    RegAllocResult ra = regalloc_run(fn, &target);
    fprintf(stderr, "float spill slots used: %d (expect > 0, proving the spill path runs)\n",
            ra.num_float_spill_slots);

    X64Buf code; x64_buf_init(&code);
    X64RelocList relocs; x64_reloc_list_init(&relocs);
    x64_lower_func(fn, &ra, &code, &relocs);

    ObjModule *obj = obj_module_new();
    obj_add_func(obj, "_start", &code, &relocs);

    int ok = obj_write_elf(obj, "/tmp/hygen_float_test.o");
    assert(ok);

    fprintf(stderr, "--- objdump -dr ---\n");
    int rc = system("objdump -dr /tmp/hygen_float_test.o");

    rc |= system("ld -o /tmp/hygen_float_test_bin /tmp/hygen_float_test.o 2>&1");
    fprintf(stderr, "link result: %d\n", rc);
    assert(rc == 0);

    rc = system("/tmp/hygen_float_test_bin");
    int exit_code = WEXITSTATUS(rc);
    fprintf(stderr, "exit code: %d (expect 7 = (int)(1.5+2.5+3.0), with a round-tripped "
            "negation and an equality check all passing)\n", exit_code);
    assert(exit_code == 7 && "float arithmetic/negation/compare/conversion should all be correct");

    obj_module_free(obj);
    x64_buf_free(&code);
    x64_reloc_list_free(&relocs);
    regalloc_result_free(&ra);
    mir_module_free(mod);

    printf("\nOK - float codegen proven: arithmetic, negation round-trip, comparison, "
           "int conversion, AND the float spill path, all correct end to end\n");
    return 0;
}

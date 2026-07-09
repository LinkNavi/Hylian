#include "ir.h"
#include "ir_to_mir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include "hyobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>

/* builds:
     int fact(int n) {
         if (n <= 1) return 1;
         return n * fact(n - 1);
     }
     _start (naked): syscall(60 [exit], fact(5))   // exit code should be 120

   fact(5) forces 5 nested live frames of the local "n" simultaneously (each
   call is still mid-multiplication, waiting on its recursive call's result,
   when the next level starts) - if locals aliased onto a shared global
   symbol (the old design) instead of real per-call-frame storage, every
   recursive call would stomp the same storage and this would NOT produce
   120. This is about as strong a proof of real stack frames as a test gets. */
int main(void) {
    IRModule *ir = ir_module_new();

    /* ---- fact(n) ---- */
    IRInstr *fb = ir_emit(ir, IR_FUNC_BEGIN);
    fb->str_extra = "fact";
    static IRParam params[1];
    params[0].name = "n";
    params[0].type_name = "int";
    fb->params = params;
    fb->param_count = 1;

    /* t0 = load n; t1 = (t0 <= 1) */
    IRInstr *ld_n1 = ir_emit(ir, IR_LOAD_VAR);
    ld_n1->str_extra = "n"; ld_n1->dest = irop_temp(0);

    IRInstr *cmp = ir_emit(ir, IR_LE);
    cmp->src1 = irop_temp(0); cmp->src2 = irop_const_int(1); cmp->dest = irop_temp(1);

    /* if !(n<=1) jump to L_recurse */
    IRInstr *jmp_unless = ir_emit(ir, IR_JUMP_UNLESS);
    jmp_unless->src1 = irop_temp(1); jmp_unless->src2 = irop_label(0);

    /* base case: return 1 */
    IRInstr *ret1 = ir_emit(ir, IR_RETURN);
    ret1->src1 = irop_const_int(1);

    /* L_recurse: */
    IRInstr *lbl = ir_emit(ir, IR_LABEL);
    lbl->dest = irop_label(0);

    /* t2 = load n; t3 = n - 1 */
    IRInstr *ld_n2 = ir_emit(ir, IR_LOAD_VAR);
    ld_n2->str_extra = "n"; ld_n2->dest = irop_temp(2);
    IRInstr *sub = ir_emit(ir, IR_SUB);
    sub->src1 = irop_temp(2); sub->src2 = irop_const_int(1); sub->dest = irop_temp(3);

    /* t4 = fact(t3) */
    IRInstr *call = ir_emit(ir, IR_CALL);
    call->str_extra = "fact";
    static IROperand call_args[1];
    call_args[0] = irop_temp(3);
    call->args = call_args; call->arg_count = 1;
    call->dest = irop_temp(4);

    /* t5 = load n (again); t6 = t5 * t4; return t6 */
    IRInstr *ld_n3 = ir_emit(ir, IR_LOAD_VAR);
    ld_n3->str_extra = "n"; ld_n3->dest = irop_temp(5);
    IRInstr *mul = ir_emit(ir, IR_MUL);
    mul->src1 = irop_temp(5); mul->src2 = irop_temp(4); mul->dest = irop_temp(6);
    IRInstr *ret2 = ir_emit(ir, IR_RETURN);
    ret2->src1 = irop_temp(6);

    ir_emit(ir, IR_FUNC_END);

    /* ---- _start ---- */
    IRInstr *fb2 = ir_emit(ir, IR_FUNC_BEGIN);
    fb2->str_extra = "_start"; fb2->extra_int = 1;

    IRInstr *call_fact = ir_emit(ir, IR_CALL);
    call_fact->str_extra = "fact";
    static IROperand fact_args[1];
    fact_args[0] = irop_const_int(5);
    call_fact->args = fact_args; call_fact->arg_count = 1;
    call_fact->dest = irop_temp(10);

    IRInstr *ex = ir_emit(ir, IR_CALL);
    ex->str_extra = "syscall";
    static IROperand ex_args[2];
    ex_args[0] = irop_const_int(60);
    ex_args[1] = irop_temp(10);
    ex->args = ex_args; ex->arg_count = 2;
    ex->dest = irop_temp(11);

    ir_emit(ir, IR_FUNC_END);

    MIRModule *mir = lower_ir_to_mir(ir);
    printf("=== lowered MIR ===\n");
    mir_dump(mir, stdout);

    for (int fi = 0; fi < mir->func_count; fi++)
        if (strcmp(mir->funcs[fi]->name, "_start") == 0) mir->funcs[fi]->is_naked = 1;

    MIRVerifyResult vr = mir_verify(mir);
    if (!vr.ok) { fprintf(stderr, "verify failed: %s\n", vr.msg); assert(0); }

    for (int fi = 0; fi < mir->func_count; fi++) {
        MIRFunc *fn = mir->funcs[fi];
        printf("func %s: local_count=%d param_count=%d\n", fn->name, fn->local_count, fn->param_count);

        RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 4 };
        RegAllocResult ra = regalloc_run(fn, &target);

        X64Buf code; x64_buf_init(&code);
        X64RelocList relocs; x64_reloc_list_init(&relocs);
        x64_lower_func(fn, &ra, &code, &relocs);

        static ObjModule *obj = NULL;
        static int obj_init = 0;
        if (!obj_init) { obj = obj_module_new(); obj_init = 1; }
        obj_add_func(obj, fn->name, &code, &relocs);

        x64_buf_free(&code);
        x64_reloc_list_free(&relocs);
        regalloc_result_free(&ra);

        if (fi == mir->func_count - 1) {
            int ok = obj_write_elf(obj, "/tmp/hygen_locals_test.o");
            assert(ok);

            fprintf(stderr, "--- objdump -dr ---\n");
            int rc = system("objdump -dr /tmp/hygen_locals_test.o");

            rc |= system("ld -o /tmp/hygen_locals_test_bin /tmp/hygen_locals_test.o 2>&1");
            fprintf(stderr, "link result: %d\n", rc);
            assert(rc == 0);

            rc = system("/tmp/hygen_locals_test_bin");
            int exit_code = WEXITSTATUS(rc);
            fprintf(stderr, "exit code: %d (expect 120 = fact(5), proving real per-call-frame locals)\n", exit_code);
            assert(exit_code == 120 && "fact(5) via real recursion should be 120 - "
                   "wrong value means locals are aliasing across call frames");

            obj_module_free(obj);
        }
    }

    mir_module_free(mir);
    ir_module_free(ir);

    printf("\nOK - recursive fact(5)=120 proves real per-call-frame local storage, "
           "not aliased global symbols\n");
    return 0;
}

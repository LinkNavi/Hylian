#include "ir.h"
#include "ir_to_mir.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* builds IR equivalent to:
   func main() {
       println("hi");
       let arr = array_alloc();
       arr.push(42);
       outb(0x3f8, 65);
   }
*/
int main(void) {
    IRModule *ir = ir_module_new();

    IRInstr *fb = ir_emit(ir, IR_FUNC_BEGIN);
    fb->str_extra = "main"; fb->extra_int = 1;

    IRInstr *pr = ir_emit(ir, IR_PRINTLN);
    pr->src1 = irop_const_str("hi");
    pr->extra_int = PRINT_ARG_STR_LIT;

    IRInstr *arr_alloc = ir_emit(ir, IR_ARRAY_ALLOC);
    arr_alloc->dest = irop_temp(0);

    IRInstr *arr_push = ir_emit(ir, IR_ARRAY_PUSH);
    arr_push->src1 = irop_temp(0);
    arr_push->src2 = irop_const_int(42);

    IRInstr *outb = ir_emit(ir, IR_OUTB);
    outb->src1 = irop_const_int(0x3f8);
    outb->src2 = irop_const_int(65);

    ir_emit(ir, IR_RETURN);
    ir_emit(ir, IR_FUNC_END);

    printf("=== source IR ===\n");
    ir_dump(ir, stdout);

    MIRModule *mir = lower_ir_to_mir(ir);

    printf("\n=== lowered MIR ===\n");
    mir_dump(mir, stdout);

    MIRVerifyResult vr = mir_verify(mir);
    if (!vr.ok) { fprintf(stderr, "verify failed: %s\n", vr.msg); assert(0); }

    MIRFunc *fn = mir->funcs[0];
    int found_call_println = 0, found_call_alloc = 0, found_call_push = 0, found_outb = 0;
    for (int i = 0; i < fn->count; i++) {
        MIRInstr *ins = &fn->instrs[i];
        if (ins->op == MIR_CALL && ins->callee) {
            if (strcmp(ins->callee, "hylian_println") == 0) found_call_println = 1;
            if (strcmp(ins->callee, "hylian_array_alloc") == 0) found_call_alloc = 1;
            if (strcmp(ins->callee, "hylian_array_push") == 0) found_call_push = 1;
        }
        if (ins->op == MIR_OUTB) found_outb = 1;
    }
    assert(found_call_println && "string literal println should lower to hylian_println call");
    assert(found_call_alloc && "array alloc should lower to hylian_array_alloc call");
    assert(found_call_push && "array push should lower to hylian_array_push call");
    assert(found_outb && "outb should lower to a dedicated MIR_OUTB, not a call");

    assert(mir->str_count == 1 && "\"hi\" should be interned exactly once");

    mir_module_free(mir);
    ir_module_free(ir);
    printf("\nOK\n");
    return 0;
}

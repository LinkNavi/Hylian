#include "hymir.h"
#include <stdio.h>
#include <assert.h>

/* builds: fn add_and_cast(i32 a, i32 b) -> i64
     v0 = a (i32), v1 = b (i32)
     v2 = v0 + v1        (i32 add)
     v3 = sext v2 -> i64
     ret v3
*/
int main(void) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = mir_func_new(mod, "add_and_cast");

    int a = mir_new_vreg(fn);
    int b = mir_new_vreg(fn);
    int sum = mir_new_vreg(fn);
    int wide = mir_new_vreg(fn);

    MIRInstr *i1 = mir_emit(fn, MIR_ADD);
    i1->dest = mir_vreg(sum, MIR_I32);
    i1->src1 = mir_vreg(a, MIR_I32);
    i1->src2 = mir_vreg(b, MIR_I32);

    MIRInstr *i2 = mir_emit(fn, MIR_SEXT);
    i2->dest = mir_vreg(wide, MIR_I64);
    i2->src1 = mir_vreg(sum, MIR_I32);

    MIRInstr *i3 = mir_emit(fn, MIR_RET);
    i3->src1 = mir_vreg(wide, MIR_I64);

    assert(fn->count == 3);
    assert(fn->vreg_count == 4);
    assert(mir_type_size(MIR_I32) == 4);
    assert(mir_type_size(MIR_I64) == 8);
    assert(!mir_type_is_unsigned(MIR_I32));
    assert(mir_type_is_unsigned(MIR_U32));
    assert(!mir_type_is_float(MIR_I32));
    assert(mir_type_is_float(MIR_F64));

    mir_dump(mod, stdout);

    mir_module_free(mod);
    printf("OK\n");
    return 0;
}

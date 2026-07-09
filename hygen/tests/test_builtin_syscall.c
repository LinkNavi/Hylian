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

/* mirrors what sys_write/sys_exit would generate once sys.hy calls the
   syscall() builtin directly instead of routing through naked asm wrappers:
     int sys_write(int fd, str buf, int len) { return syscall(1, fd, buf, len); }
     void sys_exit(int code) { syscall(60, code); }
   built as raw IR calling the "syscall" builtin, to prove the recognition
   path works on realistic input, not just a hand-built MIR shortcut. */
int main(void) {
    IRModule *ir = ir_module_new();

    IRInstr *fb = ir_emit(ir, IR_FUNC_BEGIN);
    fb->str_extra = "_start"; fb->extra_int = 1;

    IRInstr *cstr = ir_emit(ir, IR_CONST_STR);
    cstr->src1 = irop_const_str("hi\n");
    cstr->dest = irop_temp(0);

    /* sys_write(1, "hi\n", 3) -> syscall(1 [__NR_write], 1, buf, 3) */
    IRInstr *wr = ir_emit(ir, IR_CALL);
    wr->str_extra = "syscall";
    IROperand wr_args[4] = { irop_const_int(1), irop_const_int(1), irop_temp(0), irop_const_int(3) };
    wr->args = wr_args; wr->arg_count = 4;
    wr->dest = irop_temp(1);

    /* sys_exit(0) -> syscall(60 [__NR_exit], 0) - note only 2 args needed,
       not padded to a fixed wrapper arity like the old _sc3 approach required */
    IRInstr *ex = ir_emit(ir, IR_CALL);
    ex->str_extra = "syscall";
    IROperand ex_args[2] = { irop_const_int(60), irop_const_int(0) };
    ex->args = ex_args; ex->arg_count = 2;
    ex->dest = irop_temp(2);

    ir_emit(ir, IR_FUNC_END);

    MIRModule *mir = lower_ir_to_mir(ir);
    printf("=== lowered MIR ===\n");
    mir_dump(mir, stdout);

    /* the actual point of this test: confirm no dead call to "syscall" survived */
    MIRFunc *fn = mir->funcs[0];
    fn->is_naked = 1; /* this is _start, not a normal function */
    int syscall_count = 0, bad_call_count = 0;
    for (int i = 0; i < fn->count; i++) {
        if (fn->instrs[i].op == MIR_SYSCALL) syscall_count++;
        if (fn->instrs[i].op == MIR_CALL && fn->instrs[i].callee &&
            strcmp(fn->instrs[i].callee, "syscall") == 0) bad_call_count++;
    }
    printf("syscall_count=%d bad_call_count=%d\n", syscall_count, bad_call_count);
    assert(syscall_count == 2 && "sys_write and sys_exit should both become MIR_SYSCALL");
    assert(bad_call_count == 0 && "no real call to syscall() should remain - it's a builtin");

    RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 4 };
    RegAllocResult ra = regalloc_run(fn, &target);

    X64Buf code; x64_buf_init(&code);
    X64RelocList relocs; x64_reloc_list_init(&relocs);
    x64_lower_func(fn, &ra, &code, &relocs);

    ObjModule *obj = obj_module_new();
    obj_add_func(obj, "_start", &code, &relocs);
    for (int i = 0; i < mir->str_count; i++)
        obj_add_string(obj, mir->strs[i].label, mir->strs[i].data, mir->strs[i].len);

    int ok = obj_write_elf(obj, "/tmp/hygen_builtin_syscall_test.o");
    assert(ok);

    int rc = system("ld -o /tmp/hygen_builtin_syscall_test_bin /tmp/hygen_builtin_syscall_test.o 2>&1");
    assert(rc == 0 && "link should succeed");

    rc = system("/tmp/hygen_builtin_syscall_test_bin > /tmp/hygen_builtin_syscall_test_out.txt");
    int exit_code = WEXITSTATUS(rc);
    FILE *out = fopen("/tmp/hygen_builtin_syscall_test_out.txt", "r");
    char buf[16] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    fclose(out);
    fprintf(stderr, "exit_code=%d output=%zu bytes: %s", exit_code, n, buf);

    assert(exit_code == 0);
    assert(n == 3 && memcmp(buf, "hi\n", 3) == 0);

    obj_module_free(obj);
    x64_buf_free(&code);
    x64_reloc_list_free(&relocs);
    regalloc_result_free(&ra);
    mir_module_free(mir);
    ir_module_free(ir);

    printf("\nOK - IR_CALL to syscall() (the builtin, not a real function) lowers to genuine "
           "syscalls, linked with plain ld, executed correctly\n");
    return 0;
}

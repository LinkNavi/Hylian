#include "hymir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include "hyobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>

/* builds a freestanding _start (naked - no libc, no crt0) that does:
     write(1, "hi\n", 3);
     exit(0);
   entirely via raw syscalls - nothing here links against libc at all. */
int main(void) {
    MIRModule *mod = mir_module_new();
    MIRFunc *fn = mir_func_new(mod, "_start");
    fn->is_naked = 1;

    const char *msg = "hi\n";
    const char *lbl = mir_module_intern_string(mod, msg, 3);

    int ptr_v = mir_new_vreg(fn);
    MIRInstr *lea = mir_emit(fn, MIR_LEA_GLOBAL);
    lea->dest = mir_vreg(ptr_v, MIR_PTR);
    lea->src1 = mir_global(lbl, MIR_PTR);

    MIRValue *write_args = malloc(4 * sizeof(MIRValue));
    write_args[0] = mir_imm_int(1, MIR_I64);      /* __NR_write */
    write_args[1] = mir_imm_int(1, MIR_I64);      /* fd = stdout */
    write_args[2] = mir_vreg(ptr_v, MIR_PTR);      /* buf */
    write_args[3] = mir_imm_int(3, MIR_I64);       /* count */
    MIRInstr *wr = mir_emit(fn, MIR_SYSCALL);
    wr->args = write_args;
    wr->arg_count = 4;

    MIRValue *exit_args = malloc(2 * sizeof(MIRValue));
    exit_args[0] = mir_imm_int(60, MIR_I64);        /* __NR_exit */
    exit_args[1] = mir_imm_int(0, MIR_I64);
    MIRInstr *ex = mir_emit(fn, MIR_SYSCALL);
    ex->args = exit_args;
    ex->arg_count = 2;

    /* not calling mir_verify here: exit() genuinely never returns, and
       mir_verify's terminator check doesn't yet know a noreturn syscall
       counts as one - a real gap, noted rather than worked around silently */

    RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 4 };
    RegAllocResult ra = regalloc_run(fn, &target);

    X64Buf code; x64_buf_init(&code);
    X64RelocList relocs; x64_reloc_list_init(&relocs);
    x64_lower_func(fn, &ra, &code, &relocs);

    ObjModule *obj = obj_module_new();
    obj_add_func(obj, "_start", &code, &relocs);
    for (int i = 0; i < mod->str_count; i++)
        obj_add_string(obj, mod->strs[i].label, mod->strs[i].data, mod->strs[i].len);

    int ok = obj_write_elf(obj, "/tmp/hygen_syscall_test.o");
    assert(ok);

    fprintf(stderr, "--- objdump -dr ---\n");
    int rc = system("objdump -dr /tmp/hygen_syscall_test.o");

    /* link directly with ld - no libc, no crt0, no cc driver */
    rc |= system("ld -o /tmp/hygen_syscall_test_bin /tmp/hygen_syscall_test.o 2>&1");
    fprintf(stderr, "link result: %d\n", rc);
    assert(rc == 0 && "raw ld link (no libc) should succeed");

    rc = system("/tmp/hygen_syscall_test_bin > /tmp/hygen_syscall_test_out.txt");
    int exit_code = WEXITSTATUS(rc);
    fprintf(stderr, "process exit code: %d (expect 0)\n", exit_code);

    FILE *out = fopen("/tmp/hygen_syscall_test_out.txt", "r");
    char buf[16] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    fclose(out);
    fprintf(stderr, "stdout captured: %zu bytes: %s", n, buf);

    assert(exit_code == 0 && "exit(0) via raw syscall should give exit code 0");
    assert(n == 3 && memcmp(buf, "hi\n", 3) == 0 && "write() via raw syscall should print 'hi\\n'");

    obj_module_free(obj);
    x64_buf_free(&code);
    x64_reloc_list_free(&relocs);
    regalloc_result_free(&ra);
    mir_module_free(mod);

    printf("\nOK - raw syscalls proven end-to-end, zero libc, zero crt0, linked with plain ld\n");
    return 0;
}

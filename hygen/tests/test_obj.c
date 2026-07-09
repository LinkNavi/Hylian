#include "hymir.h"
#include "hyregalloc.h"
#include "hyx64.h"
#include "hyobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void build_func(MIRModule *mod, ObjModule *obj) {
    MIRFunc *fn = mir_func_new(mod, "ret42");
    MIRInstr *m = mir_emit(fn, MIR_RET);
    m->src1 = mir_imm_int(42, MIR_I64);

    RegAllocTarget target = { .num_int_regs = X64_NUM_ALLOCATABLE_INT_REGS, .num_float_regs = 4 };
    RegAllocResult ra = regalloc_run(fn, &target);

    X64Buf code; x64_buf_init(&code);
    X64RelocList relocs; x64_reloc_list_init(&relocs);
    x64_lower_func(fn, &ra, &code, &relocs);

    obj_add_func(obj, "ret42", &code, &relocs);

    x64_buf_free(&code);
    x64_reloc_list_free(&relocs);
    regalloc_result_free(&ra);
}

int main(void) {
    MIRModule *mod = mir_module_new();
    ObjModule *obj = obj_module_new();

    build_func(mod, obj);

    int ok = obj_write_elf(obj, "/tmp/hygen_test.o");
    assert(ok && "obj_write_elf should succeed");

    /* basic structural sanity: ELF magic + non-trivial size */
    FILE *f = fopen("/tmp/hygen_test.o", "rb");
    assert(f);
    uint8_t magic[4];
    size_t nread = fread(magic, 1, 4, f);
    assert(nread == 4);
    assert(magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F');
    fclose(f);
    printf("ELF object written, magic OK\n");

    fprintf(stderr, "\n--- readelf -h ---\n");
    int rc = system("readelf -h /tmp/hygen_test.o");
    fprintf(stderr, "--- readelf -S ---\n");
    rc |= system("readelf -S /tmp/hygen_test.o");
    fprintf(stderr, "--- readelf -s (symbols) ---\n");
    rc |= system("readelf -s /tmp/hygen_test.o");
    fprintf(stderr, "--- objdump -dr (disassembly + relocs) ---\n");
    rc |= system("objdump -dr /tmp/hygen_test.o");
    (void)rc;

    /* full pipeline proof: real link against a tiny C caller, then execute it */
    FILE *cf = fopen("/tmp/hygen_test_main.c", "w");
    fprintf(cf, "#include <stdio.h>\nextern long ret42(void);\nint main(){ long r = ret42(); printf(\"%%ld\\n\", r); return r == 42 ? 0 : 1; }\n");
    fclose(cf);

    int link_rc = system("cc /tmp/hygen_test_main.c /tmp/hygen_test.o -o /tmp/hygen_test_bin 2>&1");
    fprintf(stderr, "\nlink result: %d\n", link_rc);
    assert(link_rc == 0 && "linking the emitted .o with a real C caller should succeed");

    int run_rc = system("/tmp/hygen_test_bin");
    fprintf(stderr, "run exit code: %d (expect 0, meaning ret42() returned 42)\n", run_rc);
    assert(run_rc == 0 && "linked binary should run and confirm ret42() == 42");

    obj_module_free(obj);
    mir_module_free(mod);
    printf("\nOK - full pipeline verified: MIR -> regalloc -> x64 bytes -> ELF -> real linker -> executed, returned 42\n");
    return 0;
}

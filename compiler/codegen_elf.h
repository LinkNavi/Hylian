#ifndef CODEGEN_ELF_H
#define CODEGEN_ELF_H

#include "ir.h"

/* Emit a relocatable ELF64 object file (.o) from the IR module.
   Equivalent to codegen_ir() + nasm, but with no external tools.
   Link the output with: ld -o prog out.o runtime.a -lc
   Returns 0 on success, -1 on error. */
int codegen_elf(IRModule *mod, const char *outfile,
                const char *src_filename,
                const char *target,
                int freestanding);

#endif /* CODEGEN_ELF_H */

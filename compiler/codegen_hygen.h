#ifndef CODEGEN_HYGEN_H
#define CODEGEN_HYGEN_H
#include "ir.h"

/* Emits a linkable ELF64 object file for `mod` using the hygen backend
   (libhymir -> libhyregalloc -> libhyx64 -> libhyobj), the replacement for
   the old codegen_asm.c/codegen_elf.c pipeline.
   Returns 0 on success, non-zero on failure. */
int codegen_hygen(IRModule *mod, const char *outfile,
                   const char *src_filename, const char *target,
                   int freestanding);

#endif

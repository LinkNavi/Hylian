#ifndef CODEGEN_TERMINA_H
#define CODEGEN_TERMINA_H

#include "ir.h"
#include <stdio.h>

/*
 * Emit a Termina bytecode binary from an IRModule.
 *
 * Output format:
 *   [0..3]   magic      0x4D524554 ("TERM")
 *   [4..7]   data_size  bytes in the data (string/rodata) section
 *   [8..11]  data_off   byte offset of the data section from file start (always 16)
 *   [12..15] code_count number of 32-bit instructions in the code section
 *   [16..]   data       raw data bytes (strings, etc.)
 *   [16+data_size..] code  uint32_t instruction words
 *
 * The Termina VM should:
 *   1. Copy data bytes to address DATA_BASE  (0x1000)
 *   2. Copy code words  to address CODE_BASE (0x10000)
 *   3. Set PC = CODE_BASE and run.
 */
void codegen_termina(IRModule *mod, FILE *out, const char *src_filename);

#endif /* CODEGEN_TERMINA_H */

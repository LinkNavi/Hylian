#ifndef LSP_C_EXTRACTOR_H
#define LSP_C_EXTRACTOR_H

#include "lsp_analysis.h"  // for CompletionItem, etc.

/* Extract all exported function signatures from a C source file.
   Returns number of items filled, or -1 on parse error. */
int c_extract_functions(const char *filepath,
                        const char *source_code,
                        CompletionItem *out, int max_out,
                        const char *module_name);  /* e.g. "std.crypto" */

#endif

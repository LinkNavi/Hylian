#ifndef OPT_H
#define OPT_H

#include "ir.h"

/* Constant folding: evaluate binary/unary ops whose all operands are
   compile-time constants and replace the instruction with IR_CONST_INT.
   Returns the number of instructions folded. */
int opt_constant_fold(IRModule *mod);

/* Constant propagation: track temps that are definitively assigned a
   constant value (IR_CONST_INT/BOOL/NIL) and substitute that constant
   wherever the temp is later used as a source operand.
   Returns the number of substitutions made. */
int opt_constant_prop(IRModule *mod);

/* Dead-code elimination: mark instructions whose destination temp is
   never consumed by any other instruction (and that have no observable
   side effects) as IR_NOP.
   Returns the number of instructions eliminated. */
int opt_dce(IRModule *mod);

/* Convenience: run all three passes in a fixed-point loop until no
   more changes occur.  Returns total changes made across all passes. */
int opt_run_all(IRModule *mod);

#endif /* OPT_H */

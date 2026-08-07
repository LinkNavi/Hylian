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

/* Branch folding: constant-condition jumps collapse to unconditional
   jump/NOP, jump chains are threaded to their final target, and code
   after an unconditional jump/return up to the next label is NOP'd.
   Returns the number of changes made. */
int opt_branch_fold(IRModule *mod);

/* Whole-function unused-code elimination.

   Deletes the IR of any function that (a) arrived through an `include` — see
   IRModule.weak_funcs — and (b) is not reachable by any chain of calls from a
   function the user actually wrote.

   The point is not code size. Every function in an included stdlib module used
   to be lowered and linked whether or not the program called it, so a bug in
   an untouched stdlib function could break the build of a program that never
   went near it. Pruning first means only code the program can actually reach
   has to be compilable.

   Deliberately conservative: it only removes functions from includes (a
   function in the file being compiled may be the whole point of that file),
   and it treats a function's address being taken as a call. Returns the number
   of functions removed. */
int opt_strip_unreachable(IRModule *mod);

/* Convenience: run all passes in a fixed-point loop until no
   more changes occur.  Returns total changes made across all passes. */
int opt_run_all(IRModule *mod);

#endif /* OPT_H */

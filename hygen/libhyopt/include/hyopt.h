#ifndef HYOPT_H
#define HYOPT_H

#include "hymir.h"

/* Target-independent MIR optimizer.
 *
 * Runs on typed MIR before register allocation, so every frontend (and every
 * future backend) gets the same optimizations - the LLVM-style split, with the
 * frontend emitting simple code and the middle/back end cleaning it up.
 *
 * MIR is not SSA: a vreg may be assigned many times. That keeps the passes
 * simple (no phis, and copying a block never needs renaming) at the price of
 * the analyses below being explicit dataflow rather than def-use chains.
 *
 * Passes: mem2reg, copy/constant propagation, folding + algebraic
 * simplification, local CSE, dead-code elimination (liveness based), CFG
 * cleanup (unreachable code, jump threading), loop inversion and inlining of
 * small leaf functions.
 *
 * Environment (debugging aids):
 *   HYLIAN_OPT=0        disable everything
 *   HYLIAN_OPT_OFF=a,b  disable passes by name (mem2reg,prop,cse,dce,cfg,
 *                       loops,inline,strength,licm)
 */

void hyopt_func(MIRFunc *fn);
void hyopt_module(MIRModule *mod);

#endif

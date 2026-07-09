#ifndef IR_TO_MIR_H
#define IR_TO_MIR_H

#include "ir.h"
#include "hymir.h"

/* Lowers a whole Hylian IRModule to a MIRModule.
   Does its own tiny type-inference pass over temps/vars since the existing
   IR doesn't carry per-temp types (only IR_ALLOCA/IR_CAST/consts hint type). */
MIRModule *lower_ir_to_mir(const IRModule *ir);

#endif

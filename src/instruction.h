#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "types.h"

typedef struct {
    u32 value;
} Instruction;

typedef struct {
    u32 value;
} RegisterIndex;

u32 instruction_function(Instruction instruction);
RegisterIndex instruction_s(Instruction instruction);
RegisterIndex instruction_t(Instruction instruction);
RegisterIndex instruction_d(Instruction instruction);
u32 instruction_imm(Instruction instruction);
u32 instruction_imm_se(Instruction instruction);
u32 instruction_subfunction(Instruction instruction);
u32 instruction_shift(Instruction instruction);
u32 instruction_imm_jump(Instruction instruction);
u32 instruction_cop_opcode(Instruction instruction);

#endif

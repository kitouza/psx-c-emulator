#include "instruction.h"

// Return bits [31:26]
u32 instruction_function(Instruction instruction) {
    return instruction.value >> 26;
}

// Return register index in bits [25:21]
RegisterIndex instruction_s(Instruction instruction) {
    return (RegisterIndex){(instruction.value >> 21) & 0x1f};
}

// Return register index in bits [20:16]
RegisterIndex instruction_t(Instruction instruction) {
    return (RegisterIndex){(instruction.value >> 16) & 0x1f};
}

// Return immediate value in bits [15:0]
u32 instruction_imm(Instruction instruction) {
    return instruction.value & 0xffff;
}

// Return immediate value in bits [15:0] (sign extended)
u32 instruction_imm_se(Instruction instruction) {
    return (u32)(i32)(i16)(instruction.value & 0xffff);
}

// return register index in bits [15:11]
RegisterIndex instruction_d(Instruction instruction) {
    return (RegisterIndex){(instruction.value >> 11) & 0x1f};
}

//return bits [5:0] of instruction
u32 instruction_subfunction(Instruction instruction) {
    return instruction.value & 0x3f;
}

//return shift immediate in [10:6]
u32 instruction_shift(Instruction instruction) {
    return(instruction.value >> 6) & 0x1f;
}

// return jump target stored in [25:0]
u32 instruction_imm_jump(Instruction instruction) {
    return(instruction.value & 0x3ffffff);
}

// Return coprocessor opcode in bits [25:21]
u32 instruction_cop_opcode(Instruction instruction) {
    return (instruction.value >> 21) & 0x1f;
}

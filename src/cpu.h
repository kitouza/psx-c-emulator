#ifndef CPU_H
#define CPU_H

#include "instruction.h"
#include "interconnect.h"
#include "types.h"

typedef struct {
    RegisterIndex reg;
    u32 value;
} PendingLoad;

typedef struct {
    Interconnect* inter;
    u32 pc;
    Instruction next_instruction;
    u32 regs[32];
    u32 out_regs[32];
    PendingLoad load;
    u32 sr;
    u32 hi;
    u32 lo;
} Cpu;

void cpu_init(Cpu* cpu, Interconnect* inter);
void cpu_run_next_instruction(Cpu* cpu);
u32 cpu_load32(Cpu* cpu, u32 addr);
u8 cpu_load8(Cpu* cpu, u32 addr);
void cpu_store32(Cpu* cpu, u32 addr, u32 val);
void cpu_store16(Cpu* cpu, u32 addr, u16 val);
void cpu_store8(Cpu* cpu, u32 addr, u8 val);
u32 load_reg(Cpu* cpu, RegisterIndex index);
void set_reg(Cpu* cpu, RegisterIndex index, u32 val);
void branch(Cpu* cpu, u32 offset);
void op_bxx(Cpu* cpu, Instruction instruction);
void op_blez(Cpu* cpu, Instruction instruction);
void op_bgtz(Cpu* cpu, Instruction instruction);
void op_bne(Cpu* cpu, Instruction instruction);
void op_beq(Cpu* cpu, Instruction instruction);
void op_lui(Cpu* cpu, Instruction instruction);
void op_andi(Cpu* cpu, Instruction instruction);
void op_ori(Cpu* cpu, Instruction instruction);
void op_and(Cpu* cpu, Instruction instruction);
void op_or(Cpu* cpu, Instruction instruction);
void op_lw(Cpu* cpu, Instruction instruction);
void op_lbu(Cpu* cpu, Instruction instruction);
void op_lb(Cpu* cpu, Instruction instruction);
void op_sw(Cpu* cpu, Instruction instruction);
void op_sh(Cpu* cpu, Instruction instruction);
void op_sb(Cpu* cpu, Instruction instruction);
void op_sll(Cpu* cpu, Instruction instruction);
void op_srl(Cpu* cpu, Instruction instruction);
void op_sra(Cpu* cpu, Instruction instruction);
void op_add(Cpu* cpu, Instruction instruction);
void op_addi(Cpu* cpu, Instruction instruction);
void op_addiu(Cpu* cpu, Instruction instruction);
void op_addu(Cpu* cpu, Instruction instruction);
void op_subu(Cpu* cpu, Instruction instruction);
void op_div(Cpu* cpu, Instruction instruction);
void op_divu(Cpu* cpu, Instruction instruction);
void op_mfhi(Cpu* cpu, Instruction instruction);
void op_mflo(Cpu* cpu, Instruction instruction);
void op_j(Cpu* cpu, Instruction instruction);
void op_jal(Cpu* cpu, Instruction instruction);
void op_jr(Cpu* cpu, Instruction instruction);
void op_jalr(Cpu* cpu, Instruction instruction);
void op_slt(Cpu* cpu, Instruction instruction);
void op_sltu(Cpu* cpu, Instruction instruction);
void op_slti(Cpu* cpu, Instruction instruction);
void op_sltiu(Cpu* cpu, Instruction instruction);
void op_mtc0(Cpu* cpu, Instruction instruction);
void op_mfc0(Cpu* cpu, Instruction instruction);
void op_cop0(Cpu* cpu, Instruction instruction);
void cpu_decode_and_execute(Cpu* cpu, Instruction instruction);

#endif

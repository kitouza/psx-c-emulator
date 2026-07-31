#ifndef CPU_H
#define CPU_H

#include "instruction.h"
#include "interconnect.h"
#include "types.h"

typedef struct Debugger Debugger;

typedef struct {
    RegisterIndex reg;
    u32 value;
} PendingLoad;

typedef struct Cpu {
    Interconnect* inter;
    Debugger* debugger;
    u32 pc;
    u32 next_pc;
    u32 current_pc;
    bool branch;
    bool delay_slot;
    u32 regs[32];
    u32 out_regs[32];
    PendingLoad load;
    u32 sr;
    u32 cause;
    u32 badvaddr;
    u32 epc;
    u32 hi;
    u32 lo;
} Cpu;

typedef enum {
    EXCEPTION_LOADADDRESSERROR = 0x4,
    EXCEPTION_STOREADDRESSERROR = 0x5,
    EXCEPTION_SYSCALL = 0x8,
    EXCEPTION_BREAK = 0x9,
    EXCEPTION_ILLEGALINSTRUCTION = 0xa,
    EXCEPTION_COPROCESSORERROR = 0xb,
    EXCEPTION_OVERFLOW = 0xc
} Exception;

void cpu_init(Cpu* cpu, Interconnect* inter, Debugger* debugger);
bool cpu_run_next_instruction(Cpu* cpu);
u32 cpu_load(Cpu* cpu, u32 addr, AccessWidth width);
void cpu_store(Cpu* cpu, u32 addr, u32 val, AccessWidth width);
bool cpu_examine(const Cpu* cpu, u32 addr, AccessWidth width, u32* value);
bool cpu_deposit(Cpu* cpu, u32 addr, AccessWidth width, u32 value);
u32 load_reg(Cpu* cpu, RegisterIndex index);
void set_reg(Cpu* cpu, RegisterIndex index, u32 val);
void branch(Cpu* cpu, u32 offset);
void cpu_exception(Cpu* cpu, Exception exception);
void op_syscall(Cpu* cpu, Instruction instruction);
void op_break(Cpu* cpu, Instruction instruction);
void op_bxx(Cpu* cpu, Instruction instruction);
void op_blez(Cpu* cpu, Instruction instruction);
void op_bgtz(Cpu* cpu, Instruction instruction);
void op_bne(Cpu* cpu, Instruction instruction);
void op_beq(Cpu* cpu, Instruction instruction);
void op_lui(Cpu* cpu, Instruction instruction);
void op_andi(Cpu* cpu, Instruction instruction);
void op_ori(Cpu* cpu, Instruction instruction);
void op_xori(Cpu* cpu, Instruction instruction);
void op_and(Cpu* cpu, Instruction instruction);
void op_or(Cpu* cpu, Instruction instruction);
void op_xor(Cpu* cpu, Instruction instruction);
void op_nor(Cpu* cpu, Instruction instruction);
void op_lw(Cpu* cpu, Instruction instruction);
void op_lwl(Cpu* cpu, Instruction instruction);
void op_lwr(Cpu* cpu, Instruction instruction);
void op_lbu(Cpu* cpu, Instruction instruction);
void op_lh(Cpu* cpu, Instruction instruction);
void op_lhu(Cpu* cpu, Instruction instruction);
void op_lb(Cpu* cpu, Instruction instruction);
void op_sw(Cpu* cpu, Instruction instruction);
void op_swl(Cpu* cpu, Instruction instruction);
void op_swr(Cpu* cpu, Instruction instruction);
void op_sh(Cpu* cpu, Instruction instruction);
void op_sb(Cpu* cpu, Instruction instruction);
void op_sll(Cpu* cpu, Instruction instruction);
void op_srl(Cpu* cpu, Instruction instruction);
void op_sra(Cpu* cpu, Instruction instruction);
void op_srav(Cpu* cpu, Instruction instruction);
void op_srlv(Cpu* cpu, Instruction instruction);
void op_sllv(Cpu* cpu, Instruction instruction);
void op_add(Cpu* cpu, Instruction instruction);
void op_addi(Cpu* cpu, Instruction instruction);
void op_addiu(Cpu* cpu, Instruction instruction);
void op_addu(Cpu* cpu, Instruction instruction);
void op_subu(Cpu* cpu, Instruction instruction);
void op_sub(Cpu* cpu, Instruction instruction);
void op_div(Cpu* cpu, Instruction instruction);
void op_divu(Cpu* cpu, Instruction instruction);
void op_multu(Cpu* cpu, Instruction instruction);
void op_mult(Cpu* cpu, Instruction instruction);
void op_mfhi(Cpu* cpu, Instruction instruction);
void op_mthi(Cpu* cpu, Instruction instruction);
void op_mflo(Cpu* cpu, Instruction instruction);
void op_mtlo(Cpu* cpu, Instruction instruction);
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
void op_rfe(Cpu* cpu, Instruction instruction);
void op_cop0(Cpu* cpu, Instruction instruction);
void op_cop_unusable(Cpu* cpu, Instruction instruction);
void op_cop2(Cpu* cpu, Instruction instruction);
void op_lwc(Cpu* cpu, Instruction instruction);
void op_swc(Cpu* cpu, Instruction instruction);
void op_illegal(Cpu* cpu, Instruction instruction);
void cpu_decode_and_execute(Cpu* cpu, Instruction instruction);

#endif

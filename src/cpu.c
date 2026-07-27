#include "cpu.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// initialize clean cpu
void cpu_init(Cpu* cpu, Interconnect* inter) {
    memset(cpu, 0, sizeof(Cpu)); // zero out cpu structure

    cpu->inter = inter; // borrow the interconnect; do not copy BIOS or RAM
    cpu->pc = 0xbfc00000; // PC reset value at beginning of BIOS
    cpu->next_pc = cpu->pc + 4;

    cpu->sr = 0;
    cpu->hi = 0xdeadbeef;
    cpu->lo = 0xdeadbeef;

    // initialize all registers to 0xdeadbeef
    for(int i = 0; i < 32; i++) {
        cpu->regs[i] = 0xdeadbeef;
    }

    // initialize $0 register to 0
    cpu->regs[0] = 0;

    memcpy(cpu->out_regs, cpu->regs, sizeof(cpu->regs));
    cpu->load = (PendingLoad){
        .reg = (RegisterIndex){0},
        .value = 0
    };
}

void cpu_run_next_instruction(Cpu* cpu) {
    cpu->current_pc = cpu->pc;

    // A branch affects the instruction after its delay slot.
    cpu->delay_slot = cpu->branch;
    cpu->branch = false;

    if (cpu->current_pc % 4 != 0) {
        cpu_exception(cpu, EXCEPTION_LOADADDRESSERROR);
        return;
    }

    Instruction instruction = (Instruction){
        cpu_load(cpu, cpu->pc, ACCESS_WORD)
    };

    // Keep one PC ahead so branches execute the following delay-slot instruction.
    cpu->pc = cpu->next_pc;
    cpu->next_pc += 4;

    // Commit the previous instruction's pending load to the output registers.
    // The current instruction still reads the old values from regs.
    set_reg(cpu, cpu->load.reg, cpu->load.value);

    // Unless the current instruction schedules a load, target $zero (a no-op).
    cpu->load = (PendingLoad){
        .reg = (RegisterIndex){0},
        .value = 0
    };

    cpu_decode_and_execute(cpu, instruction); // execute  instruciton

    // Make this instruction's output visible to the next instruction.
    memcpy(cpu->regs, cpu->out_regs, sizeof(cpu->regs));
}

// store/load funcs

u32 cpu_load(Cpu* cpu, u32 addr, AccessWidth width) {
    return interconnect_load(cpu->inter, addr, width);
}

void cpu_store(Cpu* cpu, u32 addr, u32 val, AccessWidth width) {
    if ((cpu->sr & 0x10000) != 0) {
        fprintf(stderr, "Ignoring store while cache is isolated\n");
        return;
    }

    interconnect_store(cpu->inter, addr, val, width);
}
// register funcs

u32 load_reg(Cpu* cpu, RegisterIndex index) {
    return cpu->regs[index.value];
}

void set_reg(Cpu* cpu, RegisterIndex index, u32 val) {
    cpu->out_regs[index.value] = val;

    // make sure $0 register is always 0
    cpu->out_regs[0] = 0;
}


// functions operations

// branch to pc + offset
void branch(Cpu* cpu, u32 offset) {
    offset = offset << 2;
    cpu->next_pc = cpu->pc + offset;
}

void op_bxx(Cpu* cpu, Instruction instruction) {
    cpu->branch = true;

    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 is_bgez = (instruction.value >> 16) & 1;
    u32 is_link = ((instruction.value >> 17) & 0xf) == 8;

    u32 is_negative = (i32)load_reg(cpu, s) < 0;
    u32 should_branch = is_negative ^ is_bgez;

    if (is_link) {
        set_reg(cpu, (RegisterIndex){31}, cpu->next_pc);
    }

    if (should_branch) {
        branch(cpu, i);
    }
}

void op_blez(Cpu* cpu, Instruction instruction) {
    cpu->branch = true;

    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);

    i32 v = load_reg(cpu, s);

    if(v <= 0) {
        branch(cpu, i);
    }
}

void op_bgtz(Cpu* cpu, Instruction instruction) {
    cpu->branch = true;

    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);

    i32 v = load_reg(cpu, s);

    if(v > 0) {
        branch(cpu, i);
    }
}

void op_bne(Cpu* cpu, Instruction instruction) {
    cpu->branch = true;

    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    if(load_reg(cpu, s) != load_reg(cpu, t)) {
        branch(cpu, i);
    }
}

void op_beq(Cpu* cpu, Instruction instruction) {
    cpu->branch = true;

    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    if(load_reg(cpu, s) == load_reg(cpu, t)) {
        branch(cpu, i);
    }
}

void op_lui(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm(instruction);
    RegisterIndex t = instruction_t(instruction);

    // low 16 bits set to 0
    u32 v = i << 16;

    set_reg(cpu, t, v);
}

void op_andi(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 v = load_reg(cpu, s) & i;

    set_reg(cpu, t, v);
}

void op_ori(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 v = load_reg(cpu, s) | i;

    set_reg(cpu, t, v);
}

void op_xori(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 v = load_reg(cpu, s) ^ i;

    set_reg(cpu, t, v);
}

void op_and(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 v = load_reg(cpu, s) & load_reg(cpu, t);

    set_reg(cpu, d, v);
}


void op_or(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 v = load_reg(cpu, s) | load_reg(cpu, t);

    set_reg(cpu, d, v);
}

void op_xor(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 v = load_reg(cpu, s) ^ load_reg(cpu, t);

    set_reg(cpu, d, v);
}

void op_nor(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 v = ~(load_reg(cpu, s) | load_reg(cpu, t));

    set_reg(cpu, d, v);
}

void op_lw(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 v = cpu_load(cpu, addr, ACCESS_WORD);

    cpu->load = (PendingLoad){
        .reg = t,
        .value = v
    };
}

void op_lwl(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 current = cpu->out_regs[t.value];
    u32 word = cpu_load(cpu, addr & ~3u, ACCESS_WORD);
    u32 v;

    switch (addr & 3) {
        case 0: v = (current & 0x00ffffff) | (word << 24); break;
        case 1: v = (current & 0x0000ffff) | (word << 16); break;
        case 2: v = (current & 0x000000ff) | (word << 8); break;
        case 3: v = word; break;
        default: abort();
    }

    cpu->load = (PendingLoad){.reg = t, .value = v};
}

void op_lwr(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 current = cpu->out_regs[t.value];
    u32 word = cpu_load(cpu, addr & ~3u, ACCESS_WORD);
    u32 v;

    switch (addr & 3) {
        case 0: v = word; break;
        case 1: v = (current & 0xff000000) | (word >> 8); break;
        case 2: v = (current & 0xffff0000) | (word >> 16); break;
        case 3: v = (current & 0xffffff00) | (word >> 24); break;
        default: abort();
    }

    cpu->load = (PendingLoad){.reg = t, .value = v};
}

void op_lb(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 v = (u32)(i32)(i8)cpu_load(cpu, addr, ACCESS_BYTE);

    cpu->load = (PendingLoad){
        .reg = t,
        .value = v
    };
}

void op_lbu(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u8 v = (u8)cpu_load(cpu, addr, ACCESS_BYTE);

    cpu->load = (PendingLoad){
        .reg = t,
        .value = v
    };
}

void op_lh(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;

    if (addr % 2 != 0) {
        cpu_exception(cpu, EXCEPTION_LOADADDRESSERROR);
        return;
    }

    u32 v = (u32)(i32)(i16)cpu_load(cpu, addr, ACCESS_HALFWORD);
    cpu->load = (PendingLoad){
        .reg = t,
        .value = v
    };
}

void op_lhu(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    if (addr % 2 != 0) {
        cpu_exception(cpu, EXCEPTION_LOADADDRESSERROR);
        return;
    }

    u16 v = (u16)cpu_load(cpu, addr, ACCESS_HALFWORD);
    cpu->load = (PendingLoad){
        .reg = t,
        .value = v
    };
}

void op_sw(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 v = load_reg(cpu, t);

    cpu_store(cpu, addr, v, ACCESS_WORD);
}

void op_swl(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 aligned_addr = addr & ~3u;
    u32 v = load_reg(cpu, t);
    u32 current = cpu_load(cpu, aligned_addr, ACCESS_WORD);
    u32 word;

    switch (addr & 3) {
        case 0: word = (current & 0xffffff00) | (v >> 24); break;
        case 1: word = (current & 0xffff0000) | (v >> 16); break;
        case 2: word = (current & 0xff000000) | (v >> 8); break;
        case 3: word = v; break;
        default: abort();
    }

    cpu_store(cpu, aligned_addr, word, ACCESS_WORD);
}

void op_swr(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 aligned_addr = addr & ~3u;
    u32 v = load_reg(cpu, t);
    u32 current = cpu_load(cpu, aligned_addr, ACCESS_WORD);
    u32 word;

    switch (addr & 3) {
        case 0: word = v; break;
        case 1: word = (current & 0x000000ff) | (v << 8); break;
        case 2: word = (current & 0x0000ffff) | (v << 16); break;
        case 3: word = (current & 0x00ffffff) | (v << 24); break;
        default: abort();
    }

    cpu_store(cpu, aligned_addr, word, ACCESS_WORD);
}

void op_sh(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u16 v = (u16)load_reg(cpu, t);

    cpu_store(cpu, addr, v, ACCESS_HALFWORD);
}

void op_sb(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u8 v = (u8)load_reg(cpu, t);

    cpu_store(cpu, addr, v, ACCESS_BYTE);
}

void op_sll(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_shift(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    u32 v = load_reg(cpu, t) << i;

    set_reg(cpu, d, v);
}

void op_srl(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_shift(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    u32 v = load_reg(cpu, t) >> i;

    set_reg(cpu, d, v);
}

void op_sra(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_shift(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    u32 v = (i32)load_reg(cpu, t) >> i;

    set_reg(cpu, d, v);
}

void op_srav(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    u32 v = (i32) load_reg(cpu, t) >> (load_reg(cpu, s) & 0x1f);

    set_reg(cpu, d, v);
}

void op_srlv(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    u32 v = load_reg(cpu, t) >> (load_reg(cpu, s) & 0x1f);

    set_reg(cpu, d, v);
}

void op_sllv(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    u32 v = load_reg(cpu, t) << (load_reg(cpu, s) & 0x1f);

    set_reg(cpu, d, v);
}

void op_add(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    i32 v1 = (i32)load_reg(cpu, s);
    i32 v2 = (i32)load_reg(cpu, t);

    i32 result;

    bool overflow = __builtin_add_overflow(v1, v2, &result);
    if(overflow) {
        cpu_exception(cpu, EXCEPTION_OVERFLOW);
        return;
    }

    set_reg(cpu, d, (u32)result);
}

void op_addi(Cpu* cpu, Instruction instruction) {
    i32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex source = instruction_s(instruction);

    i32 source_value = (i32)load_reg(cpu, source);

    i32 result;

    bool overflow = __builtin_add_overflow(source_value, i, &result);
    if(overflow) {
        cpu_exception(cpu, EXCEPTION_OVERFLOW);
        return;
    }

    set_reg(cpu, t, (u32)result);
}

void op_addiu(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 v = load_reg(cpu, s) + i;

    set_reg(cpu, t, v);
}

void op_addu(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    u32 v = load_reg(cpu, s) + load_reg(cpu, t);

    set_reg(cpu, d, v);
}

void op_subu(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    u32 v = load_reg(cpu, s) - load_reg(cpu, t);

    set_reg(cpu, d, v);
}

void op_sub(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    i32 result;
    bool overflow = __builtin_sub_overflow((i32)load_reg(cpu, s),
                                           (i32)load_reg(cpu, t),
                                           &result);
    if (overflow) {
        cpu_exception(cpu, EXCEPTION_OVERFLOW);
        return;
    }

    set_reg(cpu, d, (u32)result);
}

void op_div(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    i32 numerator = (i32)load_reg(cpu, s);
    i32 denominator = (i32)load_reg(cpu, t);

    if (denominator == 0) {
        cpu->hi = (u32)numerator;
        cpu->lo = numerator >= 0 ? 0xffffffff : 1;
    } else if ((u32)numerator == 0x80000000 && denominator == -1) {
        cpu->hi = 0;
        cpu->lo = 0x80000000;
    } else {
        cpu->hi = (u32)(numerator % denominator);
        cpu->lo = (u32)(numerator / denominator);
    }
}

void op_divu(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 numerator = load_reg(cpu, s);
    u32 denominator = load_reg(cpu, t);

    if (denominator == 0) {
        cpu->hi = numerator;
        cpu->lo = 0xffffffff;
    } else {
        cpu->hi = numerator % denominator;
        cpu->lo = numerator / denominator;
    }
}

void op_multu(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u64 result = (u64)load_reg(cpu, s) * (u64)load_reg(cpu, t);

    cpu->hi = (u32)(result >> 32);
    cpu->lo = (u32)result;
}

void op_mult(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    i64 a = (i64)(i32)load_reg(cpu, s);
    i64 b = (i64)(i32)load_reg(cpu, t);
    u64 result = (u64)(a * b);

    cpu->hi = (u32)(result >> 32);
    cpu->lo = (u32)result;
}

void op_mfhi(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    u32 v = cpu->hi;

    set_reg(cpu, d, v);
}

void op_mthi(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    u32 v = load_reg(cpu, s);

    cpu->hi = v;
}

void op_mflo(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    u32 v = cpu->lo;

    set_reg(cpu, d, v);
}

void op_mtlo(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    u32 v = load_reg(cpu, s);

    cpu->lo = v;
}

void op_j(Cpu* cpu, Instruction instruction) {
    cpu->branch = true;

    u32 i = instruction_imm_jump(instruction);

    cpu->next_pc = (cpu->pc & 0xf0000000) | (i << 2);
}

void op_jal(Cpu* cpu, Instruction instruction) {
    u32 ra = cpu->next_pc;

    set_reg(cpu, (RegisterIndex){31}, ra);
    op_j(cpu, instruction);
}

void op_jr(Cpu* cpu, Instruction instruction) {
    cpu->branch = true;

    RegisterIndex s = instruction_s(instruction);

    cpu->next_pc = load_reg(cpu, s);
}

void op_jalr(Cpu* cpu, Instruction instruction) {
    cpu->branch = true;

    u32 ra = cpu->next_pc;
    RegisterIndex d = instruction_d(instruction);
    RegisterIndex s = instruction_s(instruction);

    set_reg(cpu, d, ra);

    cpu->next_pc = load_reg(cpu, s);
}

void cpu_exception(Cpu* cpu, Exception exception) {
    u32 handler = (cpu->sr & (1 << 22)) != 0
        ? 0xbfc00180
        : 0x80000080;

    // Push the current interrupt-enable/user-mode pair onto SR's mode stack.
    u32 mode = cpu->sr & 0x3f;
    cpu->sr &= ~0x3f;
    cpu->sr |= (mode << 2) & 0x3f;

    cpu->cause = (u32)exception << 2;
    cpu->epc = cpu->current_pc;

    if (cpu->delay_slot) {
        cpu->epc -= 4;
        cpu->cause |= 1u << 31;
    }

    // Exceptions bypass the normal branch delay slot.
    cpu->pc = handler;
    cpu->next_pc = handler + 4;
}

void op_syscall(Cpu* cpu, Instruction instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_SYSCALL);
}

void op_break(Cpu* cpu, Instruction instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_BREAK);
}

void op_slt(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 v = (i32)load_reg(cpu, s) < (i32)load_reg(cpu, t);

    set_reg(cpu, d, v);
}

void op_sltu(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 v = load_reg(cpu, s) < load_reg(cpu, t);

    set_reg(cpu, d, v);
}

void op_slti(Cpu* cpu, Instruction instruction) {
    i32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 v = (i32)load_reg(cpu, s) < i;

    set_reg(cpu, t, v);
}

void op_sltiu(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    u32 v = load_reg(cpu, s) < i;

    set_reg(cpu, t, v);
}

// coprocessor ops

void op_mtc0(Cpu* cpu, Instruction instruction) {
    RegisterIndex cpu_r = instruction_t(instruction);
    u32 cop_r = instruction_d(instruction).value;

    u32 v = load_reg(cpu, cpu_r);

    switch(cop_r) {
        case 12:
            cpu->sr = v;
            break;
        case 3: case 5: case 6: case 7: case 9: case 11:
            if(v != 0) {
                fprintf(stderr, "Unhandled write to cop0 register: 0x%08x\n", cop_r);
                exit(EXIT_FAILURE);
            }
            break;
        case 13:
            if(v != 0) {
                fprintf(stderr, "Unhandled write to CAUSE register: 0x%08x\n", cop_r);
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "Unhandled cop0 register: 0x%08x\n", cop_r);
            exit(EXIT_FAILURE);
    }
}

void op_mfc0(Cpu* cpu, Instruction instruction) {
    RegisterIndex cpu_r = instruction_t(instruction);
    u32 cop_r = instruction_d(instruction).value;
    u32 v;

    switch(cop_r) {
        case 12:
            v = cpu->sr;
            break;
        case 13:
            v = cpu->cause;
            break;
        case 14:
            v = cpu->epc;
            break;
        default:
            fprintf(stderr, "Unhandled read from cop0 register: 0x%08x\n", cop_r);
            exit(EXIT_FAILURE);
    }

    cpu->load = (PendingLoad) {cpu_r, v};
}

void op_rfe(Cpu* cpu, Instruction instruction) {
    // This coprocessor opcode also encodes unsupported virtual-memory operations.
    if (instruction_subfunction(instruction) != 0x10) {
        fprintf(stderr,
                "Invalid cop0 instruction: 0x%08x\n",
                instruction.value);
        exit(EXIT_FAILURE);
    }

    // Restore the previous interrupt-enable/user-mode pair from SR's mode stack.
    u32 mode = cpu->sr & 0x3f;
    cpu->sr &= ~0x3f;
    cpu->sr |= mode >> 2;
}

void op_cop0(Cpu* cpu, Instruction instruction) {
    switch(instruction_cop_opcode(instruction)){
        case 0b00000:
            op_mfc0(cpu, instruction);
            break;
        case 0b00100:
            op_mtc0(cpu, instruction);
            break;
        case 0b10000:
            op_rfe(cpu, instruction);
            break;
        default:
            fprintf(stderr,
            "Error: unhandled cop0 instruction 0x%08x\n",
            instruction.value);
            exit(EXIT_FAILURE);
    }
}

void op_cop_unusable(Cpu* cpu, Instruction instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSORERROR);
}

void op_cop2(Cpu* cpu, Instruction instruction) {
    (void)cpu;
    fprintf(stderr,
            "Unhandled GTE instruction: 0x%08x\n",
            instruction.value);
    exit(EXIT_FAILURE);
}

void op_lwc(Cpu* cpu, Instruction instruction) {
    u32 coprocessor = instruction_function(instruction) - 0x30;

    if (coprocessor == 2) {
        fprintf(stderr, "Unhandled GTE LWC: 0x%08x\n", instruction.value);
        exit(EXIT_FAILURE);
    }

    cpu_exception(cpu, EXCEPTION_COPROCESSORERROR);
}

void op_swc(Cpu* cpu, Instruction instruction) {
    u32 coprocessor = instruction_function(instruction) - 0x38;

    if (coprocessor == 2) {
        fprintf(stderr, "Unhandled GTE SWC: 0x%08x\n", instruction.value);
        exit(EXIT_FAILURE);
    }

    cpu_exception(cpu, EXCEPTION_COPROCESSORERROR);
}

void op_illegal(Cpu* cpu, Instruction instruction) {
    fprintf(stderr, "Illegal instruction: 0x%08x\n", instruction.value);
    cpu_exception(cpu, EXCEPTION_ILLEGALINSTRUCTION);
}


void cpu_decode_and_execute(Cpu* cpu, Instruction instruction) {
    switch (instruction_function(instruction)) {
        case 0b010000:
            op_cop0(cpu, instruction);
            break;
        case 0b000000:
            switch (instruction_subfunction(instruction)) {
                case 0b000000:
                    op_sll(cpu, instruction);
                    break;
                case 0b000010:
                    op_srl(cpu, instruction);
                    break;
                case 0b000011:
                    op_sra(cpu, instruction);
                    break;
                case 0b000110:
                    op_srlv(cpu, instruction);
                    break;
                case 0b000111:
                    op_srav(cpu, instruction);
                    break;
                case 0b000100:
                    op_sllv(cpu, instruction);
                    break;
                case 0b100100:
                    op_and(cpu, instruction);
                    break;
                case 0b100101:
                    op_or(cpu, instruction);
                    break;
                case 0b100110:
                    op_xor(cpu, instruction);
                    break;
                case 0b100111:
                    op_nor(cpu, instruction);
                    break;
                case 0b101010:
                    op_slt(cpu, instruction);
                    break;
                case 0b101011:
                    op_sltu(cpu, instruction);
                    break;
                case 0b100000:
                    op_add(cpu, instruction);
                    break;
                case 0b100001:
                    op_addu(cpu, instruction);
                    break;
                case 0b100010:
                    op_sub(cpu, instruction);
                    break;
                case 0b100011:
                    op_subu(cpu, instruction);
                    break;
                case 0b011000:
                    op_mult(cpu, instruction);
                    break;
                case 0b011010:
                    op_div(cpu, instruction);
                    break;
                case 0b011011:
                    op_divu(cpu, instruction);
                    break;
                case 0b011001:
                    op_multu(cpu, instruction);
                    break;
                case 0b010000:
                    op_mfhi(cpu, instruction);
                    break;
                case 0b010001:
                    op_mthi(cpu, instruction);
                    break;
                case 0b010010:
                    op_mflo(cpu, instruction);
                    break;
                case 0b010011:
                    op_mtlo(cpu, instruction);
                    break;
                case 0b001000:
                    op_jr(cpu, instruction);
                    break;
                case 0b001001:
                    op_jalr(cpu, instruction);
                    break;
                case 0b001100:
                    op_syscall(cpu, instruction);
                    break;
                case 0b001101:
                    op_break(cpu, instruction);
                    break;
                default:
                    op_illegal(cpu, instruction);
            }
            break;
        case 0b000001:
            op_bxx(cpu, instruction);
            break;
        case 0b000010:
            op_j(cpu, instruction);
            break;
        case 0b000011:
            op_jal(cpu, instruction);
            break;
        case 0b000101:
            op_bne(cpu, instruction);
            break;
        case 0b000110:
            op_blez(cpu, instruction);
            break;
        case 0b000111:
            op_bgtz(cpu, instruction);
            break;
        case 0b000100:
            op_beq(cpu, instruction);
            break;
        case 0b001000:
            op_addi(cpu, instruction);
            break;
        case 0b001001:
            op_addiu(cpu, instruction);
            break;
        case 0b001010:
            op_slti(cpu, instruction);
            break;
        case 0b001011:
            op_sltiu(cpu, instruction);
            break;
        case 0b101011:
            op_sw(cpu, instruction);
            break;
        case 0b101001:
            op_sh(cpu, instruction);
            break;
        case 0b101000:
            op_sb(cpu, instruction);
            break;
        case 0b100011:
            op_lw(cpu, instruction);
            break;
        case 0b100000:
            op_lb(cpu, instruction);
            break;
        case 0b100100:
            op_lbu(cpu, instruction);
            break;
        case 0b100001:
            op_lh(cpu, instruction);
            break;
        case 0b100101:
            op_lhu(cpu, instruction);
            break;
        case 0b001111:
            op_lui(cpu, instruction);
            break;
        case 0b001100:
            op_andi(cpu, instruction);
            break;
        case 0b001101:
            op_ori(cpu, instruction);
            break;
        case 0b001110:
            op_xori(cpu, instruction);
            break;
        case 0b010001:
        case 0b010011:
            op_cop_unusable(cpu, instruction);
            break;
        case 0b010010:
            op_cop2(cpu, instruction);
            break;
        case 0b100010:
            op_lwl(cpu, instruction);
            break;
        case 0b100110:
            op_lwr(cpu, instruction);
            break;
        case 0b101010:
            op_swl(cpu, instruction);
            break;
        case 0b101110:
            op_swr(cpu, instruction);
            break;
        case 0b110000:
        case 0b110001:
        case 0b110010:
        case 0b110011:
            op_lwc(cpu, instruction);
            break;
        case 0b111000:
        case 0b111001:
        case 0b111010:
        case 0b111011:
            op_swc(cpu, instruction);
            break;

        default:
            op_illegal(cpu, instruction);
    }
}

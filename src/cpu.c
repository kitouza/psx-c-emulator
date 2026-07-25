#include "cpu.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// initialize clean cpu
void cpu_init(Cpu* cpu, Interconnect* inter) {
    memset(cpu, 0, sizeof(Cpu)); // zero out cpu structure

    cpu->inter = inter; // borrow the interconnect; do not copy BIOS or RAM
    cpu->pc = 0xbfc00000; // PC reset value at beginning of BIOS

    cpu->sr = 0;
    cpu->hi = 0xdeadbeef;
    cpu->lo = 0xdeadbeef;

    cpu->next_instruction = (Instruction){0x0}; // store next instruction for branch delay

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

    Instruction instruction = cpu->next_instruction; //use prev loaded instruction

    //fetch next instruction at PC
    cpu->next_instruction = (Instruction){cpu_load32(cpu, cpu->pc)};

    cpu->pc += 4; // increment PC

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

u32 cpu_load32(Cpu* cpu, u32 addr) {
    return interconnect_load32(cpu->inter, addr);
}

u8 cpu_load8(Cpu* cpu, u32 addr) {
    return interconnect_load8(cpu->inter, addr);
}

void cpu_store32(Cpu* cpu, u32 addr, u32 val) {
    interconnect_store32(cpu->inter, addr, val);
}

void cpu_store16(Cpu* cpu, u32 addr, u16 val) {
    interconnect_store16(cpu->inter, addr, val);
}

void cpu_store8(Cpu* cpu, u32 addr, u8 val) {
    interconnect_store8(cpu->inter, addr, val);
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

    cpu->pc += offset;

    // 4 is added to pc in run_next isntruction, accounting for this
    cpu->pc -= 4;
}

void op_bxx(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 is_bgez = (instruction.value >> 16) & 1;
    u32 is_link = ((instruction.value >> 17) & 0xf) == 8;

    u32 is_negative = (i32)load_reg(cpu, s) < 0;
    u32 should_branch = is_negative ^ is_bgez;

    if (is_link) {
        set_reg(cpu, (RegisterIndex){31}, cpu->pc);
    }

    if (should_branch) {
        branch(cpu, i);
    }
}

void op_blez(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);

    i32 v = load_reg(cpu, s);

    if(v <= 0) {
        branch(cpu, i);
    }
}

void op_bgtz(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);

    i32 v = load_reg(cpu, s);

    if(v > 0) {
        branch(cpu, i);
    }
}

void op_bne(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);

    if(load_reg(cpu, s) != load_reg(cpu, t)) {
        branch(cpu, i);
    }
}

void op_beq(Cpu* cpu, Instruction instruction) {
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

void op_lw(Cpu* cpu, Instruction instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        //isolated cache, do not load memory
        fprintf(stderr, "Ignoring load while cache is isolated\n");
        return;
    }

    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 v = cpu_load32(cpu, addr);

    cpu->load = (PendingLoad){
        .reg = t,
        .value = v
    };
}

void op_lb(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 v = (u32)(i32)(i8)cpu_load8(cpu, addr);

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
    u8 v = cpu_load8(cpu, addr);

    cpu->load = (PendingLoad){
        .reg = t,
        .value = v
    };
}

void op_sw(Cpu* cpu, Instruction instruction) {

    if ((cpu->sr & 0x10000) != 0) {
        //isolated cache, do not write to memory
        fprintf(stderr, "Ignoring store while cache is isolated\n");
        return;
    }

    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u32 v = load_reg(cpu, t);

    cpu_store32(cpu, addr, v);
}

void op_sh(Cpu* cpu, Instruction instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        fprintf(stderr, "Ignoring store while cache is isolated\n");
        return;
    }

    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u16 v = (u16)load_reg(cpu, t);

    cpu_store16(cpu, addr, v);
}

void op_sb(Cpu* cpu, Instruction instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        fprintf(stderr, "Ignoring store while cache is isolated\n");
        return;
    }

    u32 i = instruction_imm_se(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex s = instruction_s(instruction);

    u32 addr = load_reg(cpu, s) + i;
    u8 v = (u8)load_reg(cpu, t);

    cpu_store8(cpu, addr, v);
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

void op_add(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);
    RegisterIndex t = instruction_t(instruction);
    RegisterIndex d = instruction_d(instruction);

    i32 v1 = (i32)load_reg(cpu, s);
    i32 v2 = (i32)load_reg(cpu, t);

    i32 result;

    bool overflow = __builtin_add_overflow(v1, v2, &result);
    if(overflow) {
        fprintf(stderr, "ADD overflow\n");
        exit(EXIT_FAILURE);
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
        fprintf(stderr, "ADDI overflow\n");
        exit(EXIT_FAILURE);
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

void op_mfhi(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    u32 v = cpu->hi;

    set_reg(cpu, d, v);
}

void op_mflo(Cpu* cpu, Instruction instruction) {
    RegisterIndex d = instruction_d(instruction);
    u32 v = cpu->lo;

    set_reg(cpu, d, v);
}

void op_j(Cpu* cpu, Instruction instruction) {
    u32 i = instruction_imm_jump(instruction);

    cpu->pc = (cpu->pc & 0xf0000000) | (i << 2);
}

void op_jal(Cpu* cpu, Instruction instruction) {
    u32 ra = cpu->pc;

    set_reg(cpu, (RegisterIndex){31}, ra);
    op_j(cpu, instruction);
}

void op_jr(Cpu* cpu, Instruction instruction) {
    RegisterIndex s = instruction_s(instruction);

    cpu->pc = load_reg(cpu, s);
}

void op_jalr(Cpu* cpu, Instruction instruction) {
    u32 ra = cpu->pc;
    RegisterIndex d = instruction_d(instruction);
    RegisterIndex s = instruction_s(instruction);

    set_reg(cpu, d, ra);

    cpu->pc = load_reg(cpu, s);
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
                fprintf(stderr, "Unhandled write to cop0 register: %08x\n", cop_r);
                exit(EXIT_FAILURE);
            }
            break;
        case 13:
            if(v != 0) {
                fprintf(stderr, "Unhandled write to CAUSE register: %08x\n", cop_r);
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "Unhandled cop0 register: %08x\n", cop_r);
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
            fprintf(stderr, "Unhandled read from CAUSE register: %08x\n", cop_r);
            exit(EXIT_FAILURE);
            break;
        default:
            fprintf(stderr, "Unhandled read from cop0 register: %08x\n", cop_r);
            exit(EXIT_FAILURE);
    }

    cpu->load = (PendingLoad) {cpu_r, v};
}

void op_cop0(Cpu* cpu, Instruction instruction) {
    switch(instruction_cop_opcode(instruction)){
        case 0b00000:
            op_mfc0(cpu, instruction);
            break;
        case 0b00100:
            op_mtc0(cpu, instruction);
            break;
        default:
            fprintf(stderr,
            "Error: unhandled cop0 instruction %08x\n",
            instruction.value);
            exit(EXIT_FAILURE);
    }
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
                case 0b100100:
                    op_and(cpu, instruction);
                    break;
                case 0b100101:
                    op_or(cpu, instruction);
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
                case 0b100011:
                    op_subu(cpu, instruction);
                    break;
                case 0b011010:
                    op_div(cpu, instruction);
                    break;
                case 0b011011:
                    op_divu(cpu, instruction);
                    break;
                case 0b010000:
                    op_mfhi(cpu, instruction);
                    break;
                case 0b010010:
                    op_mflo(cpu, instruction);
                    break;
                case 0b001000:
                    op_jr(cpu, instruction);
                    break;
                case 0b001001:
                    op_jalr(cpu, instruction);
                    break;
                default:
                    fprintf(stderr,
                    "Error: unhandled instruction %08x\n",
                    instruction.value);
                    exit(EXIT_FAILURE);
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
        case 0b001111:
            op_lui(cpu, instruction);
            break;
        case 0b001100:
            op_andi(cpu, instruction);
            break;
        case 0b001101:
            op_ori(cpu, instruction);
            break;

        default:
            fprintf(stderr,
                    "Error: unhandled instruction %08x\n",
                    instruction.value);
            exit(EXIT_FAILURE);
    }
}

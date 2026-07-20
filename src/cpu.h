#ifndef CPU_H
#define CPU_H

#include "interconnect.h"
#include "types.h"

typedef struct {
    Interconnect inter;
    u32 pc;
} Cpu;

void cpu_init(Cpu* cpu, Interconnect* inter);
void cpu_run_next_instruction(Cpu* cpu);
u32 cpu_load32(Cpu* cpu, u32 addr);
void cpu_decode_and_execute(Cpu* cpu, u32 instruction);

#endif

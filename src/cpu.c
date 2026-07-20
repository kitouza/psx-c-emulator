#include "cpu.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// initialize clean cpu
void cpu_init(Cpu* cpu, Interconnect* inter) {
    memset(cpu, 0, sizeof(Cpu)); // zero out cpu structure

    cpu->inter = *inter; // deep copy of inter to store in cpu struct
    cpu->pc = 0xbfc00000; // PC reset value at beginning of BIOS
}

void cpu_run_next_instruction(Cpu* cpu) {
    u32 instruction = cpu_load32(cpu, cpu->pc); // get current instruction

    cpu->pc += 4; // increment PC

    cpu_decode_and_execute(cpu, instruction); // execute current instruciton

}

u32 cpu_load32(Cpu* cpu, u32 addr) {
    return(interconnect_load32(&cpu->inter, addr));
}

void cpu_decode_and_execute(Cpu* cpu, u32 instruction) {
    (void)cpu;
    fprintf(stderr, "Error: unhandled instruction %08x\n", instruction);
    exit(EXIT_FAILURE);
}

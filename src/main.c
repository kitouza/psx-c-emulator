#include "types.h"
#include "cpu.h"

int main(void) {
    Bios bios;
    if (!bios_init(&bios, "roms/SCPH1001.bin")) {
        return 1;
    }

    Interconnect inter;
    interconnect_init(&inter, &bios);

    Cpu cpu;
    cpu_init(&cpu, &inter);

    while(true) {
        cpu_run_next_instruction(&cpu);
    }

    return 0;
}

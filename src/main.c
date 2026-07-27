#include "types.h"
#include "cpu.h"

int main(void) {
    Bios bios;
    if (!bios_init(&bios, "roms/SCPH1001.bin")) {
        return 1;
    }

    Interconnect inter;
    if (!interconnect_init(&inter, &bios)) {
        return 1;
    }

    Cpu cpu;
    cpu_init(&cpu, &inter);

    bool running = true;
    while (running) {
        // Process events regularly so macOS can display and update the window.
        for (u32 i = 0; i < 10000; ++i) {
            cpu_run_next_instruction(&cpu);
        }

        running = renderer_handle_events(&inter.gpu.renderer);
    }

    interconnect_destroy(&inter);
    return 0;
}

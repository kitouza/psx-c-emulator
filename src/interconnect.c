#include "interconnect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    u32 target_start;
    u32 target_size;

    // function pointer that handles read for this peripheral
    u32 (*load32_fn)(Interconnect*, u32 offset);
} MemoryRegion;

// helper wrapper functions matching function pointer signature
static u32 route_bios(Interconnect* inter, u32 offset) {
    return bios_load32(&inter->bios, offset);
}

// static u32 route_ram(Interconnect* inter, u32 offset) {
//     return ram(&inter->ram, offset);
// }

// memory map containing physical memory/peripheral offsets
static const MemoryRegion MEMORY_MAP[] = {

    {0xbfc00000, BIOS_SIZE, route_bios}

};

#define MEMORY_MAP_SIZE (sizeof(MEMORY_MAP) / sizeof(MEMORY_MAP[0]))

u32 interconnect_load32(Interconnect* inter, u32 addr) {
    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];

        if (addr >= region->target_start &&
            addr - region->target_start < region->target_size) {
            return region->load32_fn(inter, addr - region->target_start);
        }
    }

    fprintf(stderr, "Unhandled load32 address: 0x%08x\n", addr);
    exit(EXIT_FAILURE);
}

void interconnect_init(Interconnect* inter, const Bios* bios) {
    // clear interconnect
    memset(inter, 0, sizeof(Interconnect));

    // deep copy bios into interconnect
    inter->bios = *bios;
}

#ifndef INTERCONNECT_H
#define INTERCONNECT_H

#include "bios.h"
#include "ram.h"
#include "dma.h"
#include "gpu.h"
#include "types.h"

typedef struct {
    Bios bios;
    Ram ram;
    Dma dma;
    Gpu gpu;
} Interconnect;

bool interconnect_init(Interconnect* inter, const Bios* bios);
void interconnect_destroy(Interconnect* inter);
u32 mask_region(u32 addr);
u32 interconnect_load(Interconnect* inter, u32 addr, AccessWidth width);
void interconnect_store(Interconnect* inter,
                        u32 addr,
                        u32 val,
                        AccessWidth width);

#endif

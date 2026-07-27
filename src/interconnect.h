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

void interconnect_init(Interconnect* inter, const Bios* bios);
u32 mask_region(u32 addr);
u8 interconnect_load8(Interconnect* inter, u32 addr);
u16 interconnect_load16(Interconnect* inter, u32 addr);
u32 interconnect_load32(Interconnect* inter, u32 addr);
void interconnect_store32(Interconnect* inter, u32 addr, u32 val);
void interconnect_store16(Interconnect* inter, u32 addr, u16 val);
void interconnect_store8(Interconnect* inter, u32 addr, u8 val);

#endif

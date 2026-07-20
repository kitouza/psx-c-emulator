#ifndef INTERCONNECT_H
#define INTERCONNECT_H

#include "bios.h"
#include "types.h"

typedef struct {
    Bios bios;
} Interconnect;

void interconnect_init(Interconnect* inter, const Bios* bios);
u32 interconnect_load32(Interconnect* inter, u32 addr);

#endif

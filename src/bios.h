#ifndef BIOS_H
#define BIOS_H

// BIOS image size of 512 KB
#define BIOS_SIZE (512 * 1024)

#include "types.h"

typedef struct {
    u8 data[BIOS_SIZE];
} Bios;

bool bios_init(Bios* bios, const char* filename);
u32 bios_load32(const Bios* bios, u32 offset);

#endif

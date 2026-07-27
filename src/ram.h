#ifndef RAM_H
#define RAM_H

// BIOS image size of 512 KB
#define RAM_SIZE (2 * 1024 * 1024)

#include "types.h"

typedef struct {
    u8 data[RAM_SIZE];
} Ram;

bool ram_init(Ram* ram);
u32 ram_load(const Ram* ram, u32 offset, AccessWidth width);
void ram_store(Ram* ram, u32 offset, u32 val, AccessWidth width);


#endif

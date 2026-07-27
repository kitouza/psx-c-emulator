#ifndef RAM_H
#define RAM_H

// BIOS image size of 512 KB
#define RAM_SIZE (2 * 1024 * 1024)

#include "types.h"

typedef struct {
    u8 data[RAM_SIZE];
} Ram;

bool ram_init(Ram* ram);
u8 ram_load8(const Ram* ram, u32 offset);
u16 ram_load16(const Ram* ram, u32 offset);
u32 ram_load32(const Ram* ram, u32 offset);
void ram_store8(Ram* ram, u32 offset, u8 val);
void ram_store16(Ram* ram, u32 offset, u16 val);
void ram_store32(Ram* ram, u32 offset, u32 val);


#endif

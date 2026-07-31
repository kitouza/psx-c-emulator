#include "ram.h"
#include <stdio.h>

bool ram_init(Ram* ram) {
    (void)ram;

    // ram contents garbage by default
    return true;

}

u32 ram_load(const Ram* ram, u32 offset, AccessWidth width) {
    u32 val = 0;
    for (u32 i = 0; i < (u32)width; ++i) {
        val |= (u32)ram->data[offset + i] << (i * 8);
    }
    return val;
}

void ram_store(Ram* ram, u32 offset, u32 val, AccessWidth width) {
    for (u32 i = 0; i < (u32)width; ++i) {
        ram->data[offset + i] = val >> (i * 8);
    }
}

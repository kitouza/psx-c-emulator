#include "ram.h"
#include <stdio.h>

bool ram_init(Ram* ram) {
    (void)ram;

    // ram contents garbage by default
    return true;

}

u8 ram_load8(const Ram* ram, u32 offset) {
    return ram->data[offset];
}

u32 ram_load32(const Ram* ram, u32 offset) {

    // Grab 4 individual bytes from data array
    u8 b0 = ram->data[offset + 0];
    u8 b1 = ram->data[offset + 1];
    u8 b2 = ram->data[offset + 2];
    u8 b3 = ram->data[offset + 3];

    // Arrange into 32 bit little-endian word

    return (u32)b0 | ((u32)b1 << 8) | ((u32)b2 << 16) | ((u32)b3 << 24);

}

void ram_store32(Ram* ram, u32 offset, u32 val) {

    // Grab 4 individual bytes from val
    u8 b0 = val;
    u8 b1 = val >> 8;
    u8 b2 = val >> 16;
    u8 b3 = val >> 24;


    ram->data[offset + 0] = b0;
    ram->data[offset + 1] = b1;
    ram->data[offset + 2] = b2;
    ram->data[offset + 3] = b3;

}

void ram_store8(Ram* ram, u32 offset, u8 val) {
    ram->data[offset] = val;
}

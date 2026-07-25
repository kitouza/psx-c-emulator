#include "bios.h"
#include <stdio.h>

bool bios_init(Bios* bios, const char* filename) {
    FILE* file = fopen(filename, "rb");
    if(!file) {
        fprintf(stderr, "Error, could not open BIOS file %s\n", filename);
        return false;
    }

    // read BIOS_SIZE bytes into data array
    size_t bytes_read = fread(bios->data, 1, BIOS_SIZE, file);
    fclose(file);

    if(bytes_read != BIOS_SIZE) {
        fprintf(stderr, "Error: Invalid BIOS size; expected %d bytes, received %zu bytes\n",
                BIOS_SIZE, bytes_read);
        return false;
    }

    return true;

}

u8 bios_load8(const Bios* bios, u32 offset) {
    return bios->data[offset];
}

u32 bios_load32(const Bios* bios, u32 offset) {

    // Grab 4 individual bytes from data array
    u8 b0 = bios->data[offset + 0];
    u8 b1 = bios->data[offset + 1];
    u8 b2 = bios->data[offset + 2];
    u8 b3 = bios->data[offset + 3];

    // Arrange into 32 bit little-endian word

    return (u32)b0 | ((u32)b1 << 8) | ((u32)b2 << 16) | ((u32)b3 << 24);

}

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

u32 bios_load(const Bios* bios, u32 offset, AccessWidth width) {
    u32 val = 0;
    for (u32 i = 0; i < (u32)width; ++i) {
        val |= (u32)bios->data[offset + i] << (i * 8);
    }
    return val;
}

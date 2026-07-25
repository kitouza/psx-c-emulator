#include "interconnect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef u32 (*Load32Fn)(Interconnect* inter, u32 offset);
typedef u8 (*Load8Fn)(Interconnect* inter, u32 offset);
typedef void (*Store32Fn)(Interconnect* inter, u32 offset, u32 val);
typedef void (*Store16Fn)(Interconnect* inter, u32 offset, u16 val);
typedef void (*Store8Fn)(Interconnect* inter, u32 offset, u8 val);

typedef struct {
    u32 start;
    u32 size;
    Load32Fn load32;
    Load8Fn load8;
    Store32Fn store32;
    Store16Fn store16;
    Store8Fn store8;
} MemoryRegion;

static const u32 REGION_MASK[8] = {
    0xffffffff, // KUSEG
    0xffffffff, // KUSEG
    0xffffffff, // KUSEG
    0xffffffff, // KUSEG
    0x7fffffff, // KSEG0: cached mirror
    0x1fffffff, // KSEG1: uncached mirror
    0xffffffff, // KSEG2
    0xffffffff  // KSEG2
};

u32 mask_region(u32 addr) {
    u32 region = addr >> 29;
    return addr & REGION_MASK[region];
}

static bool memory_region_contains(const MemoryRegion* region,
                                   u32 addr,
                                   u32* offset) {
    if (addr >= region->start && addr - region->start < region->size) {
        *offset = addr - region->start;
        return true;
    }

    return false;
}

static u32 route_bios_load32(Interconnect* inter, u32 offset) {
    return bios_load32(&inter->bios, offset);
}

static u8 route_bios_load8(Interconnect* inter, u32 offset) {
    return bios_load8(&inter->bios, offset);
}

static void route_memcontrol_store32(Interconnect* inter,
                                     u32 offset,
                                     u32 val) {
    (void)inter;

    switch (offset) {
        case 0:
            if (val != 0x1f000000) {
                fprintf(stderr,
                        "Bad expansion 1 base address: 0x%08x\n",
                        val);
                exit(EXIT_FAILURE);
            }
            break;

        case 4:
            if (val != 0x1f802000) {
                fprintf(stderr,
                        "Bad expansion 2 base address: 0x%08x\n",
                        val);
                exit(EXIT_FAILURE);
            }
            break;

        default:
            fprintf(stderr,
                    "Ignoring MEMCONTROL write at offset 0x%08x\n",
                    offset);
            break;
    }
}

static void route_ram_store32(Interconnect* inter,
                              u32 offset,
                              u32 val) {
    ram_store32(&inter->ram, offset, val);
}

static u32 route_ram_load32(Interconnect* inter,
                            u32 offset) {
    return ram_load32(&inter->ram, offset);
}

static u8 route_ram_load8(Interconnect* inter, u32 offset) {
    return ram_load8(&inter->ram, offset);
}

static void route_ram_store8(Interconnect* inter, u32 offset, u8 val) {
    ram_store8(&inter->ram, offset, val);
}

static void route_ramsize_store32(Interconnect* inter,
                                  u32 offset,
                                  u32 val) {
    (void)inter;
    (void)offset;
    (void)val;
}

static u32 route_irqcontrol_load32(Interconnect* inter,
                                     u32 offset) {
    (void)inter;
    printf("IRQ control read at offset 0x%08x",
            offset);
    return 0;
}

static void route_irqcontrol_store32(Interconnect* inter,
                                     u32 offset,
                                     u32 val) {
    (void)inter;
    fprintf(stderr,
            "Ignoring write to IRQ control register at offset 0x%x: 0x%08x\n",
            offset,
            val);
}

static void route_cachecontrol_store32(Interconnect* inter,
                                       u32 offset,
                                       u32 val) {
    (void)inter;
    (void)offset;
    (void)val;
    fprintf(stderr, "Ignoring write to CACHECONTROL\n");
}

static void route_spu_store16(Interconnect* inter,
                              u32 offset,
                              u16 val) {
    (void)inter;
    fprintf(stderr,
            "Ignoring write to SPU register at offset 0x%03x: 0x%04x\n",
            offset,
            val);
}

static void route_timers_store16(Interconnect* inter,
                                 u32 offset,
                                 u16 val) {
    (void)inter;
    fprintf(stderr,
            "Ignoring write to timer register at offset 0x%02x: 0x%04x\n",
            offset,
            val);
}

static u8 route_expansion_1_load8(Interconnect* inter, u32 offset) {
    (void)inter;
    (void)offset;
    // expansion not implemented
    return 0xff;
}

static void route_expansion_2_store8(Interconnect* inter, u32 offset, u8 val) {
    (void)inter;
    fprintf(stderr,
            "Ignoring write to expansion 2 memory map at offset 0x%03x: 0x%04x\n",
            offset,
            val);
}

static const MemoryRegion MEMORY_MAP[] = {
    {
        .start = 0x1fc00000,
        .size = BIOS_SIZE,
        .load32 = route_bios_load32,
        .load8 = route_bios_load8,
        .store32 = NULL
    },
    {
        .start = 0x1f801000,
        .size = 36,
        .load32 = NULL,
        .store32 = route_memcontrol_store32
    },
    {
        .start = 0x00000000,
        .size = RAM_SIZE,
        .load32 = route_ram_load32,
        .load8 = route_ram_load8,
        .store32 = route_ram_store32,
        .store8 = route_ram_store8
    },
    {
        .start = 0x1f801060,
        .size = 4,
        .load32 = NULL,
        .store32 = route_ramsize_store32
    },
    {
        .start = 0x1f801070,
        .size = 8,
        .load32 = route_irqcontrol_load32,
        .store32 = route_irqcontrol_store32
    },
    {
        .start = 0xfffe0130,
        .size = 4,
        .load32 = NULL,
        .store32 = route_cachecontrol_store32
    },
    {
        .start = 0x1f801c00,
        .size = 640,
        .load32 = NULL,
        .store32 = NULL,
        .store16 = route_spu_store16
    },
    {
        .start = 0x1f801100,
        .size = 0x30,
        .store16 = route_timers_store16
    },
    {
        .start = 0x1f000000,
        .size = 512 * 1024,
        .load8 = route_expansion_1_load8
    },
    {
        .start = 0x1f802000,
        .size = 66,
        .store8 = route_expansion_2_store8
    }
};

#define MEMORY_MAP_SIZE (sizeof(MEMORY_MAP) / sizeof(MEMORY_MAP[0]))

void interconnect_init(Interconnect* inter, const Bios* bios) {
    memset(inter, 0, sizeof(Interconnect));
    inter->bios = *bios;
}

u32 interconnect_load32(Interconnect* inter, u32 addr) {
    if (addr % 4 != 0) {
        fprintf(stderr, "Unaligned load32 address: 0x%08x\n", addr);
        exit(EXIT_FAILURE);
    }

    u32 physical_addr = mask_region(addr);

    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];
        u32 offset;

        if (memory_region_contains(region, physical_addr, &offset)) {
            if (region->load32 == NULL) {
                fprintf(stderr,
                        "Region at 0x%08x does not support load32\n",
                        region->start);
                exit(EXIT_FAILURE);
            }

            return region->load32(inter, offset);
        }
    }

    fprintf(stderr,
            "Unhandled load32 address: 0x%08x (physical: 0x%08x)\n",
            addr,
            physical_addr);
    exit(EXIT_FAILURE);
}

void interconnect_store32(Interconnect* inter, u32 addr, u32 val) {
    if (addr % 4 != 0) {
        fprintf(stderr, "Unaligned store32 address: 0x%08x\n", addr);
        exit(EXIT_FAILURE);
    }

    u32 physical_addr = mask_region(addr);

    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];
        u32 offset;

        if (memory_region_contains(region, physical_addr, &offset)) {
            if (region->store32 == NULL) {
                fprintf(stderr,
                        "Region at 0x%08x does not support store32\n",
                        region->start);
                exit(EXIT_FAILURE);
            }

            region->store32(inter, offset, val);
            return;
        }
    }

    fprintf(stderr,
            "Unhandled store32 address: 0x%08x (physical: 0x%08x)\n",
            addr,
            physical_addr);
    exit(EXIT_FAILURE);
}

void interconnect_store16(Interconnect* inter, u32 addr, u16 val) {
    if (addr % 2 != 0) {
        fprintf(stderr, "Unaligned store16 address: 0x%08x\n", addr);
        exit(EXIT_FAILURE);
    }

    u32 physical_addr = mask_region(addr);

    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];
        u32 offset;

        if (memory_region_contains(region, physical_addr, &offset)) {
            if (region->store16 == NULL) {
                fprintf(stderr,
                        "Region at 0x%08x does not support store16\n",
                        region->start);
                exit(EXIT_FAILURE);
            }

            region->store16(inter, offset, val);
            return;
        }
    }

    fprintf(stderr,
            "Unhandled store16 address: 0x%08x\n",
            addr);
    exit(EXIT_FAILURE);
}

void interconnect_store8(Interconnect* inter, u32 addr, u8 val) {
    u32 physical_addr = mask_region(addr);

    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];
        u32 offset;

        if (memory_region_contains(region, physical_addr, &offset)) {
            if (region->store8 == NULL) {
                fprintf(stderr,
                        "Region at 0x%08x does not support store8\n",
                        region->start);
                exit(EXIT_FAILURE);
            }

            region->store8(inter, offset, val);
            return;
        }
    }

    fprintf(stderr,
            "Unhandled store8 address: 0x%08x (physical: 0x%08x)\n",
            addr,
            physical_addr);
    exit(EXIT_FAILURE);
}

u8 interconnect_load8(Interconnect* inter, u32 addr) {
    u32 physical_addr = mask_region(addr);

    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];
        u32 offset;

        if (memory_region_contains(region, physical_addr, &offset)) {
            if (region->load8 == NULL) {
                fprintf(stderr,
                        "Region at 0x%08x does not support load8\n",
                        region->start);
                exit(EXIT_FAILURE);
            }

            return region->load8(inter, offset);
        }
    }

    fprintf(stderr,
            "Unhandled load8 address: 0x%08x (physical: 0x%08x)\n",
            addr,
            physical_addr);
    exit(EXIT_FAILURE);
}

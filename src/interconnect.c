#include "interconnect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef u32 (*Load32Fn)(Interconnect* inter, u32 offset);
typedef u16 (*Load16Fn)(Interconnect* inter, u32 offset);
typedef u8 (*Load8Fn)(Interconnect* inter, u32 offset);
typedef void (*Store32Fn)(Interconnect* inter, u32 offset, u32 val);
typedef void (*Store16Fn)(Interconnect* inter, u32 offset, u16 val);
typedef void (*Store8Fn)(Interconnect* inter, u32 offset, u8 val);

typedef struct {
    u32 start;
    u32 size;
    Load32Fn load32;
    Load16Fn load16;
    Load8Fn load8;
    Store32Fn store32;
    Store16Fn store16;
    Store8Fn store8;
} MemoryRegion;

static void route_gpu_store32(Interconnect* inter, u32 offset, u32 val);

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

static void route_ram_store16(Interconnect* inter,
                              u32 offset,
                              u16 val) {
    ram_store16(&inter->ram, offset, val);
}

static u32 route_ram_load32(Interconnect* inter,
                            u32 offset) {
    return ram_load32(&inter->ram, offset);
}

static u16 route_ram_load16(Interconnect* inter, u32 offset) {
    return ram_load16(&inter->ram, offset);
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
    fprintf(stderr, "IRQ control read at offset 0x%08x",
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

static u16 route_irqcontrol_load16(Interconnect* inter, u32 offset) {
    (void)inter;
    fprintf(stderr, "IRQ control read at offset 0x%x\n", offset);
    return 0;
}

static void route_irqcontrol_store16(Interconnect* inter,
                                     u32 offset,
                                     u16 val) {
    (void)inter;
    fprintf(stderr,
            "Ignoring write to IRQ control register at offset 0x%x: 0x%04x\n",
            offset,
            val);
}

static void interconnect_do_dma_block(Interconnect* inter, DmaPort port) {
    // Snapshot the configuration so RAM writes cannot invalidate our state.
    DmaChannel channel = *dma_channel(&inter->dma, port);
    u32 words;

    if (!dma_channel_transfer_size(&channel, &words)) {
        fprintf(stderr, "DMA block transfer has no fixed size\n");
        exit(EXIT_FAILURE);
    }

    bool gpu_upload = port == DMA_PORT_GPU
        && channel.direction == DMA_FROM_RAM;
    bool otc_clear = port == DMA_PORT_OTC
        && channel.direction == DMA_TO_RAM;

    if (!gpu_upload && !otc_clear) {
        fprintf(stderr,
                "Unsupported DMA block transfer on port %u in direction %u\n",
                (u32)port,
                (u32)channel.direction);
        exit(EXIT_FAILURE);
    }

    u32 addr = channel.base;
    u32 increment = channel.step == DMA_INCREMENT ? 4 : (u32)-4;

    while (words > 0) {
        // DMA RAM accesses wrap within 2 MiB and ignore the low alignment bits.
        u32 ram_addr = addr & 0x001ffffc;

        if (gpu_upload) {
            // A block sent through GPU channel 2 is a stream of GP0 commands.
            route_gpu_store32(inter, 0, ram_load32(&inter->ram, ram_addr));
        } else {
            u32 word = words == 1
                ? 0x00ffffff
                : (addr - 4) & 0x001fffff;
            ram_store32(&inter->ram, ram_addr, word);
        }

        addr += increment;
        --words;
    }

    dma_channel_done(dma_channel_mut(&inter->dma, port));
}

static void interconnect_do_dma_linked_list(Interconnect* inter,
                                            DmaPort port) {
    DmaChannel channel = *dma_channel(&inter->dma, port);

    if (port != DMA_PORT_GPU || channel.direction != DMA_FROM_RAM) {
        fprintf(stderr,
                "Unsupported linked-list DMA on port %u in direction %u\n",
                (u32)port,
                (u32)channel.direction);
        exit(EXIT_FAILURE);
    }

    u32 addr = channel.base & 0x001ffffc;
    u32 nodes = 0;

    for (;;) {
        u32 header = ram_load32(&inter->ram, addr);
        u32 words = header >> 24;

        while (words > 0) {
            addr = (addr + 4) & 0x001ffffc;
            route_gpu_store32(inter, 0, ram_load32(&inter->ram, addr));
            --words;
        }

        // Bit 23 terminates the list; otherwise bits 0-23 hold the next node.
        if ((header & 0x00800000) != 0) {
            break;
        }

        addr = header & 0x001ffffc;

        // A valid list cannot contain more distinct headers than RAM has words.
        if (++nodes >= RAM_SIZE / sizeof(u32)) {
            fprintf(stderr, "GPU DMA linked list does not terminate\n");
            exit(EXIT_FAILURE);
        }
    }

    dma_channel_done(dma_channel_mut(&inter->dma, port));
}

static void interconnect_do_dma(Interconnect* inter, DmaPort port) {
    const DmaChannel* channel = dma_channel(&inter->dma, port);

    if (channel->sync == DMA_SYNC_LINKED_LIST) {
        interconnect_do_dma_linked_list(inter, port);
        return;
    }

    interconnect_do_dma_block(inter, port);
}

static u32 route_dma_load32(Interconnect* inter, u32 offset) {
    u32 major = (offset & 0x70) >> 4;
    u32 minor = offset & 0xf;

    if (major <= 6) {
        const DmaChannel* channel = dma_channel(&inter->dma,
                                                (DmaPort)major);
        switch (minor) {
            case 0: return dma_channel_base(channel);
            case 4: return dma_channel_block_control(channel);
            case 8: return dma_channel_control(channel);
        }
    } else if (major == 7) {
        switch (minor) {
            case 0: return dma_control(&inter->dma);
            case 4: return dma_interrupt(&inter->dma);
        }
    }

    fprintf(stderr, "Unhandled DMA read at offset 0x%02x\n", offset);
    exit(EXIT_FAILURE);
}

static void route_dma_store32(Interconnect* inter,
                              u32 offset,
                              u32 val) {
    u32 major = (offset & 0x70) >> 4;
    u32 minor = offset & 0xf;

    if (major <= 6) {
        DmaPort port = (DmaPort)major;
        DmaChannel* channel = dma_channel_mut(&inter->dma,
                                              port);
        switch (minor) {
            case 0:
                dma_channel_set_base(channel, val);
                break;
            case 4:
                dma_channel_set_block_control(channel, val);
                break;
            case 8:
                if (!dma_channel_set_control(channel, val)) {
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                fprintf(stderr,
                        "Unhandled DMA write at offset 0x%02x: 0x%08x\n",
                        offset,
                        val);
                exit(EXIT_FAILURE);
        }

        if (dma_channel_active(channel)) {
            interconnect_do_dma(inter, port);
        }

        return;
    } else if (major == 7) {
        switch (minor) {
            case 0:
                dma_set_control(&inter->dma, val);
                return;
            case 4:
                dma_set_interrupt(&inter->dma, val);
                return;
        }
    }

    fprintf(stderr,
            "Unhandled DMA write at offset 0x%02x: 0x%08x\n",
            offset,
            val);
    exit(EXIT_FAILURE);
}

static u32 route_gpu_load32(Interconnect* inter, u32 offset) {
    fprintf(stderr, "GPU read at offset 0x%x\n", offset);
    switch (offset) {
        case 0:
            return gpu_read(&inter->gpu);
        case 4:
            return gpu_status(&inter->gpu);
        default:
            fprintf(stderr, "Unhandled GPU read offset: 0x%x\n", offset);
            exit(EXIT_FAILURE);
    }
}

static void route_gpu_store32(Interconnect* inter,
                              u32 offset,
                              u32 val) {
    fprintf(stderr,
            "GPU write at offset 0x%x: 0x%08x\n",
            offset,
            val);

    switch (offset) {
        case 0:
            if (!gpu_gp0(&inter->gpu, val)) {
                exit(EXIT_FAILURE);
            }
            return;
        case 4:
            if (!gpu_gp1(&inter->gpu, val)) {
                exit(EXIT_FAILURE);
            }
            return;
        default:
            fprintf(stderr, "Unhandled GPU write offset: 0x%x\n", offset);
            exit(EXIT_FAILURE);
    }
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

static u16 route_spu_load16(Interconnect* inter, u32 offset) {
    (void)inter;
    fprintf(stderr,
            "Ignoring read from SPU register at offset 0x%03x\n",
            offset);
    return 0;
}

static u32 route_timers_load32(Interconnect* inter, u32 offset) {
    (void)inter;
    fprintf(stderr,
            "Ignoring read from timer register at offset 0x%02x\n",
            offset);
    return 0;
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

static void route_timers_store32(Interconnect* inter,
                                 u32 offset,
                                 u32 val) {
    (void)inter;
    fprintf(stderr,
            "Ignoring write to timer register at offset 0x%02x: 0x%08x\n",
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
        .load16 = route_ram_load16,
        .load8 = route_ram_load8,
        .store32 = route_ram_store32,
        .store16 = route_ram_store16,
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
        .load16 = route_irqcontrol_load16,
        .store32 = route_irqcontrol_store32,
        .store16 = route_irqcontrol_store16
    },
    {
        .start = 0x1f801080,
        .size = 0x80,
        .load32 = route_dma_load32,
        .store32 = route_dma_store32
    },
    {
        .start = 0x1f801810,
        .size = 8,
        .load32 = route_gpu_load32,
        .store32 = route_gpu_store32
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
        .load16 = route_spu_load16,
        .store32 = NULL,
        .store16 = route_spu_store16
    },
    {
        .start = 0x1f801100,
        .size = 0x30,
        .load32 = route_timers_load32,
        .store32 = route_timers_store32,
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
    ram_init(&inter->ram);
    dma_init(&inter->dma);
    gpu_init(&inter->gpu);
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

u16 interconnect_load16(Interconnect* inter, u32 addr) {
    if (addr % 2 != 0) {
        fprintf(stderr, "Unaligned load16 address: 0x%08x\n", addr);
        exit(EXIT_FAILURE);
    }

    u32 physical_addr = mask_region(addr);

    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];
        u32 offset;

        if (memory_region_contains(region, physical_addr, &offset)) {
            if (region->load16 == NULL) {
                fprintf(stderr,
                        "Region at 0x%08x does not support load16\n",
                        region->start);
                exit(EXIT_FAILURE);
            }

            return region->load16(inter, offset);
        }
    }

    fprintf(stderr,
            "Unhandled load16 address: 0x%08x (physical: 0x%08x)\n",
            addr,
            physical_addr);
    exit(EXIT_FAILURE);
}

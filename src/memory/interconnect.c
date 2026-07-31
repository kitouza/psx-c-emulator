#include "interconnect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef u32 (*LoadFn)(Interconnect* inter, u32 offset, AccessWidth width);
typedef void (*StoreFn)(Interconnect* inter,
                        u32 offset,
                        u32 val,
                        AccessWidth width);

typedef struct {
    u32 start;
    u32 size;
    LoadFn load;
    StoreFn store;
} MemoryRegion;

static void route_gpu_store(Interconnect* inter,
                            u32 offset,
                            u32 val,
                            AccessWidth width);

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
                                   AccessWidth width,
                                   u32* offset) {
    if (addr >= region->start
        && addr - region->start <= region->size - (u32)width) {
        *offset = addr - region->start;
        return true;
    }

    return false;
}

static u32 route_bios_load(Interconnect* inter,
                           u32 offset,
                           AccessWidth width) {
    return bios_load(&inter->bios, offset, width);
}

static void route_memcontrol_store(Interconnect* inter,
                                   u32 offset,
                                   u32 val,
                                   AccessWidth width) {
    (void)inter;

    if (width != ACCESS_WORD) {
        fprintf(stderr, "MEMCONTROL does not support %s stores\n",
                access_width_name(width));
        exit(EXIT_FAILURE);
    }

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

static u32 route_ram_load(Interconnect* inter,
                          u32 offset,
                          AccessWidth width) {
    return ram_load(&inter->ram, offset, width);
}

static void route_ram_store(Interconnect* inter,
                            u32 offset,
                            u32 val,
                            AccessWidth width) {
    ram_store(&inter->ram, offset, val, width);
}

static void route_ramsize_store(Interconnect* inter,
                                u32 offset,
                                u32 val,
                                AccessWidth width) {
    (void)inter;
    (void)offset;
    (void)val;
    (void)width;
}

static u32 route_irqcontrol_load(Interconnect* inter,
                                 u32 offset,
                                 AccessWidth width) {
    (void)inter;
    fprintf(stderr, "IRQ control %s read at offset 0x%08x\n",
            access_width_name(width), offset);
    return 0;
}

static void route_irqcontrol_store(Interconnect* inter,
                                   u32 offset,
                                   u32 val,
                                   AccessWidth width) {
    (void)inter;
    fprintf(stderr,
            "Ignoring %s write to IRQ control register at offset 0x%x: "
            "0x%08x\n",
            access_width_name(width),
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
            route_gpu_store(inter,
                            0,
                            ram_load(&inter->ram, ram_addr, ACCESS_WORD),
                            ACCESS_WORD);
        } else {
            u32 word = words == 1
                ? 0x00ffffff
                : (addr - 4) & 0x001fffff;
            ram_store(&inter->ram, ram_addr, word, ACCESS_WORD);
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
        u32 header = ram_load(&inter->ram, addr, ACCESS_WORD);
        u32 words = header >> 24;

        while (words > 0) {
            addr = (addr + 4) & 0x001ffffc;
            route_gpu_store(inter,
                            0,
                            ram_load(&inter->ram, addr, ACCESS_WORD),
                            ACCESS_WORD);
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

static u32 route_dma_load(Interconnect* inter,
                          u32 offset,
                          AccessWidth width) {
    if (width != ACCESS_WORD) {
        fprintf(stderr, "DMA does not support %s loads\n",
                access_width_name(width));
        exit(EXIT_FAILURE);
    }

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

static void route_dma_store(Interconnect* inter,
                            u32 offset,
                            u32 val,
                            AccessWidth width) {
    if (width != ACCESS_WORD) {
        fprintf(stderr, "DMA does not support %s stores\n",
                access_width_name(width));
        exit(EXIT_FAILURE);
    }

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

static u32 route_gpu_load(Interconnect* inter,
                          u32 offset,
                          AccessWidth width) {
    if (width != ACCESS_WORD) {
        fprintf(stderr, "GPU does not support %s loads\n",
                access_width_name(width));
        exit(EXIT_FAILURE);
    }

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

static void route_gpu_store(Interconnect* inter,
                            u32 offset,
                            u32 val,
                            AccessWidth width) {
    if (width != ACCESS_WORD) {
        fprintf(stderr, "GPU does not support %s stores\n",
                access_width_name(width));
        exit(EXIT_FAILURE);
    }

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

static u32 route_cdrom_load(Interconnect* inter,
                            u32 offset,
                            AccessWidth width) {
    (void)inter;

    if (width != ACCESS_BYTE) {
        fprintf(stderr, "CD-ROM does not support %s loads\n",
                access_width_name(width));
        exit(EXIT_FAILURE);
    }

    // Temporary CD-ROM placeholder. Bits 3 and 4 report an empty parameter
    // FIFO that is ready to accept another byte.
    if (offset == 0) {
        return 0x18;
    }

    fprintf(stderr,
            "Ignoring read from CD-ROM register at offset 0x%x\n",
            offset);
    return 0;
}

static void route_cdrom_store(Interconnect* inter,
                              u32 offset,
                              u32 val,
                              AccessWidth width) {
    (void)inter;
    if (width != ACCESS_BYTE) {
        fprintf(stderr, "CD-ROM does not support %s stores\n",
                access_width_name(width));
        exit(EXIT_FAILURE);
    }
    fprintf(stderr,
            "Ignoring write to CD-ROM register at offset 0x%x: 0x%02x\n",
            offset,
            val);
}

static void route_cachecontrol_store(Interconnect* inter,
                                     u32 offset,
                                     u32 val,
                                     AccessWidth width) {
    (void)inter;
    (void)offset;
    (void)val;
    if (width != ACCESS_WORD) {
        fprintf(stderr, "CACHECONTROL does not support %s stores\n",
                access_width_name(width));
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Ignoring write to CACHECONTROL\n");
}

static void route_spu_store(Interconnect* inter,
                            u32 offset,
                            u32 val,
                            AccessWidth width) {
    (void)inter;
    fprintf(stderr,
            "Ignoring %s write to SPU register at offset 0x%03x: 0x%08x\n",
            access_width_name(width),
            offset,
            val);
}

static u32 route_spu_load(Interconnect* inter,
                          u32 offset,
                          AccessWidth width) {
    (void)inter;
    fprintf(stderr,
            "Ignoring %s read from SPU register at offset 0x%03x\n",
            access_width_name(width),
            offset);
    return 0;
}

static u32 route_timers_load(Interconnect* inter,
                             u32 offset,
                             AccessWidth width) {
    (void)inter;
    fprintf(stderr,
            "Ignoring %s read from timer register at offset 0x%02x\n",
            access_width_name(width),
            offset);
    return 0;
}

static void route_timers_store(Interconnect* inter,
                               u32 offset,
                               u32 val,
                               AccessWidth width) {
    (void)inter;
    fprintf(stderr,
            "Ignoring %s write to timer register at offset 0x%02x: 0x%08x\n",
            access_width_name(width),
            offset,
            val);
}

static u32 route_expansion_1_load(Interconnect* inter,
                                  u32 offset,
                                  AccessWidth width) {
    (void)inter;
    (void)offset;
    (void)width;
    // expansion not implemented
    return 0xffffffff;
}

static void route_expansion_2_store(Interconnect* inter,
                                    u32 offset,
                                    u32 val,
                                    AccessWidth width) {
    (void)inter;
    fprintf(stderr,
            "Ignoring %s write to expansion 2 memory map at offset 0x%03x: "
            "0x%08x\n",
            access_width_name(width),
            offset,
            val);
}

static const MemoryRegion MEMORY_MAP[] = {
    {
        .start = 0x1fc00000,
        .size = BIOS_SIZE,
        .load = route_bios_load
    },
    {
        .start = 0x1f801000,
        .size = 36,
        .store = route_memcontrol_store
    },
    {
        .start = 0x00000000,
        .size = RAM_SIZE,
        .load = route_ram_load,
        .store = route_ram_store
    },
    {
        .start = 0x1f801060,
        .size = 4,
        .store = route_ramsize_store
    },
    {
        .start = 0x1f801070,
        .size = 8,
        .load = route_irqcontrol_load,
        .store = route_irqcontrol_store
    },
    {
        .start = 0x1f801080,
        .size = 0x80,
        .load = route_dma_load,
        .store = route_dma_store
    },
    {
        .start = 0x1f801810,
        .size = 8,
        .load = route_gpu_load,
        .store = route_gpu_store
    },
    {
        .start = 0x1f801800,
        .size = 4,
        .load = route_cdrom_load,
        .store = route_cdrom_store
    },
    {
        .start = 0xfffe0130,
        .size = 4,
        .store = route_cachecontrol_store
    },
    {
        .start = 0x1f801c00,
        .size = 640,
        .load = route_spu_load,
        .store = route_spu_store
    },
    {
        .start = 0x1f801100,
        .size = 0x30,
        .load = route_timers_load,
        .store = route_timers_store
    },
    {
        .start = 0x1f000000,
        .size = 512 * 1024,
        .load = route_expansion_1_load
    },
    {
        .start = 0x1f802000,
        .size = 66,
        .store = route_expansion_2_store
    }
};

#define MEMORY_MAP_SIZE (sizeof(MEMORY_MAP) / sizeof(MEMORY_MAP[0]))

bool interconnect_init(Interconnect* inter, const Bios* bios) {
    memset(inter, 0, sizeof(Interconnect));
    inter->bios = *bios;
    ram_init(&inter->ram);
    dma_init(&inter->dma);
    return gpu_init(&inter->gpu);
}

void interconnect_destroy(Interconnect* inter) {
    gpu_destroy(&inter->gpu);
}

static bool examine_dma(const Dma* dma, u32 offset, u32* value) {
    u32 major = (offset & 0x70) >> 4;
    u32 minor = offset & 0xf;

    if (major <= 6) {
        const DmaChannel* channel = dma_channel(dma, (DmaPort)major);
        switch (minor & ~3u) {
            case 0: *value = dma_channel_base(channel); return true;
            case 4: *value = dma_channel_block_control(channel); return true;
            case 8: *value = dma_channel_control(channel); return true;
        }
    } else if (major == 7) {
        switch (minor & ~3u) {
            case 0: *value = dma_control(dma); return true;
            case 4: *value = dma_interrupt(dma); return true;
        }
    }
    return false;
}

bool interconnect_examine(const Interconnect* inter,
                          u32 addr,
                          AccessWidth width,
                          u32* value) {
    if (value == NULL || access_width_mask(width) == 0
        || addr % (u32)width != 0) {
        return false;
    }

    u32 physical_addr = mask_region(addr);
    u32 raw = 0;
    u32 byte_offset = 0;

    if (physical_addr < RAM_SIZE
        && physical_addr <= RAM_SIZE - (u32)width) {
        *value = ram_load(&inter->ram, physical_addr, width);
        return true;
    }
    if (physical_addr >= 0x1fc00000
        && physical_addr - 0x1fc00000 <= BIOS_SIZE - (u32)width) {
        *value = bios_load(&inter->bios,
                           physical_addr - 0x1fc00000,
                           width);
        return true;
    }

    // Device registers are snapshotted as words and narrowed to the byte(s)
    // GDB requested. None of these helpers mutate emulated hardware state.
    if (physical_addr >= 0x1f801080 && physical_addr < 0x1f801100) {
        u32 aligned = (physical_addr - 0x1f801080) & ~3u;
        if (!examine_dma(&inter->dma, aligned, &raw)) return false;
        byte_offset = physical_addr & 3;
    } else if (physical_addr >= 0x1f801810
               && physical_addr < 0x1f801818) {
        u32 reg = (physical_addr - 0x1f801810) & ~3u;
        raw = reg == 0 ? gpu_read(&inter->gpu) : gpu_status(&inter->gpu);
        byte_offset = physical_addr & 3;
    } else if (physical_addr >= 0x1f801800
               && physical_addr < 0x1f801804) {
        if (width != ACCESS_BYTE) return false;
        *value = physical_addr == 0x1f801800 ? 0x18 : 0;
        return true;
    } else if ((physical_addr >= 0x1f801070
                && physical_addr < 0x1f801078)
               || (physical_addr >= 0x1f801100
                   && physical_addr < 0x1f801130)
               || (physical_addr >= 0x1f801c00
                   && physical_addr < 0x1f801e80)) {
        // These devices currently expose placeholder zero-valued state.
        *value = 0;
        return true;
    } else if (physical_addr >= 0x1f000000
               && physical_addr < 0x1f080000) {
        *value = access_width_mask(width);
        return true;
    } else {
        // Write-only, unimplemented, or unmapped state cannot be examined.
        return false;
    }

    if (byte_offset + (u32)width > sizeof(u32)) return false;
    *value = (raw >> (byte_offset * 8)) & access_width_mask(width);
    return true;
}

bool interconnect_deposit(Interconnect* inter,
                          u32 addr,
                          u32 value,
                          AccessWidth width) {
    if (access_width_mask(width) == 0 || addr % (u32)width != 0) {
        return false;
    }

    u32 physical_addr = mask_region(addr);
    if (physical_addr >= RAM_SIZE
        || physical_addr > RAM_SIZE - (u32)width) {
        return false;
    }

    // Debugger writes are deliberately RAM-only: writing through normal
    // device callbacks could start DMA or submit GPU commands unexpectedly.
    ram_store(&inter->ram, physical_addr, value, width);
    return true;
}

u32 interconnect_load(Interconnect* inter, u32 addr, AccessWidth width) {
    if (access_width_mask(width) == 0) {
        fprintf(stderr, "Invalid interconnect load width: %u\n", (u32)width);
        exit(EXIT_FAILURE);
    }
    if (addr % (u32)width != 0) {
        fprintf(stderr, "Unaligned %s load address: 0x%08x\n",
                access_width_name(width), addr);
        exit(EXIT_FAILURE);
    }

    u32 physical_addr = mask_region(addr);
    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];
        u32 offset;

        if (memory_region_contains(region, physical_addr, width, &offset)) {
            if (region->load == NULL) {
                fprintf(stderr,
                        "Region at 0x%08x does not support %s loads\n",
                        region->start,
                        access_width_name(width));
                exit(EXIT_FAILURE);
            }

            return region->load(inter, offset, width)
                & access_width_mask(width);
        }
    }

    fprintf(stderr,
            "Unhandled %s load address: 0x%08x (physical: 0x%08x)\n",
            access_width_name(width), addr, physical_addr);
    exit(EXIT_FAILURE);
}

void interconnect_store(Interconnect* inter,
                        u32 addr,
                        u32 val,
                        AccessWidth width) {
    if (access_width_mask(width) == 0) {
        fprintf(stderr, "Invalid interconnect store width: %u\n", (u32)width);
        exit(EXIT_FAILURE);
    }
    if (addr % (u32)width != 0) {
        fprintf(stderr, "Unaligned %s store address: 0x%08x\n",
                access_width_name(width), addr);
        exit(EXIT_FAILURE);
    }

    u32 physical_addr = mask_region(addr);
    for (size_t i = 0; i < MEMORY_MAP_SIZE; ++i) {
        const MemoryRegion* region = &MEMORY_MAP[i];
        u32 offset;

        if (memory_region_contains(region, physical_addr, width, &offset)) {
            if (region->store == NULL) {
                fprintf(stderr,
                        "Region at 0x%08x does not support %s stores\n",
                        region->start,
                        access_width_name(width));
                exit(EXIT_FAILURE);
            }

            region->store(inter,
                          offset,
                          val & access_width_mask(width),
                          width);
            return;
        }
    }

    fprintf(stderr,
            "Unhandled %s store address: 0x%08x (physical: 0x%08x)\n",
            access_width_name(width), addr, physical_addr);
    exit(EXIT_FAILURE);
}

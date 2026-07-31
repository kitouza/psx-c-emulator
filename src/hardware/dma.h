#ifndef DMA_H
#define DMA_H

#include "types.h"

typedef enum {
    DMA_TO_RAM = 0,
    DMA_FROM_RAM = 1
} DmaDirection;

typedef enum {
    DMA_INCREMENT = 0,
    DMA_DECREMENT = 1
} DmaStep;

typedef enum {
    DMA_SYNC_MANUAL = 0,
    DMA_SYNC_REQUEST = 1,
    DMA_SYNC_LINKED_LIST = 2
} DmaSync;

typedef enum {
    DMA_PORT_MDEC_IN = 0,
    DMA_PORT_MDEC_OUT = 1,
    DMA_PORT_GPU = 2,
    DMA_PORT_CDROM = 3,
    DMA_PORT_SPU = 4,
    DMA_PORT_PIO = 5,
    DMA_PORT_OTC = 6
} DmaPort;

typedef struct {
    u32 base;
    u16 block_size;
    u16 block_count;
    bool enable;
    DmaDirection direction;
    DmaStep step;
    DmaSync sync;
    bool trigger;
    bool chop;
    u8 chop_dma_size;
    u8 chop_cpu_size;
    u8 dummy;
} DmaChannel;

typedef struct Dma {
    u32 control;
    bool irq_enable;
    u8 channel_irq_enable;
    u8 channel_irq_flags;
    bool force_irq;
    u8 irq_dummy;
    DmaChannel channels[7];
} Dma;

void dma_channel_init(DmaChannel* channel);
u32 dma_channel_base(const DmaChannel* channel);
void dma_channel_set_base(DmaChannel* channel, u32 val);
u32 dma_channel_block_control(const DmaChannel* channel);
void dma_channel_set_block_control(DmaChannel* channel, u32 val);
u32 dma_channel_control(const DmaChannel* channel);
bool dma_channel_set_control(DmaChannel* channel, u32 val);
bool dma_channel_active(const DmaChannel* channel);
bool dma_channel_transfer_size(const DmaChannel* channel, u32* words);
void dma_channel_done(DmaChannel* channel);
bool dma_init(Dma* dma);
u32 dma_control(const Dma* dma);
void dma_set_control(Dma* dma, u32 val);
bool dma_irq(const Dma* dma);
u32 dma_interrupt(const Dma* dma);
void dma_set_interrupt(Dma* dma, u32 val);
const DmaChannel* dma_channel(const Dma* dma, DmaPort port);
DmaChannel* dma_channel_mut(Dma* dma, DmaPort port);

#endif

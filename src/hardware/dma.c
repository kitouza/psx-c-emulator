#include "dma.h"
#include <stdio.h>

void dma_channel_init(DmaChannel* channel) {
    channel->base = 0;
    channel->block_size = 0;
    channel->block_count = 0;
    channel->enable = false;
    channel->direction = DMA_TO_RAM;
    channel->step = DMA_INCREMENT;
    channel->sync = DMA_SYNC_MANUAL;
    channel->trigger = false;
    channel->chop = false;
    channel->chop_dma_size = 0;
    channel->chop_cpu_size = 0;
    channel->dummy = 0;
}

u32 dma_channel_base(const DmaChannel* channel) {
    return channel->base;
}

void dma_channel_set_base(DmaChannel* channel, u32 val) {
    // DMA addresses contain only 24 significant bits.
    channel->base = val & 0x00ffffff;
}

u32 dma_channel_block_control(const DmaChannel* channel) {
    return ((u32)channel->block_count << 16) | channel->block_size;
}

void dma_channel_set_block_control(DmaChannel* channel, u32 val) {
    channel->block_size = (u16)val;
    channel->block_count = (u16)(val >> 16);
}

u32 dma_channel_control(const DmaChannel* channel) {
    u32 val = (u32)channel->direction;
    val |= (u32)channel->step << 1;
    val |= (u32)channel->chop << 8;
    val |= (u32)channel->sync << 9;
    val |= (u32)channel->chop_dma_size << 16;
    val |= (u32)channel->chop_cpu_size << 20;
    val |= (u32)channel->enable << 24;
    val |= (u32)channel->trigger << 28;
    val |= (u32)channel->dummy << 29;
    return val;
}

bool dma_channel_set_control(DmaChannel* channel, u32 val) {
    u32 sync = (val >> 9) & 3;
    if (sync == 3) {
        fprintf(stderr, "Unknown DMA synchronization mode: %u\n", sync);
        return false;
    }

    channel->direction = (val & 1) != 0 ? DMA_FROM_RAM : DMA_TO_RAM;
    channel->step = ((val >> 1) & 1) != 0
        ? DMA_DECREMENT
        : DMA_INCREMENT;
    channel->chop = ((val >> 8) & 1) != 0;
    channel->sync = (DmaSync)sync;
    channel->chop_dma_size = (val >> 16) & 7;
    channel->chop_cpu_size = (val >> 20) & 7;
    channel->enable = ((val >> 24) & 1) != 0;
    channel->trigger = ((val >> 28) & 1) != 0;
    channel->dummy = (val >> 29) & 3;
    return true;
}

bool dma_channel_active(const DmaChannel* channel) {
    // Manual transfers require both enable and trigger. Request and linked-list
    // transfers start as soon as their enable bit is set.
    bool triggered = channel->sync == DMA_SYNC_MANUAL
        ? channel->trigger
        : true;
    return channel->enable && triggered;
}

bool dma_channel_transfer_size(const DmaChannel* channel, u32* words) {
    switch (channel->sync) {
        case DMA_SYNC_MANUAL:
            *words = channel->block_size;
            return true;
        case DMA_SYNC_REQUEST:
            *words = (u32)channel->block_count * channel->block_size;
            return true;
        case DMA_SYNC_LINKED_LIST:
            return false;
    }

    return false;
}

void dma_channel_done(DmaChannel* channel) {
    channel->enable = false;
    channel->trigger = false;
}

bool dma_init(Dma* dma) {
    dma->control = 0x07654321;
    dma->irq_enable = false;
    dma->channel_irq_enable = 0;
    dma->channel_irq_flags = 0;
    dma->force_irq = false;
    dma->irq_dummy = 0;

    for (u32 i = 0; i < 7; ++i) {
        dma_channel_init(&dma->channels[i]);
    }

    return true;
}

u32 dma_control(const Dma* dma) {
    return dma->control;
}

void dma_set_control(Dma* dma, u32 val) {
    dma->control = val;
}

bool dma_irq(const Dma* dma) {
    u8 channel_irq = dma->channel_irq_flags & dma->channel_irq_enable;
    return dma->force_irq || (dma->irq_enable && channel_irq != 0);
}

u32 dma_interrupt(const Dma* dma) {
    u32 val = dma->irq_dummy;
    val |= (u32)dma->force_irq << 15;
    val |= (u32)dma->channel_irq_enable << 16;
    val |= (u32)dma->irq_enable << 23;
    val |= (u32)dma->channel_irq_flags << 24;
    val |= (u32)dma_irq(dma) << 31;
    return val;
}

void dma_set_interrupt(Dma* dma, u32 val) {
    dma->irq_dummy = val & 0x3f;
    dma->force_irq = ((val >> 15) & 1) != 0;
    dma->channel_irq_enable = (val >> 16) & 0x7f;
    dma->irq_enable = ((val >> 23) & 1) != 0;

    // Writing a one acknowledges and clears the corresponding channel flag.
    u8 acknowledge = (val >> 24) & 0x7f;
    dma->channel_irq_flags &= (u8)~acknowledge;
}

const DmaChannel* dma_channel(const Dma* dma, DmaPort port) {
    return &dma->channels[(u32)port];
}

DmaChannel* dma_channel_mut(Dma* dma, DmaPort port) {
    return &dma->channels[(u32)port];
}

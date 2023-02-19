/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 14, 2023
 */

#include "emulator.h"

#define GB_DMA_LENGTH_BYTES (GB_PPU_MAX_SPRITES * 4)

void reset_dma(struct emulator *gameboy) {
    struct gameboy_dma *dma = &gameboy->dma;

    dma->running = false;
    dma->source_address = 0;
    dma->position = 0;
}

void sync_dma(struct emulator *gameboy) {
    struct gameboy_dma *dma = &gameboy->dma;
    int32_t elapsed = resync_sync(gameboy, GB_SYNC_DMA);
    unsigned length;

    if (!dma->running) {
        sync_next(gameboy, GB_SYNC_DMA, GB_SYNC_NEVER);
        return;
    }

    length = elapsed / 4;

    while (length && dma->position < GB_DMA_LENGTH_BYTES) {
        uint32_t b = read_bus(gameboy, dma->source_address + dma->position);

        gameboy->ppu.oam[dma->position] = b;

        length--;
        dma->position++;
    }

    if (dma->position >= GB_DMA_LENGTH_BYTES) {
        dma->running = false;
        sync_next(gameboy, GB_SYNC_DMA, GB_SYNC_NEVER);
    } else {
        sync_next(gameboy, GB_SYNC_DMA, 4);
    }
}

void start_dma(struct emulator *gameboy, uint8_t source) {
    struct gameboy_dma *dma = &gameboy->dma;

    sync_dma(gameboy);

    dma->source_address = (uint16_t)source << 8;
    dma->position = 0;

    // GBC can copy directly from the cartridge ; DMG only from RAM
    if ((!gameboy->gbc && dma->source_address < 0x8000U) || dma->source_address >= 0xE000U) {
        dma->running = false; // DMA can't access this memory region
    } else {
        dma->running = true;
    }

    sync_dma(gameboy);
}

/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 9, 2023
 */

// Direct Memory Access

#ifndef DMA_H
#define DMA_H

struct gameboy_dma {
    bool running;
    uint16_t source_address;
    uint8_t position; // number of bytes copied so far
} gameboy_dma;

void reset_dma(struct emulator *gameboy);
void sync_dma(struct emulator *gameboy);
void start_dma(struct emulator *gameboy, uint8_t source_address);

#endif

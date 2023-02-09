/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 9, 2023
 */

// Direct Memory Access

#ifndef DMA_H
#define DMA_H

#include "gameboy.h"

struct gameboy_dma {
    bool running;
    uint16_t source_address;
    uint8_t position; // number of bytes copied so far
} gameboy_dma;

void reset_dma(gameboy *gb);
void sync_dma(gameboy *gb);
void start_dma(gameboy *gb, uint8_t source_address);

#endif

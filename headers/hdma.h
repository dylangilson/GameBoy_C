/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 9, 2023
 */

// Horizontal blanking Direct Memory Access

#ifndef HDMA_H
#define HDMA_H

struct gameboy_hdma {
    uint16_t source_address;
    uint16_t destination_offset; // offset in Video RAM
    uint8_t length; // remaining length to copy, divided by 0x10 and decremented
    bool run_on_hblank; // true if the current transfer is 0x10 bytes at a time during horizontal blanking
} gameboy_hdma;

void start_hdma(struct emulator *gameboy, bool hblank);
void hblank_hdma(struct emulator *gameboy);

#endif

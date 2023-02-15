/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 14, 2023
 */

#include "emulator.h"

static void copy_hdma(struct emulator *gameboy, uint16_t length) {
    struct gameboy_hdma *hdma = &gameboy->hdma;
    uint16_t src = hdma->source_address;
    uint16_t dst = hdma->destination_offset;

    gameboy->timestamp += length * 2;

    while (length--) {
        uint16_t vram_address;
        uint8_t value;

        vram_address = 0x8000U + (dst % 0x2000U);

        value = read_bus(gameboy, src);
        write_bus(gameboy, vram_address, value);

        src++;
        dst++;
    }

    hdma->source_address = src;
    hdma->destination_offset = dst;
}

// called by the PPU on every HBLANK when hdma->run_on_hblank is true
void hblank_hdma(struct emulator *gameboy) {
    struct gameboy_hdma *hdma = &gameboy->hdma;

    copy_hdma(gameboy, 0x10);

    if (hdma->length == 0) {
        // DMA done
        hdma->run_on_hblank = false;
        hdma->length = 0x7F;
    } else {
        hdma->length--;
    }
}

void start_hdma(struct emulator *gameboy, bool hblank) {
    struct gameboy_hdma *hdma = &gameboy->hdma;

    if (hblank) {
        sync_ppu(gameboy);
        hdma->run_on_hblank = true;
        sync_ppu(gameboy);
    } else {
        uint16_t length = hdma->length;

        length = (length + 1) * 0x10;

        copy_hdma(gameboy, length);

        // transfer completed
        hdma->run_on_hblank = false;
        hdma->length = 0x7F;
    }
}

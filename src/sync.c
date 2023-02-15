/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#include "emulator.h"

void reset_sync(struct emulator *gameboy) {
    struct gameboy_sync *sync = &gameboy->sync;

    for (unsigned i = 0; i < GB_SYNC_NUM; i++) {
        sync->last_sync[i] = 0;
        sync->next_event[i] = 0;
    }

    gameboy->timestamp = 0;
    sync->first_event = 0;
}

int32_t resync_sync(struct emulator *gameboy, enum sync_token token) {
    struct gameboy_sync *sync = &gameboy->sync;
    int32_t elapsed = gameboy->timestamp - sync->last_sync[token];

    if (elapsed < 0) {
        fprintf(stderr, "Got negative sync %d for token %u\n", elapsed, token);
    }

    sync->last_sync[token] = gameboy->timestamp;

    return elapsed;
}

void sync_next(struct emulator *gameboy, enum sync_token token, int32_t cycles) {
    struct gameboy_sync *sync = &gameboy->sync;

    sync->next_event[token] = gameboy->timestamp + cycles;
    sync->first_event = sync->next_event[0]; // recompute the date of next event

    for (unsigned i = 1; i < GB_SYNC_NUM; i++) {
        int32_t event = sync->next_event[i];

        if (event < sync->first_event) {
            sync->first_event = event;
        }
    }
}

void check_sync_events(struct emulator *gameboy) {
    struct gameboy_sync *sync = &gameboy->sync;

    while (gameboy->timestamp >= gameboy->sync.first_event) {
        int32_t timestamp = gameboy->timestamp;

        if (timestamp >= sync->next_event[GB_SYNC_PPU]) {
            sync_ppu(gameboy);
        }

        if (timestamp >= sync->next_event[GB_SYNC_DMA]) {
            sync_dma(gameboy);
        }

        if (timestamp >= sync->next_event[GB_SYNC_TIMER]) {
            sync_timer(gameboy);
        }

        if (timestamp >= sync->next_event[GB_SYNC_SPU]) {
            sync_spu(gameboy);
        }

        if (timestamp >= sync->next_event[GB_SYNC_CART]) {
            sync_cart(gameboy);
        }
    }
}

// subtract current value of timestamp from all last_sync and next_event dates to avoid potential overflows and remain insync
void rebase_sync(struct emulator *gameboy) {
    struct gameboy_sync *sync = &gameboy->sync;

    for (unsigned i = 0; i < GB_SYNC_NUM; i++) {
        sync->last_sync[i] -= gameboy->timestamp;
        sync->next_event[i] -= gameboy->timestamp;
    }

    sync->first_event -= gameboy->timestamp;
    gameboy->timestamp = 0;
}

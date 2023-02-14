/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 9, 2023
 */

#ifndef SYNC_H
#define SYNC_H

#define GB_SYNC_NEVER 10000000 // sync at low frequency if there is no event planned

enum sync_token {
     GB_SYNC_GPU,
     GB_SYNC_DMA,
     GB_SYNC_TIMER,
     GB_SYNC_CART,
     GB_SYNC_SPU,
     GB_SYNC_NUM
} sync_token;

struct gameboy_sync {
    int32_t first_event; // smallest value in next_event
    int32_t last_sync[GB_SYNC_NUM]; // timestamp of last time this token was synchronized
    int32_t next_event[GB_SYNC_NUM]; // timestamp of next time this token must be synchronized
} gameboy_sync;

void reset_sync(struct emulator *gameboy);
int32_t resync_sync(struct emulator *gameboy, enum sync_token token); // resync the token and return the number of cycles since last sync
void sync_next(struct emulator *gameboy, enum sync_token token, int32_t cycles);
void check_sync_events(struct emulator *gameboy);
void rebase_sync(struct emulator *gameboy);

#endif

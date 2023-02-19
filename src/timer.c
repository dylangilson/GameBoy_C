/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 14, 2023
 */

#include "emulator.h"

void reset_timer(struct emulator *gameboy) {
    struct gameboy_timer *timer = &gameboy->timer;

    timer->divider_counter = 0;
    timer->counter = 0;
    timer->modulo = 0;
    timer->divider = GB_TIMER_DIV_1024;
    timer->started = false;
}

void sync_timer(struct emulator *gameboy) {
    struct gameboy_timer *timer = &gameboy->timer;
    int32_t elapsed = resync_sync(gameboy, GB_SYNC_TIMER);
    int32_t next;
    uint32_t count;
    unsigned div;

    switch (timer->divider) {
        case GB_TIMER_DIV_16:
            div = 16;
            break;
        case GB_TIMER_DIV_64:
            div = 64;
            break;
        case GB_TIMER_DIV_256:
            div = 256;
            break;
        case GB_TIMER_DIV_1024:
            div = 1024;
            break;
        default:
            exit(EXIT_FAILURE); // should not be reached
    }

    count = (elapsed + timer->divider_counter % div) / div; // number of counter ticks since last sync
    timer->divider_counter = (timer->divider_counter + elapsed) & 0xffff; // new value of divider

    if (!timer->started) {
        sync_next(gameboy, GB_SYNC_TIMER, GB_SYNC_NEVER);
        return;
    }

    count += timer->counter;

    while (count > 0xff) {
        // timer saturated ; reload it with the modulo
        count -= 0x100;
        count += timer->modulo;

        trigger_interrupt_request(gameboy, GB_INTERRUPT_REQUEST_TIMER);
    }

    timer->counter = count;
    next = (0x100 - count) * div; // compute remaining number of cycles until next overflow
    next -= timer->divider_counter % div; // subtract remainder in divider

    sync_next(gameboy, GB_SYNC_TIMER, next);
}

void set_timer_configuration(struct emulator *gameboy, uint8_t configuration) {
    struct gameboy_timer *timer = &gameboy->timer;

    sync_timer(gameboy);

    timer->started = configuration & 4;
    timer->divider = configuration & 3;

    sync_timer(gameboy);
}

uint8_t get_timer_configuration(struct emulator *gameboy) {
    struct gameboy_timer *timer = &gameboy->timer;
    uint8_t configuration = 0;

    configuration |= timer->divider;

    if (timer->started) {
        configuration |= 4;
    }

    return configuration;
}

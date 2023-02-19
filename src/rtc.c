/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 15, 2023
 */

#include <time.h>

#include "emulator.h"

static uint64_t get_system_time(void) {
    return time(NULL);
}

static bool is_rtc_halted(struct emulator *gameboy) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;

    return rtc->latched_date.days_high & 0x40;
}

static uint64_t get_current_timestamp(struct emulator *gameboy) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;

    if (is_rtc_halted(gameboy)) {
        return rtc->halt_date;
    } else {
        return get_system_time();
    }
}

// measure time elapsed since base date and return it ; if RTC is halted, we measure time between base and halt_time, othersie base and now
static void get_latch_date(struct emulator *gameboy, struct gameboy_rtc_date *date) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;
    uint64_t now = get_current_timestamp(gameboy);

    if (now >= rtc->base) {
        now = now - rtc->base; // convert now to a number of seconds relative to timer's base
    } else {
        rtc->base = now; // reset system time
        now = 0;
    }

    date->seconds = now % 60;
    now /= 60;

    date->minutes = now % 60;
    now /= 60;

    date->hours = now % 24;
    now /= 24;

    date->days_low = now & 0xFF;
    date->days_high &= 0x40; //  do not change halt bit, but clear day MSB and carry
    date->days_high |= (now >> 8) & 1; // day MSB

    // day carry
    if (now > 0x1FF) {
        date->days_high |= 0x80; // if day > 511, then overflow ; set carry bit
    }
}

// recompute base value so that the current time matches the provided date
void set_date(struct emulator *gameboy, const struct gameboy_rtc_date *date) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;
    uint64_t base = get_current_timestamp(gameboy);
    uint64_t days;

    days = date->days_low;
    days += (date->days_high & 1) * 0x100U; // day MSB
    days += ((date->days_high >> 8) & 1) * 0x200U; // day carry

    base -= days * 60 * 60 * 24;
    base -= date->hours * 60 * 60;
    base -= date->minutes * 60;
    base -= date->seconds;

    rtc->base = base;
}

void init_rtc(struct emulator *gameboy) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;

    rtc->base = get_system_time();
    rtc->halt_date = 0;
    rtc->latch = false;
    rtc->latched_date.days_high = 0; // ensure halt bit is 0

    get_latch_date(gameboy, &rtc->latched_date);
}

void latch_rtc(struct emulator *gameboy, bool latch) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;

    if (!rtc->latch && latch) {
        get_latch_date(gameboy, &rtc->latched_date);
    }

    rtc->latch = latch;
}

uint8_t read_rtc(struct emulator *gameboy, unsigned address) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;

    switch (address) {
        case 0x08:
            return rtc->latched_date.seconds;
        case 0x09:
            return rtc->latched_date.minutes;
        case 0x0A:
            return rtc->latched_date.hours;
        case 0x0B:
            return rtc->latched_date.days_low;
        case 0x0C:
            return rtc->latched_date.days_high;
        default:
            return 0xFF;
    }
}

void write_rtc(struct emulator *gameboy, unsigned address, uint8_t value) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;
    struct gameboy_rtc_date date;
    bool was_halted = is_rtc_halted(gameboy);

    get_latch_date(gameboy, &date);

    switch (address) {
        case 0x08:
            rtc->latched_date.seconds = value;
            date.seconds = value;
            break;
        case 0x09:
            rtc->latched_date.minutes = value;
            date.minutes = value;
            break;
        case 0x0A:
            rtc->latched_date.hours = value;
            date.hours = value;
            break;
        case 0x0B:
            rtc->latched_date.days_low = value;
            date.days_low = value;
            break;
        case 0x0C:
            rtc->latched_date.days_high = value;
            date.days_high = value;

            if (!was_halted && is_rtc_halted(gameboy)) {
                rtc->halt_date = get_system_time();
            }

            break;
        default:
            return;
    }

    set_date(gameboy, &date);
    get_latch_date(gameboy, &date);
}

static void dump_u8(FILE *file, uint8_t value) {
    if (fwrite(&value, 1, 1, file) < 0) {
        perror("fwrite failed\n");
        exit(EXIT_FAILURE);
    }
}

static uint8_t load_u8(FILE *file) {
    uint8_t value;

    if (fread(&value, 1, 1, file) < 1) {
        fprintf(stderr, "Failed to load RTC state\n");
        return 0;
    }

    return value;
}

static void dump_u64(FILE *file, uint64_t value) {
    dump_u8(file, value >> 56);
    dump_u8(file, value >> 48);
    dump_u8(file, value >> 40);
    dump_u8(file, value >> 32);
    dump_u8(file, value >> 24);
    dump_u8(file, value >> 16);
    dump_u8(file, value >> 8);
    dump_u8(file, value);
}

static uint64_t load_u64(FILE *file) {
    uint64_t value = 0;

    value |= ((uint64_t)load_u8(file)) << 56;
    value |= ((uint64_t)load_u8(file)) << 48;
    value |= ((uint64_t)load_u8(file)) << 40;
    value |= ((uint64_t)load_u8(file)) << 32;
    value |= ((uint64_t)load_u8(file)) << 24;
    value |= ((uint64_t)load_u8(file)) << 16;
    value |= ((uint64_t)load_u8(file)) << 8;
    value |= ((uint64_t)load_u8(file));

    return value;
}

void dump_rtc(struct emulator *gameboy, FILE *file) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;

    dump_u64(file, rtc->base);
    dump_u64(file, rtc->halt_date);
    dump_u8(file, rtc->latch);
    dump_u8(file, rtc->latched_date.seconds);
    dump_u8(file, rtc->latched_date.minutes);
    dump_u8(file, rtc->latched_date.hours);
    dump_u8(file, rtc->latched_date.days_low);
    dump_u8(file, rtc->latched_date.days_high);
}

void load_rtc(struct emulator *gameboy, FILE *file) {
    struct gameboy_rtc *rtc = &gameboy->cart.rtc;

    rtc->base = load_u64(file);
    rtc->halt_date = load_u64(file);
    rtc->latch = load_u8(file);
    rtc->latched_date.seconds = load_u8(file);
    rtc->latched_date.minutes = load_u8(file);
    rtc->latched_date.hours = load_u8(file);
    rtc->latched_date.days_low = load_u8(file);
    rtc->latched_date.days_high = load_u8(file);
}

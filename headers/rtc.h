/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 9, 2023
 */

// Real-Time Clock

#ifndef RTC_H
#define RTC_H

struct gameboy_rtc_date {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t days_low; // low 8 bits ; in range [0, 255]
    uint8_t days_high; // MSB + HALT (bit 6) + day carry (bit 7)
} gameboy_rtc_date;

struct gameboy_rtc {
    uint64_t base; // system time corresponding to 00:00:00 day 0 in the emulated RTC time
    uint64_t halt_date; // if halted is true, then this value contains the date and time of the halt
    bool latch; // date is latched when this switches from 0 to 1
    struct gameboy_rtc_date latched_date;
} gameboy_rtc;

void init_rtc(struct emulator *gameboy);
void latch_rtc(struct emulator *gameboy, bool latch);
uint8_t read_rtc(struct emulator *gameboy, unsigned address);
void write_rtc(struct emulator *gameboy, unsigned address, uint8_t value);
void load_rtc(struct emulator *gameboy, FILE *file);
void dump_rtc(struct emulator *gameboy, FILE *file);

#endif

/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#ifndef TIMER_H
#define TIMER_H

#include "gameboy.h"

enum timer_divider {
     GB_TIMER_DIV_1024, // timer frequency: 4096Hz
     GB_TIMER_DIV_16, // timer frequency: 262144Hz
     GB_TIMER_DIV_64, // timer frequency: 65535Hz
     GB_TIMER_DIV_256, // timer frequency: 16384Hz
} timer_divider;

struct gameboy_timer {
     uint16_t divider_counter;
     uint8_t counter;
     uint8_t modulo;
     timer_divider divider;
     bool started;
} gameboy_timer;

void reset_timer(gameboy *gb);
void sync_timer(gameboy *gb);
void set_timer_configuration(gameboy *gb, uint8_t configuration);
uint8_t get_timer_configuration(gameboy *gb);

#endif

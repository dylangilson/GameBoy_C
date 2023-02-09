/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#ifndef GAMEPAD_H
#define GAMEPAD_H

#include "gameboy.h"

#define GB_INPUT_RIGHT 0
#define GB_INPUT_LEFT 1
#define GB_INPUT_UP 2
#define GB_INPUT_DOWN 3
#define GB_INPUT_A 4
#define GB_INPUT_B 5
#define GB_INPUT_SELECT 6
#define GB_INPUT_START 7

struct gameboy_gamepad {
     uint8_t dpad_state; // state of the D-pad (up, down, right, left), active low
     bool dpad_selected;
     uint8_t buttons_state; // state of the buttons (A, B, select, start), active low
     bool buttons_selected;
} gameboy_gamepad;

void reset_gamepad(gameboy *gb);
void set_gamepad(gameboy *gb, unsigned button, bool pressed);
void select_gamepad(gameboy *gb, uint8_t selection);
uint8_t get_gamepad_state(gameboy *gb);

#endif

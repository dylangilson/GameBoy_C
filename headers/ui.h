/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 9, 2023
 */

#ifndef UI_H
#define UI_H

#include "gameboy.h"

struct gameboy_ui {
    void (*draw_line_dmg)(gameboy *gb, unsigned ly, lcd_colour colour[GB_LCD_WIDTH]); // draw a single line in DMG mode
    void (*draw_line_gbc)(gameboy *gb, unsigned ly, lcd_colour colour[GB_LCD_WIDTH]); // draw a single line in GBC mode
    void (*flip)(gameboy *gb); // called when a frame is drawn and ready to be displayed
    void (*refresh_input)(gameboy *gb); // handle user input
    void (*destroy)(gameboy *gb); // called when the emulator is told to quit and the UI should be free'd
    void *data;
} gameboy_ui;

#endif

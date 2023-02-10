/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 9, 2023
 */

#ifndef UI_H
#define UI_H

struct gameboy_ui {
    void (*draw_line_dmg)(struct emulator *gameboy, unsigned ly, union lcd_colour colour[GB_LCD_WIDTH]); // draw a single line in DMG mode
    void (*draw_line_gbc)(struct emulator *gameboy, unsigned ly, union lcd_colour colour[GB_LCD_WIDTH]); // draw a single line in GBC mode
    void (*flip)(struct emulator *gameboy); // called when a frame is drawn and ready to be displayed
    void (*refresh_input)(struct emulator *gameboy); // handle user input
    void (*destroy)(struct emulator *gameboy); // called when the emulator is told to quit and the UI should be free'd
    void *data;
} gameboy_ui;

#endif

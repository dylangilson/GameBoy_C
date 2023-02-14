/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

// Picture Processing Unit

#ifndef PPU_H
#define PPU_H

#define MAX_SPRITES 40 // PPU supports a maximum of 40 sprites at once
#define GB_LCD_WIDTH 160
#define GB_LCD_HEIGHT 144

enum dmg_colour {
    WHITE,
    LIGHT_GREY,
    DARK_GREY,
    BLACK
} dmg_colour;

union lcd_colour {
    enum dmg_colour dmg; // DMG only has 4 colour options
    uint16_t gbc; // GBC colours: xRGB 1555
} lcd_colour;

// GBC only
struct colour_palette {
    uint16_t colours[8][4]; // 8 palettes of 4 colours each
    uint8_t write_index; // index of next write in palette
    bool auto_increment; // if true write_index will automatically increment after each write
} colour_palette;

struct gameboy_ppu {
    uint8_t scroll_x;
    uint8_t scroll_y;
    bool lyc_flag;
    bool mode0_flag;
    bool mode1_flag;
    bool mode2_flag;
    bool master_enable; // true if PPU is enable
    bool background_enable;
    bool window_enable;
    bool sprite_enable;
    bool tall_sprites; // true if sprites are 8x16 ; false when sprites are 8x8
    bool background_use_high_tile_map;
    bool window_use_high_tile_map;
    bool background_window_use_sprite_tile_set;
    uint8_t ly; // register ly
    uint8_t lyc; // register lyc
    uint8_t background_palette;
    uint8_t sprite_palette0;
    uint8_t sprite_palette1;
    uint8_t window_x;
    uint8_t window_y;
    uint8_t window_line; // NEW
    uint16_t line_position; // current position within line
    uint8_t oam[MAX_SPRITES * 4]; // Object Attribute Memory (sprite configuration) ; each sprite uses 4 bytes
    struct colour_palette background_palettes; // GBC only
    struct colour_palette sprite_palettes; // GBC only
} gameboy_ppu;

void reset_ppu(struct emulator *gameboy);
void sync_ppu(struct emulator *gameboy);
uint8_t get_lcd_stat(struct emulator *gameboy);
void set_lcd_stat(struct emulator *gameboy, uint8_t value);
uint8_t get_lcdc(struct emulator *gameboy);
void set_lcdc(struct emulator *gameboy, uint8_t value);
uint8_t get_ly(struct emulator *gameboy);

#endif

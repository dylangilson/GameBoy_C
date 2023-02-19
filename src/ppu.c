/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 8, 2023
 */

#include "emulator.h"

/* ppu timings:
 * - one line:
 *      | Mode 2: 80 cycles | Mode 3: 172 cycles | Mode 0: 204 cycles |
 *   Total: 456 cycles
 *
 *   Mode 2: OAM in use
 *   Mode 3: OAM and VRAM in use
 *   Mode 0: Horizontal blanking (CPU can access OAM and VRAM)
 *
 * - draw each line at the boundary between Mode 3 and Mode 0 (not very accurate, but simple and works well enough)
 *
 * - one frame:
 *      | Active video (Modes 2/3/0): 144 lines |
 *      | VSYNC (Mode 1): 10 lines              |
 *   Total: 154 lines (154 lines * 456 cycles = 70224 cycles)
 *
 *   Mode 1: Vertical blanking (CPU can access OAM and VRAM)
 */

#define MODE_2_CYCLES 80U // number of clock cycles spent in Mode 2 (OAM in use)
#define MODE_3_CYCLES 172U // number of clock cycles spent in Mode 3 (OAM + display RAM in use)
#define MODE_3_END (MODE_2_CYCLES + MODE_3_CYCLES)
#define MODE_0_CYCLES 204U // number of clock cycles spent in Mode 0 (HSYNC)
#define HTOTAL (MODE_2_CYCLES + MODE_3_CYCLES + MODE_0_CYCLES) // total number of cycles per line
#define VSYNC_START 144U // first line of the vertical blanking
#define VSYNC_LINES 10U // number of lines spent in vertical blanking
#define VTOTAL (VSYNC_START + VSYNC_LINES) // total number of lines (including vertical blanking)
#define GB_LINE_SPRITES 10 // max number of sprites per line

void reset_ppu(struct emulator *gameboy) {
    struct gameboy_ppu *ppu = &gameboy->ppu;

    ppu->scroll_x = 0;
    ppu->scroll_y = 0;
    ppu->lyc_flag = false;
    ppu->mode0_flag = false;
    ppu->mode1_flag = false;
    ppu->mode2_flag = false;
    ppu->master_enable = true;
    ppu->background_enable = false;
    ppu->window_enable = false;
    ppu->sprite_enable = false;
    ppu->tall_sprites = false;
    ppu->background_use_high_tile_map = false;
    ppu->window_use_high_tile_map = false;
    ppu->background_window_use_sprite_tile_set = false;
    ppu->ly = 0;
    ppu->lyc = 0;
    ppu->background_palette = 0;
    ppu->sprite_palette0 = 0;
    ppu->sprite_palette1 = 0;
    ppu->window_x = 0;
    ppu->window_y = 0;
    ppu->line_position = 0;

    for (unsigned i = 0; i < sizeof(ppu->oam); i++) {
        ppu->oam[i] = 0;
    }
}

static uint8_t get_ppu_mode(struct emulator *gameboy) {
    struct gameboy_ppu *ppu = &gameboy->ppu;

    if (ppu->ly >= VSYNC_START) {
        return 1; // Mode 1 : VBLANK
    }

    if (ppu->line_position < MODE_2_CYCLES) {
        return 2; // Mode 2 : OAM access
    }

    if (ppu->line_position < MODE_3_END) {
        return 3; // Mode 3 : OAM + display RAM in use
    }

    return 0; // Mode 0 : horizontal blanking 
}

struct ppu_pixel {
    union lcd_colour colour;
    bool opaque;
    bool priority; // GBC only: true if the background pixel has priority
} ppu_pixel;

// get a pixel value from the tileset
static enum dmg_colour get_ppu_tile_colour(struct emulator *gameboy, uint8_t tile_index, uint8_t x, uint8_t y, bool use_sprite_tile_set, bool use_high_bank) {
    unsigned tile_address;

    // each tile is 8x8 pixels and stores 2bits per pixels for a total of 16bytes per tile
    const unsigned tile_size = 16;
    unsigned lsb;
    unsigned msb;

    if (use_sprite_tile_set) {
        tile_address = tile_index * tile_size; // sprite tile set starts at the beginning of VRAM
    } else {
        tile_address = 0x1000 + (int8_t)tile_index * tile_size; // other tile set
    }

    // GBC-only: use the high bank if requested
    if (use_high_bank) {
        tile_address += 0x2000;
    }

    x = 7 - x; // pixel data is stored backwards in VRAM: the leftmost pixel (x = 0) is stored in the MSB (byte >> 7)

    // the pixel value is two bits split across two contiguous bytes
    lsb = (gameboy->video_ram[tile_address + y * 2 + 0] >> x) & 1;
    msb = (gameboy->video_ram[tile_address + y * 2 + 1] >> x) & 1;

    return (msb << 1) | lsb;
}

static enum dmg_colour ppu_palette_transform(enum dmg_colour colour, uint8_t palette) {
    unsigned offset = colour * 2;

    return (palette >> offset) & 3;
}

static struct ppu_pixel get_ppu_background_window_pixel(struct emulator *gameboy, uint8_t x, uint8_t y, bool use_high_tile_map) {
    struct gameboy_ppu *ppu = &gameboy->ppu;

    // coordinates of the tile in the tile map (each tile is 8x8 pixels)
    unsigned tile_map_x = x / 8;
    unsigned tile_map_y = y / 8;

    // coordinates of the pixel within the tile
    unsigned tile_x = x % 8;
    unsigned tile_y = y % 8;

    unsigned tile_map_address; // offset of the tile map entry in the VRAM
    uint8_t tile_index; // index of the tile entry in the tile set
    struct ppu_pixel pixel;
    bool use_sprite_tile_set = ppu->background_window_use_sprite_tile_set;

    // there are two independent tile maps the game can use
    if (use_high_tile_map) {
        tile_map_address = 0x1C00;
    } else {
        tile_map_address = 0x1800;
    }

    tile_map_address += tile_map_y * 32 + tile_map_x; // the tile map is a square map of 32 * 32 tiles ; for each tile it contains one byte which is an index in the tile set
    tile_index = gameboy->video_ram[tile_map_address]; // lookup the tile map entry in VRAM

    if (gameboy->gbc) {
        // on the GBC we have additional attributes in the 2nd VRAM bank
        uint8_t attrs = gameboy->video_ram[tile_map_address + 0x2000];
        bool priority = attrs & 0x80;
        bool y_flip = attrs & 0x40;
        bool x_flip = attrs & 0x20;
        bool high_bank = attrs & 0x08;
        uint8_t palette = attrs & 0x07;
        enum dmg_colour colour;

        pixel.priority = priority;

        if (x_flip) {
            tile_x = 7 - tile_x;
        }

        if (y_flip) {
            tile_y = 7 - tile_y;
        }

        colour = get_ppu_tile_colour(gameboy, tile_index, tile_x, tile_y, use_sprite_tile_set, high_bank);
        pixel.opaque = colour != WHITE;
        pixel.colour.dmg = ppu->background_palettes.colours[palette][colour];
    } else {
        pixel.priority = false;
        pixel.colour.dmg = get_ppu_tile_colour(gameboy, tile_index, tile_x, tile_y, use_sprite_tile_set, false);
        pixel.opaque = pixel.colour.dmg != WHITE;
        pixel.colour.dmg = ppu_palette_transform(pixel.colour.dmg, ppu->background_palette);
    }

    return pixel;
}

static struct ppu_pixel get_ppu_background_pixel(struct emulator *gameboy, unsigned x, unsigned y) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    uint8_t background_x = (x + ppu->scroll_x) & 0xFF;
    uint8_t background_y = (y + ppu->scroll_y) & 0xFF;

    return get_ppu_background_window_pixel(gameboy, background_x, background_y, ppu->background_use_high_tile_map);
}

static struct ppu_pixel get_ppu_window_pixel(struct emulator *gameboy, unsigned x, unsigned y) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    uint8_t window_x = x + 7 - ppu->window_x;
    uint8_t window_y = y - ppu->window_y;

    return get_ppu_background_window_pixel(gameboy, window_x, window_y, ppu->window_use_high_tile_map);
}

struct sprite {
    // coordinates of the sprite's top-left corner
    int x;
    int y;
    uint8_t tile_index; // index of the sprite's pixel data in the sprite tile set ; 8 * 16 sprites use two consecutive tiles
    bool background; // true, if the sprite must be displayed behind the background
    bool x_flip;
    bool y_flip;
    bool use_sprite_palette1; // GB-only: true if sprite uses palette sprite_palette1, otherwise use sprite_palette0
    bool high_bank; // GBC-only: true, if the tile is in the high bank
    uint8_t palette; // GBC-only: select which palette to use
} sprite;

static struct sprite get_oam_sprite(struct emulator *gameboy, unsigned index) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    struct sprite s;
    unsigned oam_off = index * 4;
    uint8_t flags;

    s.y = (int)ppu->oam[oam_off] - 16; // y coordinates have an offset of 16 (so that they can clip at the top of the screen)
    s.x = (int)ppu->oam[oam_off + 1] - 8; // x coordinates have an offset of 8 (so that they can clip to the left of the screen)
    s.tile_index = ppu->oam[oam_off + 2];
    flags = ppu->oam[oam_off + 3];
    s.use_sprite_palette1 = flags & 0x10;
    s.x_flip = flags & 0x20;
    s.y_flip = flags & 0x40;
    s.background = flags & 0x80;

    if (gameboy->gbc) {
        s.high_bank = flags & 0x08;
        s.palette = flags & 0x07;
    } else {
        s.high_bank = false;
        s.palette = 0;
    }

    return s;
}

static void get_ppu_line_sprites(struct emulator *gameboy, unsigned ly, struct sprite sprites[GB_LINE_SPRITES + 1]) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    int i;
    unsigned n_sprites;
    unsigned sprite_height;

    if (!ppu->sprite_enable) {
        // sprites are disabled ; mark the end of the list with an out-of-frame sprite
        sprites[0].x = GB_LCD_WIDTH * 2;
        return;
    }

    if (ppu->tall_sprites) {
        sprite_height = 16;
    } else {
        sprite_height = 8;
    }

    // iterate over the OAM and store the sprites that are in the current line
    n_sprites = 0;
    for (i = 0; i < GB_PPU_MAX_SPRITES; i++) {
        struct sprite s = get_oam_sprite(gameboy, i);

        if ((int)ly < s.y || (int)ly >= (s.y + (int)sprite_height)) {
            continue; // sprite isn't on this line
        }

        sprites[n_sprites] = s;
        n_sprites++;

        if (n_sprites >= GB_LINE_SPRITES) {
            break;
        }
    }

    sprites[n_sprites].x = GB_LCD_WIDTH * 2; // out-of-frame sprite for end of list

    if (gameboy->gbc) {
        // in GBC mode, the sprite priority is not based on X-coordinates, instead on the index in OAM ; entries already in correct order
        return;
    }

    // stable sort the sprites by x-coordinate
    for (i = 1; i < n_sprites; i++) {
        struct sprite current= sprites[i];
        int j;

        for (j = i - 1; j >= 0; j--) {
            if (sprites[j].x <= current.x) {
                break;
            }

            sprites[j + 1] = sprites[j];
        }

        sprites[j + 1] = current;
    }
}

static bool get_ppu_sprite_colour(struct emulator *gameboy, const struct sprite *sprite, unsigned x, unsigned y, struct ppu_pixel *p) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    unsigned sprite_x;
    unsigned sprite_y;
    unsigned sprite_flip_height;
    uint8_t tile_index;
    enum dmg_colour colour;

    if (sprite->background && p->opaque) {
        return false; // sprite is behind an opaque background pixel ; return background colour
    }

    sprite_x = (int)x - sprite->x;
    sprite_y = (int)y - sprite->y;

    if (ppu->tall_sprites) {
        // 8 * 16 sprites use two consecutive tiles ; The first tile's index's LSB is always assumed to be 0
        tile_index = sprite->tile_index & 0xFE;
        sprite_flip_height = 15;
    } else {
        tile_index = sprite->tile_index;
        sprite_flip_height = 7;
    }

    if (sprite->x_flip) {
        sprite_x = 7 - sprite_x;
    }

    if (sprite->y_flip) {
        sprite_y = sprite_flip_height - sprite_y;
    }

    colour = get_ppu_tile_colour(gameboy, tile_index, sprite_x, sprite_y, true, sprite->high_bank);

    // white pixel colour denotes a transparent pixel
    if (colour == WHITE) {
        return false;
    }

    if (gameboy->gbc) {
        p->colour.gbc = ppu->sprite_palettes.colours[sprite->palette][colour];
    } else {
        uint8_t palette;

        if (sprite->use_sprite_palette1) {
            palette = ppu->sprite_palette1;
        } else {
            palette = ppu->sprite_palette0;
        }

        p->colour.dmg = ppu_palette_transform(colour, palette);
    }

    return true;
}

// returns true if the given screen coordinates lie within the window
static bool get_ppu_pixel_in_window(struct emulator *gameboy, unsigned x, unsigned y) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    int window_x = (int)ppu->window_x - 7;

    return (int)x >= window_x && y >= ppu->window_y;
}

static void ppu_draw_current_line(struct emulator *gameboy) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    union lcd_colour line[GB_LCD_WIDTH];
    struct sprite line_sprites[GB_LINE_SPRITES + 1]; // fake, out-of-frame sprite at the end to avoid checking for bounds while we draw the line
    unsigned x;
    unsigned next_sprite = 0;

    get_ppu_line_sprites(gameboy, ppu->ly, line_sprites);

    for (x = 0; x < GB_LCD_WIDTH; x++) {
        struct ppu_pixel pixel;
        struct sprite s;

        pixel.colour.dmg = WHITE;
        pixel.opaque = false;
        pixel.priority = false;

        if (ppu->window_enable && get_ppu_pixel_in_window(gameboy, x, ppu->ly)) {
            pixel = get_ppu_window_pixel(gameboy, x, ppu->ly); // pixel lies within the window
        } else if (ppu->background_enable) {
            pixel = get_ppu_background_pixel(gameboy, x, ppu->ly);
        }

        if (!pixel.priority || !pixel.opaque) {
            if (gameboy->gbc) {
                for (unsigned i = 0; line_sprites[i].x < GB_LCD_WIDTH * 2; i++) {
                    s = line_sprites[i];

                    if ((int)x < s.x || (int)x >= s.x + 8) {
                        continue; // sprite is not visible at this location
                    }

                    if (get_ppu_sprite_colour(gameboy, &s, x, ppu->ly, &pixel)) {
                        break;
                    }
                }
            } else {
                while (next_sprite < GB_LINE_SPRITES) {
                    if (line_sprites[next_sprite].x + 8 <= x ) {
                        next_sprite++; // sprite already finished being displayed
                    } else {
                        break;
                    }
                }

                // iterate on all sprites at this position until we find one that's visible or exhaust the list
                for (unsigned i = next_sprite; line_sprites[i].x <= (int)x; i++) {
                    s = line_sprites[i];

                    if (get_ppu_sprite_colour(gameboy, &s, x, ppu->ly, &pixel)) {
                        break;
                    }
                }
            }
        }

        line[x] = pixel.colour;
    }

    if (gameboy->gbc) {
        gameboy->ui.draw_line_gbc(gameboy, ppu->ly, line);
    } else {
        gameboy->ui.draw_line_dmg(gameboy, ppu->ly, line);
    }
}

void sync_ppu(struct emulator *gameboy) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    struct gameboy_hdma *hdma = &gameboy->hdma;
    int32_t elapsed = resync_sync(gameboy, GB_SYNC_PPU);
    uint16_t line_remaining = HTOTAL - ppu->line_position; // number of cycles needed to finish the current line
    int32_t next_event;

    if (!ppu->master_enable) {
        sync_next(gameboy, GB_SYNC_PPU, GB_SYNC_NEVER); // ppu isn't running
        return;
    }

    while (elapsed > 0) {
        uint8_t prev_mode = get_ppu_mode(gameboy);

        if (elapsed < line_remaining) {
            ppu->line_position += elapsed;
            line_remaining -= elapsed;
            elapsed = 0;

            if (prev_mode != 0 && get_ppu_mode(gameboy) == 0) {
                // didn't finish the line but we did cross the Mode 3 -> Mode 0 boundary ; draw the current line
                ppu_draw_current_line(gameboy);

                if (ppu->mode0_flag) {
                    trigger_interrupt_request(gameboy, GB_INTERRUPT_REQUEST_LCD_STAT);
                }

                if (hdma->run_on_hblank) {
                    hblank_hdma(gameboy);
                }
            }
        } else {
            elapsed -= line_remaining;

            if (prev_mode == 2 || prev_mode == 3) {
                // about to finish the current line, but hadn't reached the Mode 0 boundary yet, which means that has still yet to be drawn
                ppu_draw_current_line(gameboy);

                if (ppu->mode0_flag) {
                    trigger_interrupt_request(gameboy, GB_INTERRUPT_REQUEST_LCD_STAT);
                }

                if (hdma->run_on_hblank) {
                    hblank_hdma(gameboy);
                }
            }

            ppu->ly++;
            ppu->line_position = 0;
            line_remaining = HTOTAL;

            if (ppu->ly == VSYNC_START) {
                // finished drawing the current frame
                gameboy->ui.flip(gameboy);
                trigger_interrupt_request(gameboy, GB_INTERRUPT_REQUEST_VSYNC);

                if (ppu->mode1_flag) {
                    trigger_interrupt_request(gameboy, GB_INTERRUPT_REQUEST_LCD_STAT); // entered VSYNC ; trigger the interrupt request
                }
            }

            if (ppu->ly >= VTOTAL) {
                ppu->ly = 0; // move to next frame
            }

            if (ppu->lyc_flag && ppu->ly == ppu->lyc) {
                trigger_interrupt_request(gameboy, GB_INTERRUPT_REQUEST_LCD_STAT); // reached LYC ; trigger interrupt
            }

            if (ppu->mode2_flag && ppu->ly < VSYNC_START) {
                trigger_interrupt_request(gameboy, GB_INTERRUPT_REQUEST_LCD_STAT); // Mode 2 is the first mode entered on a new line (outside of blanking)
            }
        }
    }

    next_event = line_remaining; // force a sync at end of current line

    if ((ppu->mode0_flag || hdma->run_on_hblank) && get_ppu_mode(gameboy) >= 2) {
        next_event -= MODE_0_CYCLES;
    }

    /* Force a sync at the beginning of the next line */
    sync_next(gameboy, GB_SYNC_PPU, next_event); // force a sync at beginning of new line
}

void set_lcd_stat(struct emulator *gameboy, uint8_t stat) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    bool prev_mode0_flag = ppu->mode0_flag;

    sync_ppu(gameboy);

    ppu->mode0_flag = stat & 0x8;
    ppu->mode1_flag = stat & 0x10;
    ppu->mode2_flag = stat & 0x20;
    ppu->lyc_flag = stat & 0x40;

    // enabling mode 0 interrupts may change the date of the next event
    if (!prev_mode0_flag && ppu->mode0_flag) {
        sync_ppu(gameboy);
    }
}

uint8_t get_lcd_stat(struct emulator *gameboy) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    uint8_t r = 0;

    if (!ppu->master_enable) {
        return 0;
    }

    sync_ppu(gameboy);

    r |= get_ppu_mode(gameboy);
    r |= (ppu->ly == ppu->lyc) << 2;
    r |= ppu->mode0_flag << 3;
    r |= ppu->mode1_flag << 4;
    r |= ppu->mode2_flag << 5;
    r |= ppu->lyc_flag << 6;

    return r;
}

void set_lcdc(struct emulator *gameboy, uint8_t lcdc) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    bool master_enable;

    sync_ppu(gameboy);

    ppu->background_enable = lcdc & 0x01;
    ppu->sprite_enable = lcdc & 0x02;
    ppu->tall_sprites = lcdc & 0x04;
    ppu->background_use_high_tile_map = lcdc & 0x08;
    ppu->background_window_use_sprite_tile_set = lcdc & 0x10;
    ppu->window_enable = lcdc & 0x20;
    ppu->window_use_high_tile_map = lcdc & 0x40;
    master_enable = lcdc & 0x80;

    if (master_enable != ppu->master_enable) {
        ppu->master_enable = master_enable;

        if (master_enable == false) {
            union lcd_colour line[GB_LCD_WIDTH];

            for (unsigned i = 0; i < GB_LCD_WIDTH; i++) {
                line[i].dmg = WHITE;
            }

            for (unsigned i = 0; i < GB_LCD_HEIGHT; i++) {
                gameboy->ui.draw_line_dmg(gameboy, i, line);
            }

            ppu->ly = 0;
            ppu->line_position = 0;
        }

        sync_ppu(gameboy);
    }
}

uint8_t get_lcdc(struct emulator *gameboy) {
    struct gameboy_ppu *ppu = &gameboy->ppu;
    uint8_t lcdc = 0;

    sync_ppu(gameboy);

    lcdc |= (ppu->background_enable << 0);
    lcdc |= (ppu->sprite_enable << 1);
    lcdc |= (ppu->tall_sprites << 2);
    lcdc |= (ppu->background_use_high_tile_map << 3);
    lcdc |= (ppu->background_window_use_sprite_tile_set << 4);
    lcdc |= (ppu->window_enable << 5);
    lcdc |= (ppu->window_use_high_tile_map << 6);
    lcdc |= (ppu->master_enable << 7);

    return lcdc;
}

uint8_t get_ly(struct emulator *gameboy) {
    struct gameboy_ppu *ppu = &gameboy->ppu;

    sync_ppu(gameboy);

    return ppu->ly;
}

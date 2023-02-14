/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#include "emulator.h"

// ROM (bank 0 + 1)
#define ROM_BASE 0x0000U
#define ROM_END (ROM_BASE + 0x8000U)
// VIDEO RAM
#define VIDEO_RAM_BASE 0x8000U
#define VIDEO_RAM_END (VIDEO_RAM_BASE + 0x2000U)
// Cartridge (generally battery-backed) RAM 
#define CARTRIDGE_RAM_BASE 0xA000U
#define CARTRIDGE_RAM_END (CARTRIDGE_RAM_BASE + 0x2000U)
// Internal RAM 
#define INTERNAL_RAM_BASE 0xC000U
#define INTERNAL_RAM_END (INTERNAL_RAM_BASE + 0x2000U)
// Internal RAM mirror 
#define INTERNAL_RAM_ECHO_BASE 0xE000U
#define INTERNAL_RAM_ECHO_END (INTERNAL_RAM_ECHO_BASE + 0x1E00U)
// Object Attribute Memory (sprite configuration) 
#define OAM_BASE 0xFE00U
#define OAM_END (OAM_BASE + 0xA0U)
// Zero page RAM 
#define ZERO_PAGE_RAM_BASE 0xFF80U
#define ZERO_PAGE_RAM_END (ZERO_PAGE_RAM_BASE + 0x7FU)
#define REGISTER_INPUT 0xFF00U // Input buttons register 
#define REGISTER_SB 0xFF01U // Serial Data 
#define REGISTER_SC 0xFF02U // Serial Control 
#define REGISTER_DIV 0xFF04U // Timer divider 
#define REGISTER_TIMA 0xFF05U // Timer counter 
#define REGISTER_TMA 0xFF06U // Timer modulo 
#define REGISTER_TAC 0xFF07U // Timer controller 
#define REGISTER_IF 0xFF0FU // Interrupt flags
// Sound 1 registers 
#define REGISTER_NR10 0xFF10U
#define REGISTER_NR11 0xFF11U
#define REGISTER_NR12 0xFF12U
#define REGISTER_NR13 0xFF13U
#define REGISTER_NR14 0xFF14U
// Sound 2 registers 
#define REGISTER_NR21 0xFF16U
#define REGISTER_NR22 0xFF17U
#define REGISTER_NR23 0xFF18U
#define REGISTER_NR24 0xFF19U
// Sound 3 registers 
#define REGISTER_NR30 0xFF1AU
#define REGISTER_NR31 0xFF1BU
#define REGISTER_NR32 0xFF1CU
#define REGISTER_NR33 0xFF1DU
#define REGISTER_NR34 0xFF1EU
// Sound 4 registers 
#define REGISTER_NR41 0xFF20U
#define REGISTER_NR42 0xFF21U
#define REGISTER_NR43 0xFF22U
#define REGISTER_NR44 0xFF23U
// Sound control registers 
#define REGISTER_NR50 0xFF24U
#define REGISTER_NR51 0xFF25U
#define REGISTER_NR52 0xFF26U
// Sound 3 waveform RAM 
#define NR3_RAM_BASE 0xFF30U
#define NR3_RAM_END 0xFF40U
// Window registers
#define REGISTER_LCDC 0xFF40U
#define REGISTER_LCD_STAT 0xFF41U
#define REGISTER_SCROLL_Y 0xFF42U
#define REGISTER_SCROLL_X 0xFF43U
#define REGISTER_LY 0xFF44U 
#define REGISTER_LYC 0xFF45U
#define REGISTER_DMA 0xFF46U 
#define REGISTER_BACKGROUND_PALETTE 0xFF47U
#define REGISTER_OBP0 0xFF48U // sprite palette 0
#define REGISTER_OBP1 0xFF49U // sprite palette 0
#define REGISTER_WINDOW_Y 0xFF4AU // Window Y position 
#define REGISTER_WINDOW_X 0xFF4BU // Window X position 
#define REGISTER_IE 0xFFFFU // Interrupt Enable register 
// gbc-only registers
#define REGISTER_VBK 0xFF4FU // VRAM banking
#define REGISTER_HDMA1 0xFF51U // HDMA source addressess high
#define REGISTER_HDMA2 0xFF52U // HDMA source addressess low
#define REGISTER_HDMA3 0xFF53U // HDMA destination addressess high
#define REGISTER_HDMA4 0xFF54U // HDMA destination addressess low
#define REGISTER_HDMA5 0xFF55U // HDMA length, mode and start
#define REGISTER_BCPS 0xFF68U // Background palette addressess
#define REGISTER_BCPD 0xFF69U // Background palette data
#define REGISTER_OCPS 0xFF6AU // Sprite palette addressess
#define REGISTER_OCPD 0xFF6BU // Sprite palette data
#define REGISTER_SVBK 0xFF70U // Internal RAM banking

static uint16_t get_internal_ram_offset(struct emulator *gameboy, uint16_t offset) {
    if (offset >= 0x1000) {
        unsigned bank = gameboy->internal_ram_high_bank;

        if (bank == 0) {
            bank = 1;
        }

        offset += (bank - 1) * 0x1000;
    }

    return offset;
}

// read one byte from memory at address
uint8_t read_bus(struct emulator *gameboy, uint16_t address) {
    if (address >= ROM_BASE && address < ROM_END) {
        return read_cart_rom(gameboy, address - ROM_BASE);
    }

    if (address >= ZERO_PAGE_RAM_BASE && address < ZERO_PAGE_RAM_END) {
        return gameboy->zero_page_ram[address - ZERO_PAGE_RAM_BASE];
    }

    if (address >= INTERNAL_RAM_BASE && address < INTERNAL_RAM_END) {
        uint16_t offset = get_internal_ram_offset(gameboy, address - INTERNAL_RAM_BASE);

        return gameboy->internal_ram[offset];
    }

    if (address >= INTERNAL_RAM_ECHO_BASE && address < INTERNAL_RAM_ECHO_END) {
        uint16_t offset = get_internal_ram_offset(gameboy, address - INTERNAL_RAM_ECHO_BASE);

        return gameboy->internal_ram[offset];
    }

    if (address >= VIDEO_RAM_BASE && address < VIDEO_RAM_END) {
        uint16_t offset = address - VIDEO_RAM_BASE;

        offset += 0x2000 * gameboy->video_ram_high_bank;

        return gameboy->video_ram[offset];
    }

    if (address >= CARTRIDGE_RAM_BASE && address < CARTRIDGE_RAM_END) {
        return read_cart_ram(gameboy, address - CARTRIDGE_RAM_BASE);
    }

    if (address >= OAM_BASE && address < OAM_END) {
        return gameboy->ppu.oam[address - OAM_BASE];
    }

    if (address == REGISTER_INPUT) {
        return get_gamepad_state(gameboy);
    }

    if (address == REGISTER_SB) {
        return 0xFF;
    }

    if (address == REGISTER_SC) {
        return 0;
    }

    if (address == REGISTER_DIV) {
        sync_timer(gameboy);

        return gameboy->timer.divider_counter >> 8; // return the high 8 bits of the divider counter
    }

    if (address == REGISTER_TIMA) {
        sync_timer(gameboy);

        return gameboy->timer.counter;
    }

    if (address == REGISTER_TMA) {
        return gameboy->timer.modulo;
    }

    if (address == REGISTER_TAC) {
        return get_timer_configuration(gameboy);
    }

    if (address == REGISTER_IF) {
        return gameboy->interrupt_request.interrupt_request_flags;
    }

    if (address == REGISTER_NR10) {
        uint8_t r = 0x80;

        r |= gameboy->spu.nr1.sweep.shift;
        r |= gameboy->spu.nr1.sweep.subtract << 3;
        r |= gameboy->spu.nr1.sweep.time << 4;

        return r;
    }

    if (address == REGISTER_NR11) {
        return (gameboy->spu.nr1.wave.duty_cycle << 6) | 0x3F;
    }

    if (address == REGISTER_NR12) {
        return gameboy->spu.nr1.envelope_configuration;
    }

    if (address == REGISTER_NR13) {
        return 0xFF; // write-only register
    }

    if (address == REGISTER_NR14) {
        return (gameboy->spu.nr1.duration.enable << 6) | 0xBF;
    }

    if (address == REGISTER_NR21) {
        return (gameboy->spu.nr2.wave.duty_cycle << 6) | 0x3F;
    }

    if (address == REGISTER_NR22) {
        return gameboy->spu.nr2.envelope_configuration;
    }

    if (address == REGISTER_NR23) {
        return 0xFF; // write-only register
    }

    if (address == REGISTER_NR24) {
        return (gameboy->spu.nr2.duration.enable << 6) | 0xbF;
    }

    if (address == REGISTER_NR30) {
        sync_spu(gameboy);
        return (gameboy->spu.nr3.enable << 7) | 0x7F;
    }

    if (address == REGISTER_NR31) {
        return gameboy->spu.nr3.t1;
    }

    if (address == REGISTER_NR32) {
        return (gameboy->spu.nr3.volume_shift << 5) | 0x9F;
    }

    if (address == REGISTER_NR33) {
        return 0xFF; // write-only register
    }

    if (address == REGISTER_NR34) {
        return (gameboy->spu.nr3.duration.enable << 6) | 0xBF;
    }

    if (address == REGISTER_NR41) {
        return 0xFF; // write-only register
    }

    if (address == REGISTER_NR42) {
        return gameboy->spu.nr4.envelope_configuration;
    }

    if (address == REGISTER_NR43) {
        return gameboy->spu.nr4.lfsr_configuration;
    }

    if (address == REGISTER_NR44) {
        return (gameboy->spu.nr4.duration.enable << 6) | 0xBF;
    }

    if (address == REGISTER_NR50) {
        return gameboy->spu.output_level;
    }

    if (address == REGISTER_NR51) {
        return gameboy->spu.sound_mux;
    }

    if (address == REGISTER_NR52) {
        uint8_t r = 0;

        r |= gameboy->spu.nr2.running << 1;
        r |= gameboy->spu.nr3.running << 2;
        r |= gameboy->spu.enable << 7;

        return r;
    }

    if (address >= NR3_RAM_BASE && address < NR3_RAM_END) {
        return gameboy->spu.nr3.ram[address - NR3_RAM_BASE];
    }

    if (address == REGISTER_LCDC) {
        return get_lcdc(gameboy);
    }

    if (address == REGISTER_LCD_STAT) {
        return get_lcd_stat(gameboy);
    }

    if (address == REGISTER_SCROLL_Y) {
        return gameboy->ppu.scroll_y;
    }

    if (address == REGISTER_SCROLL_X) {
        return gameboy->ppu.scroll_x;
    }

    if (address == REGISTER_LY) {
        return get_ly(gameboy);
    }

    if (address == REGISTER_LYC) {
        return gameboy->ppu.lyc;
    }

    if (address == REGISTER_DMA) {
        return gameboy->dma.source_address >> 8;
    }

    if (address == REGISTER_BACKGROUND_PALETTE) {
        return gameboy->ppu.background_palette;
    }

    if (address == REGISTER_OBP0) {
        return gameboy->ppu.sprite_palette0;
    }

    if (address == REGISTER_OBP1) {
        return gameboy->ppu.sprite_palette1;
    }

    if (address == REGISTER_WINDOW_Y) {
        return gameboy->ppu.window_y;
    }

    if (address == REGISTER_WINDOW_X) {
        return gameboy->ppu.window_x;
    }

    if (address == REGISTER_IE) {
        return gameboy->interrupt_request.interrupt_request_enable;
    }

    if (gameboy->gbc && address == REGISTER_VBK) {
        return gameboy->video_ram_high_bank | 0xFE;
    }

    if (gameboy->gbc && address == REGISTER_HDMA1) {
        return gameboy->hdma.source_address >> 8;
    }

    if (gameboy->gbc && address == REGISTER_HDMA2) {
        return gameboy->hdma.source_address & 0xFF;
    }

    if (gameboy->gbc && address == REGISTER_HDMA3) {
        return gameboy->hdma.destination_offset >> 8;
    }

    if (gameboy->gbc && address == REGISTER_HDMA4) {
        return gameboy->hdma.destination_offset & 0xFF;
    }

    if (gameboy->gbc && address == REGISTER_HDMA5) {
        bool active = gameboy->hdma.run_on_hblank; // if HDMA is configured to run without hblank then everything is copied at once
        uint8_t r = 0;

        r |= (!active) << 7;
        r |= gameboy->hdma.length & 0x7f;

        return r;
    }

    if (gameboy->gbc && address == REGISTER_BCPS) {
        uint8_t r = 0;

        r |= gameboy->ppu.background_palettes.auto_increment << 7;
        r |= gameboy->ppu.background_palettes.write_index;

        return r;
    }

    if (gameboy->gbc && address == REGISTER_BCPD) {
        struct colour_palette *p = &gameboy->ppu.background_palettes;
        uint16_t index = p->write_index;
        unsigned palette = index >> 3;
        unsigned colour_index = (index >> 1) & 3;
        bool high = index & 1;
        uint16_t colour = p->colours[palette][colour_index];

        if (high) {
            return colour >> 8;
        } else {
            return colour & 0xFF;
        }
    }

    if (gameboy->gbc && address == REGISTER_OCPS) {
        uint8_t r = 0;

        r |= gameboy->ppu.sprite_palettes.auto_increment << 7;
        r |= gameboy->ppu.sprite_palettes.write_index;

        return r;
    }

    if (gameboy->gbc && address == REGISTER_OCPD) {
        struct colour_palette *p = &gameboy->ppu.sprite_palettes;
        uint16_t index = p->write_index;
        unsigned palette = index >> 3;
        unsigned colour_index = (index >> 1) & 3;
        bool high = index & 1;
        uint16_t colour = p->colours[palette][colour_index];

        if (high) {
            return colour >> 8;
        } else {
            return colour & 0xff;
        }
    }

    if (gameboy->gbc && address == REGISTER_SVBK) {
        return gameboy->internal_ram_high_bank | 0xF8;
    }

    printf("Unsupported bus read at address 0x%04x\n", address);

    return 0xFF;
}

// write one byte (value) to memory at address
void write_bus(struct emulator *gameboy, uint16_t address, uint8_t value) {
    if (address >= ROM_BASE && address < ROM_END) {
        write_cart_rom(gameboy, address - ROM_BASE, value);
        return;
    }

    if (address >= ZERO_PAGE_RAM_BASE && address < ZERO_PAGE_RAM_END) {
        gameboy->zero_page_ram[address - ZERO_PAGE_RAM_BASE] = value;
        return;
    }

    if (address >= INTERNAL_RAM_BASE && address < INTERNAL_RAM_END) {
        uint16_t offset = get_internal_ram_offset(gameboy, address - INTERNAL_RAM_BASE);

        gameboy->internal_ram[offset] = value;
        return;
    }

    if (address >= INTERNAL_RAM_ECHO_BASE && address < INTERNAL_RAM_ECHO_END) {
        uint16_t offset = get_internal_ram_offset(gameboy, address - INTERNAL_RAM_ECHO_BASE);

        gameboy->internal_ram[offset] = value;
        return;
    }

    if (address >= VIDEO_RAM_BASE && address < VIDEO_RAM_END) {
        uint16_t offset = address - VIDEO_RAM_BASE;

        offset += 0x2000 * gameboy->video_ram_high_bank;

        sync_ppu(gameboy);
        gameboy->video_ram[offset] = value;
        return;
    }

    if (address >= CARTRIDGE_RAM_BASE && address < CARTRIDGE_RAM_END) {
        write_cart_ram(gameboy, address - CARTRIDGE_RAM_BASE, value);
        return;
    }

    if (address >= OAM_BASE && address < OAM_END) {
        sync_ppu(gameboy);
        gameboy->ppu.oam[address - OAM_BASE] = value;
        return;
    }

    if (address == REGISTER_INPUT) {
        select_gamepad(gameboy, value);
        return;
    }

    if (address == REGISTER_SB) {
        return;
    }

    if (address == REGISTER_SC) {
        return;
    }

    if (address == REGISTER_DIV) {
        sync_timer(gameboy);

        gameboy->timer.divider_counter = 0;
        return;
    }

    if (address == REGISTER_TIMA) {
        sync_timer(gameboy);
        gameboy->timer.counter = value;
        sync_timer(gameboy);
        return;
    }

    if (address == REGISTER_TMA) {
        sync_timer(gameboy);
        gameboy->timer.modulo = value;
        sync_timer(gameboy);
        return;
    }

    if (address == REGISTER_TAC) {
        set_timer_configuration(gameboy, value);
        return;
    }

    if (address == REGISTER_IF) {
        gameboy->interrupt_request.interrupt_request_flags = value | 0xE0;
        return;
    }

    if (address == REGISTER_IE) {
        gameboy->interrupt_request.interrupt_request_enable = value;
        return;
    }

    if (address == REGISTER_NR10) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            reload_spu_sweep(&gameboy->spu.nr1.sweep, value);
        }
        return;
    }

    if (address == REGISTER_NR11) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr1.wave.duty_cycle = value >> 6;
            gameboy_spu_duration_reload(&gameboy->spu.nr1.duration, GB_SPU_NR1_T1_MAX, value & 0x3F);
        }
        return;
    }

    if (address == REGISTER_NR12) {
        if (gameboy->spu.enable) {
            gameboy->spu.nr1.envelope_configuration = value; // envelope configuration takes effect on sound start
        }
        return;
    }

    if (address == REGISTER_NR13) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr1.sweep.divider.offset &= 0x700;
            gameboy->spu.nr1.sweep.divider.offset |= value;
        }
        return;
    }

    if (address == REGISTER_NR14) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr1.sweep.divider.offset &= 0xFF;
            gameboy->spu.nr1.sweep.divider.offset |= ((uint16_t)value & 7) << 8;

            gameboy->spu.nr1.duration.enable = value & 0x40;

            if (value & 0x80) {
                start_spu_nr1(gameboy);
            }
        }
        return;
    }

    if (address == REGISTER_NR21) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr2.wave.duty_cycle = value >> 6;
            reload_spu_duration(&gameboy->spu.nr2.duration, GB_SPU_NR2_T1_MAX, value & 0x3F);
        }
        return;
    }

    if (address == REGISTER_NR22) {
        if (gameboy->spu.enable) {
            gameboy->spu.nr2.envelope_configuration = value; // envelope configuration takes effect on sound start
        }
        return;
    }

    if (address == REGISTER_NR23) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr2.divider.offset &= 0x700;
            gameboy->spu.nr2.divider.offset |= value;
        }
        return;
    }

    if (address == REGISTER_NR24) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr2.divider.offset &= 0xFF;
            gameboy->spu.nr2.divider.offset |= ((uint16_t)value & 7) << 8;

            gameboy->spu.nr2.duration.enable = value & 0x40;

            if (value & 0x80) {
                start_spu_nr2(gameboy);
            }
        }
        return;
    }

    if (address == REGISTER_NR30) {
        if (gameboy->spu.enable) {
            bool enable = (value & 0x80); // enabling doesn't start Sound 3 until 0x80 is written to NR34

            sync_spu(gameboy);
            gameboy->spu.nr3.enable = enable;
            if (!enable) {
                gameboy->spu.nr3.running = false;
            }
        }
        return;
    }

    if (address == REGISTER_NR31) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr3.t1 = value;
            reload_spu_duration(&gameboy->spu.nr3.duration, GB_SPU_NR3_T1_MAX, value);
        }
        return;
    }

    if (address == REGISTER_NR32) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr3.volume_shift = (value >> 5) & 3;
        }
        return;
    }

    if (address == REGISTER_NR33) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr3.divider.offset &= 0x700;
            gameboy->spu.nr3.divider.offset |= value;
        }
        return;
    }

    if (address == REGISTER_NR34) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr3.divider.offset &= 0xFF;
            gameboy->spu.nr3.divider.offset |= ((uint16_t)value & 7) << 8;

            gameboy->spu.nr3.duration.enable = value & 0x40;

            if (value & 0x80) {
                start_spu_nr3(gameboy);
            }
        }
        return;
    }

    if (address == REGISTER_NR41) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            reload_spu_duration(&gameboy->spu.nr4.duration, GB_SPU_NR4_T1_MAX, value & 0x3F);
        }
        return;
    }

    if (address == REGISTER_NR42) {
        if (gameboy->spu.enable) {
            gameboy->spu.nr4.envelope_configuration = value; // envelope configuration takes effect on sound start
        }
        return;
    }

    if (address == REGISTER_NR43) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr4.lfsr_configuration = value;
        }
        return;
    }

    if (address == REGISTER_NR44) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.nr4.duration.enable = value & 0x40;

            if (value & 0x80) {
                start_spu_nr4(gameboy);
            }
        }
        return;
    }

    if (address == REGISTER_NR50) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.output_level = value;
            update_spu_sound_amp(gameboy);
        }
        return;
    }

    if (address == REGISTER_NR51) {
        if (gameboy->spu.enable) {
            sync_spu(gameboy);
            gameboy->spu.sound_mux = value;
            update_spu_sound_amp(gameboy);
        }
        return;
    }

    if (address == REGISTER_NR52) {
        bool enable = value & 0x80;

        if (gameboy->spu.enable == enable) {
            return;
        }

        sync_spu(gameboy);

        if (!enable) {
            reset_spu(gameboy);
        }

        gameboy->spu.enable = enable;
        return;
    }

    if (address >= NR3_RAM_BASE && address < NR3_RAM_END) {
        gameboy->spu.nr3.ram[address - NR3_RAM_BASE] = value;
        return;
    }

    if (address == REGISTER_LCDC) {
        set_lcdc(gameboy, value);
        return;
    }

    if (address == REGISTER_LCD_STAT) {
        set_lcd_stat(gameboy, value);
        return;
    }

    if (address == REGISTER_SCROLL_Y) {
        sync_ppu(gameboy);
        gameboy->ppu.scroll_y = value;
        return;
    }

    if (address == REGISTER_SCROLL_X) {
        sync_ppu(gameboy);
        gameboy->ppu.scroll_x = value;
        return;
    }

    if (address == REGISTER_LYC) {
        gameboy->ppu.lyc = value;
        return;
    }

    if (address == REGISTER_DMA) {
        start_dma(gameboy, value);
        return;
    }

    if (address == REGISTER_BACKGROUND_PALETTE) {
        sync_ppu(gameboy);
        gameboy->ppu.background_palette = value;
        return;
    }

    if (address == REGISTER_OBP0) {
        sync_ppu(gameboy);
        gameboy->ppu.sprite_palette0 = value;
        return;
    }

    if (address == REGISTER_OBP1) {
        sync_ppu(gameboy);
        gameboy->ppu.sprite_palette1 = value;
        return;
    }

    if (address == REGISTER_WINDOW_Y) {
        sync_ppu(gameboy);
        gameboy->ppu.window_y = value;
        return;
    }

    if (address == REGISTER_WINDOW_X) {
        sync_ppu(gameboy);
        gameboy->ppu.window_x = value;
        return;
    }

    if (gameboy->gbc && address == REGISTER_VBK) {
        gameboy->video_ram_high_bank = value & 1;
        return;
    }

    if (gameboy->gbc && address == REGISTER_HDMA1) {
        gameboy->hdma.source_address &= 0xFF;
        gameboy->hdma.source_address |= (value << 8);
        return;
    }

    if (gameboy->gbc && address == REGISTER_HDMA2) {
        gameboy->hdma.source_address &= 0xFF00;
        gameboy->hdma.source_address |= value & 0xF0; // lower 4 bits are ignored
        return;
    }

    if (gameboy->gbc && address == REGISTER_HDMA3) {
        gameboy->hdma.destination_offset &= 0xFF;
        gameboy->hdma.destination_offset |= (value << 8);
        return;
    }

    if (gameboy->gbc && address == REGISTER_HDMA4) {
        gameboy->hdma.destination_offset &= 0xFF00;
        gameboy->hdma.destination_offset |= value & 0xF0; // lower 4 bits are ignored
        return;
    }

    if (gameboy->gbc && address == REGISTER_HDMA5) {
        bool run_on_hblank = value & 0x80;

        gameboy->hdma.length = value & 0x7F;

        if (!run_on_hblank && gameboy->hdma.run_on_hblank) {
            // stop the current transfer
            sync_ppu(gameboy);
            gameboy->hdma.run_on_hblank = false;
        } else {
            start_hdma(gameboy, run_on_hblank);
        }

        return;
    }

    if (gameboy->gbc && address == REGISTER_BCPS) {
        gameboy->ppu.background_palettes.auto_increment = value & 0x80;
        gameboy->ppu.background_palettes.write_index = value & 0x3f;
        return;
    }

    if (gameboy->gbc && address == REGISTER_BCPD) {
        struct colour_palette *p = &gameboy->ppu.background_palettes;
        uint16_t index = p->write_index;
        unsigned palette = index >> 3;
        unsigned colour_index = (index >> 1) & 3;
        bool high = index & 1;
        uint16_t colour = p->colours[palette][colour_index];

        if (high) {
            colour &= 0xFF;
            colour |= value << 8;
        } else {
            colour &= 0xFF00;
            colour |= value;
        }

        p->colours[palette][colour_index] = colour;

        if (p->auto_increment) {
            p->write_index = (p->write_index + 1) & 0x3F;
        }

        return;
    }

    if (gameboy->gbc && address == REGISTER_OCPS) {
        gameboy->ppu.sprite_palettes.auto_increment = value & 0x80;
        gameboy->ppu.sprite_palettes.write_index = value & 0x3F;
        return;
    }

    if (gameboy->gbc && address == REGISTER_OCPD) {
        struct colour_palette *p = &gameboy->ppu.sprite_palettes;
        uint16_t index = p->write_index;
        unsigned palette = index >> 3;
        unsigned colour_index = (index >> 1) & 3;
        bool high = index & 1;
        uint16_t colour = p->colours[palette][colour_index];

        if (high) {
            colour &= 0xFF;
            colour |= value << 8;
        } else {
            colour &= 0xFF00;
            colour |= value;
        }

        p->colours[palette][colour_index] = colour;

        if (p->auto_increment) {
            p->write_index = (p->write_index + 1) & 0x3F;
        }

        return;
    }

    if (gameboy->gbc && address == REGISTER_SVBK) {
        gameboy->internal_ram_high_bank = value & 7;
        return;
    }

    printf("Unsupported bus write at address 0x%04x [value=0x%02x]\n", address, value);
}

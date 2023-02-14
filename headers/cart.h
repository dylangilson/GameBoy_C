/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#ifndef CART_H
#define CART_H

enum cart_model {
    GB_CART_SIMPLE, // no mapper: 2 ROM banks, no RAM
    GB_CART_MBC1, // MBC1 mapper: up to 64 ROM banks, 4 RAM banks
    GB_CART_MBC2, // MBC2 mapper: up to 16 ROM banks, one single 512 * 4bit RAM
    GB_CART_MBC3, // MBC3 mapper: up to 128 ROM banks, 4 RAM banks, optional RTC
    GB_CART_MBC5, // MBC5 mapper: up to 512 ROM banks, 16 RAM banks
} cart_model;

struct gameboy_cart {
    uint8_t *rom; // full ROM contents
    unsigned rom_length; // ROM length in bytes
    unsigned rom_banks; // number of ROM banks ; each bank is 16KB
    unsigned current_rom_bank;
    uint8_t *ram; // full RAM contents
    unsigned ram_length; // RAM length in bytes
    unsigned ram_banks; // number of RAM banks ; each bank is 8KB
    unsigned current_ram_bank;
    bool ram_write_protected; // true if RAM is write-protected ; read-only
    enum cart_model model; // type of cartridge
    bool mbc1_bank_ram; // false if MBC1 cart operates in 128 ROM banks / 1 RAM bank ; otherwise true if 32 ROM banks / 4 RAM banks
    char *save_file;
    bool write_ram_flag; // set to true when RAM has been written to
    bool has_rtc; // true if cartridge has RTC
    struct gameboy_rtc rtc; // RTC state ; if cartridge has one
} gameboy_cart;

void load_cart_error(struct gameboy_cart *cart, FILE *file);
void load_cart(struct emulator *gameboy, const char *rom_path);
void unload_cart(struct emulator *gameboy);
void sync_cart(struct emulator *gameboy);
uint8_t read_cart_rom(struct emulator *gameboy, uint16_t address);
void write_cart_rom(struct emulator *gameboy, uint16_t address, uint8_t value);
uint8_t read_cart_ram(struct emulator *gameboy, uint16_t address);
void write_cart_ram(struct emulator *gameboy, uint16_t address, uint8_t value);

#endif

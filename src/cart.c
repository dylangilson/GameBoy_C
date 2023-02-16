/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 13, 2023
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "emulator.h"

#define GB_ROM_BANK_SIZE (16 * 1024) // 16KB ROM banks
#define GB_RAM_BANK_SIZE (8 * 1024) // 8KB RAM banks
#define GB_CART_MIN_SIZE (GB_ROM_BANK_SIZE * 2) // GB ROMs are at least 32KB (2 banks)
#define GB_CART_MAX_SIZE (32U * 1024 * 1024) // largest licensed GB cartridge is 8MB ; but allocate extra space for homebrew GB ROMs
#define GB_CART_OFF_TITLE 0x134
#define GB_CART_OFF_GBC 0x143
#define GB_CART_OFF_TYPE 0x147
#define GB_CART_OFF_ROM_BANKS 0x148
#define GB_CART_OFF_RAM_BANKS 0x149

static void get_cart_rom_title(struct emulator *gameboy, char title[17]) {
    struct gameboy_cart *cart = &gameboy->cart;
    int i;

    for (i = 0; i < 16; i++) {
        char c = cart->rom[GB_CART_OFF_TITLE + i];

        if (c == 0) {
            break; // end of ROM title
        }

        if (!isprint(c)) {
            c = '?';
        }

        title[i] = c;
    }

    title[i] = '\0'; // null-terminating value to end string
}

void load_cart_error(struct gameboy_cart *cart, FILE *file) {
    if (cart->rom) {
        free(cart->rom);
        cart->rom = NULL;
    }

    if (cart->ram) {
        free(cart->ram);
        cart->ram = NULL;
    }

    if (cart->save_file) {
        free(cart->save_file);
    }

    if (file) {
        fclose(file);
    }

    exit(EXIT_FAILURE);
}

void load_cart(struct emulator *gameboy, const char *rom_path) {
    struct gameboy_cart *cart = &gameboy->cart;
    FILE *file = fopen(rom_path, "rb");
    long length;
    size_t nread;
    char rom_title[17];
    bool has_battery_backup = false;

    cart->rom = NULL;
    cart->current_rom_bank = 1;
    cart->ram = NULL;
    cart->current_ram_bank = 0;
    cart->ram_write_protected = true;
    cart->mbc1_bank_ram = false;
    cart->save_file = NULL;
    cart->write_ram_flag = false;
    cart->has_rtc = false;

    if (file == NULL) {
        perror("Can't open ROM file");
        load_cart_error(cart, file);
    }

    if (fseek(file, 0, SEEK_END) == -1 || (length = ftell(file)) == -1 || fseek(file, 0, SEEK_SET) == -1) {
        fclose(file);
        perror("Can't get ROM file length");
        load_cart_error(cart, file);
    }

    if (length == 0) {
        fprintf(stderr, "ROM file is empty!\n");
        load_cart_error(cart, file);
    }

    if (length > GB_CART_MAX_SIZE) {
        fprintf(stderr, "ROM file is too big!\n");
        load_cart_error(cart, file);
    }

    if (length < GB_CART_MIN_SIZE) {
        fprintf(stderr, "ROM file is too small!\n");
        load_cart_error(cart, file);
    }

    cart->rom_length = length;
    cart->rom = calloc(1, cart->rom_length);
    if (cart->rom == NULL) {
        perror("Can't allocate ROM buffer");
        load_cart_error(cart, file);
    }

    nread = fread(cart->rom, 1, cart->rom_length, file);
    if (nread < cart->rom_length) {
        fprintf(stderr, "Failed to load ROM file (read %u bytes, expected %u)\n", (unsigned)nread, cart->rom_length);
        load_cart_error(cart, file);
    }

    // determine the number of ROM banks for this cartridge
    switch (cart->rom[GB_CART_OFF_ROM_BANKS]) {
        case 0:
            cart->rom_banks = 2;
            break;
        case 1:
            cart->rom_banks = 4;
            break;
        case 2:
            cart->rom_banks = 8;
            break;
        case 3:
            cart->rom_banks = 16;
            break;
        case 4:
            cart->rom_banks = 32;
            break;
        case 5:
            cart->rom_banks = 64;
            break;
        case 6:
            cart->rom_banks = 128;
            break;
        case 7:
            cart->rom_banks = 256;
            break;
        case 8:
            cart->rom_banks = 512;
            break;
        case 0x52:
            cart->rom_banks = 72;
            break;
        case 0x53:
            cart->rom_banks = 80;
            break;
        case 0x54:
            cart->rom_banks = 96;
            break;
        default:
            fprintf(stderr, "Unknown ROM size configuration: %x\n", cart->rom[GB_CART_OFF_ROM_BANKS]);
            load_cart_error(cart, file);
    }

    // ensure the ROM file size works with the declared number of ROM banks
    if (cart->rom_length < cart->rom_banks * GB_ROM_BANK_SIZE) {
        fprintf(stderr, "ROM file is too small to hold the declared %d ROM banks\n", cart->rom_banks);
        load_cart_error(cart, file);
    }

    // determine the number of RAM banks for this cartridge
    switch (cart->rom[GB_CART_OFF_RAM_BANKS]) {
        case 0:
            // no RAM
            cart->ram_banks = 0;
            cart->ram_length = 0;
            break;
        case 1:
            // one RAM bank but only 2kB (1/4 of a typical bank)
            cart->ram_banks = 1;
            cart->ram_length = GB_RAM_BANK_SIZE / 4;
            break;
        case 2:
            cart->ram_banks = 1;
            cart->ram_length = GB_RAM_BANK_SIZE;
            break;
        case 3:
            cart->ram_banks = 4;
            cart->ram_length = GB_RAM_BANK_SIZE * 4;
            break;
        case 4:
            cart->ram_banks = 16;
            cart->ram_length = GB_RAM_BANK_SIZE * 16;
            break;
        default:
            fprintf(stderr, "Unknown RAM size configuration: %x\n", cart->rom[GB_CART_OFF_RAM_BANKS]);
            load_cart_error(cart, file);
    }

    switch (cart->rom[GB_CART_OFF_TYPE]) {
        case 0x00:
            cart->model = GB_CART_SIMPLE;
            break;
        case 0x01: // MBC1 ; no RAM
        case 0x02: // MBC1 ; with RAM
        case 0x03: // MBC1 ; with RAM and battery backup
            cart->model = GB_CART_MBC1;
            break;
        case 0x05: // MBC2
        case 0x06: // MBC2 ; with battery backup
            cart->model = GB_CART_MBC2;
            cart->ram_banks = 1; // MBC2 always has 512 * 4bits of RAM available
            cart->ram_length = 512; // allocate 512 bytes for convenience, but only the low 4 bits should be used
            break;
        case 0x0F: // MBC3 ; with battery backup and RTC
        case 0x10: // MBC3 ; with RAM, battery backup and RTC
        case 0x11: // MBC3 ; no RAM
        case 0x12: // MBC3 ; with RAM
        case 0x13: // MBC3 ; with RAM and battery backup
            cart->model = GB_CART_MBC3;
            break;
        case 0x19: // MBC5 ; no RAM
        case 0x1A: // MBC5 ; with RAM
        case 0x1B: // MBC5 ; with RAM and battery backup
            cart->model = GB_CART_MBC5;
            break;
        default:
            fprintf(stderr, "Unsupported cartridge type %x!\n", cart->rom[GB_CART_OFF_TYPE]);
            load_cart_error(cart, file);
    }

    // check if cart has a battery for memory backup
    switch (cart->rom[GB_CART_OFF_TYPE]) {
        case 0x03:
        case 0x06:
        case 0x09:
        case 0x0F:
        case 0x10:
        case 0x13:
        case 0x1B:
        case 0x1E:
        case 0xFF:
            has_battery_backup = true;
    }

    // check if cart has an RTC
    switch (cart->rom[GB_CART_OFF_TYPE]) {
        case 0xF:
        case 0x10:
            cart->has_rtc = true;
            break;
    }

    // allocate RAM buffer
    if (cart->ram_length > 0) {
        cart->ram = calloc(1, cart->ram_length);
        if (cart->ram == NULL) {
            perror("Can't allocate RAM buffer!\n");
            load_cart_error(cart, file);
        }
    } else if (!cart->has_rtc) {
        has_battery_backup = false; // memory backup isn't possible without RAM or RTC
    }

    if (has_battery_backup) {
        const size_t path_len = strlen(rom_path);
        FILE *file = NULL;
        size_t pos;

        cart->save_file = malloc(path_len + strlen(".sav"));
        if (cart->save_file == NULL) {
            perror("malloc failed");
            load_cart_error(cart, file);
        }

        strcpy(cart->save_file, rom_path);

        // scan for extension
        for (pos = path_len - 1; pos > 0; pos--) {
            if (cart->save_file[pos] == '.') {
                cart->save_file[pos] = '\0'; // found the extension ; truncate it
                break;
            }
        }

        strcat(cart->save_file, ".sav");

        file = fopen(cart->save_file, "rb"); // attempt to open save file if it already exists
        if (file != NULL) {
            // the file exists ; load RAM contents
            if (cart->ram_length > 0) {
                nread = fread(cart->ram, 1, cart->ram_length, file);
            } else {
                nread = 0;
            }

            if (nread != cart->ram_length) {
                fprintf(stderr, "RAM save file is too small!\n");
                fclose(file);
                load_cart_error(cart, file);
            }

            if (cart->has_rtc) {
                load_rtc(gameboy, file);
            }

            fclose(file);
            printf("Loaded RAM save from '%s'\n", cart->save_file);
        } else {
            // no active save file
            if (cart->has_rtc) {
                init_rtc(gameboy);
            }
        }

    }

    fclose(file);
    
    gameboy->gbc = (cart->rom[GB_CART_OFF_GBC] & 0x80); // check if we have a DMG or GBC game

    get_cart_rom_title(gameboy, rom_title);

    printf("Succesfully Loaded %s\n", rom_path);
    printf("Title: '%s'\n", rom_title);

    return;
}

static void save_cart_ram(struct emulator *gameboy) {
    struct gameboy_cart *cart = &gameboy->cart;
    FILE *file;

    if (cart->save_file == NULL) {
        return; // no battery backup
    }

    if (!cart->write_ram_flag) {
        return; // no changes to RAM since last save
    }

    file = fopen(cart->save_file, "wb");
    if (file == NULL) {
        fprintf(stderr, "Can't create or open save file '%s': %s\n", cart->save_file, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (cart->ram_length > 0) {
        // dump RAM to file
        if (fwrite(cart->ram, 1, cart->ram_length, file) < 0) {
            perror("fwrite failed!\n");
            fclose(file);
            exit(EXIT_FAILURE);
        }
    }

    if (cart->has_rtc) {
        dump_rtc(gameboy, file);
    }

    fflush(file);
    fclose(file);
    
    cart->write_ram_flag = false;
}

void unload_cart(struct emulator *gameboy) {
    struct gameboy_cart *cart = &gameboy->cart;

    save_cart_ram(gameboy);

    if (cart->save_file) {
        free(cart->save_file);
    }

    if (cart->rom) {
        free(cart->rom);
        cart->rom = NULL;
    }

    if (cart->ram) {
        free(cart->ram);
        cart->ram = NULL;
    }
}

void sync_cart(struct emulator *gameboy) {
    save_cart_ram(gameboy);
    sync_next(gameboy, GB_SYNC_CART, GB_SYNC_NEVER);
}

uint8_t read_cart_rom(struct emulator *gameboy, uint16_t address) {
    struct gameboy_cart *cart = &gameboy->cart;
    unsigned rom_offset = address;

    switch (cart->model) {
        case GB_CART_SIMPLE:
            break; // no mapper
        case GB_CART_MBC1:
            if (address >= GB_ROM_BANK_SIZE) {
                unsigned bank = cart->current_rom_bank; // bank 1 can be remapped through this controller

                if (cart->mbc1_bank_ram) {
                    bank %= 32; // when MBC1 is configured to bank RAM it can only address 16 ROM banks
                } else {
                    bank %= 128;
                }

                if (bank == 0) {
                    bank = 1; // bank 0 can't be mirrored that way ; using a bank of 0 is the same thing as using 1
                }

                bank %= cart->rom_banks;
                rom_offset += (bank - 1) * GB_ROM_BANK_SIZE;
            }
            break;
        case GB_CART_MBC2:
        case GB_CART_MBC3:
            if (address >= GB_ROM_BANK_SIZE) {
                rom_offset += (cart->current_rom_bank - 1) * GB_ROM_BANK_SIZE;
            }
            break;
        case GB_CART_MBC5:
            if (address >= GB_ROM_BANK_SIZE) {
                unsigned bank = cart->current_rom_bank % cart->rom_banks; // handle this carefully, because bank 0 can be remapped as bank 1 with this controller

                rom_offset -= GB_ROM_BANK_SIZE;
                rom_offset += bank * GB_ROM_BANK_SIZE;
            }
            break;
        default:
            exit(EXIT_FAILURE); // should not be reached
    }

    return cart->rom[rom_offset];
}

void write_cart_rom(struct emulator *gameboy, uint16_t address, uint8_t value) {
    struct gameboy_cart *cart = &gameboy->cart;

    switch (cart->model) {
        case GB_CART_SIMPLE:
            break;
        case GB_CART_MBC1:
            if (address < 0x2000) {
                cart->ram_write_protected = ((value & 0xF) != 0xA);
            } else if (address < 0x4000) {
                // set ROM bank, bits [4:0]
                cart->current_rom_bank &= ~0x1F;
                cart->current_rom_bank |= value & 0x1F;
            } else if (address < 0x6000) {
                // set RAM bank OR ROM bank [6:5] depending on the mode
                cart->current_rom_bank &= 0x1F;
                cart->current_rom_bank |= (value & 3) << 5;

                if (cart->ram_banks > 0) {
                    cart->current_ram_bank = (value & 3) % cart->ram_banks;
                }
            } else {
                cart->mbc1_bank_ram = value & 1; // change MBC1 banking mode
            }

            break;
        case GB_CART_MBC2:
            if (address < 0x2000) {
                cart->ram_write_protected = ((value & 0xF) != 0xA);
            } else if (address < 0x4000) {
                cart->current_rom_bank = value & 0xF;
                if (cart->current_rom_bank == 0) {
                    cart->current_rom_bank = 1;
                }
            }

            break;
        case GB_CART_MBC3:
            if (address < 0x2000) {
                cart->ram_write_protected = ((value & 0xF) != 0xA);
            } else if (address < 0x4000) {
                // set ROM bank
                cart->current_rom_bank = (value & 0x7F) % cart->rom_banks;
                if (cart->current_rom_bank == 0) {
                    cart->current_rom_bank = 1;
                }
            } else if (address < 0x6000) {
                // set RAM bank (v < 3) OR RTC access
                cart->current_ram_bank = value;
            } else if (address < 0x8000) {
                if (cart->has_rtc) {
                    latch_rtc(gameboy, value == 1);
                }
            }

            break;
        case GB_CART_MBC5:
            if (address < 0x2000) {
                cart->ram_write_protected = ((value & 0xF) != 0xA);
            } else if (address < 0x3000) {
                // set ROM bank ; low 8 bits
                cart->current_rom_bank &= 0x100;
                cart->current_rom_bank |= value;
            } else if (address < 0x4000) {
                // set ROM bank ; MSB
                cart->current_rom_bank &= 0xFF;
                cart->current_rom_bank |= (value & 1) << 8;
            } else if (address < 0x6000) {
                // set RAM bank
                if (cart->ram_banks > 0) {
                    cart->current_ram_bank = (value & 0xF) % cart->ram_banks;
                }
            }

            break;
        default:
            exit(EXIT_FAILURE); // should not be reached
    }
}


unsigned get_cart_mbc1_ram_offset(struct emulator *gameboy, uint16_t address) {
    struct gameboy_cart *cart = &gameboy->cart;
    unsigned bank;

    if (cart->ram_banks == 1) {
        return address % cart->ram_length; // cartridges which only have one RAM bank can have only a partial 2KB RAM chip that's mirrored 4 times
    }

    bank = cart->current_ram_bank;

    if (cart->mbc1_bank_ram) {
        bank %= 4;
    } else {
        bank = 0; // in this mode, only one bank supported
    }

    return bank * GB_RAM_BANK_SIZE + address;
}

uint8_t read_cart_ram(struct emulator *gameboy, uint16_t address) {
    struct gameboy_cart *cart = &gameboy->cart;
    unsigned ram_offset;

    switch (cart->model) {
        case GB_CART_SIMPLE:
            return 0xFF; // no RAM
        case GB_CART_MBC1:
            if (cart->ram_banks == 0) {
                return 0xFF; // no RAM
            }

            ram_offset = get_cart_mbc1_ram_offset(gameboy, address);

            break;
        case GB_CART_MBC2:
            ram_offset = address % 512;
            break;
        case GB_CART_MBC3:
            if (cart->current_ram_bank <= 3) {
                unsigned b;

                if (cart->ram_banks == 0) {
                    return 0xFF; // no RAM
                }

                // RAM access
                b = cart->current_ram_bank % cart->ram_banks;
                ram_offset = b * GB_RAM_BANK_SIZE + address;
            } else {
                // RTC access ; only accessible when the RAM is not write-protected (even for reads)
                if (cart->has_rtc && !cart->ram_write_protected) {
                    return read_rtc(gameboy, cart->current_ram_bank);
                } else {
                    return 0xFF;
                }
            }

            break;
        case GB_CART_MBC5:
            if (cart->ram_banks == 0) {
                return 0xFF; // no RAM
            }

            ram_offset = cart->current_ram_bank * GB_RAM_BANK_SIZE + address;
            break;
        default:
            return 0xFF; // should not be reached
    }

    return cart->ram[ram_offset];
}

void write_cart_ram(struct emulator *gameboy, uint16_t address, uint8_t value) {
    struct gameboy_cart *cart = &gameboy->cart;
    unsigned ram_offset = 0;

    if (cart->ram_write_protected) {
        return;
    }

    switch (cart->model) {
        case GB_CART_SIMPLE:
            return; // no RAM
        case GB_CART_MBC1:
            if (cart->ram_banks == 0) {
                return; // no RAM
            }

            ram_offset = get_cart_mbc1_ram_offset(gameboy, address);
            break;
        case GB_CART_MBC2:
            ram_offset = address % 512;
            value |= 0xF0; // MBC2 only has 4 bits per address, so the high nibble is unusable
            break;
        case GB_CART_MBC3:
            if (cart->current_ram_bank <= 3) {
                unsigned b;

                if (cart->ram_banks == 0) {
                    return; // no RAM
                }

                // RAM access
                b = cart->current_ram_bank % cart->ram_banks;
                ram_offset = b * GB_RAM_BANK_SIZE + address;
            } else {
                // RTC access ; only accessible when the RAM is not write-protected (even for reads)
                if (cart->has_rtc) {
                    write_rtc(gameboy, cart->current_ram_bank, value);
                }

                if (cart->save_file) {
                    cart->write_ram_flag = true;
                    sync_next(gameboy, GB_SYNC_CART, CPU_FREQUENCY_HZ * 3); // schedule a save in a while, even if there are no changes
                }
            }

            break;
        case GB_CART_MBC5:
            if (cart->ram_banks == 0) {
                return; // no RAM
            }

            ram_offset = cart->current_ram_bank * GB_RAM_BANK_SIZE + address;
            break;
        default:
            exit(EXIT_FAILURE); // should not be reached
    }

    cart->ram[ram_offset] = value;
}

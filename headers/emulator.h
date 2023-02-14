/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#ifndef EMULATOR_H
#define EMULATOR_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
// #include <semaphore.h>

struct emulator;

#include "sync.h"
#include "interrupts.h"
#include "cpu.h"
#include "bus.h"
#include "rtc.h"
#include "cart.h"
#include "ppu.h"
#include "gamepad.h"
#include "dma.h"
#include "hdma.h"
#include "timer.h"
#include "spu.h"
#include "ui.h"
#include "ui.h"

#define EMULATION_SPEED 1U // NOTE: change this to increase / decrease game speed
#define CPU_FREQUENCY_HZ 4194304U * EMULATION_SPEED // CPU frequency ; Super GameBoy runs slightly faster at 4.295454MHz

struct emulator {
    bool gbc; // true if emulating a GBC ; false if emulating a DMG
    bool quit; // set to true by user if they wish to end the emulation
    struct gameboy_interrupt_request interrupt_request;
    struct gameboy_ui ui;
    struct gameboy_sync sync;
    struct gameboy_cpu cpu;
    struct gameboy_cart cart;
    struct gameboy_ppu ppu;
    struct gameboy_gamepad gamepad;
    struct gameboy_dma dma;
    struct gameboy_hdma hdma;
    struct gameboy_timer timer;
    struct gameboy_spu spu;
    uint32_t timestamp; // counter of how many CPU cycles have elapsed ; used to synchronize other devices
    uint8_t internal_ram[0x8000]; // 8KiB on DMG ; 32 KiB on GBC
    uint8_t internal_ram_high_bank; // always 1 on DMG ; in range [1, 7] on GBC
    uint8_t zero_page_ram[0x7F];
    uint8_t video_ram[0x4000]; // 8KiB on DMG ; 16KiB on GBC
    bool video_ram_high_bank; // always false on DMG
} emulator;

#endif

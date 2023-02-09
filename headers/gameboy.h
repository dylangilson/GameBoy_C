/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

// TODO: stopped at bus.c line 182

#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h>

#include "bus.h"
#include "cart.h"
#include "cpu.h"
#include "dma.h"
#include "hdma.h"
#include "gamepad.h"
#include "interrupts.h"
#include "ppu.h"
#include "rtc.h"
#include "spu.h"
#include "sync.h"
#include "timer.h"
#include "ui.h"

#define CPU_FREQUENCY_HZ 4194304U // DMG CPU frequency ; Super GameBoy runs slightly faster at 4.295454MHz

struct gameboy {
    bool gbc; // true if emulating a GBC ; false if emulating a DMG
    bool quit; // set to true by user if they wish to end the emulation
    gameboy_interrupt_request interrupt_request;
    gameboy_ui ui;
    gameboy_sync sync;
    gameboy_cpu cpu;
    gameboy_cart cart;
    gameboy_ppu ppu;
    gameboy_gamepad gamepad;
    gameboy_dma dma;
    gameboy_hdma hdma;
    gameboy_timer timer;
    gameboy_spu;
    uint32_t timestamp; // counter of how many CPU cycles have elapsed ; used to synchronize other devices
    uint8_t internal_ram[0x8000]; // 8KiB on DMG ; 32 KiB on GBC
    uint8_t internal_ram_high_bank; // always 1 on DMG ; in range [1, 7] on GBC
    uint8_t zero_page_ram[0x7F];
    uint8_t video_ram[0x4000]; // 8KiB on DMG ; 16KiB on GBC
    bool video_ram_high_bank; // always false on DMG
} gameboy;

#endif

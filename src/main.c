/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 9, 2023
 */

#include <stdio.h>
#include <string.h>

#include "emulator.h"
#include "sdl.h"

// TODO fix dmg-acid2.gb's output ; window internal line counter is incorrect
// TODO fix cgb-acid2.gbc'2 output ; master priority (bit 0) is incorrect

int main(int argc, char *argv[]) {
    struct emulator *gameboy;
    const char *rom_file;

    if (argc < 2) {
        fprintf(stderr, "Not enough command line arguments provided!\n");
        return EXIT_FAILURE;
    }

    gameboy = calloc(1, sizeof(*gameboy)); // context contains semaphores ; allocate to the heap so it is visible on all threads
    if (gameboy == NULL) {
        perror("GameBoy memory allocation failed!\n");
        return EXIT_FAILURE;
    }

    // initialize semaphores before we start the UI
    for (unsigned i = 0; i < GB_SPU_SAMPLE_BUFFER_COUNT; i++) {
        struct spu_sample_buffer *buffer = &gameboy->spu.buffers[i];

        memset(buffer->samples, 0, sizeof(buffer->samples));

        sem_init(&buffer->free, 0, 0);
        sem_init(&buffer->ready, 0, 1);
    }

    init_sdl_ui(gameboy);

    rom_file = argv[1];

    load_cart(gameboy, rom_file);
    reset_sync(gameboy);
    reset_interrupt_request(gameboy);
    reset_cpu(gameboy);
    reset_ppu(gameboy);
    reset_gamepad(gameboy);
    reset_dma(gameboy);
    reset_timer(gameboy);
    reset_spu(gameboy);

    gameboy->internal_ram_high_bank = 1;
    gameboy->video_ram_high_bank = false;
    gameboy->quit = false;

    while (!gameboy->quit) {
        gameboy->ui.refresh_gamepad(gameboy);

        run_cpu_cycles(gameboy, CPU_FREQUENCY_HZ / 120); // refresh at 120Hz to maintain performance
    }

    gameboy->ui.destroy(gameboy);
    unload_cart(gameboy);

    free(gameboy);

    return 0;
}

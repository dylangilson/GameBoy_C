/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 14, 2023
 */

#include "emulator.h"

void reset_interrupt_request(struct emulator *gameboy) {
    struct gameboy_interrupt_request *interrupt_request = &gameboy->interrupt_request;

    interrupt_request->interrupt_request_flags = 0xE0;
    interrupt_request->interrupt_request_enable = 0;
}

void trigger_interrupt_request(struct emulator *gameboy, enum interrupt_request_token token) {
    struct gameboy_interrupt_request *interrupt_request = &gameboy->interrupt_request;

    interrupt_request->interrupt_request_flags |= (1U << token);
}

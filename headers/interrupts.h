/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#ifndef INTERRUPTS_H
#define INTERRUPTS_H

// Interrupt Requests

enum interrupt_request_token {
    GB_INTERRUPT_REQUEST_VSYNC, // triggered by LCD VSYNC
    GB_INTERRUPT_REQUEST_LCD_STAT, // triggered based on LCD STAT value
    GB_INTERRUPT_REQUEST_TIMER, // triggered by timer overflow
    GB_INTERRUPT_REQUEST_SERIAL, // triggered by serial transfer completion
    GB_INTERRUPT_REQUEST_INPUT, // triggered by button press
} interrupt_request_token;

struct gameboy_interrupt_request {
    uint8_t interrupt_request_flags;
    uint8_t interrupt_request_enable;
} gameboy_interrupt_request;

void reset_interrupt_request(struct emulator *gameboy);
void trigger_interrupt_request(struct emulator *gameboy, enum interrupt_request_token value);

#endif

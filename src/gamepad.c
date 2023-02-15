/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#include "emulator.h"

void reset_gamepad(struct emulator *gameboy) {
    struct gameboy_gamepad *gamepad = &gameboy->gamepad;

    gamepad->dpad_state = ~0x10;
    gamepad->dpad_selected = false;
    gamepad->buttons_state = ~0x20;
    gamepad->buttons_selected = false;
}

void set_gamepad(struct emulator *gameboy, unsigned button, bool pressed) {
    struct gameboy_gamepad *gamepad = &gameboy->gamepad;
    uint8_t *state;
    uint8_t prev_state;
    unsigned bit;

    prev_state = get_gamepad_state(gameboy);

    if (button <= GB_INPUT_DOWN) {
        state = &gamepad->dpad_state;
        bit = button;

    } else {
        state = &gamepad->buttons_state;
        bit = button - 4;
    }

    // all input is active low ; the bit is set to 0 when pressed, 1 otherwise
    if (pressed) {
        *state &= ~(1U << bit);
    } else {
        *state |= 1U << bit;
    }

    if (pressed && prev_state != get_gamepad_state(gameboy)) {
        // a button was pressed and it's currently selected ; which triggers the interrupt ; will also exit a STOP state
        trigger_interrupt_request(gameboy, GB_INTERRUPT_REQUEST_INPUT);
    }
}

void select_gamepad(struct emulator *gameboy, uint8_t selection) {
    struct gameboy_gamepad *gamepad = &gameboy->gamepad;

    gamepad->dpad_selected = ((selection & 0x10) == 0);
    gamepad->buttons_selected = ((selection & 0x20) == 0);
}

uint8_t get_gamepad_state(struct emulator *gameboy) {
    struct gameboy_gamepad *gamepad = &gameboy->gamepad;
    uint8_t value = 0xFF;

    if (gamepad->dpad_selected) {
        value &= gamepad->dpad_state;
    }

    if (gamepad->buttons_selected) {
        value &= gamepad->buttons_state;
    }

    return value;
}

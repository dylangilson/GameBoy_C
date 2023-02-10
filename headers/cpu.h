/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

// Central Processing Unit

#ifndef CPU_H
#define CPU_H

struct gameboy_cpu {
     bool interrupt_master_enable;
     bool interrupt_request_enable_next;
     bool halted;
     uint16_t program_counter;
     uint16_t stack_pointer;
     uint8_t a; // register A
     uint8_t b; // register B
     uint8_t c; // register C
     uint8_t d; // register D
     uint8_t e; // register E
     uint8_t h; // register H
     uint8_t l; // register L
     bool zero_flag;
     bool null_flag;
     bool half_carry_flag;
     bool carry_flag;
} gameboy_cpu;

void reset_cpu(struct emulator *gameboy);
int32_t run_cpu_cycles(struct emulator *gameboy, int32_t cycles);

#endif

/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 14, 2023
 */

#include "emulator.h"

void reset_cpu(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->interrupt_master_enable = false;
    cpu->interrupt_request_enable_next = false;
    cpu->halted = false;
    cpu->stack_pointer = 0xFFFE;
    cpu->a = 0;
    cpu->b = 0;
    cpu->c = 0;
    cpu->d = 0;
    cpu->e = 0;
    cpu->h = 0;
    cpu->l = 0;
    cpu->zero_flag = false;
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = false;
    cpu->program_counter = 0x100; // push execution past bootrom

    if (gameboy->gbc) {
        cpu->a = 0x11; // GBC sets bootrom register A to 0x11 before game starts ; allows cart to detect if its a DMG orGBC
    }
}

static inline void cpu_clock_tick(struct emulator *gameboy, int32_t cycles) {
    gameboy->timestamp += cycles;

    if (gameboy->timestamp >= gameboy->sync.first_event) {
        check_sync_events(gameboy); // device sync pending
    }
}

static uint8_t read_cpu(struct emulator *gameboy, uint16_t address) {
    uint8_t b = read_bus(gameboy, address);

    cpu_clock_tick(gameboy, 4);

    return b;
}

static void write_cpu(struct emulator *gameboy, uint16_t address, uint8_t value) {
    write_bus(gameboy, address, value);
    cpu_clock_tick(gameboy, 4);
}

static uint16_t get_cpu_bc(struct emulator *gameboy) {
    uint16_t b = gameboy->cpu.b;
    uint16_t c = gameboy->cpu.c;

    return (b << 8) | c;
}

static void set_cpu_bc(struct emulator *gameboy, uint16_t value) {
    gameboy->cpu.b = value >> 8;
    gameboy->cpu.c = value & 0xFF;
}

static uint16_t get_cpu_de(struct emulator *gameboy) {
    uint16_t d = gameboy->cpu.d;
    uint16_t e = gameboy->cpu.e;

    return (d << 8) | e;
}

static void set_cpu_de(struct emulator *gameboy, uint16_t value) {
    gameboy->cpu.d = value >> 8;
    gameboy->cpu.e = value & 0xFF;
}

static uint16_t get_cpu_hl(struct emulator *gameboy) {
    uint16_t h = gameboy->cpu.h;
    uint16_t l = gameboy->cpu.l;

    return (h << 8) | l;
}

static void set_cpu_hl(struct emulator *gameboy, uint16_t value) {
    gameboy->cpu.h = value >> 8;
    gameboy->cpu.l = value & 0xFF;
}

void cpu_dump(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    fprintf(stderr, "flags: %c %c %c %c  IME: %d\n", cpu->zero_flag ? 'Z' : '-', cpu->null_flag ? 'N' : '-', cpu->half_carry_flag ? 'H' : '-', cpu->carry_flag ? 'C' : '-',
                cpu->interrupt_master_enable);
    fprintf(stderr, "PC: 0x%04x [%02x %02x %02x]\n", cpu->program_counter, read_bus(gameboy, cpu->program_counter), read_bus(gameboy, cpu->program_counter + 1),
                read_bus(gameboy, cpu->program_counter + 2));
    fprintf(stderr, "SP: 0x%04x\n", cpu->stack_pointer);
    fprintf(stderr, "A : 0x%02x\n", cpu->a);
    fprintf(stderr, "B : 0x%02x  C : 0x%02x  BC : 0x%04x\n", cpu->b, cpu->c, get_cpu_bc(gameboy));
    fprintf(stderr, "D : 0x%02x  E : 0x%02x  DE : 0x%04x\n", cpu->d, cpu->e, get_cpu_de(gameboy));
    fprintf(stderr, "H : 0x%02x  L : 0x%02x  HL : 0x%04x\n", cpu->h, cpu->l, get_cpu_hl(gameboy));
}

static void cpu_load_pc(struct emulator *gameboy, uint16_t new_program_counter) {
    gameboy->cpu.program_counter = new_program_counter;

    cpu_clock_tick(gameboy, 4);
}

static void cpu_pushb(struct emulator *gameboy, uint8_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->stack_pointer = (cpu->stack_pointer - 1) & 0xFFFF;

    write_cpu(gameboy, cpu->stack_pointer, b);
}

static uint8_t cpu_popb(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    uint8_t b = read_cpu(gameboy, cpu->stack_pointer);

    cpu->stack_pointer = (cpu->stack_pointer + 1) & 0xFFFF;

    return b;
}

static void cpu_pushw(struct emulator *gameboy, uint16_t w) {
    cpu_pushb(gameboy, w >> 8);
    cpu_pushb(gameboy, w & 0xFF);
}

static uint16_t cpu_popw(struct emulator *gameboy) {
    uint16_t b0 = cpu_popb(gameboy);
    uint16_t b1 = cpu_popb(gameboy);

    return b0 | (b1 << 8);
}

static uint8_t get_cpu_next_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    uint8_t i8 = read_cpu(gameboy, cpu->program_counter);

    cpu->program_counter = (cpu->program_counter + 1) & 0xFFFF;

    return i8;
}

static uint16_t get_cpu_next_i16(struct emulator *gameboy) {
    uint16_t b0 = get_cpu_next_i8(gameboy);
    uint16_t b1 = get_cpu_next_i8(gameboy);

    return b0 | (b1 << 8);
}

typedef void (*gameboy_instruction)(struct emulator *);

static void process_nop(struct emulator *gameboy) {
    // NOP
}

static void process_undefined(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t instruction_pc = (cpu->program_counter - 1) & 0xFFFF;
    uint8_t instruction = read_bus(gameboy, instruction_pc);

    // Undefined opcode ; freezes the CPU on real hardware
    fprintf(stderr, "Undefined instruction instruction 0x%02x at 0x%04x\n", instruction, instruction_pc);
    exit(EXIT_FAILURE);
}

static void process_di(struct emulator *gameboy) {
    gameboy->cpu.interrupt_master_enable = false;
    gameboy->cpu.interrupt_request_enable_next = false;
}

static void process_ei(struct emulator *gameboy) {
    gameboy->cpu.interrupt_request_enable_next = true; // interrupts are re-enabled after the next instruction
}

static void process_stop(struct emulator *gameboy) {
    fprintf(stderr, "PROCESS STOP!\n");
    exit(EXIT_FAILURE);
}

static void process_halt(struct emulator *gameboy) {
    gameboy->cpu.halted = true; // halt and wait for interrupt
}

static void process_scf(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = true; // set carry flag
}

static void process_ccf(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = !cpu->carry_flag; // complement carry flag
}

static uint8_t cpu_inc(struct emulator *gameboy, uint8_t value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t r = (value + 1) & 0xFF;

    cpu->zero_flag = (r == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = ((value & 0xF) == 0xF); // half-carry if the low nibble is 0xF

    return r;
}

static uint8_t cpu_dec(struct emulator *gameboy, uint8_t value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t r = (value - 1) & 0xFF;

    cpu->zero_flag = (r == 0);
    cpu->null_flag = true;
    cpu->half_carry_flag = ((value & 0xF) == 0); // half-carry if the low nibble is 0

    return r;
}

static void process_inc_a(struct emulator *gameboy) {
    gameboy->cpu.a = cpu_inc(gameboy, gameboy->cpu.a);
}

static void process_inc_b(struct emulator *gameboy) {
    gameboy->cpu.b = cpu_inc(gameboy, gameboy->cpu.b);
}

static void process_inc_c(struct emulator *gameboy) {
    gameboy->cpu.c = cpu_inc(gameboy, gameboy->cpu.c);
}

static void process_inc_d(struct emulator *gameboy) {
    gameboy->cpu.d = cpu_inc(gameboy, gameboy->cpu.d);
}

static void process_inc_e(struct emulator *gameboy) {
    gameboy->cpu.e = cpu_inc(gameboy, gameboy->cpu.e);
}

static void process_inc_h(struct emulator *gameboy) {
    gameboy->cpu.h = cpu_inc(gameboy, gameboy->cpu.h);
}

static void process_inc_l(struct emulator *gameboy) {
    gameboy->cpu.l = cpu_inc(gameboy, gameboy->cpu.l);
}

static void process_inc_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    value = cpu_inc(gameboy, value);

    write_cpu(gameboy, hl, value);
}

static void process_dec_a(struct emulator *gameboy) {
    gameboy->cpu.a = cpu_dec(gameboy, gameboy->cpu.a);
}

static void process_dec_b(struct emulator *gameboy) {
    gameboy->cpu.b = cpu_dec(gameboy, gameboy->cpu.b);
}

static void process_dec_c(struct emulator *gameboy) {
    gameboy->cpu.c = cpu_dec(gameboy, gameboy->cpu.c);
}

static void process_dec_d(struct emulator *gameboy) {
    gameboy->cpu.d = cpu_dec(gameboy, gameboy->cpu.d);
}

static void process_dec_e(struct emulator *gameboy) {
    gameboy->cpu.e = cpu_dec(gameboy, gameboy->cpu.e);
}

static void process_dec_h(struct emulator *gameboy) {
    gameboy->cpu.h = cpu_dec(gameboy, gameboy->cpu.h);
}

static void process_dec_l(struct emulator *gameboy) {
    gameboy->cpu.l = cpu_dec(gameboy, gameboy->cpu.l);
}

static void process_dec_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    value = cpu_dec(gameboy, value);

    write_cpu(gameboy, hl, value);
}

// add two 16 bit values ; update the CPU flags
static uint16_t cpu_addw_set_flags(struct emulator *gameboy, uint16_t a, uint16_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    // widen to 32bits to get the carry
    uint32_t wa = a;
    uint32_t wb = b;
    uint32_t r = a + b;

    cpu->null_flag = false;
    cpu->carry_flag = r & 0x10000;
    cpu->half_carry_flag = (wa ^ wb ^ r) & 0x1000;

    cpu_clock_tick(gameboy, 4);

    return r;
}

static uint8_t cpu_sub_set_flags(struct emulator *gameboy, uint8_t a, uint8_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    // check for carry using 16bit arithmetic
    uint16_t al = a;
    uint16_t bl = b;
    uint16_t r = al - bl;

    cpu->zero_flag = !(r & 0xFF);
    cpu->null_flag = true;
    cpu->half_carry_flag = (a ^ b ^ r) & 0x10;
    cpu->carry_flag = r & 0x100;

    return r;
}

static void process_sub_a_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, cpu->a);
}

static void process_sub_a_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, cpu->b);
}

static void process_sub_a_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, cpu->c);
}

static void process_sub_a_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, cpu->d);
}

static void process_sub_a_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, cpu->e);
}

static void process_sub_a_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, cpu->h);
}

static void process_sub_a_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, cpu->l);
}

static void process_sub_a_mhl(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, value);
}

static void process_sub_a_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);

    cpu->a = cpu_sub_set_flags(gameboy, cpu->a, i8);
}

// subract with carry
static uint8_t cpu_sbc_set_flags(struct emulator *gameboy, uint8_t a, uint8_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    // check for carry using 16bit arithmetic
    uint16_t al = a;
    uint16_t bl = b;
    uint16_t c = cpu->carry_flag;
    uint16_t r = al - bl - c;

    cpu->zero_flag = !(r & 0xFF);
    cpu->null_flag = true;
    cpu->half_carry_flag = (a ^ b ^ r) & 0x10;
    cpu->carry_flag = r & 0x100;

    return r;
}

static void process_sbc_a_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, cpu->a);
}

static void process_sbc_a_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, cpu->b);
}

static void process_sbc_a_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, cpu->c);
}

static void process_sbc_a_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, cpu->d);
}

static void process_sbc_a_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, cpu->e);
}

static void process_sbc_a_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, cpu->h);
}

static void process_sbc_a_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, cpu->l);
}

static void process_sbc_a_mhl(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, value);
}

static void process_sbc_a_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);

    cpu->a = cpu_sbc_set_flags(gameboy, cpu->a, i8);
}

static uint8_t cpu_add_set_flags(struct emulator *gameboy, uint8_t a, uint8_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    // check for carry using 16bit arithmetic
    uint16_t al = a;
    uint16_t bl = b;
    uint16_t r = al + bl;

    cpu->zero_flag = !(r & 0xFF);
    cpu->null_flag = false;
    cpu->half_carry_flag = (a ^ b ^ r) & 0x10;
    cpu->carry_flag = r & 0x100;

    return r;
}

static void process_add_a_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, cpu->a);
}

static void process_add_a_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, cpu->b);
}

static void process_add_a_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, cpu->c);
}

static void process_add_a_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, cpu->d);
}

static void process_add_a_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, cpu->e);
}

static void process_add_a_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, cpu->h);
}

static void process_add_a_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, cpu->l);
}

static void process_add_a_mhl(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, value);
}

static void process_add_a_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);

    cpu->a = cpu_add_set_flags(gameboy, cpu->a, i8);
}

// add with carry and set flags
static uint8_t cpu_adc_set_flags(struct emulator *gameboy, uint8_t a, uint8_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    // check for carry using 16bit arithmetic
    uint16_t al = a;
    uint16_t bl = b;
    uint16_t c = cpu->carry_flag;
    uint16_t r = al + bl + c;

    cpu->zero_flag = !(r & 0xFF);
    cpu->null_flag = false;
    cpu->half_carry_flag = (a ^ b ^ r) & 0x10;
    cpu->carry_flag = r & 0x100;

    return r;
}

static void process_adc_a_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, cpu->a);
}

static void process_adc_a_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, cpu->b);
}

static void process_adc_a_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, cpu->c);
}

static void process_adc_a_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, cpu->d);
}

static void process_adc_a_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, cpu->e);
}

static void process_adc_a_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, cpu->h);
}

static void process_adc_a_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, cpu->l);
}

static void process_adc_a_mhl(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, value);
}

static void process_adc_a_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);

    cpu->a = cpu_adc_set_flags(gameboy, cpu->a, i8);
}

static uint16_t gameboy_add_sp_si8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    int8_t i8 = get_cpu_next_i8(gameboy); // offset is signed

    // use 32bit arithmetic to avoid signed integer overflow UB
    int32_t r = cpu->stack_pointer;
    r += i8;

    cpu->zero_flag = false;
    cpu->null_flag = false;

    // carry and half-carry are for the low byte
    cpu->half_carry_flag = (cpu->stack_pointer ^ i8 ^ r) & 0x10;
    cpu->carry_flag = (cpu->stack_pointer ^ i8 ^ r) & 0x100;

    return (uint16_t)r;
}

static void process_add_sp_si8(struct emulator *gameboy) {
    gameboy->cpu.stack_pointer = gameboy_add_sp_si8(gameboy);

    cpu_clock_tick(gameboy, 8);
}

static void process_add_hl_bc(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t bc = get_cpu_bc(gameboy);

    hl = cpu_addw_set_flags(gameboy, hl, bc);

    set_cpu_hl(gameboy, hl);
}

static void process_add_hl_de(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t de = get_cpu_de(gameboy);

    hl = cpu_addw_set_flags(gameboy, hl, de);

    set_cpu_hl(gameboy, hl);
}

static void process_add_hl_hl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    hl = cpu_addw_set_flags(gameboy, hl, hl);

    set_cpu_hl(gameboy, hl);
}

static void process_add_hl_sp(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    hl = cpu_addw_set_flags(gameboy, hl, gameboy->cpu.stack_pointer);

    set_cpu_hl(gameboy, hl);
}

static void process_inc_sp(struct emulator *gameboy) {
    uint16_t stack_pointer = gameboy->cpu.stack_pointer;

    stack_pointer = (stack_pointer + 1) & 0xFFFF;
    gameboy->cpu.stack_pointer = stack_pointer;

    cpu_clock_tick(gameboy, 4);
}

static void process_inc_bc(struct emulator *gameboy) {
    uint16_t bc = get_cpu_bc(gameboy);

    bc = (bc + 1) & 0xFFFF;

    set_cpu_bc(gameboy, bc);
    cpu_clock_tick(gameboy, 4);
}

static void process_inc_de(struct emulator *gameboy) {
    uint16_t de = get_cpu_de(gameboy);

    de = (de + 1) & 0xFFFF;

    set_cpu_de(gameboy, de);
    cpu_clock_tick(gameboy, 4);
}

static void process_inc_hl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    hl = (hl + 1) & 0xFFFF;

    set_cpu_hl(gameboy, hl);
    cpu_clock_tick(gameboy, 4);
}

static void process_dec_sp(struct emulator *gameboy) {
    uint16_t stack_pointer = gameboy->cpu.stack_pointer;

    stack_pointer = (stack_pointer - 1) & 0xFFFF;
    gameboy->cpu.stack_pointer = stack_pointer;

    cpu_clock_tick(gameboy, 4);
}

static void process_dec_bc(struct emulator *gameboy) {
    uint16_t bc = get_cpu_bc(gameboy);

    bc = (bc - 1) & 0xFFFF;

    set_cpu_bc(gameboy, bc);
    cpu_clock_tick(gameboy, 4);
}

static void process_dec_de(struct emulator *gameboy) {
    uint16_t de = get_cpu_de(gameboy);

    de = (de - 1) & 0xFFFF;

    set_cpu_de(gameboy, de);
    cpu_clock_tick(gameboy, 4);
}

static void process_dec_hl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    hl = (hl - 1) & 0xFFFF;

    set_cpu_hl(gameboy, hl);
    cpu_clock_tick(gameboy, 4);
}

static void process_cp_a_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sub_set_flags(gameboy, cpu->a, cpu->a);
}

static void process_cp_a_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sub_set_flags(gameboy, cpu->a, cpu->b);
}

static void process_cp_a_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sub_set_flags(gameboy, cpu->a, cpu->c);
}

static void process_cp_a_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sub_set_flags(gameboy, cpu->a, cpu->d);
}

static void process_cp_a_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sub_set_flags(gameboy, cpu->a, cpu->e);
}

static void process_cp_a_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sub_set_flags(gameboy, cpu->a, cpu->h);
}

static void process_cp_a_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sub_set_flags(gameboy, cpu->a, cpu->l);
}

static void process_cp_a_mhl(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_sub_set_flags(gameboy, cpu->a, value);
}

static void process_cp_a_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);

    cpu_sub_set_flags(gameboy, cpu->a, i8);
}

static uint8_t cpu_and_set_flags(struct emulator *gameboy, uint8_t a, uint8_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    uint8_t r = a & b;

    cpu->zero_flag = (r == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = true;
    cpu->carry_flag = false;

    return r;
}

static void process_and_a_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, cpu->a);
}

static void process_and_a_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, cpu->b);
}

static void process_and_a_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, cpu->c);
}

static void process_and_a_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, cpu->d);
}

static void process_and_a_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, cpu->e);
}

static void process_and_a_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, cpu->h);
}

static void process_and_a_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, cpu->l);
}

static void process_and_a_mhl(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, value);
}

static void process_and_a_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);

    cpu->a = cpu_and_set_flags(gameboy, cpu->a, i8);
}

static uint8_t cpu_xor_set_flags(struct emulator *gameboy, uint8_t a, uint8_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    uint8_t r = a ^ b;

    cpu->zero_flag = (r == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = false;

    return r;
}

static void process_xor_a_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, cpu->a);
}

static void process_xor_a_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, cpu->b);
}

static void process_xor_a_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, cpu->c);
}

static void process_xor_a_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, cpu->d);
}

static void process_xor_a_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, cpu->e);
}

static void process_xor_a_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, cpu->h);
}

static void process_xor_a_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, cpu->l);
}

static void process_xor_a_mhl(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, value);
}

static void process_xor_a_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);

    cpu->a = cpu_xor_set_flags(gameboy, cpu->a, i8);
}

static uint8_t cpu_or_set_flags(struct emulator *gameboy, uint8_t a, uint8_t b) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    uint8_t r = a | b;

    cpu->zero_flag = (r == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = false;

    return r;
}

static void process_or_a_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, cpu->a);
}

static void process_or_a_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, cpu->b);
}

static void process_or_a_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, cpu->c);
}

static void process_or_a_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, cpu->d);
}

static void process_or_a_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, cpu->e);
}

static void process_or_a_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, cpu->h);
}

static void process_or_a_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, cpu->l);
}

static void process_or_a_mhl(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, value);
}

static void process_or_a_i8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);

    cpu->a = cpu_or_set_flags(gameboy, cpu->a, i8);
}

static void process_cpl_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu->a = ~cpu->a;
    cpu->null_flag = true;
    cpu->half_carry_flag = true;
}

// rotate left A
static void process_rlca(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t a = cpu->a;
    uint8_t c = a >> 7;

    a = (a << 1) & 0xFF;
    a |= c;

    cpu->a = a;
    cpu->zero_flag = false;
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = c;
}

// rotate left A through carry
static void process_rla(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t a = cpu->a;
    uint8_t c = cpu->carry_flag;
    uint8_t new_c = a >> 7;

    a = (a << 1) & 0xFF;
    a |= c;

    cpu->a = a;
    cpu->zero_flag = false;
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = new_c;
}

// rotate right A
static void process_rrca(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t a = cpu->a;
    uint8_t c = a & 1;

    a = a >> 1;
    a |= (c << 7);

    cpu->a = a;
    cpu->zero_flag = false;
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = c;
}

// rotate right A through carry
static void process_rra(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t a = cpu->a;
    uint8_t c = cpu->carry_flag;
    uint8_t new_c = a & 1; // current carry goes to LSB of A ; MSB of A becomes new carry

    a = a >> 1;
    a |= (c << 7);

    cpu->a = a;
    cpu->zero_flag = false;
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = new_c;
}

// decimal adjust A for BCD operations
static void process_daa(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t a = cpu->a;
    uint8_t adj = 0;

    // check if there is a carry/borrow for the low nibble in the last operation
    if (cpu->half_carry_flag) {
        adj |= 0x06;
    }

    // check if there is a carry/borrow for the high nibble in the last operation
    if (cpu->carry_flag) {
        adj |= 0x60;
    }

    if (cpu->null_flag) {
        a -= adj; // subtraction can never result in A-F range with a half-carry ; easy operation
    } else {
        // addition is trickier ; may result in an overflow
        if ((a & 0xF) > 0x09) {
            adj |= 0x06;
        }

        if (a > 0x99) {
            adj |= 0x60;
        }

        a += adj;
    }

    cpu->a = a;
    cpu->zero_flag = (a == 0);
    cpu->carry_flag = ((adj & 0x60) != 0);
    cpu->half_carry_flag = false;
}

static void process_ld_a_i8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);

    gameboy->cpu.a = i8;
}

static void process_ld_b_i8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);

    gameboy->cpu.b = i8;
}

static void process_ld_c_i8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);

    gameboy->cpu.c = i8;
}

static void process_ld_d_i8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);

    gameboy->cpu.d = i8;
}

static void process_ld_e_i8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);

    gameboy->cpu.e = i8;
}

static void process_ld_h_i8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);

    gameboy->cpu.h = i8;
}

static void process_ld_l_i8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);

    gameboy->cpu.l = i8;
}

static void process_ld_mhl_i8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);
    uint16_t hl = get_cpu_hl(gameboy);

    write_cpu(gameboy, hl, i8);
}

static void process_ld_mi16_a(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);

    write_cpu(gameboy, i16, gameboy->cpu.a);
}

static void process_ld_mi16_sp(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);
    uint16_t stack_pointer = gameboy->cpu.stack_pointer;

    write_cpu(gameboy, i16, stack_pointer & 0xFF);
    write_cpu(gameboy, i16 + 1, stack_pointer >> 8);
}

static void process_ld_a_mi16(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);

    gameboy->cpu.a = read_cpu(gameboy, i16);
}

static void process_ldh_mi8_a(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);
    uint16_t address = 0xFF00 | i8;

    write_cpu(gameboy, address, gameboy->cpu.a);
}

static void process_ldh_a_mi8(struct emulator *gameboy) {
    uint8_t i8 = get_cpu_next_i8(gameboy);
    uint16_t address = 0xFF00 | i8;

    gameboy->cpu.a = read_cpu(gameboy, address);
}

static void process_ldh_mc_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t address = 0xFF00 | cpu->c;

    write_cpu(gameboy, address, cpu->a);
}

static void process_ldh_a_mc(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t address = 0xFF00 | cpu->c;

    cpu->a = read_cpu(gameboy, address);
}

static void process_ld_bc_i16(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);

    set_cpu_bc(gameboy, i16);
}

static void process_ld_de_i16(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);

    set_cpu_de(gameboy, i16);
}

static void process_ld_sp_i16(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);

    gameboy->cpu.stack_pointer = i16;
}

static void process_ld_sp_hl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    gameboy->cpu.stack_pointer = hl;

    cpu_clock_tick(gameboy, 4);
}

static void process_ld_hl_i16(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);

    set_cpu_hl(gameboy, i16);
}

static void process_ld_mbc_a(struct emulator *gameboy) {
    uint16_t bc = get_cpu_bc(gameboy);
    uint16_t a = gameboy->cpu.a;

    write_cpu(gameboy, bc, a);
}

static void process_ld_mde_a(struct emulator *gameboy) {
    uint16_t de = get_cpu_de(gameboy);
    uint16_t a = gameboy->cpu.a;

    write_cpu(gameboy, de, a);
}

static void process_ld_mhl_a(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t a = gameboy->cpu.a;

    write_cpu(gameboy, hl, a);
}

static void process_ld_mhl_b(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t b = gameboy->cpu.b;

    write_cpu(gameboy, hl, b);
}

static void process_ld_mhl_c(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t c = gameboy->cpu.c;

    write_cpu(gameboy, hl, c);
}

static void process_ld_mhl_d(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t d = gameboy->cpu.d;

    write_cpu(gameboy, hl, d);
}

static void process_ld_mhl_e(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t e = gameboy->cpu.e;

    write_cpu(gameboy, hl, e);
}

static void process_ld_mhl_h(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t h = gameboy->cpu.h;

    write_cpu(gameboy, hl, h);
}

static void process_ld_mhl_l(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t l = gameboy->cpu.l;

    write_cpu(gameboy, hl, l);
}

static void process_ldi_mhl_a(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t a = gameboy->cpu.a;

    write_cpu(gameboy, hl, a);

    hl = (hl + 1) & 0xFFFF;

    set_cpu_hl(gameboy, hl);
}

static void process_ldd_mhl_a(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint16_t a = gameboy->cpu.a;

    write_cpu(gameboy, hl, a);

    hl = (hl - 1) & 0xFFFF;

    set_cpu_hl(gameboy, hl);
}

static void process_ld_a_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    gameboy->cpu.a = value;
}

static void process_ldi_a_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    gameboy->cpu.a = read_cpu(gameboy, hl);

    hl = (hl + 1) & 0xFFFF;

    set_cpu_hl(gameboy, hl);
}

static void process_ldd_a_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    gameboy->cpu.a = read_cpu(gameboy, hl);

    hl = (hl - 1) & 0xFFFF;

    set_cpu_hl(gameboy, hl);
}

static void process_ld_a_mbc(struct emulator *gameboy) {
    uint16_t bc = get_cpu_bc(gameboy);
    uint8_t value = read_cpu(gameboy, bc);

    gameboy->cpu.a = value;
}

static void process_ld_a_mde(struct emulator *gameboy) {
    uint16_t de = get_cpu_de(gameboy);
    uint8_t value = read_cpu(gameboy, de);

    gameboy->cpu.a = value;
}

static void process_ld_b_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    gameboy->cpu.b = value;
}

static void process_ld_c_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    gameboy->cpu.c = value;
}

static void process_ld_d_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    gameboy->cpu.d = value;
}

static void process_ld_e_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    gameboy->cpu.e = value;
}

static void process_ld_h_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    gameboy->cpu.h = value;
}

static void process_ld_l_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    gameboy->cpu.l = value;
}

static void process_ld_hl_sp_si8(struct emulator *gameboy) {
    uint16_t hl = gameboy_add_sp_si8(gameboy);

    set_cpu_hl(gameboy, hl);

    cpu_clock_tick(gameboy, 4);
}

static void process_push_bc(struct emulator *gameboy) {
    uint16_t bc = get_cpu_bc(gameboy);

    cpu_pushw(gameboy, bc);

    cpu_clock_tick(gameboy, 4);
}

static void process_push_de(struct emulator *gameboy) {
    uint16_t de = get_cpu_de(gameboy);

    cpu_pushw(gameboy, de);

    cpu_clock_tick(gameboy, 4);
}

static void process_push_hl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    cpu_pushw(gameboy, hl);

    cpu_clock_tick(gameboy, 4);
}

static void process_push_af(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t f = 0;

    f |= cpu->zero_flag << 7;
    f |= cpu->null_flag << 6;
    f |= cpu->half_carry_flag << 5;
    f |= cpu->carry_flag << 4;

    cpu_pushb(gameboy, cpu->a);
    cpu_pushb(gameboy, f);

    cpu_clock_tick(gameboy, 4);
}

static void process_pop_bc(struct emulator *gameboy) {
    uint16_t bc = cpu_popw(gameboy);

    set_cpu_bc(gameboy, bc);
}

static void process_pop_de(struct emulator *gameboy) {
    uint16_t de = cpu_popw(gameboy);

    set_cpu_de(gameboy, de);
}

static void process_pop_hl(struct emulator *gameboy) {
    uint16_t hl = cpu_popw(gameboy);

    set_cpu_hl(gameboy, hl);
}

static void process_pop_af(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t f = cpu_popb(gameboy);
    uint8_t a = cpu_popb(gameboy);

    cpu->a = a;

    // restore flags from memory (low 4 bits are ignored)
    cpu->zero_flag = f & (1U << 7);
    cpu->null_flag = f & (1U << 6);
    cpu->half_carry_flag = f & (1U << 5);
    cpu->carry_flag = f & (1U << 4);
}

static void process_ld_a_b(struct emulator *gameboy) {
    gameboy->cpu.a = gameboy->cpu.b;
}

static void process_ld_a_c(struct emulator *gameboy) {
    gameboy->cpu.a = gameboy->cpu.c;
}

static void process_ld_a_d(struct emulator *gameboy) {
    gameboy->cpu.a = gameboy->cpu.d;
}

static void process_ld_a_e(struct emulator *gameboy) {
    gameboy->cpu.a = gameboy->cpu.e;
}

static void process_ld_a_h(struct emulator *gameboy) {
    gameboy->cpu.a = gameboy->cpu.h;
}

static void process_ld_a_l(struct emulator *gameboy) {
    gameboy->cpu.a = gameboy->cpu.l;
}

static void process_ld_b_a(struct emulator *gameboy) {
    gameboy->cpu.b = gameboy->cpu.a;
}

static void process_ld_b_c(struct emulator *gameboy) {
    gameboy->cpu.b = gameboy->cpu.c;
}

static void process_ld_b_d(struct emulator *gameboy) {
    gameboy->cpu.b = gameboy->cpu.d;
}

static void process_ld_b_e(struct emulator *gameboy) {
    gameboy->cpu.b = gameboy->cpu.e;
}

static void process_ld_b_h(struct emulator *gameboy) {
    gameboy->cpu.b = gameboy->cpu.h;
}

static void process_ld_b_l(struct emulator *gameboy) {
    gameboy->cpu.b = gameboy->cpu.l;
}

static void process_ld_c_a(struct emulator *gameboy) {
    gameboy->cpu.c = gameboy->cpu.a;
}

static void process_ld_c_b(struct emulator *gameboy) {
    gameboy->cpu.c = gameboy->cpu.b;
}

static void process_ld_c_d(struct emulator *gameboy) {
    gameboy->cpu.c = gameboy->cpu.d;
}

static void process_ld_c_e(struct emulator *gameboy) {
    gameboy->cpu.c = gameboy->cpu.e;
}

static void process_ld_c_h(struct emulator *gameboy) {
    gameboy->cpu.c = gameboy->cpu.h;
}

static void process_ld_c_l(struct emulator *gameboy) {
    gameboy->cpu.c = gameboy->cpu.l;
}

static void process_ld_d_a(struct emulator *gameboy) {
    gameboy->cpu.d = gameboy->cpu.a;
}

static void process_ld_d_b(struct emulator *gameboy) {
    gameboy->cpu.d = gameboy->cpu.b;
}

static void process_ld_d_c(struct emulator *gameboy) {
    gameboy->cpu.d = gameboy->cpu.c;
}

static void process_ld_d_e(struct emulator *gameboy) {
    gameboy->cpu.d = gameboy->cpu.e;
}

static void process_ld_d_h(struct emulator *gameboy) {
    gameboy->cpu.d = gameboy->cpu.h;
}

static void process_ld_d_l(struct emulator *gameboy) {
    gameboy->cpu.d = gameboy->cpu.l;
}

static void process_ld_e_a(struct emulator *gameboy) {
    gameboy->cpu.e = gameboy->cpu.a;
}

static void process_ld_e_b(struct emulator *gameboy) {
    gameboy->cpu.e = gameboy->cpu.b;
}

static void process_ld_e_c(struct emulator *gameboy) {
    gameboy->cpu.e = gameboy->cpu.c;
}

static void process_ld_e_d(struct emulator *gameboy) {
    gameboy->cpu.e = gameboy->cpu.d;
}

static void process_ld_e_h(struct emulator *gameboy) {
    gameboy->cpu.e = gameboy->cpu.h;
}

static void process_ld_e_l(struct emulator *gameboy) {
    gameboy->cpu.e = gameboy->cpu.l;
}

static void process_ld_h_a(struct emulator *gameboy) {
    gameboy->cpu.h = gameboy->cpu.a;
}

static void process_ld_h_b(struct emulator *gameboy) {
    gameboy->cpu.h = gameboy->cpu.b;
}

static void process_ld_h_c(struct emulator *gameboy) {
    gameboy->cpu.h = gameboy->cpu.c;
}

static void process_ld_h_d(struct emulator *gameboy) {
    gameboy->cpu.h = gameboy->cpu.d;
}

static void process_ld_h_e(struct emulator *gameboy) {
    gameboy->cpu.h = gameboy->cpu.e;
}

static void process_ld_h_l(struct emulator *gameboy) {
    gameboy->cpu.h = gameboy->cpu.l;
}

static void process_ld_l_a(struct emulator *gameboy) {
    gameboy->cpu.l = gameboy->cpu.a;
}

static void process_ld_l_b(struct emulator *gameboy) {
    gameboy->cpu.l = gameboy->cpu.b;
}

static void process_ld_l_c(struct emulator *gameboy) {
    gameboy->cpu.l = gameboy->cpu.c;
}

static void process_ld_l_d(struct emulator *gameboy) {
    gameboy->cpu.l = gameboy->cpu.d;
}

static void process_ld_l_e(struct emulator *gameboy) {
    gameboy->cpu.l = gameboy->cpu.e;
}

static void process_ld_l_h(struct emulator *gameboy) {
    gameboy->cpu.l = gameboy->cpu.h;
}

static void process_jp_i16(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);

    cpu_load_pc(gameboy, i16);
}

static void process_jp_nz_i16(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t i16 = get_cpu_next_i16(gameboy);

    if (!cpu->zero_flag) {
        cpu_load_pc(gameboy, i16);
    }
}

static void process_jp_z_i16(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t i16 = get_cpu_next_i16(gameboy);

    if (cpu->zero_flag) {
        cpu_load_pc(gameboy, i16);
    }
}

static void process_jp_nc_i16(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t i16 = get_cpu_next_i16(gameboy);

    if (!cpu->carry_flag) {
        cpu_load_pc(gameboy, i16);
    }
}

static void process_jp_c_i16(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint16_t i16 = get_cpu_next_i16(gameboy);

    if (cpu->carry_flag) {
        cpu_load_pc(gameboy, i16);
    }
}

static void process_jp_hl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);

    gameboy->cpu.program_counter = hl;
}

static void process_jr_si8(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t i8 = get_cpu_next_i8(gameboy);
    uint16_t program_counter = cpu->program_counter;

    program_counter = program_counter + (int8_t)i8;

    cpu_load_pc(gameboy, program_counter);
}

static void process_jr_z_si8(struct emulator *gameboy) {
    if (gameboy->cpu.zero_flag) {
        process_jr_si8(gameboy);
    } else {
        get_cpu_next_i8(gameboy); // discard immediate value
    }
}

static void process_jr_c_si8(struct emulator *gameboy) {
    if (gameboy->cpu.carry_flag) {
        process_jr_si8(gameboy);
    } else {
        get_cpu_next_i8(gameboy); // discard immediate value
    }
}

static void process_jr_nz_si8(struct emulator *gameboy) {
    if (!gameboy->cpu.zero_flag) {
        process_jr_si8(gameboy);
    } else {
        get_cpu_next_i8(gameboy); // discard immediate value
    }
}

static void process_jr_nc_si8(struct emulator *gameboy) {
    if (!gameboy->cpu.carry_flag) {
        process_jr_si8(gameboy);
    } else {
        get_cpu_next_i8(gameboy); // discard immediate value
    }
}

static void process_call_i16(struct emulator *gameboy) {
    uint16_t i16 = get_cpu_next_i16(gameboy);

    cpu_pushw(gameboy, gameboy->cpu.program_counter);
    cpu_load_pc(gameboy, i16);
}

static void process_call_nz_i16(struct emulator *gameboy) {
    if (!gameboy->cpu.zero_flag) {
        process_call_i16(gameboy);
    } else {
        get_cpu_next_i16(gameboy); // discard immediate value
    }
}

static void process_call_z_i16(struct emulator *gameboy) {
    if (gameboy->cpu.zero_flag) {
        process_call_i16(gameboy);
    } else {
        get_cpu_next_i16(gameboy); // discard immediate value
    }
}

static void process_call_nc_i16(struct emulator *gameboy) {
    if (!gameboy->cpu.carry_flag) {
        process_call_i16(gameboy);
    } else {
        get_cpu_next_i16(gameboy); // discard immediate value
    }
}

static void process_call_c_i16(struct emulator *gameboy) {
    if (gameboy->cpu.carry_flag) {
        process_call_i16(gameboy);
    } else {
        get_cpu_next_i16(gameboy); // discard immediate value
    }
}

static void process_ret(struct emulator *gameboy) {
    uint16_t address = cpu_popw(gameboy);

    cpu_load_pc(gameboy, address);
}

static void process_ret_z(struct emulator *gameboy) {
    if (gameboy->cpu.zero_flag) {
        process_ret(gameboy);
    }

    cpu_clock_tick(gameboy, 4);
}

static void process_ret_c(struct emulator *gameboy) {
    if (gameboy->cpu.carry_flag) {
        process_ret(gameboy);
    }

    cpu_clock_tick(gameboy, 4);
}

static void process_ret_nz(struct emulator *gameboy) {
    if (!gameboy->cpu.zero_flag) {
        process_ret(gameboy);
    }

    cpu_clock_tick(gameboy, 4);
}

static void process_ret_nc(struct emulator *gameboy) {
    if (!gameboy->cpu.carry_flag) {
        process_ret(gameboy);
    }

    cpu_clock_tick(gameboy, 4);
}

static void process_reti(struct emulator *gameboy) {
    process_ret(gameboy);

    gameboy->cpu.interrupt_master_enable = true;
    gameboy->cpu.interrupt_request_enable_next = true;
}

static void cpu_rst(struct emulator *gameboy, uint16_t target) {
    cpu_pushw(gameboy, gameboy->cpu.program_counter);

    cpu_load_pc(gameboy, target);
}

static void process_rst_00(struct emulator *gameboy) {
    cpu_rst(gameboy, 0x00);
}

static void process_rst_08(struct emulator *gameboy) {
    cpu_rst(gameboy, 0x08);
}

static void process_rst_10(struct emulator *gameboy) {
    cpu_rst(gameboy, 0x10);
}

static void process_rst_18(struct emulator *gameboy) {
    cpu_rst(gameboy, 0x18);
}

static void process_rst_20(struct emulator *gameboy) {
    cpu_rst(gameboy, 0x20);
}

static void process_rst_28(struct emulator *gameboy) {
    cpu_rst(gameboy, 0x28);
}

static void process_rst_30(struct emulator *gameboy) {
    cpu_rst(gameboy, 0x30);
}

static void process_rst_38(struct emulator *gameboy) {
    cpu_rst(gameboy, 0x38);
}

static void process_op_cb(struct emulator *gameboy);

static gameboy_instruction gameboy_instructions[0x100] = {
    // 0x00
    process_nop,
    process_ld_bc_i16,
    process_ld_mbc_a,
    process_inc_bc,
    process_inc_b,
    process_dec_b,
    process_ld_b_i8,
    process_rlca,
    process_ld_mi16_sp,
    process_add_hl_bc,
    process_ld_a_mbc,
    process_dec_bc,
    process_inc_c,
    process_dec_c,
    process_ld_c_i8,
    process_rrca,

    // 0x10
    process_stop,
    process_ld_de_i16,
    process_ld_mde_a,
    process_inc_de,
    process_inc_d,
    process_dec_d,
    process_ld_d_i8,
    process_rla,
    process_jr_si8,
    process_add_hl_de,
    process_ld_a_mde,
    process_dec_de,
    process_inc_e,
    process_dec_e,
    process_ld_e_i8,
    process_rra,

    // 0x20
    process_jr_nz_si8,
    process_ld_hl_i16,
    process_ldi_mhl_a,
    process_inc_hl,
    process_inc_h,
    process_dec_h,
    process_ld_h_i8,
    process_daa,
    process_jr_z_si8,
    process_add_hl_hl,
    process_ldi_a_mhl,
    process_dec_hl,
    process_inc_l,
    process_dec_l,
    process_ld_l_i8,
    process_cpl_a,

    // 0x30
    process_jr_nc_si8,
    process_ld_sp_i16,
    process_ldd_mhl_a,
    process_inc_sp,
    process_inc_mhl,
    process_dec_mhl,
    process_ld_mhl_i8,
    process_scf,
    process_jr_c_si8,
    process_add_hl_sp,
    process_ldd_a_mhl,
    process_dec_sp,
    process_inc_a,
    process_dec_a,
    process_ld_a_i8,
    process_ccf,

    // 0x40
    process_nop,
    process_ld_b_c,
    process_ld_b_d,
    process_ld_b_e,
    process_ld_b_h,
    process_ld_b_l,
    process_ld_b_mhl,
    process_ld_b_a,
    process_ld_c_b,
    process_nop,
    process_ld_c_d,
    process_ld_c_e,
    process_ld_c_h,
    process_ld_c_l,
    process_ld_c_mhl,
    process_ld_c_a,

    // 0x50
    process_ld_d_b,
    process_ld_d_c,
    process_nop,
    process_ld_d_e,
    process_ld_d_h,
    process_ld_d_l,
    process_ld_d_mhl,
    process_ld_d_a,
    process_ld_e_b,
    process_ld_e_c,
    process_ld_e_d,
    process_nop,
    process_ld_e_h,
    process_ld_e_l,
    process_ld_e_mhl,
    process_ld_e_a,

    // 0x60
    process_ld_h_b,
    process_ld_h_c,
    process_ld_h_d,
    process_ld_h_e,
    process_nop,
    process_ld_h_l,
    process_ld_h_mhl,
    process_ld_h_a,
    process_ld_l_b,
    process_ld_l_c,
    process_ld_l_d,
    process_ld_l_e,
    process_ld_l_h,
    process_nop,
    process_ld_l_mhl,
    process_ld_l_a,

    // 0x70
    process_ld_mhl_b,
    process_ld_mhl_c,
    process_ld_mhl_d,
    process_ld_mhl_e,
    process_ld_mhl_h,
    process_ld_mhl_l,
    process_halt,
    process_ld_mhl_a,
    process_ld_a_b,
    process_ld_a_c,
    process_ld_a_d,
    process_ld_a_e,
    process_ld_a_h,
    process_ld_a_l,
    process_ld_a_mhl,
    process_nop,

    // 0x80
    process_add_a_b,
    process_add_a_c,
    process_add_a_d,
    process_add_a_e,
    process_add_a_h,
    process_add_a_l,
    process_add_a_mhl,
    process_add_a_a,
    process_adc_a_b,
    process_adc_a_c,
    process_adc_a_d,
    process_adc_a_e,
    process_adc_a_h,
    process_adc_a_l,
    process_adc_a_mhl,
    process_adc_a_a,

    // 0x90
    process_sub_a_b,
    process_sub_a_c,
    process_sub_a_d,
    process_sub_a_e,
    process_sub_a_h,
    process_sub_a_l,
    process_sub_a_mhl,
    process_sub_a_a,
    process_sbc_a_b,
    process_sbc_a_c,
    process_sbc_a_d,
    process_sbc_a_e,
    process_sbc_a_h,
    process_sbc_a_l,
    process_sbc_a_mhl,
    process_sbc_a_a,

    // 0xA0
    process_and_a_b,
    process_and_a_c,
    process_and_a_d,
    process_and_a_e,
    process_and_a_h,
    process_and_a_l,
    process_and_a_mhl,
    process_and_a_a,
    process_xor_a_b,
    process_xor_a_c,
    process_xor_a_d,
    process_xor_a_e,
    process_xor_a_h,
    process_xor_a_l,
    process_xor_a_mhl,
    process_xor_a_a,

    // 0xB0
    process_or_a_b,
    process_or_a_c,
    process_or_a_d,
    process_or_a_e,
    process_or_a_h,
    process_or_a_l,
    process_or_a_mhl,
    process_or_a_a,
    process_cp_a_b,
    process_cp_a_c,
    process_cp_a_d,
    process_cp_a_e,
    process_cp_a_h,
    process_cp_a_l,
    process_cp_a_mhl,
    process_cp_a_a,

    // 0xC0
    process_ret_nz,
    process_pop_bc,
    process_jp_nz_i16,
    process_jp_i16,
    process_call_nz_i16,
    process_push_bc,
    process_add_a_i8,
    process_rst_00,
    process_ret_z,
    process_ret,
    process_jp_z_i16,
    process_op_cb,
    process_call_z_i16,
    process_call_i16,
    process_adc_a_i8,
    process_rst_08,

    // 0xD0
    process_ret_nc,
    process_pop_de,
    process_jp_nc_i16,
    process_undefined,
    process_call_nc_i16,
    process_push_de,
    process_sub_a_i8,
    process_rst_10,
    process_ret_c,
    process_reti,
    process_jp_c_i16,
    process_undefined,
    process_call_c_i16,
    process_undefined,
    process_sbc_a_i8,
    process_rst_18,

    // 0xE0
    process_ldh_mi8_a,
    process_pop_hl,
    process_ldh_mc_a,
    process_undefined,
    process_undefined,
    process_push_hl,
    process_and_a_i8,
    process_rst_20,
    process_add_sp_si8,
    process_jp_hl,
    process_ld_mi16_a,
    process_undefined,
    process_undefined,
    process_undefined,
    process_xor_a_i8,
    process_rst_28,

    // 0xF0
    process_ldh_a_mi8,
    process_pop_af,
    process_ldh_a_mc,
    process_di,
    process_undefined,
    process_push_af,
    process_or_a_i8,
    process_rst_30,
    process_ld_hl_sp_si8,
    process_ld_sp_hl,
    process_ld_a_mi16,
    process_ei,
    process_undefined,
    process_undefined,
    process_cp_a_i8,
    process_rst_38,
};

// addresses of the interrupt handlers in memory
static const uint16_t gameboy_interrupt_request_handlers[5] = {
    [GB_INTERRUPT_REQUEST_VSYNC] = 0x0040,
    [GB_INTERRUPT_REQUEST_LCD_STAT] = 0x0048,
    [GB_INTERRUPT_REQUEST_TIMER] = 0x0050,
    [GB_INTERRUPT_REQUEST_SERIAL] = 0x0058,
    [GB_INTERRUPT_REQUEST_INPUT] = 0x0060
};

static void check_cpu_interrupts(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    struct gameboy_interrupt_request *interrupt_request = &gameboy->interrupt_request;
    uint8_t active_interrupt_request;
    uint16_t handler;
    unsigned i;

    // check if there is an interrupt pending
    active_interrupt_request = interrupt_request->interrupt_request_enable & interrupt_request->interrupt_request_flags & 0x1F;

    if (!active_interrupt_request) {
        return;
    }

    // there is an active interrupt request ; that gets the program outside of halted mode even if the IME is not set in the CPU
    cpu->halted = false;

    if (!cpu->interrupt_master_enable) {
        return;
    }

    // find the first active interrupt request ; the order is significant, interrupt requests with a lower number have the priority
    for (i = 0; i < 5; i++) {
        if (active_interrupt_request & (1U << i)) {
            break;
        }
    }

    handler = gameboy_interrupt_request_handlers[i];

    cpu->interrupt_master_enable = false;
    cpu->interrupt_request_enable_next = false;
    
    cpu_clock_tick(gameboy, 12); // entering interrupt context takes 12 cycles
    
    cpu_pushw(gameboy, gameboy->cpu.program_counter); // push current program counter on the stack

    interrupt_request->interrupt_request_flags &= ~(1U << i); // acknowledge the interrupt request

    cpu_load_pc(gameboy, handler); // jump to the interrupt request handler
}

static void run_cpu_instruction(struct emulator *gameboy) {
    uint8_t instruction;

    instruction = get_cpu_next_i8(gameboy);

    gameboy_instructions[instruction](gameboy);
}

int32_t run_cpu_cycles(struct emulator *gameboy, int32_t cycles) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    rebase_sync(gameboy); 

    while (gameboy->timestamp < cycles) {
        check_cpu_interrupts(gameboy); // check for interrupt as it may exit system from halted mode
        cpu->interrupt_master_enable = cpu->interrupt_request_enable_next;

        if (cpu->halted) {
            int32_t skip_cycles;

            // the CPU is halted so we skip to the next event or cycles
            if (cycles < gameboy->sync.first_event) {
                skip_cycles = cycles - gameboy->timestamp;
            } else {
                skip_cycles = gameboy->sync.first_event - gameboy->timestamp;
            }

            cpu_clock_tick(gameboy, skip_cycles);
            check_sync_events(gameboy); // check if any event needs to run ; this may trigger an interrupt request which will un-halt the CPU in the next iteration
        } else {
            run_cpu_instruction(gameboy);
        }
    }

    return gameboy->timestamp;
}

static void cpu_rlc_set_flags(struct emulator *gameboy, uint8_t *value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t c = *value >> 7;

    *value = (*value << 1) | c;

    cpu->zero_flag = (*value == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = c;
}

static void process_rlc_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rlc_set_flags(gameboy, &cpu->a);
}

static void process_rlc_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rlc_set_flags(gameboy, &cpu->b);
}

static void process_rlc_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rlc_set_flags(gameboy, &cpu->c);
}

static void process_rlc_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rlc_set_flags(gameboy, &cpu->d);
}

static void process_rlc_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rlc_set_flags(gameboy, &cpu->e);
}

static void process_rlc_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rlc_set_flags(gameboy, &cpu->h);
}

static void process_rlc_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rlc_set_flags(gameboy, &cpu->l);
}

static void process_rlc_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_rlc_set_flags(gameboy, &value);
    write_cpu(gameboy, hl, value);
}

static void cpu_rrc_set_flags(struct emulator *gameboy, uint8_t *value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    uint8_t c = *value & 1;

    *value = (*value >> 1) | (c << 7);

    cpu->zero_flag = (*value == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = c;
}

static void process_rrc_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rrc_set_flags(gameboy, &cpu->a);
}

static void process_rrc_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rrc_set_flags(gameboy, &cpu->b);
}

static void process_rrc_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rrc_set_flags(gameboy, &cpu->c);
}

static void process_rrc_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rrc_set_flags(gameboy, &cpu->d);
}

static void process_rrc_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rrc_set_flags(gameboy, &cpu->e);
}

static void process_rrc_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rrc_set_flags(gameboy, &cpu->h);
}

static void process_rrc_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rrc_set_flags(gameboy, &cpu->l);
}

static void process_rrc_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_rrc_set_flags(gameboy, &value);
    write_cpu(gameboy, hl, value);
}

static void cpu_rl_set_flags(struct emulator *gameboy, uint8_t *value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    bool new_c = *value >> 7;

    *value = (*value << 1) | (uint8_t)cpu->carry_flag;

    cpu->zero_flag = (*value == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = new_c;
}

static void process_rl_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rl_set_flags(gameboy, &cpu->a);
}

static void process_rl_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rl_set_flags(gameboy, &cpu->b);
}

static void process_rl_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rl_set_flags(gameboy, &cpu->c);
}

static void process_rl_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rl_set_flags(gameboy, &cpu->d);
}

static void process_rl_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rl_set_flags(gameboy, &cpu->e);
}

static void process_rl_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rl_set_flags(gameboy, &cpu->h);
}

static void process_rl_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rl_set_flags(gameboy, &cpu->l);
}

static void process_rl_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_rl_set_flags(gameboy, &value);
    write_cpu(gameboy, hl, value);
}

static void cpu_rr_set_flags(struct emulator *gameboy, uint8_t *value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    bool new_c = *value & 1;
    uint8_t old_c = cpu->carry_flag;

    *value = (*value >> 1) | (old_c << 7);

    cpu->zero_flag = (*value == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = new_c;
}

static void process_rr_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rr_set_flags(gameboy, &cpu->a);
}

static void process_rr_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rr_set_flags(gameboy, &cpu->b);
}

static void process_rr_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rr_set_flags(gameboy, &cpu->c);
}

static void process_rr_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rr_set_flags(gameboy, &cpu->d);
}

static void process_rr_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rr_set_flags(gameboy, &cpu->e);
}

static void process_rr_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rr_set_flags(gameboy, &cpu->h);
}

static void process_rr_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_rr_set_flags(gameboy, &cpu->l);
}

static void process_rr_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_rr_set_flags(gameboy, &value);
    write_cpu(gameboy, hl, value);
}

static void cpu_sla_set_flags(struct emulator *gameboy, uint8_t *value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    bool c = *value >> 7;

    *value = *value << 1;

    cpu->zero_flag = (*value == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = c;
}

static void process_sla_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sla_set_flags(gameboy, &cpu->a);
}

static void process_sla_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sla_set_flags(gameboy, &cpu->b);
}

static void process_sla_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sla_set_flags(gameboy, &cpu->c);
}

static void process_sla_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sla_set_flags(gameboy, &cpu->d);
}

static void process_sla_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sla_set_flags(gameboy, &cpu->e);
}

static void process_sla_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sla_set_flags(gameboy, &cpu->h);
}

static void process_sla_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sla_set_flags(gameboy, &cpu->l);
}

static void process_sla_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_sla_set_flags(gameboy, &value);
    write_cpu(gameboy, hl, value);
}

static void cpu_sra_set_flags(struct emulator *gameboy, uint8_t *value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    bool c = *value & 1;

    *value = (*value >> 1) | (*value & 0x80);

    cpu->zero_flag = (*value == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = c;
}

static void process_sra_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sra_set_flags(gameboy, &cpu->a);
}

static void process_sra_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sra_set_flags(gameboy, &cpu->b);
}

static void process_sra_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sra_set_flags(gameboy, &cpu->c);
}

static void process_sra_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sra_set_flags(gameboy, &cpu->d);
}

static void process_sra_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sra_set_flags(gameboy, &cpu->e);
}

static void process_sra_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sra_set_flags(gameboy, &cpu->h);
}

static void process_sra_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_sra_set_flags(gameboy, &cpu->l);
}

static void process_sra_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_sra_set_flags(gameboy, &value);
    write_cpu(gameboy, hl, value);
}

static void cpu_swap_set_flags(struct emulator *gameboy, uint8_t *value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    *value = ((*value << 4) | (*value >> 4)) & 0xFF;

    cpu->zero_flag = (*value == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = false;
}

static void process_swap_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_swap_set_flags(gameboy, &cpu->a);
}

static void process_swap_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_swap_set_flags(gameboy, &cpu->b);
}

static void process_swap_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_swap_set_flags(gameboy, &cpu->c);
}

static void process_swap_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_swap_set_flags(gameboy, &cpu->d);
}

static void process_swap_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_swap_set_flags(gameboy, &cpu->e);
}

static void process_swap_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_swap_set_flags(gameboy, &cpu->h);
}

static void process_swap_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_swap_set_flags(gameboy, &cpu->l);
}

static void process_swap_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_swap_set_flags(gameboy, &value);
    write_cpu(gameboy, hl, value);
}

static void cpu_srl_set_flags(struct emulator *gameboy, uint8_t *value) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    bool c = *value & 1;

    *value = *value >> 1;

    cpu->zero_flag = (*value == 0);
    cpu->null_flag = false;
    cpu->half_carry_flag = false;
    cpu->carry_flag = c;
}

static void process_srl_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_srl_set_flags(gameboy, &cpu->a);
}

static void process_srl_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_srl_set_flags(gameboy, &cpu->b);
}

static void process_srl_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_srl_set_flags(gameboy, &cpu->c);
}

static void process_srl_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_srl_set_flags(gameboy, &cpu->d);
}

static void process_srl_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_srl_set_flags(gameboy, &cpu->e);
}

static void process_srl_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_srl_set_flags(gameboy, &cpu->h);
}

static void process_srl_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_srl_set_flags(gameboy, &cpu->l);
}

static void process_srl_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_srl_set_flags(gameboy, &value);
    write_cpu(gameboy, hl, value);
}

static void cpu_bit_set_flags(struct emulator *gameboy, uint8_t *value, unsigned bit) {
    struct gameboy_cpu *cpu = &gameboy->cpu;
    bool set = *value & (1U << bit);

    cpu->zero_flag = !set;
    cpu->null_flag = false;
    cpu->half_carry_flag = true;
}

static void process_bit_0_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->a, 0);
}

static void process_bit_0_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->b, 0);
}

static void process_bit_0_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->c, 0);
}

static void process_bit_0_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->d, 0);
}

static void process_bit_0_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->e, 0);
}

static void process_bit_0_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->h, 0);
}

static void process_bit_0_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->l, 0);
}

static void process_bit_0_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_bit_set_flags(gameboy, &value, 0);
}

static void process_bit_1_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->a, 1);
}

static void process_bit_1_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->b, 1);
}

static void process_bit_1_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->c, 1);
}

static void process_bit_1_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->d, 1);
}

static void process_bit_1_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->e, 1);
}

static void process_bit_1_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->h, 1);
}

static void process_bit_1_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->l, 1);
}

static void process_bit_1_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_bit_set_flags(gameboy, &value, 1);
}

static void process_bit_2_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->a, 2);
}

static void process_bit_2_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->b, 2);
}

static void process_bit_2_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->c, 2);
}

static void process_bit_2_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->d, 2);
}

static void process_bit_2_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->e, 2);
}

static void process_bit_2_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->h, 2);
}

static void process_bit_2_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->l, 2);
}

static void process_bit_2_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_bit_set_flags(gameboy, &value, 2);
}

static void process_bit_3_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->a, 3);
}

static void process_bit_3_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->b, 3);
}

static void process_bit_3_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->c, 3);
}

static void process_bit_3_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->d, 3);
}

static void process_bit_3_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->e, 3);
}

static void process_bit_3_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->h, 3);
}

static void process_bit_3_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->l, 3);
}

static void process_bit_3_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_bit_set_flags(gameboy, &value, 3);
}

static void process_bit_4_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->a, 4);
}

static void process_bit_4_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->b, 4);
}

static void process_bit_4_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->c, 4);
}

static void process_bit_4_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->d, 4);
}

static void process_bit_4_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->e, 4);
}

static void process_bit_4_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->h, 4);
}

static void process_bit_4_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->l, 4);
}

static void process_bit_4_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_bit_set_flags(gameboy, &value, 4);
}

static void process_bit_5_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->a, 5);
}

static void process_bit_5_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->b, 5);
}

static void process_bit_5_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->c, 5);
}

static void process_bit_5_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->d, 5);
}

static void process_bit_5_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->e, 5);
}

static void process_bit_5_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->h, 5);
}

static void process_bit_5_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->l, 5);
}

static void process_bit_5_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_bit_set_flags(gameboy, &value, 5);
}

static void process_bit_6_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->a, 6);
}

static void process_bit_6_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->b, 6);
}

static void process_bit_6_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->c, 6);
}

static void process_bit_6_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->d, 6);
}

static void process_bit_6_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->e, 6);
}

static void process_bit_6_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->h, 6);
}

static void process_bit_6_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->l, 6);
}

static void process_bit_6_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_bit_set_flags(gameboy, &value, 6);
}

static void process_bit_7_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->a, 7);
}

static void process_bit_7_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->b, 7);
}

static void process_bit_7_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->c, 7);
}

static void process_bit_7_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->d, 7);
}

static void process_bit_7_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->e, 7);
}

static void process_bit_7_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->h, 7);
}

static void process_bit_7_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    cpu_bit_set_flags(gameboy, &cpu->l, 7);
}

static void process_bit_7_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    cpu_bit_set_flags(gameboy, &value, 7);
}

static void restart_cpu(struct emulator *gameboy, uint8_t *value, unsigned bit) {
    *value = *value & ~(1U << bit);
}

static void process_res_0_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->a, 0);
}

static void process_res_0_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->b, 0);
}

static void process_res_0_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->c, 0);
}

static void process_res_0_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->d, 0);
}

static void process_res_0_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->e, 0);
}

static void process_res_0_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->h, 0);
}

static void process_res_0_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->l, 0);
}

static void process_res_0_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    restart_cpu(gameboy, &value, 0);
    write_cpu(gameboy, hl, value);
}

static void process_res_1_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->a, 1);
}

static void process_res_1_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->b, 1);
}

static void process_res_1_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->c, 1);
}

static void process_res_1_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->d, 1);
}

static void process_res_1_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->e, 1);
}

static void process_res_1_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->h, 1);
}

static void process_res_1_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->l, 1);
}

static void process_res_1_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    restart_cpu(gameboy, &value, 1);
    write_cpu(gameboy, hl, value);
}

static void process_res_2_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->a, 2);
}

static void process_res_2_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->b, 2);
}

static void process_res_2_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->c, 2);
}

static void process_res_2_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->d, 2);
}

static void process_res_2_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->e, 2);
}

static void process_res_2_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->h, 2);
}

static void process_res_2_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->l, 2);
}

static void process_res_2_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    restart_cpu(gameboy, &value, 2);
    write_cpu(gameboy, hl, value);
}

static void process_res_3_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->a, 3);
}

static void process_res_3_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->b, 3);
}

static void process_res_3_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->c, 3);
}

static void process_res_3_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->d, 3);
}

static void process_res_3_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->e, 3);
}

static void process_res_3_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->h, 3);
}

static void process_res_3_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->l, 3);
}

static void process_res_3_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    restart_cpu(gameboy, &value, 3);
    write_cpu(gameboy, hl, value);
}

static void process_res_4_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->a, 4);
}

static void process_res_4_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->b, 4);
}

static void process_res_4_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->c, 4);
}

static void process_res_4_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->d, 4);
}

static void process_res_4_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->e, 4);
}

static void process_res_4_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->h, 4);
}

static void process_res_4_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->l, 4);
}

static void process_res_4_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    restart_cpu(gameboy, &value, 4);
    write_cpu(gameboy, hl, value);
}

static void process_res_5_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->a, 5);
}

static void process_res_5_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->b, 5);
}

static void process_res_5_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->c, 5);
}

static void process_res_5_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->d, 5);
}

static void process_res_5_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->e, 5);
}

static void process_res_5_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->h, 5);
}

static void process_res_5_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->l, 5);
}

static void process_res_5_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    restart_cpu(gameboy, &value, 5);
    write_cpu(gameboy, hl, value);
}

static void process_res_6_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->a, 6);
}

static void process_res_6_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->b, 6);
}

static void process_res_6_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->c, 6);
}

static void process_res_6_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->d, 6);
}

static void process_res_6_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->e, 6);
}

static void process_res_6_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->h, 6);
}

static void process_res_6_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->l, 6);
}

static void process_res_6_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    restart_cpu(gameboy, &value, 6);
    write_cpu(gameboy, hl, value);
}

static void process_res_7_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->a, 7);
}

static void process_res_7_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->b, 7);
}

static void process_res_7_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->c, 7);
}

static void process_res_7_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->d, 7);
}

static void process_res_7_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->e, 7);
}

static void process_res_7_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->h, 7);
}

static void process_res_7_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    restart_cpu(gameboy, &cpu->l, 7);
}

static void process_res_7_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    restart_cpu(gameboy, &value, 7);
    write_cpu(gameboy, hl, value);
}

static void set_cpu(struct emulator *gameboy, uint8_t *value, unsigned bit) {
    *value = *value | (1U << bit);
}

static void process_set_0_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->a, 0);
}

static void process_set_0_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->b, 0);
}

static void process_set_0_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->c, 0);
}

static void process_set_0_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->d, 0);
}

static void process_set_0_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->e, 0);
}

static void process_set_0_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->h, 0);
}

static void process_set_0_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->l, 0);
}

static void process_set_0_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    set_cpu(gameboy, &value, 0);
    write_cpu(gameboy, hl, value);
}

static void process_set_1_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->a, 1);
}

static void process_set_1_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->b, 1);
}

static void process_set_1_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->c, 1);
}

static void process_set_1_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->d, 1);
}

static void process_set_1_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->e, 1);
}

static void process_set_1_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->h, 1);
}

static void process_set_1_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->l, 1);
}

static void process_set_1_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    set_cpu(gameboy, &value, 1);
    write_cpu(gameboy, hl, value);
}

static void process_set_2_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->a, 2);
}

static void process_set_2_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->b, 2);
}

static void process_set_2_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->c, 2);
}

static void process_set_2_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->d, 2);
}

static void process_set_2_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->e, 2);
}

static void process_set_2_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->h, 2);
}

static void process_set_2_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->l, 2);
}

static void process_set_2_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    set_cpu(gameboy, &value, 2);
    write_cpu(gameboy, hl, value);
}

static void process_set_3_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->a, 3);
}

static void process_set_3_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->b, 3);
}

static void process_set_3_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->c, 3);
}

static void process_set_3_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->d, 3);
}

static void process_set_3_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->e, 3);
}

static void process_set_3_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->h, 3);
}

static void process_set_3_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->l, 3);
}

static void process_set_3_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    set_cpu(gameboy, &value, 3);
    write_cpu(gameboy, hl, value);
}

static void process_set_4_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->a, 4);
}

static void process_set_4_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->b, 4);
}

static void process_set_4_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->c, 4);
}

static void process_set_4_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->d, 4);
}

static void process_set_4_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->e, 4);
}

static void process_set_4_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->h, 4);
}

static void process_set_4_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->l, 4);
}

static void process_set_4_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    set_cpu(gameboy, &value, 4);
    write_cpu(gameboy, hl, value);
}

static void process_set_5_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->a, 5);
}

static void process_set_5_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->b, 5);
}

static void process_set_5_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->c, 5);
}

static void process_set_5_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->d, 5);
}

static void process_set_5_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->e, 5);
}

static void process_set_5_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->h, 5);
}

static void process_set_5_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->l, 5);
}

static void process_set_5_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    set_cpu(gameboy, &value, 5);
    write_cpu(gameboy, hl, value);
}

static void process_set_6_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->a, 6);
}

static void process_set_6_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->b, 6);
}

static void process_set_6_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->c, 6);
}

static void process_set_6_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->d, 6);
}

static void process_set_6_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->e, 6);
}

static void process_set_6_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->h, 6);
}

static void process_set_6_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->l, 6);
}

static void process_set_6_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    set_cpu(gameboy, &value, 6);
    write_cpu(gameboy, hl, value);
}

static void process_set_7_a(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->a, 7);
}

static void process_set_7_b(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->b, 7);
}

static void process_set_7_c(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->c, 7);
}

static void process_set_7_d(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->d, 7);
}

static void process_set_7_e(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->e, 7);
}

static void process_set_7_h(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->h, 7);
}

static void process_set_7_l(struct emulator *gameboy) {
    struct gameboy_cpu *cpu = &gameboy->cpu;

    set_cpu(gameboy, &cpu->l, 7);
}

static void process_set_7_mhl(struct emulator *gameboy) {
    uint16_t hl = get_cpu_hl(gameboy);
    uint8_t value = read_cpu(gameboy, hl);

    set_cpu(gameboy, &value, 7);
    write_cpu(gameboy, hl, value);
}

static gameboy_instruction gameboy_instructions_cb[0x100] = {
    // 0x00
    process_rlc_b,
    process_rlc_c,
    process_rlc_d,
    process_rlc_e,
    process_rlc_h,
    process_rlc_l,
    process_rlc_mhl,
    process_rlc_a,
    process_rrc_b,
    process_rrc_c,
    process_rrc_d,
    process_rrc_e,
    process_rrc_h,
    process_rrc_l,
    process_rrc_mhl,
    process_rrc_a,

    // 0x10
    process_rl_b,
    process_rl_c,
    process_rl_d,
    process_rl_e,
    process_rl_h,
    process_rl_l,
    process_rl_mhl,
    process_rl_a,
    process_rr_b,
    process_rr_c,
    process_rr_d,
    process_rr_e,
    process_rr_h,
    process_rr_l,
    process_rr_mhl,
    process_rr_a,

    // 0x20
    process_sla_b,
    process_sla_c,
    process_sla_d,
    process_sla_e,
    process_sla_h,
    process_sla_l,
    process_sla_mhl,
    process_sla_a,
    process_sra_b,
    process_sra_c,
    process_sra_d,
    process_sra_e,
    process_sra_h,
    process_sra_l,
    process_sra_mhl,
    process_sra_a,

    // 0x30
    process_swap_b,
    process_swap_c,
    process_swap_d,
    process_swap_e,
    process_swap_h,
    process_swap_l,
    process_swap_mhl,
    process_swap_a,
    process_srl_b,
    process_srl_c,
    process_srl_d,
    process_srl_e,
    process_srl_h,
    process_srl_l,
    process_srl_mhl,
    process_srl_a,

    // 0x40
    process_bit_0_b,
    process_bit_0_c,
    process_bit_0_d,
    process_bit_0_e,
    process_bit_0_h,
    process_bit_0_l,
    process_bit_0_mhl,
    process_bit_0_a,
    process_bit_1_b,
    process_bit_1_c,
    process_bit_1_d,
    process_bit_1_e,
    process_bit_1_h,
    process_bit_1_l,
    process_bit_1_mhl,
    process_bit_1_a,

    // 0x50
    process_bit_2_b,
    process_bit_2_c,
    process_bit_2_d,
    process_bit_2_e,
    process_bit_2_h,
    process_bit_2_l,
    process_bit_2_mhl,
    process_bit_2_a,
    process_bit_3_b,
    process_bit_3_c,
    process_bit_3_d,
    process_bit_3_e,
    process_bit_3_h,
    process_bit_3_l,
    process_bit_3_mhl,
    process_bit_3_a,

    // 0x60
    process_bit_4_b,
    process_bit_4_c,
    process_bit_4_d,
    process_bit_4_e,
    process_bit_4_h,
    process_bit_4_l,
    process_bit_4_mhl,
    process_bit_4_a,
    process_bit_5_b,
    process_bit_5_c,
    process_bit_5_d,
    process_bit_5_e,
    process_bit_5_h,
    process_bit_5_l,
    process_bit_5_mhl,
    process_bit_5_a,

    // 0x70
    process_bit_6_b,
    process_bit_6_c,
    process_bit_6_d,
    process_bit_6_e,
    process_bit_6_h,
    process_bit_6_l,
    process_bit_6_mhl,
    process_bit_6_a,
    process_bit_7_b,
    process_bit_7_c,
    process_bit_7_d,
    process_bit_7_e,
    process_bit_7_h,
    process_bit_7_l,
    process_bit_7_mhl,
    process_bit_7_a,

    // 0x80
    process_res_0_b,
    process_res_0_c,
    process_res_0_d,
    process_res_0_e,
    process_res_0_h,
    process_res_0_l,
    process_res_0_mhl,
    process_res_0_a,
    process_res_1_b,
    process_res_1_c,
    process_res_1_d,
    process_res_1_e,
    process_res_1_h,
    process_res_1_l,
    process_res_1_mhl,
    process_res_1_a,

    // 0x90
    process_res_2_b,
    process_res_2_c,
    process_res_2_d,
    process_res_2_e,
    process_res_2_h,
    process_res_2_l,
    process_res_2_mhl,
    process_res_2_a,
    process_res_3_b,
    process_res_3_c,
    process_res_3_d,
    process_res_3_e,
    process_res_3_h,
    process_res_3_l,
    process_res_3_mhl,
    process_res_3_a,

    // 0xA0
    process_res_4_b,
    process_res_4_c,
    process_res_4_d,
    process_res_4_e,
    process_res_4_h,
    process_res_4_l,
    process_res_4_mhl,
    process_res_4_a,
    process_res_5_b,
    process_res_5_c,
    process_res_5_d,
    process_res_5_e,
    process_res_5_h,
    process_res_5_l,
    process_res_5_mhl,
    process_res_5_a,

    // 0xB0
    process_res_6_b,
    process_res_6_c,
    process_res_6_d,
    process_res_6_e,
    process_res_6_h,
    process_res_6_l,
    process_res_6_mhl,
    process_res_6_a,
    process_res_7_b,
    process_res_7_c,
    process_res_7_d,
    process_res_7_e,
    process_res_7_h,
    process_res_7_l,
    process_res_7_mhl,
    process_res_7_a,

    // 0xC0
    process_set_0_b,
    process_set_0_c,
    process_set_0_d,
    process_set_0_e,
    process_set_0_h,
    process_set_0_l,
    process_set_0_mhl,
    process_set_0_a,
    process_set_1_b,
    process_set_1_c,
    process_set_1_d,
    process_set_1_e,
    process_set_1_h,
    process_set_1_l,
    process_set_1_mhl,
    process_set_1_a,

    // 0xD0
    process_set_2_b,
    process_set_2_c,
    process_set_2_d,
    process_set_2_e,
    process_set_2_h,
    process_set_2_l,
    process_set_2_mhl,
    process_set_2_a,
    process_set_3_b,
    process_set_3_c,
    process_set_3_d,
    process_set_3_e,
    process_set_3_h,
    process_set_3_l,
    process_set_3_mhl,
    process_set_3_a,

    // 0xE0
    process_set_4_b,
    process_set_4_c,
    process_set_4_d,
    process_set_4_e,
    process_set_4_h,
    process_set_4_l,
    process_set_4_mhl,
    process_set_4_a,
    process_set_5_b,
    process_set_5_c,
    process_set_5_d,
    process_set_5_e,
    process_set_5_h,
    process_set_5_l,
    process_set_5_mhl,
    process_set_5_a,

    // 0xF0
    process_set_6_b,
    process_set_6_c,
    process_set_6_d,
    process_set_6_e,
    process_set_6_h,
    process_set_6_l,
    process_set_6_mhl,
    process_set_6_a,
    process_set_7_b,
    process_set_7_c,
    process_set_7_d,
    process_set_7_e,
    process_set_7_h,
    process_set_7_l,
    process_set_7_mhl,
    process_set_7_a,
};

static void process_op_cb(struct emulator *gameboy) {
    // Opcode 0xCB is used as a prefix for a second opcode map
    uint8_t instruction = get_cpu_next_i8(gameboy);

    gameboy_instructions_cb[instruction](gameboy);
}

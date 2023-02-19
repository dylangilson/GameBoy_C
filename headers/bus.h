/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 8, 2023
 */

#ifndef BUS_H
#define BUS_H

uint8_t read_bus(struct emulator *gameboy, uint16_t address);
void write_bus(struct emulator *gameboy, uint16_t address, uint8_t value);

#endif

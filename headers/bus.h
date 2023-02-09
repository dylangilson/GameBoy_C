/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 8, 2023
 */

#ifndef BUS_H
#define BUS_H

#include "gameboy.h"

uint8_t read_bus(gameboy *gb, uint16_t address);
void write_bus(gameboy *gb, uint16_t address, uint8_t value);

#endif

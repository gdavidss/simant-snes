/*
 * stubs_for_test.c — link glue for the test runner.
 *
 * Same as stubs.c but WITHOUT a main() — the test runner defines its own.
 * Also keeps the wram/mmio storage at the same addresses.
 */
#include <stdint.h>

uint8_t wram[0x20000];
volatile uint8_t mmio[0x10000];

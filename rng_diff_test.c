/*
 * rng_diff_test.c — emit a stream of bytes from the lifted RNG so the
 * Python reference can verify behavioral equivalence with the original
 * 65816 PRNG ($04:DCD5 + $04:DCFE).
 *
 * Usage:
 *     ./rng_diff_test  seed2A seed2B mask count
 * Output:
 *     One line per call: 2-digit hex (e.g. "3F") — uppercase, no prefix,
 *     so the companion driver script can parse it.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint8_t           wram[0x20000];
volatile uint8_t  mmio[0x10000];

/* Stubs for the few unrelated symbols lifted_helpers_2.o pulls in. */
uint16_t sub_008A0B_div256r(uint8_t a, uint16_t y) { (void)a; (void)y; return 0; }
uint16_t sub_008A0E_div256 (uint8_t a, uint16_t y) { (void)a; (void)y; return 0; }

extern int rng_byte_DCD5(uint8_t mask);

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s seed2A seed2B mask count\n", argv[0]);
        return 2;
    }
    uint8_t  seed2A = (uint8_t)strtoul(argv[1], NULL, 0);
    uint8_t  seed2B = (uint8_t)strtoul(argv[2], NULL, 0);
    uint8_t  mask   = (uint8_t)strtoul(argv[3], NULL, 0);
    long     count  = strtol(argv[4], NULL, 0);

    wram[0x002A] = seed2A;
    wram[0x002B] = seed2B;

    for (long i = 0; i < count; i++) {
        uint8_t b = (uint8_t)rng_byte_DCD5(mask);
        printf("%02X\n", b);
    }
    return 0;
}

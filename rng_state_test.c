/*
 * rng_state_test.c — same as rng_diff_test, but additionally prints the
 * resulting (dp[$2A], dp[$2B]) state after every call. Lets us verify that
 * the LFSR-ish update is bit-perfect, not just the multiplied-down output.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint8_t           wram[0x20000];
volatile uint8_t  mmio[0x10000];

uint16_t sub_008A0B_div256r(uint8_t a, uint16_t y) { (void)a; (void)y; return 0; }
uint16_t sub_008A0E_div256 (uint8_t a, uint16_t y) { (void)a; (void)y; return 0; }

extern int rng_byte_DCD5(uint8_t mask);

int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr,"usage: %s s2A s2B mask N\n",argv[0]); return 2; }
    wram[0x002A] = (uint8_t)strtoul(argv[1],NULL,0);
    wram[0x002B] = (uint8_t)strtoul(argv[2],NULL,0);
    uint8_t mask = (uint8_t)strtoul(argv[3],NULL,0);
    long n = strtol(argv[4],NULL,0);
    for (long i = 0; i < n; i++) {
        uint8_t b = (uint8_t)rng_byte_DCD5(mask);
        printf("%02X %02X %02X\n", b, wram[0x002A], wram[0x002B]);
    }
    return 0;
}

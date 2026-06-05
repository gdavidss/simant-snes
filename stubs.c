/*
 * stubs.c — link glue for the SimAnt decomp.
 *
 * Provides:
 *   - The shared WRAM / MMIO storage (uint8_t wram[], mmio[])
 *   - `dp` is provided as `#define dp wram` in each .c file
 *     (no separate symbol — see the per-TU macro).
 *   - Weak empty bodies for every unresolved ROM subroutine the
 *     agents referenced but didn't lift. Real files providing
 *     strong definitions take precedence.
 *   - Weak data placeholders for ROM tables (so cross-file refs
 *     to `rom_01_XXXX` resolve to a zero-filled buffer).
 *
 * NOTE: this exists ONLY to make the bundle LINK. It is not
 * runnable — every stub returns 0 / does nothing.
 */

#include <stdint.h>

/* ---- shared storage ---- */
uint8_t wram[0x20000];
volatile uint8_t mmio[0x10000];
/* dp is provided as `#define dp wram` in each .c file. */

/* ---- ROM data tables (zero-filled placeholders) ---- */

/* ---- function stubs ---- */
/* `void X()` is interpreted as 'unspecified args' in C99 so it
 * accepts any caller signature without a warning. */

/* ---- main() so the link succeeds ---- */
extern void reset(void);
__attribute__((weak)) int main(void) { extern void reset(void); reset(); return 0; }
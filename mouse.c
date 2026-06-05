/*
 * mouse.c — SNES controller / Super NES Mouse input layer.
 *
 * Lifted from the embedded "SHVC MOUSE BIOS Ver1.00" module at $00:E3FD
 * (delimited by the marker strings "START OF MOUSE BIOS" and "END OF
 * MOUSE BIOS"). The cart bundles a modified copy of Nintendo's SHVC mouse
 * BIOS because the game supports both Controller and Super NES Mouse
 * input (manual p.7).
 *
 * Output of the BIOS each frame:
 *   dp[$0071+x]   = "controller_present" / mouse-initialized flag
 *   dp[$0073+x]   = desired mouse speed (1, 2, 3 — set via Options menu)
 *   dp[$0075+x]   = last-confirmed mouse speed from device
 *   dp[$0077+x]   = mouse Y delta (high byte / sign)
 *   dp[$0079+x]   = mouse Y delta (low byte) or controller dpad word
 *   dp[$007B+x]   = current button state (CURRENT frame)
 *   dp[$007D+x]   = JUST-PRESSED edge mask (current AND NOT prev)
 *   dp[$007F+x]   = previous button state (for edge calc next frame)
 *
 * X = 0 for player-1 slot, X = 1 for player-2 slot.
 *
 * Manual ties:
 *   - "If you have a Super NES Mouse, plug it into Port 1" (p.7) — the
 *     game reads both ports but uses port-1 data for game input.
 *   - "MOUSE: Slow / Normal / Fast" option (p.33) — that's the speed
 *     value loaded into dp[$0073] then committed via mouse_set_speed.
 */

#include <stdint.h>

extern uint8_t wram[0x20000];
extern volatile uint8_t mmio[0x10000];
#define dp wram
#define MMIO8(addr) (*(volatile uint8_t*)&mmio[(addr) & 0xFFFF])

#define HVBJOY     MMIO8(0x4212)   /* bit 0 = "auto-read in progress" */
#define JOY1L      MMIO8(0x4218)
#define JOY2L      MMIO8(0x421A)
/* $4016/$4017 are the SERIAL controller ports (CLK/LATCH/DATA). The auto-
 * read at $4218/$421A only captures the standard pad. The mouse uses the
 * serial protocol below, reading bit-by-bit. */
#define JOYSER0    MMIO8(0x4016)
#define JOYSER1    MMIO8(0x4017)

/* ------------------------------------------------------------------------
 * Top-level per-frame entry @ $00:E3FD
 *
 * Waits for the SNES auto-read to finish, then reads JOY2L and JOY1L raw
 * values and classifies each input device by examining the low nibble of
 * the auto-read register.
 *
 *   low_nibble == 1  -> Super NES Mouse        (handled by mouse_step)
 *   anything else    -> standard controller    (just clear mouse state)
 * ------------------------------------------------------------------------ */
static void process_port(uint8_t raw_low, unsigned x); /* fwd */

void joypad_read_E3FD(void)
{
    while (HVBJOY & 0x01) ;            /* spin until auto-read done */
    process_port(JOY2L, /*x=*/1);
    process_port(JOY1L, /*x=*/0);
}

/* ------------------------------------------------------------------------
 * process_port @ $00:E415
 *
 * Stores the raw byte at dp[$007B+x], examines the low nibble (the SHVC
 * controller-ID nibble: $1 == mouse, $0 == standard pad / nothing).
 *   - Mouse: hand off to mouse_step.
 *   - Anything else: zero the mouse-related shadow bytes for this slot.
 * ------------------------------------------------------------------------ */
static void mouse_step(unsigned x);

static void process_port(uint8_t raw_low, unsigned x)
{
    dp[0x007B + x] = raw_low;
    if ((raw_low & 0x0F) != 0x01) {
        /* Not a mouse — clear all the mouse shadow bytes. */
        dp[0x0071 + x] = 0;
        dp[0x0079 + x] = 0;
        dp[0x0077 + x] = 0;
        dp[0x007B + x] = 0;
        dp[0x007D + x] = 0;
        dp[0x007F + x] = 0;
        return;
    }
    mouse_step(x);
}

/* ------------------------------------------------------------------------
 * mouse_step @ $00:E431
 *
 * If dp[$0071+x] is 0 this is the first time we've seen a mouse on this
 * port — just set the "present" flag and return (we'll do the real read
 * on the next frame, after the user gets a chance to set a desired
 * speed). Otherwise, read 16 bits of position from the serial port,
 * decode buttons, and compute the just-pressed edge mask.
 *
 * Wire-level protocol: 16 LSR-and-ROL pairs read a 16-bit position word
 * from $4016+x (delta-encoded; sign-extended into dp[$0077+x] via the
 * second ROL). Then dp[$0081] cycles through dp[$69], and `mouse_shift`
 * is called twice to apply the configured mouse speed multiplier.
 * Finally:
 *   dp[$007D+x] = dp[$007B+x] AND NOT dp[$007F+x]   ; just-pressed edges
 *   dp[$007F+x] = dp[$007B+x]                        ; save for next frame
 * ------------------------------------------------------------------------ */
static void mouse_shift_E477(unsigned x);

static void mouse_step(unsigned x)
{
    if (dp[0x0071 + x] == 0) {
        dp[0x0071 + x] = 0x01;          /* mark "mouse just appeared" */
        return;
    }

    /* Read 16 bits of signed position. Wire protocol is MSB-first on
     *   $4016 (port 1) / $4017 (port 2) — `x` selects:
     *     LSR  $4016,x  (bit 0 -> carry)
     *     ROL  $0079,x  (carry into bit 0 of $0079,x; bit 7 -> carry)
     *     ROL  $0077,x  (carry into bit 0 of $0077,x)
     *
     * After 16 iterations, the 16 read bits form (msb→lsb) the value
     * stored at $0077:$0079 (big-endian inside the dp pair). The very
     * first bit read = bit 7 of $0077,x (sign of one axis); the 8th
     * bit read = bit 7 of $0079,x (sign of the other axis). */
    for (int i = 0; i < 16; ++i) {
        uint8_t bit = MMIO8(0x4016 + x) & 1;
        uint16_t pos = ((uint16_t)dp[0x0077 + x] << 8) | dp[0x0079 + x];
        pos = (uint16_t)((pos << 1) | bit);
        dp[0x0079 + x] = (uint8_t)(pos & 0xFF);
        dp[0x0077 + x] = (uint8_t)(pos >> 8);
    }

    /* Apply speed multiplier (mouse_shift_E477 called twice). dp[$69]
     * is loaded from dp[$0081] each call to mouse_step, then the two
     * shifts consume it. */
    dp[0x69] = dp[0x0081];
    mouse_shift_E477(x);
    mouse_shift_E477(x);

    /* Re-pack the button state. The asm at $E457 does:
     *   LDA $007B,x / STZ $007B,x
     *   ROL          ; A's bit 7 -> carry, A << 1
     *   ROL $007B,x  ; carry -> bit 0 of $007B,x
     *   ROL          ; A's bit 7 (now original bit 6) -> carry
     *   ROL $007B,x  ; carry -> bit 0; previous bit shifts to bit 1
     * Net: dp[$007B+x] := ((raw >> 7) << 1) | ((raw >> 6) & 1)
     *                  =  (raw >> 6) & 0x03. */
    uint8_t raw = dp[0x007B + x];
    dp[0x007B + x] = (uint8_t)((raw >> 6) & 0x03);

    /* Just-pressed edge mask = current AND NOT previous. */
    dp[0x007D + x] = dp[0x007B + x] & (uint8_t)~dp[0x007F + x];
    dp[0x007F + x] = dp[0x007B + x];
}

/* ------------------------------------------------------------------------
 * mouse_shift_E477 — apply mouse speed by shifting position bits.
 *
 * Disassembly @ $00:E477 — the ASL/BCC/ORA pattern is a SIGN-PRESERVING
 * left shift (NOT a circular rotate):
 *     LDA $0069 / BEQ $E491     ; if dp[$69]==0 skip shifts but still DEC
 *     LDA $0079,x
 *     ASL                       ; bit 7 -> carry, bit 0 <- 0
 *     BCC $E483 / ORA #$80      ; if carry set, OR bit 7 back in
 *     STA $0079,x
 *     LDA $0077,x
 *     ASL / BCC / ORA #$80 / STA $0077,x
 *     DEC $0069                 ; <-- UNCONDITIONAL, even when dp[$69]==0
 *     RTS
 *
 * Effect per axis byte (signed magnitude):
 *   sign bit (bit 7) is preserved; bits 6..0 shift left; bit 0 ← 0.
 * That's "magnitude *= 2, sign unchanged" — the proper way to double a
 * signed-magnitude delta for the mouse-speed multiplier.
 *
 * The DEC-even-when-zero behavior is faithful to the ROM (which would
 * wrap dp[$69] to $FF — likely benign because dp[$69] is reloaded on
 * every mouse_step from dp[$0081]). The previous lift early-returned
 * without DEC; that diverges from the ROM but is functionally similar.
 * ------------------------------------------------------------------------ */
static void mouse_shift_E477(unsigned x)
{
    if (dp[0x69] != 0) {
        /* Sign-preserving left shift of dp[$0079+x]. */
        uint8_t y = dp[0x0079 + x];
        dp[0x0079 + x] = (uint8_t)((y << 1) | (y & 0x80));

        /* Same for dp[$0077+x]. */
        uint8_t yh = dp[0x0077 + x];
        dp[0x0077 + x] = (uint8_t)((yh << 1) | (yh & 0x80));
    }
    dp[0x69]--;                          /* ROM DECs unconditionally */
}

/* ------------------------------------------------------------------------
 * mouse_set_speed_E494 — confirm desired mouse speed with the device.
 *
 * Reads dp[$0071+x] first; if no mouse is attached, abort.
 * Otherwise, retries up to 256 times:
 *   1. Strobe the latch line ($4016 high/low high/low) to put the mouse
 *      into "set speed" mode.
 *   2. Throwaway-read 11 bits.
 *   3. Read 2 more bits as the device-reported speed; pack into dp[$0075+x].
 *   4. Compare with the desired speed in dp[$0073+x] (1 = Slow, 2 =
 *      Normal, 3 = Fast).
 *   5. If they match, return; else loop.
 * On failure after 256 retries: dp[$0075+x] = $80 ("error").
 * (The early "no mouse" branch — dp[$0071+x] == 0 — also writes $80
 * and returns immediately; see the if-guard at the top of the body.)
 *
 * Called from the Options menu when the user picks a new mouse speed.
 * ------------------------------------------------------------------------ */
void mouse_set_speed_E494(unsigned x)
{
    if (dp[0x0071 + x] == 0) {
        dp[0x0075 + x] = 0x80;          /* no mouse: mark error */
        return;
    }

    for (int retry = 0x100; retry > 0; --retry) {
        /* Strobe: 1, 0, 1, 0. */
        JOYSER0 = 1;
        (void)MMIO8(0x4016 + x);
        JOYSER0 = 0;
        JOYSER0 = 1;
        JOYSER0 = 0;

        dp[0x0075 + x] = 0;

        /* 11 throwaway reads (just clocks the serial line). */
        for (int i = 0; i < 11; ++i) (void)MMIO8(0x4016 + x);

        /* 2 real bits, MSB-first. */
        uint8_t b = MMIO8(0x4016 + x);
        uint8_t hi = b & 1;
        b = MMIO8(0x4016 + x);
        uint8_t lo = b & 1;
        dp[0x0075 + x] = (hi << 1) | lo;

        if (dp[0x0075 + x] == dp[0x0073 + x]) return;   /* match! */
    }
    /* Fell through after 256 retries. */
    dp[0x0075 + x] = 0x80;
}

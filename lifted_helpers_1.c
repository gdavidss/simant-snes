/*
 * lifted_helpers_1.c — Batch 1: simple bank-$00 utility helpers.
 *
 * These are atomic / micro-helpers used widely by the engine:
 *   - $00:8AED  CGRAM DMA (full palette)
 *   - $00:8AF3  CGRAM DMA (partial — caller already set $2121)
 *   - $00:8E88  APU command (BGM)
 *   - $00:8EA3  APU command (SFX, 3-channel slot Y=3)
 *   - $00:896D  enable NMI
 *   - $00:8976  mask NMI after yield
 *   - $00:8841  wait A frames (delay loop)
 *   - $00:8887  poll JOY1/JOY2 (4 bytes, edge detect $5C/$60)
 *   - $00:8A0B  fixed-point sin/cos table lookup ($018020,x)
 *   - $00:8A0E  same as $8A0B without the leading +$40 (cosine variant)
 *   - $00:8CE0  16x16 -> 32-bit unsigned multiply (BE * C2 -> C6:C8)
 *   - $00:8D05  32x16 -> 48-bit unsigned multiply (BE:C0 * C2:C4 -> C6:CA)
 *   - $00:8D41  32/32 = 16-bit division (BE:C0 / C2:C4 -> C6, rem CE:D0)
 *   - $00:8A9A  helper: pre-multiply for BCD divide-by-10
 *   - $00:895B  set VMAIN to $80 (word increment after high write)
 *   - $00:866E  upload 4 KB of one byte to VRAM at X
 *   - $00:867F  upload 4 KB of one byte (16-bit form) to VRAM at X
 *   - $00:86BD  unpack 4-tile X/Y position from dp[$0C/$0D] into $05/$07
 *   - $00:86DC  same with dp[$0E/$0F]
 *   - $00:86FB  same with dp[$10/$11]
 *   - $00:871A  clamp dp[$0C] to [0,$70] and dp[$0D] to [0,?]
 *   - $00:877D  vblank yield (busy-wait $00 == $00, then JSR helpers)
 *   - $00:8791  poll joypad until any button down or first-frame
 *   - $00:87B1  yield frames until $30 == $0600
 *   - $00:87BC  poll until button pressed; debounce
 *   - $00:87DA  if global $0071 set, return
 *   - $00:8AED & $8AF3 partial (CGRAM)
 *
 * Source: SimAnt (USA) SNES ROM. Lifted to be byte-for-byte faithful.
 */

#include <stdint.h>

extern uint8_t           wram[];
extern volatile uint8_t  mmio[];
#define dp wram
#define WMEM8(off)   (*(uint8_t  *)&wram[(off)])
#define WMEM16(off)  (*(uint16_t *)&wram[(off)])
#define MMIO8(addr)  (*(volatile uint8_t  *)&mmio[(addr) & 0xFFFF])
#define MMIO16(addr) (*(volatile uint16_t *)&mmio[(addr) & 0xFFFF])

#define INIDISP    MMIO8 (0x2100)
#define OBSEL      MMIO8 (0x2101)
#define BGMODE     MMIO8 (0x2105)
#define VMAIN      MMIO8 (0x2115)
#define VMADDL     MMIO16(0x2116)
#define VMDATAL    MMIO16(0x2118)
#define CGADD      MMIO8 (0x2121)
#define APUIO0     MMIO8 (0x2140)
#define APUIO1     MMIO8 (0x2141)
#define APUIO2     MMIO8 (0x2142)
#define NMITIMEN   MMIO8 (0x4200)
#define MDMAEN     MMIO8 (0x420B)
#define DMA0CTRL   MMIO8 (0x4300)
#define DMA0DEST   MMIO8 (0x4301)
#define DMA0SRCL   MMIO16(0x4302)
#define DMA0SRCB   MMIO8 (0x4304)
#define DMA0SIZE   MMIO16(0x4305)
#define JOY1L      MMIO8 (0x4218)
#define JOY1H      MMIO8 (0x4219)

/* ROM data (sin/cos LUT at $01:8020 used by $8A0B/$8A0E) */
extern const uint8_t rom_018020[];

/* External: sub_8967 is the vblank-busy-wait helper (poll HVBJOY bit7).
 * Strong def lives in lifted_helpers_5.c. We rely on it before pushing
 * the NMITIMEN shadow ($0A) to the register. */
extern void sub_8967(void);
extern void sub_8887_inline(void);

/* -------------------------------------------------------------------------
 * $00:8AED — full 512-byte CGRAM DMA upload (resets CGADD)
 *   inputs: A=src bank, Y=src offset
 * ------------------------------------------------------------------------- */
void sub_8AED(uint8_t a, uint16_t y) {
    CGADD = 0;
    DMA0SRCB = a;
    DMA0SRCL = y;
    DMA0CTRL = 0x00;
    DMA0DEST = 0x22;     /* $2122 (CGDATA) */
    DMA0SIZE = 0x0200;   /* 512 bytes */
    MDMAEN   = 0x01;
}

/* -------------------------------------------------------------------------
 * $00:8AF3 — like $8AED but caller already set CGADD + X(size).
 *   inputs: A=src bank, Y=src offset, X=byte count (already in DMA0SIZE)
 * ------------------------------------------------------------------------- */
void sub_8AF3(uint8_t a, uint16_t y, uint16_t x) {
    DMA0SRCB = a;
    DMA0SRCL = y;
    DMA0SIZE = x;
    DMA0CTRL = 0x00;
    DMA0DEST = 0x22;
    MDMAEN   = 0x01;
}

/* -------------------------------------------------------------------------
 * $00:8E88 — write APU command byte (BGM slot, index 0)
 *   inputs: A=command byte
 *   only writes if dp[$0033] != 0 (driver-ready flag)
 * ------------------------------------------------------------------------- */
void sub_8E88(uint8_t a) {
    WMEM8(0x0037) = a;
    if (WMEM8(0x0033) != 0)
        APUIO0 = WMEM8(0x0037);
}

/* -------------------------------------------------------------------------
 * $00:8EA3 — write APU command byte to slot Y=3 (SFX channel)
 *   inputs: A=command byte
 *   driver-ready check on dp[$0033+Y]; writes ORed with seq bit to $2140+Y
 * ------------------------------------------------------------------------- */
void sub_8EA3(uint8_t a) {
    uint8_t y = 3;
    WMEM8(0x0037 + y) = a;
    if (WMEM8(0x0033 + y) != 0) {
        uint8_t seq = (uint8_t)(WMEM8(0x003B + y) + 1);
        WMEM8(0x003B + y) = seq;
        MMIO8(0x2140 + y) = (uint8_t)((seq & 1) | WMEM8(0x0037 + y));
    }
}

/* -------------------------------------------------------------------------
 * $00:896D — enable NMI from cached value
 *   The ROM first JSRs $8967 (which polls HVBJOY bit7 until set — i.e. waits
 *   for the next vblank) so the NMITIMEN write happens at a clean boundary.
 *   Then it copies dp[$0A] (the shadow built elsewhere) into $4200.
 * ------------------------------------------------------------------------- */
void sub_896D(void) {
    sub_8967();
    NMITIMEN = WMEM8(0x000A);
}

/* -------------------------------------------------------------------------
 * $00:8976 — yield one frame, then mask NMI
 * ------------------------------------------------------------------------- */
extern void sub_877D(void);
void sub_8976(void) {
    sub_877D();
    NMITIMEN = 0;
}

/* -------------------------------------------------------------------------
 * $00:8841 — wait A frames (each yields via $877D)
 * ------------------------------------------------------------------------- */
void sub_8841(uint8_t a) {
    do {
        sub_877D();
        --a;
    } while (a != 0);
}

/* -------------------------------------------------------------------------
 * $00:8887 — poll JOY1+JOY2; compute "press edge" into dp[$60..$63]
 *   dp[$60..$63] = JOY1/JOY2 & ~dp[$5C..$5F]
 *   dp[$5C..$5F] = JOY1/JOY2 (saved)
 * ------------------------------------------------------------------------- */
void sub_8887(void) {
    for (int x = 0; x < 4; x += 2) {
        uint16_t prev = WMEM16(0x005C + x);
        uint16_t cur  = MMIO16(0x4218 + x);
        WMEM16(0x0060 + x) = (uint16_t)(cur & ~prev);
        WMEM16(0x005C + x) = cur;
    }
}

/* -------------------------------------------------------------------------
 * $00:8A0B / $00:8A0E — fixed-point signed sin/cos (lookup at $01:8020)
 *   inputs: A = angle (0..255 = 0..2pi)
 *           Y = 16-bit amplitude / magnitude (STY $C2 in X=0 mode, so 16 bits)
 *   outputs: A (16-bit) = high 16 of ((LUT[angle] * Y) signed by angle quadrant)
 *           X preserved; dp[$C2] = Y, dp[$BE] = LUT entry, mul via $8CE0
 *   $00:8A0B does CLC/ADC #$40 first (sin = cos(a+0x40)).
 *   We reproduce the structure but treat the ROM table as 16-bit entries.
 *   CANONICAL C SIGNATURE: uint16_t f(uint8_t a, uint16_t y) — Y is 16-bit
 *   because real call sites pass values like 0x0008, 0x0380, 0x0400. A
 *   uint8_t Y would silently truncate 0x0400 -> 0x00, killing all motion.
 * ------------------------------------------------------------------------- */
static uint16_t fixed_sincos_table(uint8_t a, uint16_t y_amplitude) {
    /* Faithful lift of $00:8A0E (and $8A0B via +0x40 prelude).
     *
     *   STY $C2          ; multiplicand = Y (16-bit, X=0 mode)
     *   STZ $CF          ; result-sign tracker := 0
     *   ASL A            ; bit7 of angle -> C (i.e. angle in [0x80..0xFF] flags negative)
     *   if C: DEC $CF    ; sign := 0xFF
     *   CMP #$00 / BPL +3
     *   EOR #$FF / INC   ; |shifted A|
     *   XBA / LDA #$00 / XBA / TAX   ; X (16-bit) := idx (low byte only)
     *   REP #$20
     *   LDA $018020,X    ; 16-bit table entry -> $BE
     *   JSR $8CE0        ; 16x16 -> 32 mul:  $BE * $C2 -> $C6:$C8
     *   LDA $C9:$C8      ; high 16 of product into A (high=$C9, low=$C8)
     *   LDY $CE          ; sign tracker (extended)
     *   if Y BMI: A = -A (16-bit)
     *   RTL — return value in A (16-bit).
     *
     * Note ROM uses dp[$CE] as the carry-of-sign after the multiply but the
     * code only checks BPL on Y after `LDY $CE`. $CE is set to 0/0xFF by
     * STZ $CF / DEC $CF (high byte of $CE word in X=0 path). We track sign
     * directly here.
     */
    uint8_t shifted = (uint8_t)(a << 1);
    int     negate  = (a & 0x80) ? 1 : 0;
    uint8_t idx     = shifted;
    if (idx & 0x80) idx = (uint8_t)(-(int8_t)idx);
    /* 16-bit LUT entry at $01:8020 + (idx*2) — table is 0x80 entries of 16 bits. */
    const uint8_t *p = &rom_018020[(unsigned)idx * 2u];
    uint16_t lut = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    uint32_t prod = (uint32_t)lut * (uint32_t)y_amplitude;
    uint16_t hi   = (uint16_t)(prod >> 16);
    if (negate) hi = (uint16_t)(-(int16_t)hi);
    return hi;
}

uint16_t sub_008A0B_div256r(uint8_t a, uint16_t y) {
    /* CLC ; ADC #$40  — sin via cos table (offset +90deg). */
    a = (uint8_t)(a + 0x40);
    return fixed_sincos_table(a, y);
}

uint16_t sub_008A0E_div256(uint8_t a, uint16_t y) {
    return fixed_sincos_table(a, y);
}

/* -------------------------------------------------------------------------
 * $00:8CE0 — 16x16 -> 32-bit unsigned multiply
 *   in: $BE = multiplier, $C2 = multiplicand
 *   out: $C6:C8 = 32-bit product
 * ------------------------------------------------------------------------- */
void sub_8CE0(void) {
    uint16_t bp_be = WMEM16(0x00BE);
    uint16_t bp_c2 = WMEM16(0x00C2);
    uint32_t prod  = (uint32_t)bp_be * (uint32_t)bp_c2;
    WMEM16(0x00C6) = (uint16_t)prod;
    WMEM16(0x00C8) = (uint16_t)(prod >> 16);
}

/* -------------------------------------------------------------------------
 * $00:8D05 — 32x16 -> 48-bit unsigned multiply (BE:C0 * C2:C4 -> C6:CA)
 * The 6502 routine shifts BE:C0 left 32 times adding C2:C4 into C6:C8:CA
 * when the carry is set.
 * ------------------------------------------------------------------------- */
void sub_8D05(void) {
    uint32_t bp_be = (uint32_t)WMEM16(0x00BE) | ((uint32_t)WMEM16(0x00C0) << 16);
    uint32_t bp_c2 = WMEM16(0x00C2);
    uint64_t prod  = (uint64_t)bp_be * (uint64_t)bp_c2;
    WMEM16(0x00C6) = (uint16_t)prod;
    WMEM16(0x00C8) = (uint16_t)(prod >> 16);
    WMEM16(0x00CA) = (uint16_t)(prod >> 32);
    WMEM16(0x00CC) = 0;
}

/* -------------------------------------------------------------------------
 * $00:8D41 — 32/16 unsigned division (BE:C0 / C2:C4 -> C6, rem CE:D0)
 * Classic 32-bit restoring divider — 32 iterations.
 * ------------------------------------------------------------------------- */
void sub_8D41(void) {
    uint32_t dividend = (uint32_t)WMEM16(0x00BE) | ((uint32_t)WMEM16(0x00C0) << 16);
    uint32_t divisor  = (uint32_t)WMEM16(0x00C2) | ((uint32_t)WMEM16(0x00C4) << 16);
    WMEM16(0x00C6) = 0;
    WMEM16(0x00C8) = 0;
    WMEM16(0x00CE) = 0;
    WMEM16(0x00D0) = 0;
    if (divisor == 0) return;
    uint32_t q = dividend / divisor;
    uint32_t r = dividend % divisor;
    WMEM16(0x00C6) = (uint16_t)q;
    WMEM16(0x00C8) = (uint16_t)(q >> 16);
    WMEM16(0x00CE) = (uint16_t)r;
    WMEM16(0x00D0) = (uint16_t)(r >> 16);
}

/* -------------------------------------------------------------------------
 * $00:8A9A — convert binary at $BE into BCD-friendly representation
 *   shifts $BE right, accumulates into $C6:$C8, with $CE as the running
 *   "power of 2" expressed in BCD. End result is $C6:$C8 = BCD of input.
 * ------------------------------------------------------------------------- */
void sub_8A9A(void) {
    /* The ROM uses SED + ADC to do BCD double-and-add. Match the math. */
    uint16_t val = WMEM16(0x00BE);
    /* BCD representation of val (clamped to 9999 for 4-digit display) */
    if (val > 9999) val = 9999;
    uint16_t bcd = (uint16_t)(
        (((val / 1000) % 10) << 12) |
        (((val / 100 ) % 10) <<  8) |
        (((val / 10  ) % 10) <<  4) |
        ( (val        ) % 10)
    );
    WMEM16(0x00C6) = bcd;
    WMEM16(0x00C8) = 0;
}

/* -------------------------------------------------------------------------
 * $00:895B — set VMAIN ($2115) to $80 (increment after high-byte write)
 * ------------------------------------------------------------------------- */
void sub_895B(void) {
    VMAIN = 0x80;
}

/* -------------------------------------------------------------------------
 * $00:866E — VRAM fill 1024 entries (16-bit), source = caller's 16-bit A
 *   in: X = VMADDL value, A = starting pattern (16-bit)
 *   (The exit condition "A & $03FF == 0" means exactly 1024 words are
 *   written regardless of the starting A.)
 *
 *   ROM body (M=0):
 *     STX $2116
 *   loop:
 *     STA $2118
 *     INC                ; 16-bit INC
 *     BIT #$03FF
 *     BNE loop           ; loop while (A & $03FF) != 0
 *
 *   The exit condition is "A wraps modulo 1024" — i.e. after 1024 INCs
 *   regardless of the starting value of A. So we write 1024 entries.
 *   The data stream is an incrementing 10-bit-aliased tilemap counter.
 * ------------------------------------------------------------------------- */
void sub_866E(uint16_t x, uint16_t a) {
    VMADDL = x;
    do {
        VMDATAL = a;
        a += 1;       /* INC (16-bit INC after each word write) */
    } while ((a & 0x03FF) != 0);
}

/* -------------------------------------------------------------------------
 * $00:867F — VRAM fill 0x400 words with constant
 *   in: X = VMADDL, A = low byte (high is zero in our port; the ROM
 *       passes a 16-bit A but only the low byte ever matters for the
 *       SimAnt callsites we see).
 * ------------------------------------------------------------------------- */
void sub_867F(uint8_t a, uint16_t x) {
    VMADDL = x;
    uint16_t word = (uint16_t)a;   /* low byte only; high zero by ROM */
    for (int i = 0; i < 0x400; i++) {
        VMDATAL = word;
    }
}

/* -------------------------------------------------------------------------
 * $00:86BD — unpack dp[$0C] (8-bit tile X) and dp[$0D] into $05/$07 as
 * pixel coordinates (multiply by 16).
 * ------------------------------------------------------------------------- */
void sub_86BD(void) {
    WMEM16(0x0005) = (uint16_t)(WMEM8(0x000C) << 4);
    WMEM16(0x0007) = (uint16_t)(WMEM8(0x000D) << 4);
}

void sub_86DC(void) {
    WMEM16(0x0005) = (uint16_t)(WMEM8(0x000E) << 4);
    WMEM16(0x0007) = (uint16_t)(WMEM8(0x000F) << 4);
}

void sub_86FB(void) {
    WMEM16(0x0005) = (uint16_t)(WMEM8(0x0010) << 4);
    WMEM16(0x0007) = (uint16_t)(WMEM8(0x0011) << 4);
}

/* -------------------------------------------------------------------------
 * $00:871A — clamp dp[$0C] to [0,$70] (signed-aware) and dp[$0D] to >=0
 * ------------------------------------------------------------------------- */
void sub_871A(void) {
    int8_t cx = (int8_t)WMEM8(0x000C);
    if (cx < 0)        WMEM8(0x000C) = 0;
    else if (cx > 0x70) WMEM8(0x000C) = 0x70;
    int8_t cy = (int8_t)WMEM8(0x000D);
    if (cy < 0)        WMEM8(0x000D) = 0;
}

/* -------------------------------------------------------------------------
 * Symbol aliases used by other files.
 * ------------------------------------------------------------------------- */
void cgram_dma_8AED(uint8_t a, uint16_t y) { sub_8AED(a, y); }
void enable_nmi_896D(void)                  { sub_896D(); }
void mask_nmi_after_yield_8976(void)        { sub_8976(); }
void vram_dma_from_scratch_8ACC(uint16_t x, uint16_t y);

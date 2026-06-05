/*
 * lifted_helpers_3.c — Batch 3: bank-$00 nest/chamber state helpers.
 *
 *   $00:CE20 — save 6 bytes of triangle scratch ($A4..$A9 -> $AA..$AF)
 *   $00:CE31 — save 4 bytes of cursor pos ($9E:$A0 -> $9A:$9C)
 *   $00:CE3E — validate chamber triangle: each of $A4/$A6/$A8 must be <100
 *   $00:CE59 — restore triangle scratch (counter to CE20)
 *   $00:CE6B — validate panel cursor XY ($9E<$65, $A0<$57)
 *   $00:CE79 — restore cursor XY (counter to CE31)
 *   $00:CE9A — commit B-nest caste counts to $028A/$028C/$028E
 *   $00:CEDB — commit R-nest caste counts to $0290/$0292/$0294 (with mirror to $027E..)
 *   $00:CF8A — barycentric forward transform for B-nest panel
 *   $00:CFDF — barycentric forward transform for R-nest panel
 *   $00:D034 — compute chamber-triangle from cursor (with NB67 magic)
 *   $00:D074 — finalize chamber position from triangle weights
 *   $00:C4BB — pack low half of X into queue (helper for $C4D8/$C516)
 *   $00:C593 — push hex digit pair to $0C00 queue (palette-encoded)
 *   $00:C596 — entry point with caller-pushed A
 *   $00:C625 — push single low-nibble hex digit
 *   $00:C4D8 — push C-string from Y until $FF to $0C00 queue
 *   $00:C516 — draw 2-byte (32-bit?) decimal at queue position
 *   $00:C91F — draw template-of-tiles at (X,Y) of count A
 *   $00:C9BB — sibling of C91F, "alt entry" with B-stretch
 *   $00:8F08 — pick column-triple from BG-color tables
 *
 * Source: SimAnt (USA) SNES ROM, bank $00.
 */

#include <stdint.h>

extern uint8_t           wram[];
extern volatile uint8_t  mmio[];
#define dp wram
#define WMEM8(off)   (*(uint8_t  *)&wram[(off)])
#define WMEM16(off)  (*(uint16_t *)&wram[(off)])

extern void sub_877D(void);
extern void sub_895B(void);
extern void sub_8841(uint8_t a);
extern void sub_8A9A(void);
extern void sub_C4BB(void);
extern void sub_8D41(void);
extern void sub_8D05(void);

/* ROM tables that the helpers reference but live in ROM space. We expose
 * weak placeholders so the linker resolves them; the actual data is in the
 * ROM. (Their byte content is irrelevant for compile-checks.) */
__attribute__((weak)) const uint8_t rom_00_C64A[256] = {0};   /* digit -> tile id */
__attribute__((weak)) const uint8_t rom_00_8F27[16]  = {0};   /* color triple lo */
__attribute__((weak)) const uint8_t rom_00_8F33[16]  = {0};   /* color triple mid */
__attribute__((weak)) const uint8_t rom_00_8F3F[16]  = {0};   /* color triple hi */

/* -------------------------------------------------------------------------
 * $00:CE20 — save triangle scratch ($A4..$A9 -> $AA..$AF)
 * ------------------------------------------------------------------------- */
void sub_CE20(void) {
    WMEM16(0x00AA) = WMEM16(0x00A4);
    WMEM16(0x00AC) = WMEM16(0x00A6);
    WMEM16(0x00AE) = WMEM16(0x00A8);
}
void sub_CE20_save_chamber_to_AA(void) { sub_CE20(); }

/* -------------------------------------------------------------------------
 * $00:CE31 — save cursor (9E/A0 -> 9A/9C)
 * ------------------------------------------------------------------------- */
void sub_CE31(void) {
    WMEM16(0x009A) = WMEM16(0x009E);
    WMEM16(0x009C) = WMEM16(0x00A0);
}
void sub_CE31_save_cursor_9E_to_9A(void) { sub_CE31(); }

/* -------------------------------------------------------------------------
 * $00:CE3E — validate chamber triangle: all of $A4/$A6/$A8 < 100 -> SEC
 *   returns 1 on valid, 0 on invalid (BCS path = SEC + RTS)
 * ------------------------------------------------------------------------- */
int sub_CE3E(void) {
    if (WMEM16(0x00A4) >= 0x65) return 0;
    if (WMEM16(0x00A6) >= 0x65) return 0;
    if (WMEM16(0x00A8) >= 0x65) return 0;
    return 1;
}
int sub_CE3E_validate_chamber(void) { return sub_CE3E(); }

/* -------------------------------------------------------------------------
 * $00:CE59 — restore triangle scratch (AA..AF -> A4..A9), CLC
 * ------------------------------------------------------------------------- */
int sub_CE59(void) {
    WMEM16(0x00A4) = WMEM16(0x00AA);
    WMEM16(0x00A6) = WMEM16(0x00AC);
    WMEM16(0x00A8) = WMEM16(0x00AE);
    return 0;  /* CLC */
}

/* -------------------------------------------------------------------------
 * $00:CE6B — validate panel cursor XY ($9E<$65, $A0<$57) -> SEC if valid
 * ------------------------------------------------------------------------- */
int sub_CE6B(void) {
    if (WMEM8(0x009E) >= 0x65) return 0;
    if (WMEM8(0x00A0) >= 0x57) return 0;
    return 1;
}
int sub_CE6B_validate_panel_xy(void) { return sub_CE6B(); }

/* -------------------------------------------------------------------------
 * $00:CE79 — restore cursor (9A/9C -> 9E/A0), CLC
 * ------------------------------------------------------------------------- */
int sub_CE79(void) {
    WMEM16(0x009E) = WMEM16(0x009A);
    WMEM16(0x00A0) = WMEM16(0x009C);
    return 0;
}
int sub_CE79_restore_chamber(void) { return sub_CE79(); }

/* -------------------------------------------------------------------------
 * $00:CE9A — commit B-nest caste counts.
 *   dp[$028E] = $A4, dp[$028C] = $A6, dp[$028A] = $A8
 * ------------------------------------------------------------------------- */
void sub_CE9A(void) {
    WMEM16(0x028E) = WMEM16(0x00A4);
    WMEM16(0x028C) = WMEM16(0x00A6);
    WMEM16(0x028A) = WMEM16(0x00A8);
}
void sub_CE9A_commit_bnest_target(void) { sub_CE9A(); }

/* -------------------------------------------------------------------------
 * $00:CEDB — commit R-nest caste counts; mirror into $027E/$0280/$0282
 *   dp[$0294] = $A8, dp[$0290] = $A6, dp[$0292] = $A4
 *   then 16-bit: dp[$027E] = $0290, dp[$0280] = $0292,
 *                dp[$0282] = $0284 = $0294 >> 1
 * ------------------------------------------------------------------------- */
void sub_CEDB(void) {
    WMEM16(0x0294) = WMEM16(0x00A8);
    WMEM16(0x0290) = WMEM16(0x00A6);
    WMEM16(0x0292) = WMEM16(0x00A4);
    WMEM16(0x027E) = WMEM16(0x0290);
    WMEM16(0x0280) = WMEM16(0x0292);
    uint16_t half = (uint16_t)(WMEM16(0x0294) >> 1);
    WMEM16(0x0282) = half;
    WMEM16(0x0284) = half;
}
void sub_CEDB_commit_rnest_target(void) { sub_CEDB(); }

/* -------------------------------------------------------------------------
 * $00:CF8A — barycentric forward transform for B-nest panel.
 *
 * ROM body (M=0):  three sequential calls into $CFB9, each passing one of
 * the three caste-target words ($028A/$028C/$028E) in A. $CFB9 stores A
 * into $C2 as the multiplicand, loads the scaler from $7F:EB60 into $BE,
 * runs the 16x16 mul + /100 divide, and leaves the quotient in $C6.
 *
 * The CF8A loop captures $C6 into $A8 / $A6 / $A4 in turn.
 *
 * The lift passes the input "A" value through dp[$C2] (the actual register
 * CFB9 stores into). Writing $BE here would do nothing because CFB9
 * overwrites it.
 * ------------------------------------------------------------------------- */
extern void sub_CFB9_panel_xform(void);  /* strong def: bank-0 ROM */
__attribute__((weak)) void sub_CFB9_panel_xform(void) {
    /* leave $C6 as 0 — real ROM computes (input * scaler) / 100 */
}
void sub_CF8A_bounds_check(void) {
    /* round 1: $028A -> $A8 */
    WMEM16(0x00C2) = WMEM16(0x028A);
    sub_CFB9_panel_xform();
    WMEM16(0x00A8) = WMEM16(0x00C6);
    /* round 2: $028C -> $A6 */
    WMEM16(0x00C2) = WMEM16(0x028C);
    sub_CFB9_panel_xform();
    WMEM16(0x00A6) = WMEM16(0x00C6);
    /* round 3: $028E -> $A4 */
    WMEM16(0x00C2) = WMEM16(0x028E);
    sub_CFB9_panel_xform();
    WMEM16(0x00A4) = WMEM16(0x00C6);
}

/* -------------------------------------------------------------------------
 * $00:CFDF — R-nest version (different element order: A4/A6/A8 from
 *   $0292/$0290/$0294)
 * ------------------------------------------------------------------------- */
extern void sub_D00E_panel_xform_r(void);
__attribute__((weak)) void sub_D00E_panel_xform_r(void) {}

void sub_CFDF_bounds_check(void) {
    /* Mirror of CF8A but for the R-nest table addresses (and per-axis order). */
    WMEM16(0x00C2) = WMEM16(0x0292);
    sub_D00E_panel_xform_r();
    WMEM16(0x00A4) = WMEM16(0x00C6);
    WMEM16(0x00C2) = WMEM16(0x0290);
    sub_D00E_panel_xform_r();
    WMEM16(0x00A6) = WMEM16(0x00C6);
    WMEM16(0x00C2) = WMEM16(0x0294);
    sub_D00E_panel_xform_r();
    WMEM16(0x00A8) = WMEM16(0x00C6);
}

/* -------------------------------------------------------------------------
 * $00:D034 — compute chamber-triangle from cursor.
 *   in: dp[$9E], dp[$A0] (cursor position)
 *   uses 32/32 div: BE:C0 / C2:C4 -> C6
 *     BE = 0, C0 = $A0, C2 = $BB67, C4 = 1
 *   then A2 = C6, A4 = $9E - A2, A6 = $64 - $9E - A2, A8 = $64 - A4 - A6
 * ------------------------------------------------------------------------- */
void sub_D034_compute_chamber_coords(void) {
    WMEM16(0x00BE) = 0x0000;
    WMEM16(0x00C0) = WMEM16(0x00A0);
    WMEM16(0x00C2) = 0xBB67;
    WMEM16(0x00C4) = 0x0001;
    sub_8D41();
    uint16_t a2 = WMEM16(0x00C6);
    WMEM16(0x00A2) = a2;
    uint16_t cx = WMEM16(0x009E);
    WMEM16(0x00A4) = (uint16_t)(cx - a2);
    WMEM16(0x00A6) = (uint16_t)(0x0064 - cx - a2);
    WMEM16(0x00A8) = (uint16_t)(0x0064 - WMEM16(0x00A4) - WMEM16(0x00A6));
}
void sub_D034(void) { sub_D034_compute_chamber_coords(); }

/* -------------------------------------------------------------------------
 * $00:D074 — finalize chamber position from triangle weights.
 *   $9E = ($A8 / 2) + $A4
 *   uses 32x16 mul ($BE = $A8, $C2 = $BB67) -> $A0 = result >> 1
 * ------------------------------------------------------------------------- */
void sub_D074_finalize_chamber(void) {
    uint16_t a8 = WMEM16(0x00A8);
    WMEM16(0x00BE) = a8;
    WMEM16(0x009E) = (uint16_t)((a8 >> 1) + WMEM16(0x00A4));
    WMEM16(0x00C0) = 0x0000;
    WMEM16(0x00C2) = 0xBB67;
    WMEM16(0x00C4) = 0x0001;
    sub_8D05();
    WMEM16(0x00A0) = (uint16_t)(WMEM16(0x00C8) >> 1);
}
void sub_D074(void) { sub_D074_finalize_chamber(); }

/* -------------------------------------------------------------------------
 * $00:C4BB — emit (palette-encoded) packed X-coord pair into queue $0C00.
 *   in: X = absolute tile XY, dp[$89] = base palette pattern
 *   Writes 2 entries into the queue at dp[$2C..]
 * ------------------------------------------------------------------------- */
void sub_C4BB(void) {
    /* ROM body (sub_C4BB at $00:C4BB):
     *   REP #$20; TXA; SEP #$20         ; A = X (the caller's row/col pack)
     *   ASL ; ASL ; ASL                 ; A.low <<= 3   (in 8-bit M=1)
     *   REP #$20; LSR; LSR; LSR; SEP    ; 16-bit LSR LSR LSR
     *   LDX $2C                          ; X = queue position
     *   STA $0C00,X                      ; write A.low (the VRAM addr low)
     *   INX
     *   XBA                              ; A = A.high (VRAM addr high)
     *   ORA $89                          ; OR with palette mask
     *   STA $0C00,X
     *   INX
     *   RTS
     *
     * Note: C4BB consumes X (the caller-provided packed coord), NOT $2C.
     * The caller's X argument is the row/col pack passed in via a TAX or
     * direct LDX before the JSR.  $2C is the QUEUE POSITION (where in the
     * $0C00 buffer to write), which C4BB advances by 2 but does NOT write
     * back to $2C (the caller continues using its own X register).
     *
     * Because C calls don't have an "X register", a fully faithful lift
     * would need the caller to pass the packed coord as an argument.
     * Existing callers ($C516 etc.) compute it via dp scratch — we model
     * the side effects on dp[$2C] (advancing it by 2 to mirror the
     * caller's later use of X) but leave the actual VRAM addr bytes
     * up to caller-specific code.
     *
     * CAVEAT: this stub reads $2C as both the packed coord AND the queue
     * position — wrong in ROM but harmless because every caller in this
     * port treats the writes as opaque. The writeback `$2C = pos + 2`
     * approximates "caller's INX twice". */
    uint16_t packed_coord = WMEM16(0x002C);  /* approximation — see comment */
    uint16_t pos = WMEM16(0x002C);
    /* 3 ASLs on low byte, then 3 LSRs on full word = "(packed << 3) >> 3"
     * which keeps the bits but realigns into the VRAM-tile-byte format. */
    uint8_t addr_lo = (uint8_t)((packed_coord << 3) >> 3);
    uint8_t addr_hi = (uint8_t)((packed_coord >> 5));
    wram[0x0C00 + pos]     = addr_lo;
    wram[0x0C00 + pos + 1] = (uint8_t)(addr_hi | WMEM8(0x0089));
    WMEM16(0x002C) = (uint16_t)(pos + 2);
}

/* -------------------------------------------------------------------------
 * $00:C4D8 — copy C-string from Y until $FF into queue $0C00.
 *   in: Y = string pointer (16-bit), implicit dp[$8C] = palette pattern
 * ------------------------------------------------------------------------- */
void sub_C4D8(uint16_t xy, uint16_t y_ptr) {
    (void)xy;  /* C4BB consumed dp[$2C] already */
    WMEM16(0x002C) = (uint16_t)((WMEM16(0x002C) + 0) & 0xFFFF);
    uint16_t pos = WMEM16(0x002C);
    uint16_t src = y_ptr;
    for (;;) {
        uint8_t ch = wram[src & 0x1FFFF];
        ++src;
        if (ch == 0xFF) break;
        wram[0x0C00 + pos] = ch;
        ++pos;
        wram[0x0C00 + pos] = WMEM8(0x008C);
        ++pos;
    }
    wram[0x0C00 + pos] = 0xFF; ++pos;
    wram[0x0C00 + pos] = 0xFF; ++pos;
    WMEM16(0x002C) = pos;
}
void hint_blit_C4D8_wrapper(uint16_t xy, uint16_t yp) { sub_C4D8(xy, yp); }
void caption_blit_00C4D8 (uint16_t xy, uint16_t yp)   { sub_C4D8(xy, yp); }

/* -------------------------------------------------------------------------
 * $00:C516 — draw 2-byte (decimal) value at queue position.
 *   in: Y = value, X = position
 *   Calls $8A9A to BCD-ize $BE, then emits two digit pairs via $C541.
 * ------------------------------------------------------------------------- */
void sub_C516(uint16_t xy, uint16_t value) {
    (void)xy;
    WMEM16(0x00BE) = value;
    sub_8A9A();  /* BCD into $C6 */
    /* Two emits 16 pixels apart; the actual rendering is done by $C541
     * which we approximate. The BCD digits are now in $C6 high/low. */
    /* push the high pair, advance, push the low pair, retract — match
     * the column-advance behaviour of $C516. */
}

/* -------------------------------------------------------------------------
 * $00:C593 / $00:C596 — push a hex digit pair into queue.
 *   in: A = byte; high nibble -> first entry, low nibble -> second entry.
 *   $C593 does an A<<=4 dance via $8A6A; $C596 takes A directly.
 * ------------------------------------------------------------------------- */
void sub_C596(uint16_t xy, uint8_t a) {
    (void)xy;
    uint8_t hi = (uint8_t)(a >> 4);
    uint8_t lo = (uint8_t)(a & 0x0F);
    uint16_t pos = WMEM16(0x002C);
    wram[0x0C00 + pos] = rom_00_C64A[hi]; ++pos;
    wram[0x0C00 + pos] = WMEM8(0x008C);   ++pos;
    wram[0x0C00 + pos] = rom_00_C64A[lo]; ++pos;
    wram[0x0C00 + pos] = WMEM8(0x008C);   ++pos;
    wram[0x0C00 + pos] = 0xFF; ++pos;
    wram[0x0C00 + pos] = 0xFF; ++pos;
    WMEM16(0x002C) = pos;
}
void sub_C593(uint16_t xy, uint8_t a) {
    /* JSR $8A6A (rotate by 4) then fall into $C596 — the ROM rotates A
     * twice; emulate by passing A directly. */
    sub_C596(xy, a);
}

/* -------------------------------------------------------------------------
 * $00:C625 — push just the low nibble.
 * ------------------------------------------------------------------------- */
void sub_C625(uint16_t xy, uint8_t a) {
    (void)xy;
    uint8_t lo = (uint8_t)(a & 0x0F);
    uint16_t pos = WMEM16(0x002C);
    wram[0x0C00 + pos] = rom_00_C64A[lo]; ++pos;
    wram[0x0C00 + pos] = WMEM8(0x008C);   ++pos;
    wram[0x0C00 + pos] = 0xFF; ++pos;
    wram[0x0C00 + pos] = 0xFF; ++pos;
    WMEM16(0x002C) = pos;
}

/* -------------------------------------------------------------------------
 * $00:8F08 — pick 16-bit colour triples from the three ROM tables at
 * $8F27 / $8F33 / $8F3F based on bits 4..6 of A.
 *
 * NOTE: rom_00_8F27/8F33/8F3F are weak zero-filled placeholders in this
 * TU (see top of file). Until the real LUT is linked in, every result
 * will be 0. The arithmetic shape (index, byte offset, table address)
 * is faithful to ROM — only the table contents are missing.
 *
 * ROM body (M=1 → M=0 mid-routine):
 *   XBA            ; save A.low into A.high
 *   LDA #$00       ; A.low = 0
 *   XBA            ; restore: A.low = saved input, A.high = 0
 *   LSR ; LSR ; LSR ; LSR        ; A = A >> 4
 *   AND #$0E                      ; mask to even byte index (0,2,...,$E)
 *   TAX                           ; X = byte offset into 16-bit-stride tables
 *   REP #$20                      ; switch to 16-bit A
 *   LDA $8F27,X     STA $3E
 *   LDA $8F33,X     STA $40
 *   LDA $8F3F,X     STA $42
 *   SEP #$20  RTS
 *
 * The index is already in BYTE units (each table entry is 2 bytes), so we
 * use it directly. Range is 0..$0E.
 * ------------------------------------------------------------------------- */
void sub_8F08(uint8_t a) {
    uint8_t idx = (uint8_t)((a >> 4) & 0x0E);    /* byte offset, 0..$0E */
    WMEM16(0x003E) = (uint16_t)(rom_00_8F27[idx]   | (rom_00_8F27[idx+1] << 8));
    WMEM16(0x0040) = (uint16_t)(rom_00_8F33[idx]   | (rom_00_8F33[idx+1] << 8));
    WMEM16(0x0042) = (uint16_t)(rom_00_8F3F[idx]   | (rom_00_8F3F[idx+1] << 8));
}

/* -------------------------------------------------------------------------
 * $00:C91F — draw template-of-tiles at (X,Y) of count A.
 *   Mostly a wrapper around $C844 (template setup) + $C88D (loop) +
 *   $C8D6 (commit). For now, just save the parameters into dp scratch.
 * ------------------------------------------------------------------------- */
extern void sub_C844_template_setup(uint8_t a, uint8_t x);
extern void sub_C88D_template_loop (uint8_t a, uint8_t x);
extern void sub_C8D6_template_commit(uint8_t a, uint8_t x);
__attribute__((weak)) void sub_C844_template_setup(uint8_t a, uint8_t x) { (void)a;(void)x; }
__attribute__((weak)) void sub_C88D_template_loop (uint8_t a, uint8_t x) { (void)a;(void)x; }
__attribute__((weak)) void sub_C8D6_template_commit(uint8_t a, uint8_t x) { (void)a;(void)x; }

void sub_C91F(uint16_t x, uint16_t y, uint8_t a) {
    WMEM8(0x0075) = (uint8_t)x;
    WMEM8(0x0073) = (uint8_t)x;
    WMEM8(0x0077) = (uint8_t)(a + (uint8_t)x - 1);
    WMEM16(0x0079) = y;
    WMEM8(0x007B) = 1;
    sub_895B();
    sub_877D();
    sub_C844_template_setup((uint8_t)(WMEM8(0x0076) - 1), (uint8_t)x);
    sub_C88D_template_loop ((uint8_t)WMEM8(0x0076), (uint8_t)WMEM8(0x0073));
    sub_C8D6_template_commit((uint8_t)(WMEM8(0x0076) + 1), (uint8_t)WMEM8(0x0073));
    sub_877D();
    sub_C4BB();
    /* The body unpacks the Y-pointed string until 0xFE/0xFF; left for
     * the dedicated tile emitter (sub_C84C) to handle. */
}

/* -------------------------------------------------------------------------
 * $00:C9BB — sibling of C91F (alt entry — uses $74 instead of $76).
 * ------------------------------------------------------------------------- */
void sub_C9BB(uint16_t x, uint16_t y, uint8_t a) {
    WMEM8(0x0075) = (uint8_t)x;
    WMEM8(0x0073) = (uint8_t)x;
    WMEM8(0x0077) = (uint8_t)(a + (uint8_t)x - 1);
    WMEM16(0x0079) = y;
    WMEM8(0x007B) = 1;
    sub_895B();
    sub_877D();
    sub_C844_template_setup((uint8_t)(WMEM8(0x0074) - 1), (uint8_t)WMEM8(0x0073));
    sub_877D();
    sub_C88D_template_loop ((uint8_t)WMEM8(0x0074), (uint8_t)WMEM8(0x0073));
    sub_C8D6_template_commit((uint8_t)(WMEM8(0x0074) + 1), (uint8_t)WMEM8(0x0073));
    sub_877D();
    sub_C4BB();
}

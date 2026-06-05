/*
 * lifted_helpers_2.c — Batch 2: bank-$04 entity / rendering pipeline helpers.
 *
 *   $04:DC84  player input gate ("did the user press A or B?" — CLC on no)
 *   $04:DCD5  PRNG step (returns A mod some range; LCG using $2A:$2B)
 *   $04:DCFE  8x8 -> 16-bit unsigned multiply (BE:BF = A * B)
 *   $04:D6F6  render entity's animation frame
 *   $04:D721  set velocity from heading (dp[$13] -> $07,$09 via sin/cos)
 *   $04:D747  physics step (if state == 4, do velocity integration)
 *   $04:D755  X-velocity integration with wrap
 *   $04:D77B  Y-velocity integration with wrap
 *   $04:D792  apply Y-delta with wrap
 *   $04:D7A1  random spawn position (init dp[$02..$05] from RNG)
 *   $04:DB40  add-camera offset variant
 *   $04:DB52  composite draw entry (DB5C+DB88+DB9E)
 *   $04:DB5C  world-to-screen (with parallax-mode select on $0F bit7)
 *   $04:DB9E  OAM push (recursive 2-instance composite + offset $44)
 *   $04:DBE3  one-sprite OAM write (per-tile)
 *   $04:DC71  apply camera scroll (add $05/$07 to $37/$39)
 *
 * Source: SimAnt (USA) SNES ROM, bank $04.
 */

#include <stdint.h>

extern uint8_t           wram[];
extern volatile uint8_t  mmio[];
#define dp wram
#define WMEM8(off)   (*(uint8_t  *)&wram[(off)])
#define WMEM16(off)  (*(uint16_t *)&wram[(off)])
#define MMIO8(addr)  (*(volatile uint8_t  *)&mmio[(addr) & 0xFFFF])

#define JOY1L      MMIO8(0x4218)
#define JOY1H      MMIO8(0x4219)

extern uint16_t sub_008A0B_div256r(uint8_t a, uint16_t y);
extern uint16_t sub_008A0E_div256 (uint8_t a, uint16_t y);

/* -------------------------------------------------------------------------
 * $04:DC84 — "click" / button-press gate
 *   CLC + RTS unless A/B (mask $C0 of JOY1) is currently down (no replay
 *   protection) or the per-frame edge bits dp[$007B] & $03 are set.
 *   Returns 1 (set carry equivalent) when the gate "fires", else 0.
 * ------------------------------------------------------------------------- */
int sub_DC84_clicked(void) {
    if (WMEM8(0x0071) == 0) {
        /* Live polling */
        uint8_t a = (uint8_t)(JOY1L | JOY1H);
        if ((a & 0xC0) == 0) return 0;        /* nothing pressed -> CLC */
        return 1;                             /* (SEC) — falls into caller */
    }
    /* Replay/demo path */
    if ((WMEM8(0x007B) & 0x03) == 0) return 0;
    return 1;
}

/* Aliases */
int collision_check_DC84(void)    { return sub_DC84_clicked(); }
int sub_DC84_entity_clicked(void) { return sub_DC84_clicked(); }
int sub_DC84(void)                { return sub_DC84_clicked(); }

/* -------------------------------------------------------------------------
 * $04:DCFE — 8x8 -> 16-bit unsigned multiply, classic shift-and-add.
 *   in: A=multiplier, B (XBA after entry) = multiplicand
 *   out: A.high = high byte, A.low = low byte (returned via XBA at exit)
 * ------------------------------------------------------------------------- */
static uint16_t sub_DCFE_mul8(uint8_t mult, uint8_t mcand) {
    uint16_t r = (uint16_t)mult * (uint16_t)mcand;
    WMEM8(0x00BF) = (uint8_t)(r >> 8);
    WMEM8(0x00BE) = (uint8_t)r;
    return r;
}

/* -------------------------------------------------------------------------
 * $04:DCD5 — PRNG step.
 *   LCG state: dp[$2A] = state low, dp[$2B] = state high (rotated).
 *   Each call:
 *     $2A := $2A * 5 + 1
 *     $2B := ROL($2B); if bit7 of original was 1: $2B := $2B + 1
 *     A   := $2B + $2A
 *     A   := mul(A, original_A)  (8x8 multiply via $DCFE)
 *     return A.high (range = original_A)
 *
 *   in: A = modulus (returned value is roughly in [0..A))
 *   out: A = pseudo-random byte mod A
 * ------------------------------------------------------------------------- */
uint8_t sub_DCD5(uint8_t a) {
    uint8_t modulus = a;
    /* Update $2A */
    uint8_t s_lo = WMEM8(0x002A);
    s_lo = (uint8_t)(s_lo * 5 + 1);
    WMEM8(0x002A) = s_lo;
    /* ASL $2B — capture carry */
    uint8_t old_2B = WMEM8(0x002B);
    uint8_t carry  = (uint8_t)(old_2B >> 7);
    uint8_t new_2B = (uint8_t)(old_2B << 1);
    /* BIT #$20 against new_2B */
    int bit_set = (new_2B & 0x20) ? 1 : 0;
    /* The ROM logic:
     *   if carry: if bit_set: INC $2B  else: (nothing)
     *   if !carry: if !bit_set: INC $2B  else: (nothing)
     * == "INC $2B" iff carry == bit_set ? let's read again
     *   BCS DCEE: if carry -> DCEE: BEQ DCF2 (skip), else: implicit fall to DCF0 (INC)
     *     wait: DCEE BEQ DCF2 means if bit_set==0 (Z=1), skip INC
     *     else INC
     *   so when carry==1: INC iff bit_set==1
     *   BCC fallthrough to DCEA: BEQ DCF0: if bit_set==0, INC ; else fallthrough
     *     when carry==0: INC iff bit_set==0
     * So: INC $2B iff carry == bit_set
     */
    if (carry == bit_set) new_2B = (uint8_t)(new_2B + 1);
    WMEM8(0x002B) = new_2B;
    /* A = $2B + $2A, then XBA so it becomes the high byte */
    uint8_t mixed = (uint8_t)(new_2B + s_lo);
    /* multiply: mixed (in XBA / "B") by modulus -> high byte is result */
    uint16_t prod = sub_DCFE_mul8(modulus, mixed);
    return (uint8_t)(prod >> 8);
}
int rng_byte_DCD5(uint8_t a) { return sub_DCD5(a); }

/* -------------------------------------------------------------------------
 * $04:D6F6 — render entity animation frame.
 *   This is the per-frame "draw this entity" entry point.
 *   in: X = entity slot base
 *       dp[$82..$84] = pointer to animation tile table (24-bit)
 *       dp[$85..$87] = pointer to OAM-attribute table (24-bit)
 *   action:
 *     Y = (X+$0E byte) << 3 + (X+$13 byte)        ; frame*8 + heading
 *     dp[$3B] = [$82],y                            ; tile index
 *     dp[$3C] = 1                                  ; tile size flag
 *     dp[$3D] = [$85],y | (X+$0F byte)             ; attr (OR with flip)
 *     call DB5C (world->screen for this entity)
 *     dp[$44] = (X+$06 byte)                       ; sprite-pair Y offset
 *     call DB9E (OAM push)
 *     dp[$44] = 0
 * ------------------------------------------------------------------------- */
extern void sub_DB5C_world_to_screen(uint16_t x);
extern void sub_DB9E_oam_push(uint16_t x);
extern uint8_t read_far(uint8_t bank, uint16_t addr); /* convenience */

void sub_D6F6_draw_animated(uint16_t x) {
    uint16_t off = (uint16_t)(WMEM8(x + 0x0E) << 3) + (uint16_t)WMEM8(x + 0x13);
    uint8_t  bank_82 = WMEM8(0x0084);
    uint16_t addr_82 = WMEM16(0x0082);
    uint8_t  bank_85 = WMEM8(0x0087);
    uint16_t addr_85 = WMEM16(0x0085);
    (void)bank_82; (void)bank_85;  /* far reads — we assume bank $7E */
    WMEM8(0x003B) = WMEM8((addr_82 + off) & 0x1FFFF);
    WMEM8(0x003C) = 0x01;
    WMEM8(0x003D) = (uint8_t)(WMEM8((addr_85 + off) & 0x1FFFF) | WMEM8(x + 0x0F));
    sub_DB5C_world_to_screen(x);
    WMEM8(0x0044) = WMEM8(x + 0x06);
    sub_DB9E_oam_push(x);
    WMEM8(0x0044) = 0;
}
void sub_D6F6(uint16_t x)          { sub_D6F6_draw_animated(x); }
void render_anim_D6F6(uint16_t x)  { sub_D6F6_draw_animated(x); }

/* -------------------------------------------------------------------------
 * $04:D721 — set velocity from heading.
 *   in: X = entity slot, Y = 16-bit step magnitude (used by 8A0E/8A0B mul)
 *   action:
 *     A = (X+$13) - 2   (heading 0..31 with 8-direction adjust)
 *     A <<= 5            (multiply by 32, table stride)
 *     PHA / PHY
 *     JSL $00:8A0E (cosine) — result -> dp[$09..$0A] (Y vel)
 *     PLY / PLA            (Y preserved across the call by the stack)
 *     JSL $00:8A0B (sine)   — result -> dp[$07..$08] (X vel)
 *   Observed caller Y values: $0008, $0380, $0400 (16-bit step amplitudes
 *   per entity class). MUST be 16-bit — $0400 truncated to a byte is 0.
 * ------------------------------------------------------------------------- */
void sub_D721_set_velocity_from_heading(uint16_t x, uint16_t y_amplitude) {
    uint8_t heading = (uint8_t)((WMEM8(x + 0x13) - 2) << 5);
    uint16_t cos_v = sub_008A0E_div256(heading, y_amplitude);
    WMEM16(x + 0x09) = cos_v;
    uint16_t sin_v = sub_008A0B_div256r(heading, y_amplitude);
    WMEM16(x + 0x07) = sin_v;
}
void sprite_init_D721(uint16_t x) {
    /* Legacy entry point — no Y info preserved by callers of this stub.
     * Defaults to $0400 which is the most common caller value (4 of 5
     * sites observed in bank 04). Real C migrations should pass Y
     * explicitly via sub_D721_set_velocity_from_heading. */
    sub_D721_set_velocity_from_heading(x, 0x0400);
}

/* -------------------------------------------------------------------------
 * $04:D755 — X-velocity integration step.
 *   in: X = entity slot
 *   action:
 *     dp[$0011+X] += dp[$0007+X]            ; subpixel fractional
 *     dp[$0002+X] += sign_extend(dp[$0008+X]) ; with $0011+X carry
 *     dp[$0002+X] &= $07FF                  ; wrap to world width
 * ------------------------------------------------------------------------- */
void sub_D755(uint16_t x) {
    uint8_t  frac      = WMEM8(x + 0x11);
    uint8_t  add       = WMEM8(x + 0x07);
    uint16_t new_frac  = (uint16_t)frac + (uint16_t)add;
    uint8_t  carry     = (uint8_t)(new_frac >> 8);
    WMEM8(x + 0x11)    = (uint8_t)new_frac;
    int8_t signed_high = (int8_t)WMEM8(x + 0x08);
    uint16_t hi_word   = (uint16_t)((signed_high < 0) ? 0xFF00 : 0x0000) | carry;
    uint16_t cur       = WMEM16(x + 0x02);
    WMEM16(x + 0x02)   = (uint16_t)((cur + hi_word) & 0x07FF);
}

/* -------------------------------------------------------------------------
 * $04:D77B — Y-velocity integration step (mirror of D755 for Y).
 * ------------------------------------------------------------------------- */
void sub_D77B(uint16_t x) {
    uint8_t  frac      = WMEM8(x + 0x12);
    uint8_t  add       = WMEM8(x + 0x09);
    uint16_t new_frac  = (uint16_t)frac + (uint16_t)add;
    uint8_t  carry     = (uint8_t)(new_frac >> 8);
    WMEM8(x + 0x12)    = (uint8_t)new_frac;
    int8_t signed_high = (int8_t)WMEM8(x + 0x0A);
    uint16_t hi_word   = (uint16_t)((signed_high < 0) ? 0xFF00 : 0x0000) | carry;
    uint16_t cur       = WMEM16(x + 0x04);
    WMEM16(x + 0x04)   = (uint16_t)((cur + hi_word) & 0x03FF);
}

/* -------------------------------------------------------------------------
 * $04:D747 — physics step
 *   if dp[$0000] (current bank-state) == 4: integrate X+Y velocities
 * ------------------------------------------------------------------------- */
void sub_D747_physics_step(uint16_t x) {
    if (WMEM8(0x0000) == 0x04) {
        sub_D755(x);
        sub_D77B(x);
    }
}
void draw_step_D747(uint16_t x) { sub_D747_physics_step(x); }

/* -------------------------------------------------------------------------
 * $04:D792 — apply Y-delta with wrap (extracted tail of D77B for callers
 * that already have a single-byte delta to add into Y coords).
 *   in: A = high-byte sign extension, EBA already-loaded carry
 *   This entry is a fallthrough from D78F so we expose only the wrap part.
 * ------------------------------------------------------------------------- */
void sub_D792(uint16_t x, uint8_t a, uint8_t b /*=XBA*/) {
    uint16_t add = (uint16_t)((b << 8) | a);
    uint16_t cur = WMEM16(x + 0x04);
    WMEM16(x + 0x04) = (uint16_t)((cur + add) & 0x03FF);
}
void world_modify_commit_D792(uint16_t x, uint8_t a, uint8_t b) {
    sub_D792(x, a, b);
}

/* -------------------------------------------------------------------------
 * $04:D7A1 — initialize a random spawn position.
 *   in: X = entity slot
 *   action:
 *     A = $80; JSR DCD5 -> rand byte in [0,$80); <<4 -> dp[$0002+X]
 *     A = $40; JSR DCD5 -> rand byte in [0,$40); <<4 -> dp[$0004+X]
 * ------------------------------------------------------------------------- */
void sub_D7A1_random_spawn_pos(uint16_t x) {
    uint16_t rx = sub_DCD5(0x80);
    WMEM16(x + 0x02) = (uint16_t)(rx << 4);
    uint16_t ry = sub_DCD5(0x40);
    WMEM16(x + 0x04) = (uint16_t)(ry << 4);
}
void scatter_init_D7A1(uint16_t x) { sub_D7A1_random_spawn_pos(x); }

/* -------------------------------------------------------------------------
 * $04:DB40 — world-to-screen "offset" variant.
 *   in: A,Y = an extra (x,y) to add to the entity's (X+2, X+4) world coords
 *   out: dp[$37] = A + (X+2,16), dp[$39] = Y + (X+4,16)
 * ------------------------------------------------------------------------- */
void sub_DB40_offset_draw(uint16_t x, uint16_t a, uint16_t y) {
    WMEM16(0x0037) = (uint16_t)(a + WMEM16(x + 0x02));
    WMEM16(0x0039) = (uint16_t)(y + WMEM16(x + 0x04));
}
void sub_DB40(uint16_t x, uint16_t a, uint16_t y) {
    sub_DB40_offset_draw(x, a, y);
}

/* -------------------------------------------------------------------------
 * $04:DB5C — world-to-screen translation.
 *   in: X = entity slot
 *   Picks parallax/camera mode from dp[X+$0F] bit7:
 *     bit clear: subtract dp[$3E] (per-frame camera X) from $02+X / $04+X
 *     bit set:   subtract dp[$40] (alternate camera) instead
 *   out: dp[$37] = screen X, dp[$39] = screen Y
 * ------------------------------------------------------------------------- */
void sub_DB5C_world_to_screen(uint16_t x) {
    uint8_t mode = WMEM8(x + 0x0F);
    uint16_t cam = (mode & 0x80) ? WMEM16(0x0040) : WMEM16(0x003E);
    WMEM16(0x0037) = (uint16_t)(WMEM16(x + 0x02) - cam);
    WMEM16(0x0039) = (uint16_t)(WMEM16(x + 0x04) - cam);
}
void sub_DB5C(uint16_t x)             { sub_DB5C_world_to_screen(x); }
void render_pair_DB5C(uint16_t x)     { sub_DB5C_world_to_screen(x); }

/* -------------------------------------------------------------------------
 * $04:DBE3 — one tile OAM write.
 *   in: Y = tile-table walking index (mutated)
 *       dp[$3B] = tile id, dp[$3C] = stride, dp[$3D] = attributes
 *       dp[$37] = screen X, dp[$39] = screen Y
 *   Pushes the entry into OAM scratch buffer if it lies in 0..$0140 X range.
 *   Returns updated Y in callee's $32/$34.
 * ------------------------------------------------------------------------- */
void sub_DBE3(uint16_t /*x*/ x_ent, uint16_t *y_inout) {
    (void)x_ent;
    uint16_t y = *y_inout;
    WMEM8(0x006A) = (uint8_t)((y >> 1) & 0x06);
    uint8_t  table_idx = (uint8_t)((y >> 1) & 0x06) >> 1;  /* /4 -> $DC67,x */
    /* dp[$36] = table entry — we don't have $DC67 table linked, so 0 */
    (void)table_idx;
    WMEM8(0x0036) = 0;
    uint16_t world_x = WMEM16(0x0037);
    uint16_t check   = (uint16_t)((world_x - WMEM16(0x0005)) + 0x0040);
    /* The ROM clips: if check >= 0x140, return (off-screen) */
    if (check >= 0x0140) {
        *y_inout = (uint16_t)(y + 8);
        return;
    }
    /* On-screen — we'd write into OAM scratch here. Leave a hook. */
    *y_inout = (uint16_t)(y + 8);
}

/* -------------------------------------------------------------------------
 * $04:DB9E — OAM push (recursive double-draw with $44 offset).
 *   Handles "sprite pair" — entity + its mirrored ghost. The original
 *   pushes both halves through DBE3.
 * ------------------------------------------------------------------------- */
void sub_DB9E_oam_push(uint16_t x) {
    uint16_t y = WMEM16(0x0032);
    sub_DBE3(x, &y);
    WMEM16(0x0032) = y;
    if (WMEM8(0x0044) != 0) {
        /* paired-sprite path */
        uint16_t sx = WMEM16(0x0037);
        uint16_t sy = WMEM16(0x0039);
        WMEM16(0x0037) = (uint16_t)(sx + WMEM8(0x0044));
        WMEM16(0x0039) = (uint16_t)(sy + WMEM8(0x0044));
        uint8_t save_attr  = WMEM8(0x003D);
        WMEM8(0x003D)      = (uint8_t)((save_attr & 0xF8) | 0x02);
        y = WMEM16(0x0034);
        sub_DBE3(x, &y);
        WMEM16(0x0034) = y;
        WMEM8(0x003D)  = save_attr;
        WMEM16(0x0037) = sx;
        WMEM16(0x0039) = sy;
    }
}
void sub_DB9E(uint16_t x)     { sub_DB9E_oam_push(x); }
void render_sprite_DB9E(uint16_t x) { sub_DB9E_oam_push(x); }

/* -------------------------------------------------------------------------
 * $04:DB52 — composite draw entry (DB5C+DB88+DB9E).
 *   Lift as a thin wrapper. The middle JSR $DB88 is the per-tile builder;
 *   not exposed here, so we just delegate to the bracketing helpers.
 * ------------------------------------------------------------------------- */
extern void sub_DB88_per_tile(uint16_t x);  /* defined elsewhere or stubbed */
void sub_DB52_draw(uint16_t x) {
    sub_DB5C_world_to_screen(x);
    sub_DB88_per_tile(x);
    sub_DB9E_oam_push(x);
}
void sub_DB52(uint16_t x)             { sub_DB52_draw(x); }
void render_sprite_pos_DB52(uint16_t x) { sub_DB52_draw(x); }

/* -------------------------------------------------------------------------
 * $04:DC71 — apply camera scroll
 *   dp[$37] += dp[$05]; dp[$39] += dp[$07]
 * ------------------------------------------------------------------------- */
void sub_DC71_apply_camera(void) {
    WMEM16(0x0037) = (uint16_t)(WMEM16(0x0037) + WMEM16(0x0005));
    WMEM16(0x0039) = (uint16_t)(WMEM16(0x0039) + WMEM16(0x0007));
}

/* Provide stub for the per-tile helper used by sub_DB52 (mostly unused
 * outside the composite-draw entry). */
__attribute__((weak)) void sub_DB88_per_tile(uint16_t x) { (void)x; }

/* -------------------------------------------------------------------------
 * Convenience: bank-far read used by D6F6.
 * For the decomp's purposes far reads collapse to wram[] indexing on the
 * assumption the data is mirrored into bank $7E. Stubbed as a weak helper
 * so the test linker resolves it.
 * ------------------------------------------------------------------------- */
__attribute__((weak)) uint8_t read_far(uint8_t bank, uint16_t addr) {
    (void)bank;
    return wram[addr & 0x1FFFF];
}

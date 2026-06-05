/*
 * entities_a.c — SimAnt SNES entity handlers, types 1 through 7.
 *
 * Lifted from bank $04 of the ROM. These are the "UI / cursor / HUD blink"
 * entities — they own no AI of their own; they just read game-state words
 * out of direct page and translate them into shadow-OAM writes (via the
 * sprite-emit helper at $04:DB9E).
 *
 * Conventions match simant.c:
 *   - dp[] aliases WRAM $0000..
 *   - sprites land in the shadow OAM at WRAM $0D00 (96 sprites × 4 bytes)
 *     plus the high-byte attribute table at $0F00 (32 bytes; 2 bits per
 *     sprite encoding size + X-bit-9).
 *   - $32/$33 = next-free hi-priority OAM index ($0010 at frame start)
 *     $34/$35 = next-free lo-priority OAM index ($0110 at frame start)
 *   - $05/$06, $07/$08 = camera world origin (subtracted to get screen pos)
 *
 * I keep the original ROM addresses in symbol names so anything cross-grep'd
 * against disasm.txt or dig.txt resolves.
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Shared definitions provided by simant.c (don't redefine — extern).
 * ------------------------------------------------------------------------ */
extern uint8_t wram[0x20000];
#define dp wram                              /* same alias simant.c uses     */

/* Entity layout: 20-byte record, see simant.c for the struct. */
typedef struct __attribute__((packed)) Entity {
    uint8_t  type;
    uint8_t  state;
    uint16_t x;
    uint16_t y;
    uint8_t  flag;
    uint8_t  scratch[5];
    uint16_t init_word;
    uint8_t  pad_e;
    uint8_t  init_attr;
    uint8_t  pad_10;
    uint8_t  tail[3];
} Entity;

/* Convenience: typed read/write at a fixed WRAM offset (16-bit little-endian).
 * Most of dp's "word" cells (e.g. $0002,x for entity x) need this. */
static inline uint16_t W16(unsigned addr)        { return *(uint16_t *)&wram[addr]; }
static inline void     SW16(unsigned addr, uint16_t v) { *(uint16_t *)&wram[addr] = v; }

/* ========================================================================
 * SPRITE-EMIT HELPERS — the part everyone calls
 * ========================================================================
 *
 * The renderer that every type-1..7 handler ultimately funnels into is the
 * routine at $04:DB9E. It writes ONE sprite (4 bytes in shadow OAM at
 * $00:0D00 + Y plus 2 bits in the high-byte table at $00:0F00 + index/4),
 * AND if dp[$44] is non-zero it also writes a SHADOW sprite below/right of
 * the main one at offset dp[$44] in both X and Y.
 *
 * Inputs from direct page:
 *   $37/$38   world X (post-LSR; really screen X plus camera origin)
 *   $39/$3A   world Y
 *   $3B/$3C   tile index (low) + bank/extra (high)
 *   $3D       OAM attribute byte (palette, priority, vflip/hflip)
 *   $32/$33   next-free OAM index (HI priority; incremented by 4)
 *   $34/$35   next-free OAM index (LO priority; for the shadow pass)
 *   $05/$06   camera origin X        (subtracted to get on-screen X)
 *   $07/$08   camera origin Y
 *   $44       shadow Y offset (0 = no shadow pass)
 *
 * Output: appends to shadow OAM at $00:0D00 + Y, advances Y.
 *
 * I lift the inner writer ($04:DBE3) separately because the public entry
 * ($DB9E) is only the "main + optional shadow" wrapper.
 * ------------------------------------------------------------------------ */

/* Shadow OAM is bank $00 ($00:0D00 → $00:0F1F). Banks $00 mirror $7E so we
 * just go through the same WRAM array. */
#define SHADOW_OAM_LO  (&wram[0x0D00])    /* 96 sprites * 4 bytes = $180     */
#define SHADOW_OAM_HI  (&wram[0x0F00])    /* 32 bytes of 2-bit hi extras     */

/* Per-priority bucket size lookup at $04:DC67 — 4 bytes selecting the AND
 * mask used by the high-bits packer. ROM bytes: FC F3 CF 3F. Those are the
 * masks that PRESERVE bits 0..(2k-1) for slot k mod 4 when packing the
 * 2-bit "x-bit-9 + size" pair into the byte at $0F00+index/4. */
static const uint8_t oam_hi_preserve_mask_DC67[4] = { 0xFC, 0xF3, 0xCF, 0x3F };

/* Inner sprite writer at $04:DBE3. Y on entry/exit = OAM byte index. Pulls
 * dp[$37/$39/$3B/$3D] for position/tile/attribute. Subtracts the camera
 * origin (dp[$05] / dp[$07]) so the world-space coords become screen-space.
 * Returns early without advancing Y if the resulting position is off-screen
 * (X must be in [-$40..$100], Y in [-$20..$E0]). */
static uint16_t sprite_emit_inner_DBE3(uint16_t y_index)
{
    /* DBE3 setup: A=0/XBA; A=Y; LSR; AND #$06 → low 2 bits of (Y>>1)
     * pick the bucket within byte $0F00+x. STA $6A. */
    uint8_t shift_count = (uint8_t)((y_index >> 1) & 0x06);
    /* Y>>2 picks which of the 4 sprite-records within a $0F00 byte. */
    unsigned hi_byte_index = (y_index >> 4) & 0x1F;     /* y_index/16 = sprite/4 */
    unsigned bucket_in_byte = (y_index >> 2) & 0x03;
    uint8_t  preserve_mask = oam_hi_preserve_mask_DC67[bucket_in_byte];

    /* X = entity X (signed: world X - camera + $40 for left padding). */
    int32_t sx = (int32_t)W16(0x37) - (int32_t)W16(0x05) + 0x40;
    if ((uint32_t)sx >= 0x140) return y_index;          /* off-screen X      */
    sx -= 0x40;                                          /* back to $0..$FF   */
    SHADOW_OAM_LO[y_index++] = (uint8_t)sx;
    /* Bit 8 of the corrected (pre-subtract) X→ goes into $69 lo bit. */
    uint8_t  x_bit8 = (uint8_t)((sx >> 8) & 0x01);

    /* Y = entity Y - camera + $20 padding. */
    int32_t sy = (int32_t)W16(0x39) - (int32_t)W16(0x07) + 0x20;
    if ((uint32_t)sy >= 0x100) { return y_index - 1; }  /* off-screen Y      */
    sy -= 0x20;
    SHADOW_OAM_LO[y_index++] = (uint8_t)sy;

    /* Tile + attribute. */
    SHADOW_OAM_LO[y_index++] = dp[0x3B];                /* tile low          */
    /* OAM byte 3 packs the tile's bit-8 into the low bit of the attribute:
     * the ROM does LSR $3C (carry = $3C bit 0) then ROL $3D (rotates carry
     * into A's low bit, shifts $3D bit 7 out as new carry).  We capture
     * that bit 7 below as size_bit for the OAM-hi packer. */
    uint8_t tile_bit8  = (uint8_t)(dp[0x3C] & 0x01);
    uint8_t attr_byte  = (uint8_t)((dp[0x3D] << 1) | tile_bit8);
    SHADOW_OAM_LO[y_index++] = attr_byte;

    /* Now patch the high-byte table at $0F00 + (y_index_old / 16). The
     * 2-bit field for this sprite is: hi-bit-of-X (we computed as x_bit8)
     * combined with the carry-out of dp[$3D]'s top bit ROL (the "size bit").
     * The original sequence is:
     *   LDA #$00 / ROL          ; A = carry from ROL above (= dp[$3D] bit 7)
     *   LSR $69 / ROL / STA $69
     *   if $6A != 0: ASL $69 by $6A bits
     *   LDA $0F00,x / AND $36 / ORA $69 / STA $0F00,x
     */
    uint8_t size_bit = (uint8_t)((dp[0x3D] >> 7) & 0x01);   /* large sprite? */
    uint8_t packed   = (uint8_t)((size_bit << 1) | x_bit8); /* 2-bit field   */
    packed <<= shift_count;
    SHADOW_OAM_HI[hi_byte_index] =
        (uint8_t)((SHADOW_OAM_HI[hi_byte_index] & preserve_mask) | packed);

    return y_index;
}

/* sprite_emit_with_shadow_DB9E — public entry. Writes ONE main sprite from
 * $37/$39, and if dp[$44] != 0 writes a SHADOW companion at +$44 in both
 * X and Y to the lo-priority bucket ($34) with attribute palette forced to
 * "shadow" (bits cleared, bit 1 set: AND #$F8 / ORA #$02). */
static void sprite_emit_with_shadow_DB9E(void)
{
    uint16_t y_main = W16(0x32);
    y_main = sprite_emit_inner_DBE3(y_main);
    SW16(0x32, y_main);

    if (dp[0x44] == 0) return;                          /* no shadow pass    */

    /* Translate world position to "shadow" position by adding dp[$44]. */
    SW16(0x37, (uint16_t)(W16(0x37) + dp[0x44]));
    SW16(0x39, (uint16_t)(W16(0x39) + dp[0x44]));
    uint8_t saved_attr = dp[0x3D];
    dp[0x3D] = (uint8_t)((saved_attr & 0xF8) | 0x02);   /* shadow palette    */

    uint16_t y_shadow = W16(0x34);
    y_shadow = sprite_emit_inner_DBE3(y_shadow);
    SW16(0x34, y_shadow);

    dp[0x3D] = saved_attr;
    SW16(0x37, (uint16_t)(W16(0x37) - dp[0x44]));
    SW16(0x39, (uint16_t)(W16(0x39) - dp[0x44]));
}

/* camera_translate_DC71 — adds the camera origin (dp[$05/$07]) to the
 * working coords (dp[$37/$39]) so callers can use raw screen coords as
 * input. After this, $37/$39 hold world coords that sprite_emit_inner will
 * subtract the camera origin back from — net effect: the sprite lands at
 * the caller's literal X/Y on-screen, regardless of where the camera is. */
static void camera_translate_DC71(void)
{
    SW16(0x37, (uint16_t)(W16(0x37) + W16(0x05)));
    SW16(0x39, (uint16_t)(W16(0x39) + W16(0x07)));
}

/* ========================================================================
 * draw_entity_sprite_DB52 — render an entity as a single sprite from its
 * own record.
 *
 * Reads from entity[X]:
 *   $0002/$0003 = world X    (signed)
 *   $0004/$0005 = world Y
 *   $000C/$000D = init word  (added to byte $000E to form tile index)
 *   $000E       = state-derived tile add
 *   $000F       = attribute byte (bit 7 = "use shadow camera at $40 instead
 *                 of $3E" — picks between two camera shadows)
 *
 * Inner pieces:
 *   $DB5C : choose camera shadow dp[$3E] vs dp[$40] based on attr bit 7,
 *           subtract from entity X/Y, store in $37/$39.
 *   $DB88 : compute tile index = entity[$0E] + entity[$0C..D], attr =
 *           entity[$0F]; store at $3B/$3C and $3D.
 *   $DB9E : emit (see above).
 * ------------------------------------------------------------------------ */
static void entity_sprite_camera_DB5C(const Entity *e)
{
    uint16_t cam = (e->init_attr & 0x80) ? W16(0x40) : W16(0x3E);
    SW16(0x37, (uint16_t)(e->x - cam));
    SW16(0x39, (uint16_t)(e->y - cam));
}

static void entity_sprite_tile_DB88(const Entity *e)
{
    /* tile (16-bit) = e->pad_e + e->init_word ; attr = e->init_attr. */
    uint16_t tile16 = (uint16_t)((uint16_t)e->pad_e + e->init_word);
    SW16(0x3B, tile16);
    dp[0x3D] = e->init_attr;
}

static void draw_entity_sprite_DB52(const Entity *e)
{
    entity_sprite_camera_DB5C(e);
    entity_sprite_tile_DB88(e);
    sprite_emit_with_shadow_DB9E();
}

/* ========================================================================
 * SELECTION-RECTANGLE DRAW — $04:9B55
 *
 * Used by types 3/4/5 (the three "marker" entities — one per corner of the
 * group-select rectangle in the recruit-ants UI on the surface view).
 *
 * Entry: $37/$39 already hold the top-left screen coord of the marker.
 * Draws four sprites in a 2x2 group:
 *
 *   tile $C8   at (x,        y)
 *   tile $CE   at (x + $10,  y)
 *   tile $EE   at (x + $10,  y + 8)
 *   tile $E8   at (x,        y + 8)
 *
 * Attribute $3D = $10 for all four (palette 1, no flip, low priority).
 *
 * These tile indices ($C8, $CE, $EE, $E8) form the four corner glyphs of
 * the rounded box that's drawn around a selected group of ants.
 * ------------------------------------------------------------------------ */
static void selection_box_2x2_9B55(void)
{
    /* Corner 1: top-left ($C8). */
    SW16(0x3B, 0x00C8);
    dp[0x3D] = 0x10;
    sprite_emit_with_shadow_DB9E();

    /* Corner 2: top-right ($CE) at (x + $10). */
    SW16(0x37, (uint16_t)(W16(0x37) + 0x10));
    dp[0x3B] = 0xCE;
    sprite_emit_with_shadow_DB9E();

    /* Corner 3: bottom-right ($EE) at (x + $10, y + 8). */
    SW16(0x39, (uint16_t)(W16(0x39) + 0x08));
    dp[0x3B] = 0xEE;
    sprite_emit_with_shadow_DB9E();

    /* Corner 4: bottom-left ($E8) at (x, y + 8). */
    SW16(0x37, (uint16_t)(W16(0x37) - 0x10));
    dp[0x3B] = 0xE8;
    sprite_emit_with_shadow_DB9E();
}

/* ========================================================================
 * "BLINK TIMING" HELPER — $04:9D1A
 *
 * Watches the global frame counter dp[$02B2]. When (counter + 1 == dp[$024A]),
 * draws a single $26-glyph sprite at:
 *   screen_x = dp[$0246] / 2 + $C8
 *   screen_y = dp[$0248] / 2 + $10
 * with attribute $18 (palette 3, low priority).
 *
 * Used as a one-shot "flash" — type 7/8 chain it before their per-frame
 * draw, so the player sees a brief blink at the (dp[$0246]/dp[$0248])
 * location on the frame matching the timing constant.
 * ------------------------------------------------------------------------ */
static void blink_at_xy246_when_24A_9D1A(void)
{
    if ((uint8_t)(dp[0x02B2] + 1) != dp[0x024A]) return;

    SW16(0x37, (uint16_t)((dp[0x0246] >> 1) + 0xC8));
    SW16(0x39, (uint16_t)((dp[0x0248] >> 1) + 0x10));
    camera_translate_DC71();
    dp[0x3D] = 0x18;
    SW16(0x3B, 0x0026);
    sprite_emit_with_shadow_DB9E();
}

/* ========================================================================
 * FIXED-POSITION 4-CORNER MARKER — $04:9D49
 *
 * Same 2x2 box layout as $9B55, anchored to fixed SCREEN coords ($C8,$10)
 * (then camera-translated like the other HUD draws). Same four tile
 * indices ($C8 / $CE / $EE / $E8), traversed in the same order. The two
 * meaningful differences vs $9B55:
 *   - attribute byte = $18 (palette+priority pair) instead of $10
 *   - Y step between the top and bottom rows is $10 (16 px) instead of
 *     $8, so this box is 2x taller — matches the on-screen HUD glyph it
 *     decorates.
 *
 * Used as the bottom-edge marker by types 7 and 8.
 * ------------------------------------------------------------------------ */
static void fixed_marker_9D49(void)
{
    SW16(0x37, 0x00C8);
    SW16(0x39, 0x0010);
    camera_translate_DC71();
    SW16(0x3B, 0x00C8);
    dp[0x3D] = 0x18;
    sprite_emit_with_shadow_DB9E();

    SW16(0x37, (uint16_t)(W16(0x37) + 0x10));
    dp[0x3B] = 0xCE;
    sprite_emit_with_shadow_DB9E();

    SW16(0x39, (uint16_t)(W16(0x39) + 0x10));
    dp[0x3B] = 0xEE;
    sprite_emit_with_shadow_DB9E();

    SW16(0x37, (uint16_t)(W16(0x37) - 0x10));
    dp[0x3B] = 0xE8;
    sprite_emit_with_shadow_DB9E();
}

/* ========================================================================
 * TYPE 1 — CURSOR / INPUT PICKUP ($04:9D9D)
 *
 * Polled every NMI; never moves itself. Its job is to read the "freshly
 * pressed" joypad bits stashed at dp[$60/$61] (edge-detected elsewhere)
 * and, on A-press or B/Y-press, jump into the menu-confirmation action at
 * $04:9DB9. There's a fallback path at $04:9DB1 for the SNES Mouse case
 * (dp[$007D] mouse-click latch).
 *
 * Both action paths converge on cursor_confirm_action_9DB9, which:
 *   1) clears the cursor entity's type (entity[$00,x] = 0 — destroys self)
 *   2) resets the cooperative-scheduler SP to $04FF
 *   3) sets dp[$0B] = $16 (game-state transition to the menu's "accepted"
 *      handler)
 *   4) force-blanks the display (INIDISP = $80)
 *   5) JMLs via stack to $00:935C (re-enters the game-state dispatcher)
 *
 * dp[$0071] guards the whole thing — when a modal is open, cursor input is
 * locked out.
 * ======================================================================== */

/* Forward declaration — implementation is the SP-swap; not faithful in C.
 * The original at $04:9DB9 destroys the cursor entity and tail-calls the
 * game-state dispatcher via a manufactured RTL frame. */
static void cursor_confirm_action_9DB9(Entity *self)
{
    self->type = 0;                          /* mark entity slot free       */
    /* The next 6 instructions can't be lifted faithfully — they yank the
     * stack to $04FF, push a JMP-target onto it, and RTL to $00:935C, so
     * the next thing that runs is task 1's game-state dispatcher with
     * dp[$0B] = $16. */
    dp[0x0B] = 0x16;
    /* INIDISP = $80;  -- force blank (handled by the dispatcher target) */
}

static void cursor_mouse_check_9DB1(Entity *self)
{
    /* dp[$007D] = SNES Mouse latched-button shadow; bits 0/1 = L/R click. */
    if (dp[0x007D] & 0x03) cursor_confirm_action_9DB9(self);
}

void cursor_handler_type1_9D9D(Entity *self)
{
    if (dp[0x0071]) {                        /* menu lock                   */
        cursor_mouse_check_9DB1(self);
        return;
    }
    if (dp[0x60] & 0x80) {                   /* A button just pressed       */
        cursor_confirm_action_9DB9(self);
        return;
    }
    if (dp[0x61] & 0x90) {                   /* B or Y just pressed         */
        cursor_confirm_action_9DB9(self);
        return;
    }
    /* nothing pressed this frame — leave the cursor entity in place */
}

/* ========================================================================
 * TYPE 2 — JOYPAD/MOUSE CURSOR SPRITE ($04:9B9B)
 *
 * THE polled cursor. Reads two sign-magnitude delta bytes — dp[$0079]
 * (drives the X axis at dp[$14], clipped to dp[$16..$17]) and dp[$0077]
 * (drives Y at dp[$15], clipped to dp[$18..$19]) — and draws itself as a
 * sprite via $DB52 using its own entity record. The {$77,$79} bytes are
 * SNES-Mouse delta shadows (or per-frame DPAD-decoded deltas; the source
 * routine isn't in our scope).
 *
 * Gates:
 *   1) dp[$004E] >= 0 (positive) → RTS (cursor disabled — likely the
 *      "input freeze" countdown the manual mentions for menu opens).
 *   2) If dp[$0071]==0 AND dp[$02B3] ("view changed" flag) is set AND
 *      JOY1L has L/R shoulder bits set (mask $30 — L/R buttons), skip the
 *      cursor-movement update and just redraw. The shoulders apparently
 *      double as "lock cursor" while in non-default views.
 *
 * Movement step (per axis):
 *   delta_byte = dp[$0079];  (sign-magnitude — bit 7 = direction)
 *   if delta_byte >= 0:                     // positive = +Y / +X
 *       new_pos = old_pos + delta_byte;
 *       if new_pos overflows OR new_pos >= max_bound:
 *           new_pos = max_bound - 1;        // clip high
 *   else:                                   // negative = -Y / -X
 *       new_pos = old_pos - (delta_byte & $7F);
 *       if new_pos underflows OR new_pos < min_bound:
 *           new_pos = min_bound + 1;        // clip low
 *
 * After clipping, the post-update render copies the cursor world-coords
 * into the entity record (x = dp[$14] + camera_x + 8, y = dp[$15] + camera_y
 * + 8), stamps dp[$0049] / dp[$004B] as init_word/init_attr (so palette
 * changes in those globals retint the cursor), and calls $DB52 to push a
 * sprite into the OAM stream.
 *
 * Bound table (per axis, in dp):
 *   $14 = current X     $15 = current Y
 *   $16 = X lower       $17 = X upper
 *   $18 = Y lower       $19 = Y upper
 * ------------------------------------------------------------------------ */
static void cursor_step_axis(uint8_t delta_byte,
                             uint8_t *pos,
                             uint8_t lower,
                             uint8_t upper,
                             Entity *e)
{
    if ((delta_byte & 0x80) == 0) {
        /* positive delta */
        uint16_t s = (uint16_t)*pos + delta_byte;
        if (s > 0xFF || (uint8_t)s >= upper)
            *pos = upper - 1;
        else
            *pos = (uint8_t)s;
    } else {
        /* negative delta — sign-magnitude. ROM also stashes the magnitude
         * in entity scratch[0] (offset $11) — surfacing for fidelity even
         * though nothing in our scope reads it. */
        uint8_t mag = (uint8_t)(delta_byte & 0x7F);
        e->tail[0] = mag;                                /* $0011,x */
        int16_t s = (int16_t)*pos - (int16_t)mag;
        if (s < 0 || (uint8_t)s < lower)
            *pos = lower + 1;
        else
            *pos = (uint8_t)s;
    }
}

void cursor_handler_type2_9B9B(Entity *self)
{
    /* Disable gate — bit 7 (sign) of dp[$004E] must be set ("active"). */
    if ((dp[0x004E] & 0x80) == 0) return;

    /* Lock-while-in-non-default-view path. JOY1L is the raw $4218 read;
     * mask $30 = L+R shoulders. When the user is holding L/R during a
     * view change, skip movement update — drop straight to render. */
    int skip_move = (dp[0x0071] == 0)
                 && (dp[0x02B3] != 0)
                 && ((wram[0x004218] & 0x30) != 0);

    if (!skip_move) {
        /* The ROM reads dp[$79] FIRST and feeds it into dp[$14] (the X
         * axis of the cursor world-pos), clipping against dp[$16]/dp[$17].
         * Then reads dp[$77] into dp[$15] (Y axis), clipping vs dp[$18]/$19.
         * The (delta-source, dest, bounds) wiring above doesn't follow
         * the natural ($77,$14) / ($79,$15) pairing — the joypad-shadow
         * source layout happens to be Y first in the dp window. */
        cursor_step_axis(dp[0x0079], &dp[0x14], dp[0x16], dp[0x17], self);
        cursor_step_axis(dp[0x0077], &dp[0x15], dp[0x18], dp[0x19], self);
    }

    /* Render: write cursor world-coords back into entity x/y, copy palette
     * registers, draw via the entity-sprite routine. The +8 below is the
     * half-cell offset that centers the cursor glyph on the gridded
     * position. */
    self->x = (uint16_t)(dp[0x14] + W16(0x05) + 8);
    self->y = (uint16_t)(dp[0x15] + W16(0x07) + 8);
    self->init_word = W16(0x0049);                  /* live tile/palette    */
    self->init_attr = dp[0x004B];                   /* OAM attribute byte   */
    draw_entity_sprite_DB52(self);
}

/* ========================================================================
 * TYPE 3 — SELECTION-BOX MARKER A ($04:9B1B)
 *
 * Draws the 2x2 corner box (see selection_box_2x2_9B55) at screen position
 *   x = dp[$0C] * 2,           y = dp[$0D] * 2 + $20.
 *
 * dp[$0C]/dp[$0D] are one of the three "marker positions" in the dp[$0C..$11]
 * window. Per the comment in simant.c, type 3/4/5 draw boxes at $0C/$0D,
 * $0E/$0F, $10/$11 respectively — together they form the three corners of
 * the recruit-ants drag-select rectangle on the surface view.
 *
 * The 2*X scaling means the dp cells store positions in cell units of 2px.
 * The +$20 Y shift is the HUD's top margin (the topmost row of game cells
 * starts at scanline $20).
 *
 * Note: this is the only one of the three that pre-clears dp[$38] (the
 * high byte of the X shadow) — types 4 and 5 don't, presumably because
 * the X has already been clipped to a byte by then. Not relevant to C.
 * ======================================================================== */
void marker_handler_type3_9B1B(Entity *self)
{
    (void)self;
    SW16(0x37, (uint16_t)(dp[0x0C] << 1));               /* x = dp[$0C]*2  */
    SW16(0x39, (uint16_t)((dp[0x0D] << 1) + 0x20));      /* y = dp[$0D]*2 + $20 */
    selection_box_2x2_9B55();
}

/* ========================================================================
 * TYPE 4 — SELECTION-BOX MARKER B ($04:9B30)
 *
 * Same draw routine as type 3, but reads dp[$0E/$0F] for its anchor.
 * Coordinates: x = dp[$0E] * 2, y = dp[$0F] * 2 + $20.
 *
 * Mid-rectangle corner glyph for the group-select drag-rectangle UI.
 * ======================================================================== */
void marker_handler_type4_9B30(Entity *self)
{
    (void)self;
    dp[0x37] = (uint8_t)(dp[0x0E] << 1);                 /* x lo only       */
    /* dp[$38] left alone (caller's previous high byte) — see ROM */
    dp[0x39] = (uint8_t)((dp[0x0F] << 1) + 0x20);
    /* dp[$3A] also left alone */
    selection_box_2x2_9B55();
}

/* ========================================================================
 * TYPE 5 — SELECTION-BOX MARKER C ($04:9B41)
 *
 * Variant of type 3/4 with an extra +$40 added to dp[$10] BEFORE the *2
 * shift — so its screen-X is (dp[$10] + $40) * 2 = dp[$10]*2 + $80, and
 * its screen-Y is dp[$11] * 2 + $20.
 *
 * That +$80 X offset is the width of the on-screen ant-info pane: type 5's
 * marker box anchors to the RIGHT half of the screen while types 3/4
 * cover the LEFT half. Together the three form the corners of a
 * cross-pane selection rectangle that can span both information panes.
 * ======================================================================== */
void marker_handler_type5_9B41(Entity *self)
{
    (void)self;
    dp[0x37] = (uint8_t)((dp[0x10] + 0x40) << 1);
    /* dp[$38] left alone (matches ROM — type 5 doesn't zero the X hi) */
    dp[0x39] = (uint8_t)((dp[0x11] << 1) + 0x20);
    selection_box_2x2_9B55();
}

/* ========================================================================
 * TYPE 6 — TIMED HUD INDICATOR ($04:9C46)
 *
 * Per-frame UI element on the bottom-right of the surface view. Three
 * sprites total:
 *
 *   (a) Optional BLINK at (dp[$0246]/2 + $B0, dp[$0248]/2 + $10) with
 *       tile $26, attr $18 — drawn only on the single frame where
 *       (dp[$02B2] + 1) == dp[$024A]. This is the "timer expired" flash.
 *
 *   (b) The actual position indicator at (dp[$0C]/2 + $B0, dp[$0D]/2 + $10)
 *       using tile $46 (LDY #$0046 / STY $3B), attribute $18.
 *
 *   (c) A larger 2-tile glyph at the fixed corner ($B0, $10) and ($D0, $10):
 *       tile $C8 (left half) then tile $CC (right half, after +$20 X step)
 *       with attribute $98 — palette 1, hi priority, hflip clear.
 *
 * The /2 on the position bytes means dp[$0C]/dp[$0D] are stored in
 * full-pixel units while the on-screen mapping is half-resolution.
 * The +$B0/$D0 X and +$10 Y are the bottom-right HUD anchor.
 *
 * dp[$3C] = 0 (STZ at entry); reset between the multi-sprite passes so
 * the bit-9 high-tile-bits in the OAM-hi byte stay clean.
 * ======================================================================== */
void timed_indicator_type6_9C46(Entity *self)
{
    (void)self;
    dp[0x3C] = 0;

    /* (a) one-frame blink. */
    if ((uint8_t)(dp[0x02B2] + 1) == dp[0x024A]) {
        SW16(0x37, (uint16_t)((dp[0x0246] >> 1) + 0xB0));
        SW16(0x39, (uint16_t)((dp[0x0248] >> 1) + 0x10));
        camera_translate_DC71();
        dp[0x3D] = 0x18;
        SW16(0x3B, 0x0026);
        sprite_emit_with_shadow_DB9E();
    }

    /* (b) position indicator. */
    SW16(0x37, (uint16_t)((dp[0x0C] >> 1) + 0xB0));
    SW16(0x39, (uint16_t)((dp[0x0D] >> 1) + 0x10));
    camera_translate_DC71();
    dp[0x3D] = 0x18;
    SW16(0x3B, 0x0046);
    sprite_emit_with_shadow_DB9E();

    /* (c) fixed 2-tile corner glyph: left half then right half. */
    SW16(0x37, 0x00B0);
    SW16(0x39, 0x0010);
    camera_translate_DC71();
    SW16(0x3B, 0x00C8);
    dp[0x3D] = 0x98;
    sprite_emit_with_shadow_DB9E();

    SW16(0x37, (uint16_t)(W16(0x37) + 0x20));
    dp[0x3B] = 0xCC;
    sprite_emit_with_shadow_DB9E();
}

/* ========================================================================
 * TYPE 7 — POSITION-DRIVEN STATIC PROP ($04:9CC6)
 *
 * Two-sprite UI prop anchored to the bottom-left of the screen:
 *
 *   1) Calls blink_at_xy246_when_24A_9D1A first — same one-frame flash
 *      hook as type 6, but anchored at $C8 X instead of $B0 X.
 *   2) Draws tile $46 (LDY #$0046) at
 *        x = dp[$0E]/2 + $C8,  y = dp[$0F]/2 + $10
 *      with attribute $18.
 *   3) Calls fixed_marker_9D49 to drop the four 2x2-box corner glyphs
 *      around the fixed ($C8, $10) HUD anchor.
 *
 * Type 7 differs from type 6 mainly in the X-anchor offset ($C8 vs $B0)
 * and in using the dp[$0E/$0F] marker pair instead of dp[$0C/$0D].
 * ======================================================================== */
void prop_handler_type7_9CC6(Entity *self)
{
    (void)self;
    /* Step 1: one-frame timer blink. */
    blink_at_xy246_when_24A_9D1A();

    /* Step 2: position-driven main glyph. */
    SW16(0x37, (uint16_t)((dp[0x0E] >> 1) + 0xC8));
    SW16(0x39, (uint16_t)((dp[0x0F] >> 1) + 0x10));
    camera_translate_DC71();
    dp[0x3D] = 0x18;
    SW16(0x3B, 0x0046);
    sprite_emit_with_shadow_DB9E();

    /* Step 3: fixed 2x2 corner box around ($C8, $10). */
    fixed_marker_9D49();
}

/* ========================================================================
 * Compile-check anchor — keeps the toolchain from dropping these.
 * ======================================================================== */
__attribute__((used))
static void * const _entities_a_refs[] = {
    (void *)cursor_handler_type1_9D9D,
    (void *)cursor_handler_type2_9B9B,
    (void *)marker_handler_type3_9B1B,
    (void *)marker_handler_type4_9B30,
    (void *)marker_handler_type5_9B41,
    (void *)timed_indicator_type6_9C46,
    (void *)prop_handler_type7_9CC6,
    (void *)sprite_emit_with_shadow_DB9E,
    (void *)sprite_emit_inner_DBE3,
    (void *)camera_translate_DC71,
    (void *)draw_entity_sprite_DB52,
    (void *)entity_sprite_camera_DB5C,
    (void *)entity_sprite_tile_DB88,
    (void *)selection_box_2x2_9B55,
    (void *)blink_at_xy246_when_24A_9D1A,
    (void *)fixed_marker_9D49,
    (void *)cursor_confirm_action_9DB9,
    (void *)cursor_mouse_check_9DB1,
};

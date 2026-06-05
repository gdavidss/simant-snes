/*
 * entities_d.c — Entity handlers 24..31 from $04:A951 .. $04:B5F7.
 *
 * Lifted from the 65816 disassembly of the SimAnt (SNES) ROM. Faithful
 * structural reconstruction; not runnable. Helpers from earlier handlers
 * (DB9E sprite writer, DC71 camera offset, DD24 sprite list expander,
 * DCFE 16-bit multiply, etc.) are declared extern so this file compiles
 * standalone.
 *
 * Entity-record byte fields (see simant.c::Entity for the canonical
 * layout); these handlers use:
 *   +0     type
 *   +1     state          - dispatched by the standard TXY/LDA $0001,x
 *                           per-type table
 *   +2-3   x position     - 16-bit world/screen X (.4 fixed sometimes)
 *   +4-5   y position
 *   +6     flag           - re-used per type (24/25 use it as "$0250"
 *                           snapshot, 27/28 use it as a render variant)
 *   +7-8   target_x       - 16-bit
 *   +9-A   target_y       - 16-bit
 *   +B-C   pad / reserved
 *   +D     state-scratch  - "alive frames" counter for fade-out states
 *   +E     anim frame     - low-byte sprite index offset
 *   +F     attr           - OAM attribute byte (priority+palette+flip)
 *   +10    timer          - decremented each frame until 0 -> next state
 *   +11-12 motion residue - fractional remainder for sub-pixel motion
 *   +13    anim cursor    - "current frame" within walk cycle
 *
 * Names: descriptive only when the behavior justifies it. The bulk are
 * named `typeN_dispatch_XXXX` / `typeN_stateM_XXXX` / `sub_XXXX`.
 *
 * Verify:
 *   cd /Users/guilhermedavid/simant-re &&
 *   clang -Wall -Wextra -c entities_d.c -o /tmp/ed.o
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Forward decls / externs from simant.c and earlier handlers.
 * ------------------------------------------------------------------------ */
typedef struct Entity Entity;

/* WRAM and direct page (see simant.c). */
extern uint8_t  wram[0x20000];
#define dp wram                 /* alias of &wram[0] */

#define WORD(p, off)  (*(uint16_t *)&(p)[off])
#define BYTE(p, off)  ((p)[off])

/* Direct-page tags actually referenced by handlers 24..31. */
#define DP_TASK_INDEX        BYTE(dp, 0x00)        /* free clock tick */
#define DP_MOUSE_TRACK_LO    BYTE(dp, 0x2A)        /* sub_DCD5 PRNG state */
#define DP_MOUSE_TRACK_HI    BYTE(dp, 0x2B)
#define DP_SPRITE_DX         BYTE(dp, 0x37)        /* DB9E scratch X.lo */
#define DP_SPRITE_DX_HI      BYTE(dp, 0x38)
#define DP_SPRITE_DY         BYTE(dp, 0x39)
#define DP_SPRITE_DY_HI      BYTE(dp, 0x3A)
#define DP_SPRITE_TILE_LO    BYTE(dp, 0x3B)
#define DP_SPRITE_TILE_HI    BYTE(dp, 0x3C)
#define DP_SPRITE_ATTR       BYTE(dp, 0x3D)
#define DP_DRAW_BIAS         BYTE(dp, 0x44)        /* DB9E shadow offset */
#define DP_BG_ROW_CUR        BYTE(dp, 0x57)        /* even-frame guard */
#define DP_BG_ROW_PREV       BYTE(dp, 0x58)
#define DP_PAUSE_HOLD        BYTE(dp, 0x5A)        /* draw-suppress sign */
#define DP_ODD_GUARD         BYTE(dp, 0x5B)
#define DP_JOY_PREV_LO       BYTE(dp, 0x60)
#define DP_JOY_PREV_HI       BYTE(dp, 0x61)
#define DP_MENU_OPEN_LOCK    BYTE(dp, 0x71)        /* set => suppress input */
#define DP_MENU_HOLD_TIMER   BYTE(dp, 0x7B)        /* sub_DC84 path 2 */
#define DP_MENU_HOLD_TIMER2  BYTE(dp, 0x7D)
#define DP_SCROLL_X          WORD(dp, 0x9E)
#define DP_SCROLL_Y          WORD(dp, 0xA0)
#define DP_BG1_HSCROLL       WORD(dp, 0x4A)
#define DP_BG1_VSCROLL       WORD(dp, 0x4C)

/* Bigger-page slots (still wram[0..1FFF]). */
#define MENU_TICK_CMP        WORD(dp, 0x024A)      /* per-state tick reload */
#define MENU_BG_OFFSET_X     WORD(dp, 0x0246)
#define MENU_BG_OFFSET_Y     WORD(dp, 0x0248)
#define MENU_BUTTON_LATCH    BYTE(dp, 0x0250)      /* current "down" mask */
#define DLG_FRAMECOUNT       BYTE(dp, 0x024C)      /* slow heartbeat */
#define DLG_SUBSTATE         BYTE(dp, 0x02C5)
#define DLG_RESULT_LO        WORD(dp, 0x02C3)
#define DLG_TIMER            WORD(dp, 0x02B2)
#define POPUP_ACTIVE         BYTE(dp, 0x02A7)      /* type 29 gate */
#define POPUP_LOCK           BYTE(dp, 0x02E1)      /* type 29 inner gate */
#define POPUP_GOTO_STATE     BYTE(dp, 0x02E3)      /* type 29 transition req */
#define POPUP_ANIM_FRAME_LO  BYTE(dp, 0x020E)
#define POPUP_ANIM_FRAME_HI  BYTE(dp, 0x0210)
#define CURSOR_VISIBLE       BYTE(dp, 0x0200)      /* type 30/31 gate */
#define CURSOR_X             WORD(dp, 0x0202)
#define CURSOR_Y             WORD(dp, 0x0204)
#define CURSOR_CLICK_COUNT   BYTE(dp, 0x020A)      /* state-3 trigger */

/* SRAM scratch shadows.
 *
 * V3-B + final cleanup: $7F:EB60 / $7F:EB62 are NOT dedicated "price"
 * scratchpad bytes — they are the canonical AREA_B_POP_LIVE /
 * AREA_R_POP_LIVE counters (also referenced as $EB60/$EB62 by
 * simulation.c and territory.c via DBR-relative loads). The shop dialog
 * routine at $04:AE.. computes
 *
 *     budget + AREA_B_POP_LIVE - AREA_R_POP_LIVE
 *
 * i.e. it uses the live colony balance to bias the displayed price. So
 * these are semantic aliases (population reused as a price modifier),
 * not separate variables. Renamed from SHADOW_PRICE_LO/HI to make the
 * dual use explicit. */
#define SHADOW_BUDGET        BYTE(wram, 0x1E940)   /* $7F:E940 */
#define AREA_B_POP_LIVE_7F   WORD(wram, 0x1EB60)   /* alias: shop price-bias low  */
#define AREA_R_POP_LIVE_7F   WORD(wram, 0x1EB62)   /* alias: shop price-bias high */

/* Helpers from earlier handlers — bodies live in other .c files (or
 * simant.c stubs). */
extern void   sub_DB9E(void);                      /* draw 1 sprite from $37/$39/$3B/$3D */
extern void   sub_DB52(void);                      /* draw composite via DB5C + DB88 + DB9E */
extern void   sub_DB5C(Entity *self);              /* world->screen using +$0F flip bit */
extern void   sub_DB88(Entity *self);              /* tile = $000E + $000C */
extern void   sub_DB40(Entity *self,
                       uint16_t dx, uint16_t dy);  /* tile bias + draw */
extern void   sub_DC71(void);                      /* camera bias: $37+=$05, $39+=$07 */
extern uint16_t sub_DCFE(uint8_t mul_a,
                         uint8_t mul_b);           /* A*B 8x8->16-bit MUL */
extern uint8_t sub_DCD5(uint8_t mod);              /* "Random in [0..mod-1]" via LCG */
extern int    sub_DC84(void);                      /* "user just pressed A/X?": CC=no, CS=yes */
extern int    sub_DC9E(Entity *self);              /* proximity test (cursor within 16-px box) */
extern void   sub_DD24(void);                      /* OAM sprite-list expander (RLE) */
/* Canonical signature: a = angle, y = 16-bit amplitude (STY $C2 in X=0
 * mode stores 2 bytes; the ROM mul at $8CE0 reads $C2 as 16-bit).
 * Callers MUST pass the same Y the asm has loaded at the JSL site. */
extern uint16_t sub_008A0E_div256 (uint8_t a, uint16_t y);  /* JSL $00:8A0E cos */
extern uint16_t sub_008A0B_div256r(uint8_t a, uint16_t y);  /* JSL $00:8A0B sin */
extern void   sub_0088FF(uint8_t a, uint16_t y,
                         uint16_t x);              /* JSL $00:88FF - queue VRAM transfer */

/* Anim/sprite ROM tables — referenced by helpers. */
extern const uint8_t  rom_F198[];                  /* type 27 walk-frame tile-low table */
extern const uint8_t  rom_F1A8[];                  /* type 27 walk-frame attr table */
extern const uint8_t  rom_F1B8[];                  /* type 28 walk-frame tile-low table */
extern const uint8_t  rom_F1E8[];                  /* type 28 walk-frame attr table */
extern const uint8_t  rom_01F218[];                /* sub_B0E8: row-DMA count by tile */
extern const uint16_t rom_01F308[];                /* sub_B0E8: row-DMA src ptr by tile */

/* Misc shared scratch used by indirect drawers — set by callers via dp. */
#define SCRATCH_PROBE_LO  BYTE(dp, 0x82)
#define SCRATCH_PROBE_HI  BYTE(dp, 0x83)
#define SCRATCH_PROBE_BNK BYTE(dp, 0x84)
#define SCRATCH_PROBE2_LO BYTE(dp, 0x85)
#define SCRATCH_PROBE2_HI BYTE(dp, 0x86)
#define SCRATCH_PROBE2_BNK BYTE(dp, 0x87)
#define DP_PIX_X1        BYTE(dp, 0x52)
#define DP_PIX_Y1        BYTE(dp, 0x54)
#define DP_PIX_TILE_BAS  BYTE(dp, 0x56)
#define DP_SCRATCH_69    BYTE(dp, 0x69)

/* Entity struct (mirrors the one in simant.c — kept private here to avoid
 * pulling that file in). Packed so the 16-bit fields land on their ROM
 * offsets even on hosts that would otherwise add alignment padding. */
struct __attribute__((packed)) Entity {
    uint8_t  type;                /* +0  */
    uint8_t  state;               /* +1  */
    uint16_t x;                   /* +2  */
    uint16_t y;                   /* +4  */
    uint8_t  flag;                /* +6  */
    uint16_t target_x;            /* +7-8 */
    uint16_t target_y;            /* +9-A */
    uint8_t  pad_b;               /* +B  */
    uint8_t  pad_c;               /* +C  */
    uint8_t  state_scratch_d;     /* +D  */
    uint8_t  anim_frame_e;        /* +E  */
    uint8_t  attr_f;              /* +F  */
    uint8_t  timer_10;            /* +10 */
    uint8_t  motion_res_x_11;     /* +11 */
    uint8_t  motion_res_y_12;     /* +12 */
    uint8_t  anim_cursor_13;      /* +13 */
};
_Static_assert(sizeof(struct Entity) == 20, "entity record is 20 bytes");

/* ========================================================================
 * SHARED MOTION HELPER — sub_B114 ($04:B114)
 * ------------------------------------------------------------------------
 * Step the entity halfway toward its target every frame:
 *   delta = target - current
 *   add (delta >> 2) to motion_residue
 *   add (residue overflow) to current position
 *   Y returns the number of axes that didn't reach the target (0/1/2).
 *
 * In the original the math is done with arithmetic shift via ROR/ROR_DP69
 * (a 24-bit ROR through $69 as the fractional byte), which gives a
 * "smooth deceleration" — overshoot becomes signed quarter-steps.
 * ======================================================================== */
static uint8_t sub_B114_step_toward_target(Entity *self)
{
    uint8_t axes_remaining = 0;

    /* --- X axis --- */
    int16_t dx = (int16_t)self->target_x - (int16_t)self->x;
    if (dx != 0) {
        /* Two arithmetic right-shifts through a fractional byte (DP $69):
         * net effect is "quarter-step toward target with sub-pixel
         * accumulation". */
        uint8_t frac = 0;
        int16_t shifted = dx;
        /* ROR copies bit0 of shifted out into carry; carry rotates into
         * $69's high bit, then a second ROR happens. */
        frac = (uint8_t)((shifted & 1) << 7) | (frac >> 1);
        shifted = (int16_t)(((uint16_t)shifted >> 1) | (shifted & 0x8000));
        frac = (uint8_t)((shifted & 1) << 7) | (frac >> 1);
        shifted = (int16_t)(((uint16_t)shifted >> 1) | (shifted & 0x8000));

        self->motion_res_x_11 += frac;     /* may carry into x */
        if (shifted != 0) axes_remaining++;
        self->x = (uint16_t)((int16_t)self->x + shifted);
    }

    /* --- Y axis (mirror of X) --- */
    int16_t dy = (int16_t)self->target_y - (int16_t)self->y;
    if (dy != 0) {
        uint8_t frac = 0;
        int16_t shifted = dy;
        frac = (uint8_t)((shifted & 1) << 7) | (frac >> 1);
        shifted = (int16_t)(((uint16_t)shifted >> 1) | (shifted & 0x8000));
        frac = (uint8_t)((shifted & 1) << 7) | (frac >> 1);
        shifted = (int16_t)(((uint16_t)shifted >> 1) | (shifted & 0x8000));

        self->motion_res_y_12 += frac;
        if (shifted != 0) axes_remaining++;
        self->y = (uint16_t)((int16_t)self->y + shifted);
    }

    return axes_remaining;
}

/* ========================================================================
 * TYPE 24 — $04:A951
 * ------------------------------------------------------------------------
 * 3-state animation: a falling/landing visual that descends to a target
 * Y, holds, then disappears. Used in conjunction with type 25 (which
 * mirrors it horizontally — see init_word=$0008 for 25).
 *
 *   state 0  init   - snapshot target_y from current y (so the entity
 *                     "remembers" where it should end up)
 *   state 1  fall   - every other frame DEC target_y, draw composite via
 *                     helper_24_anim. When target_y crosses $0069 the
 *                     entity "lands": carry-set path skips the INC state.
 *   state 2  stand  - keep drawing the static composite forever (until
 *                     killed externally)
 *
 * The composite draw (sub_A990 / sub_A9FD) emits a 4-tile horizontal
 * cluster with $20-pixel step in X and a 4-px tile-attr step. Tile-low
 * derives from the wall clock — every 32 frames the strip "flashes" by
 * toggling bit 6 of the OAM Y attribute, which is a sprite-row flip.
 *
 * Probable role: Egg/Larva visual — eggs are stationary in the manual
 * but "appear" with a fall-in animation, then sit until hatched. The
 * "state 2 holds forever" behavior matches Eggs/Larvae/Pupae which are
 * inert until acted on by Nurses.
 * ======================================================================== */

/* sub_AA2A — incremental Y motion via sin lookup.
 *   $13 += A
 *   JSL $00:8A0E       ; A (16-bit) = sin($13) * Y (the speed arg)
 *   ADC target_y       ; A = sin_result + target_y (16-bit)
 *   STA entity.y       ; y = result (target_y unchanged!)
 * The earlier C wrongly stored the result into target_y as well, drifting
 * the landing target by sin() each frame; the ROM only writes entity.y.
 *
 * The "amplitude" Y is NOT loaded inside AA2A — it's inherited from the
 * caller. Bank-04 caller sub_A990 does `LDA #$04 / LDY #$0008 / JSR $AA2A`,
 * so for the type-24 egg/larva fall-in we pass amplitude=0x0008. */
static void sub_AA2A_step_y(Entity *self, uint8_t step, uint16_t amplitude)
{
    self->anim_cursor_13 += step;
    /* sub_008A0E returns a 16-bit signed (sin(angle) * amplitude) >> 8 in
     * the high 16 bits of the 32-bit product. (Amplitude MUST be the same
     * 16-bit Y the asm caller had loaded — passing 0 silently zeroes the
     * motion. See bank-04 disassembly $A990..$AA40.) */
    uint16_t sin_v = sub_008A0E_div256(self->anim_cursor_13, amplitude);
    self->y = (uint16_t)(self->target_y + (int16_t)sin_v);
    /* NOTE: target_y is read-only here in the ROM. Do NOT modify it. */
}

/* sub_A9FD — draw a 4-tile composite, with bit-6-of-attr flip every 32
 * clock ticks. Caller passes the base "X step" in A. */
static void sub_A9FD_draw_composite(Entity *self, uint8_t base_x_lo)
{
    DP_SCRATCH_69 = base_x_lo;

    /* odd/even bit-toggle every 32 frames: bit 6 of OAM attr flips Y */
    uint8_t flip_y = ((DP_TASK_INDEX << 5) & 0x40);
    DP_SPRITE_TILE_LO = (uint8_t)(flip_y + base_x_lo);
    DP_SPRITE_ATTR    = 0x18;
    sub_DB9E();

    DP_SPRITE_TILE_LO += 0x04;
    /* 16-bit add #$20 to $37 (sprite X coord) — second sprite, 32 px over */
    *(uint16_t *)&DP_SPRITE_DX += 0x0020;
    sub_DB9E();

    /* Then the routine falls through into sub_AA2A.
     * Y register at entry is still 0x0008 from sub_A990's LDY. */
    sub_AA2A_step_y(self, /*step=*/4, /*amplitude=*/0x0008);
}

/* sub_A990 — helper used by type 24 states 1 and 2. */
static void helper_24_anim(Entity *self)
{
    /* ROM:  LDA #$04 / LDY #$0008 / JSR $AA2A  — the LDY here is what
     * the 8A0E mul ultimately consumes as its 16-bit amplitude. */
    sub_AA2A_step_y(self, 4, /*amplitude=*/0x0008);
    sub_DB52();
    sub_A9FD_draw_composite(self, /*base_x=*/0x80);
}

/* WIKI: Egg/Larva entity (type 24/25 dispatched from $18/$19 in the
 * dispatch table). The 3-state lifecycle (init -> fall -> stand)
 * is documented in wiki/04-entity-system.md §3 as the canonical
 * per-state machine pattern. Spawned by the Queen's "Lay Eggs"
 * action — the egg laid by the Black Queen on player death becomes
 * the next Yellow Ant body; see wiki/05-yellow-ant.md §3
 * ("Death / Rebirth"). */
static void type24_state0_init_A963(Entity *self)
{
    /* "target_y = current_y" — anchor the landing height. */
    self->target_y = self->y;
    self->state++;
}

static void type24_state1_fall_A972(Entity *self)
{
    /* Every other frame, DEC target_y so the entity "falls" by 1 px. */
    if ((DP_TASK_INDEX & 1) == 0)
        self->target_y--;
    helper_24_anim(self);
    /* Once target_y_lo < $69, advance to state 2 (the static "landed" pose).
     * ROM does `LDA $0009,x / CMP #$69 / BCC` — 8-bit compare on LOW byte
     * of target_y, not a full 16-bit check. */
    if ((uint8_t)self->target_y >= 0x69)
        return;
    self->state++;
}

static void type24_state2_stand_A98B(Entity *self)
{
    /* No motion; just keep drawing. */
    helper_24_anim(self);
}

/* Type-24 dispatcher — same TXY/LDA $1,x/ASL/JMP pattern as the rest. */
static void type24_dispatch_A951(Entity *self)
{
    switch (self->state) {
    case 0: type24_state0_init_A963(self);  break;
    case 1: type24_state1_fall_A972(self);  break;
    case 2: type24_state2_stand_A98B(self); break;
    default: /* out-of-table state — original would dereference garbage */ break;
    }
}

/* ========================================================================
 * TYPE 25 — $04:A9A1   (init_word = $0008)
 * ------------------------------------------------------------------------
 * Same 3-state shape as type 24, but the composite draw is offset:
 *   - sub_AA2A passes step=#$06 (vs #$04 for type 24)
 *   - sub_A9FD is called with base_x=#$88 (vs #$80) and AFTER subtracting
 *     #$0004 from $37 — the cluster is shifted 4 px to the left
 *   - landing threshold is $65 instead of $69
 *
 * Likely the "mirror" half of the same visual — types 24+25 cooperate
 * to render an 8-tile composite (left half / right half) for the same
 * conceptual sprite.
 * ======================================================================== */
static void helper_25_anim(Entity *self)
{
    /* ROM $04:A9E0: LDA #$06 / LDY #$0004 / JSR $AA2A
     * Amplitude is 0x0004 here (half of type 24's 0x0008). */
    sub_AA2A_step_y(self, /*step=*/6, /*amplitude=*/0x0004);
    sub_DB52();
    /* shift left by 4 px */
    *(uint16_t *)&DP_SPRITE_DX -= 0x0004;
    sub_A9FD_draw_composite(self, /*base_x=*/0x88);
}

static void type25_state0_init_A9B3(Entity *self)
{
    self->target_y = self->y;
    self->state++;
}

static void type25_state1_fall_A9C2(Entity *self)
{
    if ((DP_TASK_INDEX & 1) == 0)
        self->target_y--;
    helper_25_anim(self);
    if (self->target_y >= 0x65)
        return;
    self->state++;
}

static void type25_state2_stand_A9DB(Entity *self)
{
    helper_25_anim(self);
}

static void type25_dispatch_A9A1(Entity *self)
{
    switch (self->state) {
    case 0: type25_state0_init_A9B3(self);  break;
    case 1: type25_state1_fall_A9C2(self);  break;
    case 2: type25_state2_stand_A9DB(self); break;
    default: break;
    }
}

/* ========================================================================
 * TYPE 26 — $04:AB0B   (init_attr=$00, no dispatch pattern)
 * ------------------------------------------------------------------------
 * Stateless every-frame draw of a 4-tile horizontal strip at fixed
 * screen origin (0, 0). Tile indices are $0100, $0108, $0180, $0188 —
 * a 4-cell layout spanning 128 px across the top of the screen. The
 * attribute byte is $99 (priority 2, palette 4).
 *
 * Spacing: 64-px X step between sprites; Y stays at 0. Because the
 * coordinates are fixed (not derived from entity x/y), this is NOT a
 * world entity — it's a HUD/title-bar sprite cluster. Likely the
 * always-on "info strip" at the top of an in-game screen (BG-overlay
 * panel showing ant count / season / etc.), OR the per-screen banner
 * that frames the manual/encyclopedia view.
 *
 * Because attr_f in the entity-init table is $00 (vs $9X for creatures)
 * and there's no AI here, this is not on the list of creatures the
 * manual lists.  Best fit: a "status panel" entity that piggybacks the
 * entity walker so it gets drawn every frame.
 * ======================================================================== */
static void type26_status_panel_AB0B(Entity *self)
{
    (void)self;        /* coordinates are fixed, not from self */

    /* Sprite 1: tile $0100 at (0, 0), attr $99 */
    *(uint16_t *)&DP_SPRITE_DX   = 0x0000;
    *(uint16_t *)&DP_SPRITE_DY   = 0x0000;
    *(uint16_t *)&DP_SPRITE_TILE_LO = 0x0100;
    DP_SPRITE_ATTR = 0x99;
    sub_DB9E();

    /* Sprite 2: tile $0108 at ($40, 0) */
    *(uint16_t *)&DP_SPRITE_DX   += 0x0040;
    *(uint16_t *)&DP_SPRITE_TILE_LO = 0x0108;
    sub_DB9E();

    /* Sprite 3: tile $0180 at ($80, 0) */
    *(uint16_t *)&DP_SPRITE_DX   += 0x0040;
    *(uint16_t *)&DP_SPRITE_TILE_LO = 0x0180;
    sub_DB9E();

    /* Sprite 4: tile $0188 at ($C0, 0) */
    *(uint16_t *)&DP_SPRITE_DX   += 0x0040;
    *(uint16_t *)&DP_SPRITE_TILE_LO = 0x0188;
    sub_DB9E();
}

/* ========================================================================
 * TYPE 27 — $04:AB5B
 * ------------------------------------------------------------------------
 * 4-state creature with input gating. Probable role: a TARGETABLE
 * PREDATOR or PEST that the player can click on to dismiss (Caterpillar
 * is the most likely fit — manual says "harmless"; on-click it crawls
 * away and despawns).
 *
 * Walk frames are indexed via rom_F198/F1A8 (8-frame walk cycle).
 *
 *   state 0  init    - PRNG-place at random (x,y) via sub_D7A1, init
 *                      target via sub_D721 (with row-offset $0400),
 *                      set timer=$1E, advance
 *   state 1  walk    - update walk frame every 4 sub-ticks; render via
 *                      sub_D6F6 (which reads rom_F198 for tile-low and
 *                      rom_F1A8 for attr, with current anim_cursor as
 *                      offset). If user "clicks-on" (sub_DC84 returns
 *                      carry-set), bail to state 3 (death/fade) with
 *                      anim cursor=$0F and timer=$0A. Otherwise, when
 *                      tick4==0 and timer expires, advance to state 2
 *                      (rest), reload timer=$10.
 *   state 2  rest    - same renderer but NO motion (no JSR D747). After
 *                      timer expires, pick new target via sub_D721 and
 *                      advance to state 3.
 *   state 3  fade    - keep walking but every frame INC the rotation
 *                      counter (anim_cursor_13) modulo 8, DEC anim
 *                      frame; once frame<0, refresh and DEC state
 *                      (go back to state 2).
 *
 * Note: state 3 doesn't despawn — it loops. So if this IS the
 * Caterpillar, "harmless" means it never bothers ants; the player can
 * still pet it but it doesn't go away.
 * ======================================================================== */

/* sub_AC26 — set up render pointers to rom_F198/rom_F1A8 then JSR D6F6. */
static void type27_draw_anim_AC26(Entity *self)
{
    SCRATCH_PROBE_LO  = (uint8_t)(uintptr_t)rom_F198;
    SCRATCH_PROBE_HI  = (uint8_t)((uintptr_t)rom_F198 >> 8);
    SCRATCH_PROBE_BNK = 0x01;
    SCRATCH_PROBE2_LO = (uint8_t)(uintptr_t)rom_F1A8;
    SCRATCH_PROBE2_HI = (uint8_t)((uintptr_t)rom_F1A8 >> 8);
    SCRATCH_PROBE2_BNK = 0x01;
    /* JSR $D6F6 — composite sprite renderer; uses [$82],y + [$85],y. */
    extern void sub_D6F6(Entity *self);
    sub_D6F6(self);
}

/* sub_D7A1 — randomize entity position to a 4096-px room: x = rand(128) << 4,
 * y = rand(64) << 4. */
static void type27_random_spawn_D7A1(Entity *self)
{
    self->x = ((uint16_t)sub_DCD5(0x80)) << 4;
    self->y = ((uint16_t)sub_DCD5(0x40)) << 4;
}

/* sub_D721 — compute target.{x,y} from heading via sin/cos lookup:
 *   heading = (anim_cursor_13 - 2) << 5  (8-direction angle, table stride 32)
 *   target_y = cos(heading) * Y_amplitude  (sub_008A0E)
 *   target_x = sin(heading) * Y_amplitude  (sub_008A0B)
 * Used as "pick destination based on current walk frame".
 *
 * Caller's Y (`row_offset`) is the 16-bit amplitude — observed values from
 * bank-04 callers of D721:
 *   $04:AB76, $04:ABED, $04:AC15  -> LDY #$0400  (type27 normal walk)
 *   $04:AC50, $04:ACE1            -> LDY #$0380  (rest/reset variants)
 * It MUST be passed as 16-bit; truncating to 8 bits would zero $0400. */
static void type27_pick_target_D721(Entity *self, uint16_t row_offset)
{
    uint8_t base = (uint8_t)(((int8_t)self->anim_cursor_13 - 2) << 5);
    self->target_y = (uint16_t)(sub_008A0E_div256(base, row_offset) << 8);
    self->target_x = (uint16_t)(sub_008A0B_div256r(base, row_offset) << 8);
}

/* sub_D747 — wraps motion calls; only steps if global frame % 4 == 0. */
static void type27_maybe_move_D747(Entity *self)
{
    if (DP_TASK_INDEX != 4) return;
    /* JSR D755 advances X by 8-bit speed in entity[7]/[8], JSR D77B same
     * for Y in [9]/[A]. These are bank-04 helpers; declared extern. */
    extern void sub_D755(Entity *self);
    extern void sub_D77B(Entity *self);
    sub_D755(self);
    sub_D77B(self);
}

static void type27_state0_init_AB6F(Entity *self)
{
    type27_random_spawn_D7A1(self);
    self->anim_cursor_13 = 0;
    type27_pick_target_D721(self, /*row_off=*/0x0400);
    self->timer_10 = 0x1E;       /* 30-frame walk interval before resting */
    self->state++;
}

static void type27_state1_walk_AB85(Entity *self)
{
    type27_maybe_move_D747(self);
    /* Every 4-frame tick, advance the animation cursor (bit 1 toggles). */
    if (DP_TASK_INDEX == 4) {
        self->anim_frame_e = (DP_TASK_INDEX >> 1) & 0x01;
    }
    type27_draw_anim_AC26(self);
    /* User clicked? Jump to fade-out (state 3). */
    if (sub_DC84()) {
        self->state_scratch_d = 0x0F;
        self->timer_10        = 0x0A;
        self->state           = 3;
        return;
    }
    /* Otherwise wait for timer to expire on every 4-frame boundary. */
    if (DP_TASK_INDEX != 4) return;
    if (--self->timer_10 != 0) return;
    /* Timer expired — go to rest. */
    self->state_scratch_d = sub_DCD5(3);   /* 0..2 — initial rest counter? */
    self->timer_10        = 0x0A;
    self->state++;                          /* -> state 2 */
}

static void type27_state2_rest_ABCE(Entity *self)
{
    /* No JSR D747 — entity does not move during rest. */
    type27_draw_anim_AC26(self);
    if (DP_TASK_INDEX != 4) return;
    if (--self->timer_10 != 0) return;
    /* Rest finished — pick new wander target and advance to walk-tail */
    self->anim_cursor_13 = (self->anim_cursor_13 + 1) & 0x07;
    if ((int8_t)--self->state_scratch_d >= 0) {
        /* Quick "stretch" pose then return to rest. */
        self->timer_10 = 0x0A;
        return;
    }
    type27_pick_target_D721(self, /*row_off=*/0x0400);
    self->timer_10 = 0x1E;
    self->state--;                          /* -> state 1 */
}

static void type27_state3_fade_AC03(Entity *self)
{
    type27_draw_anim_AC26(self);
    /* Increment anim_cursor mod 8 (the "death wiggle"). */
    self->anim_cursor_13 = (self->anim_cursor_13 + 1) & 0x07;
    if ((int8_t)--self->state_scratch_d < 0) {
        /* "Done dying" — restart as fresh walker. */
        type27_pick_target_D721(self, /*row_off=*/0x0400);
        self->timer_10 = 0x1E;
        self->state    = 1;
    }
}

static void type27_dispatch_AB5B(Entity *self)
{
    switch (self->state) {
    case 0: type27_state0_init_AB6F(self); break;
    case 1: type27_state1_walk_AB85(self); break;
    case 2: type27_state2_rest_ABCE(self); break;
    case 3: type27_state3_fade_AC03(self); break;
    default: break;
    }
}

/* ========================================================================
 * TYPE 28 — $04:AC3A  (ANT LION, manual p.34)
 * ------------------------------------------------------------------------
 * WIKI: this is ONLY the visual ambush AI. The kill itself shares
 *       combat.c::spider_predation_tick_C0FD_excerpt with the Spider,
 *       but with a 4-tick cadence (vs 16 for spider). The 1-in-10
 *       "burst back to ambush" path below is the visual half of the
 *       "pounce, miss, re-ambush" loop documented in
 *       wiki/09-predation.md#2-ant-lion--03c0fd-shared--type-28-in-entities_dc.
 *
 * Variant of type 27: same 3 + tail-state structure but uses a different
 * sprite sheet (rom_F1B8 / rom_F1E8), and the timer/anim parameters are
 * different ($50 idle, $10 rest, $5A trigger, $06 walk).
 *
 * Critically, state 2's tail (at $ACBC) does:
 *   - a 1-in-10 chance (sub_DCD5 mod 10) to TRANSITION BACK to state 1
 *     (reset timer to $5A), suggesting this is a creature that bursts
 *     into motion sporadically.
 *   - otherwise picks a NEW direction (anim_cursor += rand(3)-1 mod 8)
 *     and a new target with row_offset $0380.
 *
 * Probable role: Spider OR Ant Lion — both manual entries describe
 * "ambush, lunge, retreat" patterns. State 1 has a low timer ($50 ~= 80
 * frames), state 2 has a long timer ($5A = 90 frames) — so the creature
 * spends most of its time waiting in ambush. The "random burst" matches
 * the Spider's "actively hunts" behavior more than the Ant Lion's
 * "waits in pit".
 *
 * NOTE: Neither state 1 nor state 2 here actually erases other entities
 * (no STA #$00 into anyone else's +0). So if this is a Spider, the
 * "eating ants" logic must live in the SHARED helper (sub_D6F6 maybe
 * does collision damage on top of rendering) — that would explain why
 * the F198/F1B8 tables have one byte per anim frame (no special
 * "attack" code path here).
 * ======================================================================== */

/* sub_ACED — set up render pointers to rom_F1B8/rom_F1E8, JSR D6F6. */
static void type28_draw_anim_ACED(Entity *self)
{
    SCRATCH_PROBE_LO  = (uint8_t)(uintptr_t)rom_F1B8;
    SCRATCH_PROBE_HI  = (uint8_t)((uintptr_t)rom_F1B8 >> 8);
    SCRATCH_PROBE_BNK = 0x01;
    SCRATCH_PROBE2_LO = (uint8_t)(uintptr_t)rom_F1E8;
    SCRATCH_PROBE2_HI = (uint8_t)((uintptr_t)rom_F1E8 >> 8);
    SCRATCH_PROBE2_BNK = 0x01;
    extern void sub_D6F6(Entity *self);
    sub_D6F6(self);
}

static void type28_state0_init_AC4C(Entity *self)
{
    type27_random_spawn_D7A1(self);
    type27_pick_target_D721(self, /*row_off=*/0x0380);
    self->anim_cursor_13 = 0;
    self->timer_10       = 0x50;
    self->flag           = 0x02;     /* "ambush" flag */
    self->state++;
}

static void type28_state1_ambush_AC67(Entity *self)
{
    if (DP_TASK_INDEX == 4) {
        /* Every 4-frame tick, "look around" — pick a new facing in [2..5]. */
        self->anim_frame_e = ((DP_TASK_INDEX >> 2) & 0x03) + 2;
    }
    type28_draw_anim_ACED(self);
    if (DP_TASK_INDEX != 4) return;
    if (--self->timer_10 != 0) return;
    /* Ambush over — lunge. */
    self->timer_10 = 0x06;
    self->flag     = 0x10;          /* "active" flag */
    self->state++;
}

static void type28_state2_hunt_AC99(Entity *self)
{
    /* JSR D747 — move toward target every 4 frames. */
    type27_maybe_move_D747(self);
    if (DP_TASK_INDEX == 4) {
        self->anim_frame_e = (DP_TASK_INDEX >> 1) & 0x01;
    }
    type28_draw_anim_ACED(self);
    if (DP_TASK_INDEX != 4) return;
    if (--self->timer_10 != 0) return;

    /* 1-in-10 chance: continue hunting (re-enter ambush briefly). */
    if (sub_DCD5(10) == 0) {
        self->flag      = 0x02;
        self->timer_10  = 0x5A;
        self->state--;
        return;
    }
    /* Otherwise: change direction by ±1 (or 0), pick fresh target. */
    int8_t turn = (int8_t)sub_DCD5(3) - 1;
    self->anim_cursor_13 = (uint8_t)((self->anim_cursor_13 + turn) & 0x07);
    type27_pick_target_D721(self, /*row_off=*/0x0380);
    self->timer_10 = 0x06;
}

static void type28_dispatch_AC3A(Entity *self)
{
    switch (self->state) {
    case 0: type28_state0_init_AC4C(self);     break;
    case 1: type28_state1_ambush_AC67(self);   break;
    case 2: type28_state2_hunt_AC99(self);     break;
    default: break;
    }
}

/* ========================================================================
 * TYPE 29 — $04:AD01    (DIALOG/POPUP STATE MACHINE)
 * ------------------------------------------------------------------------
 * 10-state machine that drives an interactive popup (menu/dialog box).
 *
 * Outer gate: if POPUP_ACTIVE ($02A7) == 0, skip entirely.
 * Inner gate: if POPUP_LOCK   ($02E1) != 0, skip entirely.
 *
 * Every-frame work before dispatch:
 *   sub_AD57 — guard: if entity->flag == MENU_BUTTON_LATCH AND
 *              MENU_TICK_CMP == entity->anim_cursor, bail; else write
 *              MENU_TICK_CMP into anim_cursor and JSR $B0C9 (refresh
 *              scrolled position).
 *
 * Then if POPUP_GOTO_STATE > 0 (and not negative), force a state jump:
 *   new state = POPUP_GOTO_STATE
 *   timer     = rom_AD4D[POPUP_GOTO_STATE]
 *   POPUP_GOTO_STATE = $FF (consumed)
 *   anim_frame = 0
 *
 * After that, the standard JMP ($AD39) table runs one of 10 state bodies.
 *
 *   state 0  hidden       - wait for press; on press JSR $B0C9 + reset
 *                           residue, advance.
 *   state 1  drift        - JSR $B0B0 (snapshot menu BG offset), JSR
 *                           $B114 (move toward target). On press, set
 *                           anim_frame = (clock>>1)&1 and tail. If no
 *                           press AND MENU_BUTTON_LATCH bit 3 clear,
 *                           pick anim_frame from clock bits 3+1. Then
 *                           JSR $AFBD (redraw queue). If button==$00
 *                           bail to state 2. If button == $70, take
 *                           the special "purchase" code path: compute
 *                           cost = SHADOW_BUDGET + PRICE_LO - PRICE_HI,
 *                           store into DLG_RESULT_LO, decrement
 *                           SHADOW_BUDGET by 10 (clamp >= 1), advance
 *                           to state 3.
 *   state 2  dialog_open  - waits for MENU_BUTTON_LATCH to clear, then
 *                           reset frame counter, redraw, advance.
 *   state 3  blink_1      - animate sprite tile $0A every 4 frames (2-frame
 *                           cycle), DEC timer; when timer hits 0,
 *                           advance.
 *   state 4  blink_2      - same as state 3 but tile $88 with 4-frame
 *                           cycle (slower).
 *   state 5  blink_3      - same but tile $8E with 8-frame cycle.
 *   state 6  blink_4      - tile $08, 8-frame.
 *   state 7  blink_5      - tile $8C, 4-frame.
 *   state 8  blink_6      - tile $74, 4-frame.
 *   state 9  final        - tile derived from rom_B080[clock&7] + $7C;
 *                           attr from rom_B088[i] OR'd with $9B; uses
 *                           the 16x16-composite renderer (sub_DB40),
 *                           then JSR $B0E8 (queue VRAM update for tile
 *                           data via $01:F308 ptr table). When timer
 *                           hits 0, goes back to state 0.
 *
 * The reload table at $04:AD4D is { 0,0,0,0, $64, $64, $64, $28,
 * $28, $28 } — i.e. state 4/5/6 take 100 frames (~1.7 s) and states
 * 7/8/9 take 40 frames (~0.7 s).
 *
 * Probable role: this is the main IN-GAME MENU/COMMAND popup —
 * exactly what the manual describes as the "yellow ant icon"
 * mini-menu (Recruit/Release commands). The "purchase" path with
 * SHADOW_BUDGET strongly implies the "spend pheromone budget" or
 * "spend population" UI.
 * ======================================================================== */
static const uint8_t type29_state_reload_AD4D[12] = {
    0x00, 0x00, 0x00, 0x00, 0x64, 0x64, 0x64, 0x28,
    0x28, 0x28, 0xBD, 0x06,                /* last 2 bleed into code */
};

/* Helper: sub_B0B0 — capture current bg-scrolled position into target_{x,y}. */
static void sub_B0B0_snapshot_target(Entity *self)
{
    /* MENU_BG_OFFSET_X << 4 — shift into "subpixel x", same for Y. */
    self->target_x = (uint16_t)(MENU_BG_OFFSET_X << 4);
    self->target_y = (uint16_t)(MENU_BG_OFFSET_Y << 4);
}

/* Helper: sub_B0C9 — snapshot AND also write flag := MENU_BUTTON_LATCH. */
static void sub_B0C9_snapshot_pos(Entity *self)
{
    self->x    = (uint16_t)(MENU_BG_OFFSET_X << 4);
    self->y    = (uint16_t)(MENU_BG_OFFSET_Y << 4);
    self->flag = MENU_BUTTON_LATCH;
}

/* Helper sub_B0E8 — if BG row needs an update, queue a tile-data DMA
 * derived from rom_01F218 (count) and rom_01F308 (src). */
static void sub_B0E8_queue_row_update(uint8_t row_tile)
{
    extern void sub_DD24_via_82(void);
    if (row_tile == DP_BG_ROW_PREV) return;   /* already up to date */
    /* row_tile == X via PHX. Look up count[row_tile] and src[row_tile]. */
    SCRATCH_PROBE_BNK  = rom_01F218[row_tile];
    SCRATCH_PROBE_LO   = (uint8_t)((uintptr_t)&rom_01F308[row_tile]);
    SCRATCH_PROBE_HI   = (uint8_t)(((uintptr_t)&rom_01F308[row_tile]) >> 8);
    SCRATCH_PROBE2_LO  = 0x00;
    SCRATCH_PROBE2_HI  = 0x1E;
    SCRATCH_PROBE2_BNK = 0x00;
    sub_DD24();
}

/* sub_AD57 — pre-dispatch refresh. The ROM JSRs here and ALWAYS continues
 * regardless of outcome; the early RTS at $AD67 only skips the sub_B0C9
 * call (and the anim_cursor write), it does NOT abort dispatch. */
static void type29_predispatch_guard(Entity *self)
{
    if (self->flag == MENU_BUTTON_LATCH
        && (uint8_t)MENU_TICK_CMP == self->anim_cursor_13) {
        return;             /* skip refresh — dispatcher still runs */
    }
    if (self->flag == MENU_BUTTON_LATCH) {
        /* flag matches but tick differs: store new tick into anim_cursor. */
        self->anim_cursor_13 = (uint8_t)MENU_TICK_CMP;
    }
    /* Always JSR $B0C9 in both the "flag mismatch" and "tick changed" paths. */
    sub_B0C9_snapshot_pos(self);
}

/* sub_AFBD — "redraw composite at current frame" path. Picks anim_frame
 * from a sin-ish table (rom_B070) using MENU_BUTTON_LATCH>>3. If the
 * lookup returns a negative byte, suppress the draw by zeroing
 * DP_PIX_TILE_BAS. */
static void sub_AFBD_compose_draw(Entity *self)
{
    extern const int8_t rom_B070[];
    int8_t v = rom_B070[(MENU_BUTTON_LATCH >> 3) & 0x0F];
    if (v < 0) {
        DP_PIX_TILE_BAS = 0;
        return;
    }
    /* Body at $AFDD — full draw (REP #$20 / LDA $0002,x / SBC #$0008 / ...).
     * Equivalent: position - 8 in both axes -> pix coords -> tile $9B base
     * -> JSR $B0E8 to flush row -> JSR $B058 (DMA finalize). */
    DP_PIX_X1 = (uint8_t)(self->x - 8);
    DP_PIX_Y1 = (uint8_t)(self->y - 8);
    DP_PIX_TILE_BAS = 0x9B;
    sub_B0E8_queue_row_update(0);          /* "no row" — flush */
    extern void sub_B058(void);
    sub_B058();
}

/* sub_AF90 — "if it's time, fill the screen patch". Uses DLG_TIMER+1
 * vs MENU_TICK_CMP. If they differ, suppress (DP_PIX_TILE_BAS=0); else
 * draw via $B058 with a 16-px-left-shifted source rect at tile $9B. */
static void sub_AF90_tile_blit(uint8_t tile_offset)
{
    if ((uint8_t)(DLG_TIMER + 1) != (uint8_t)MENU_TICK_CMP) {
        DP_PIX_TILE_BAS = 0;
        return;
    }
    DP_PIX_X1 = (uint8_t)(*(int16_t *)&dp[0x37] - 8);
    DP_PIX_Y1 = (uint8_t)(*(int16_t *)&dp[0x39] - 8);
    DP_PIX_TILE_BAS = 0x9B + tile_offset;  /* base + animation step */
    sub_B0E8_queue_row_update(0);
    extern void sub_B058(void);
    sub_B058();
}

/* sub_B114 — also called explicitly from state 2: stepwise motion. */

static void type29_state0_hidden_AD6F(Entity *self)
{
    sub_B0C9_snapshot_pos(self);
    DP_PIX_TILE_BAS = 0;
    self->motion_res_x_11 = 0;
    self->motion_res_y_12 = 0;
    self->anim_frame_e    = 0;
    POPUP_GOTO_STATE      = 0;
    self->state++;
}

static void type29_state1_drift_AD85(Entity *self)
{
    sub_B0B0_snapshot_target(self);
    if (sub_B114_step_toward_target(self) != 0) {
        /* Still moving — alternate anim frame using clock bit 1. */
        self->anim_frame_e = (DP_TASK_INDEX >> 1) & 0x01;
    } else if ((MENU_BUTTON_LATCH & 0x08) == 0) {
        /* Settled — idle "thinking" frames 2/3. */
        self->anim_frame_e = ((DP_TASK_INDEX >> 3) & 0x01) + 2;
    } else {
        self->anim_frame_e = 0;
    }
    sub_AFBD_compose_draw(self);

    if (MENU_BUTTON_LATCH == 0) {
        /* User released — pop to dialog-open state. */
        self->state = 2;
        return;
    }
    if (MENU_BUTTON_LATCH == 0x70) {
        /* "Confirm purchase" gesture.
         *
         * ROM ($04:ADC3-$ADEB):
         *   REP #$20
         *   LDY $02C5                  ; purchase-row index (DLG_SUBSTATE)
         *   LDA $7FE940                ; budget
         *   CLC / ADC $7FEB60          ; + PRICE_LO
         *   SEC / SBC $7FEB62          ; - PRICE_HI
         *   CMP #$8000 / ROR x3        ; arithmetic >>3 (sign-preserving)
         *   CLC / ADC $AE06,y          ; add per-row offset
         *   BPL +3 / LDA #$0000        ; clamp negative to 0
         *   STA $02C3                  ; -> DLG_RESULT_LO
         *
         * G1 fix (was V4-2 BLOCKER #3, F6 stub):
         *   The table at $04:AE06 is 16 16-bit entries (verified by direct
         *   file-offset read at 0x22E06):
         */
        static const uint16_t rom_04_AE06[16] = {
            0x0019, 0x000A, 0x0023, 0x0000,  /* +25, +10, +35,  0 */
            0x0028, 0x0019, 0x0028, 0x0005,  /* +40, +25, +40, +5 */
            0x000F, 0x000A, 0x0019, 0x000F,  /* +15, +10, +25, +15 */
            0x001E, 0x0019, 0x000F, 0x000F   /* +30, +25, +15, +15 */
        };
        uint16_t budget = SHADOW_BUDGET;
        /* Signed arithmetic to mirror the ROM's "CMP #$8000 / ROR" sign-aware
         * right-shift-by-3 (preserves the sign across the three RORs). */
        /* Note: AREA_B/R_POP_LIVE_7F are aliases for the colony population
         * counters — see header comment near macro definitions. */
        int16_t  signed_acc = (int16_t)(budget + AREA_B_POP_LIVE_7F - AREA_R_POP_LIVE_7F);
        signed_acc >>= 3;                     /* arithmetic >>3 */
        /* Y is the byte offset used in `ADC $AE06,y`; entries are 16-bit, so
         * the table index is (Y >> 1).  DLG_SUBSTATE ($02C5) holds the byte
         * offset directly when populated by the dialog setup. Clamp to range. */
        unsigned idx = (unsigned)(DLG_SUBSTATE >> 1) & 0x0F;
        signed_acc = (int16_t)(signed_acc + (int16_t)rom_04_AE06[idx]);
        if (signed_acc < 0) signed_acc = 0;
        DLG_RESULT_LO = (uint16_t)signed_acc;

        SHADOW_BUDGET = (uint8_t)((budget < 10) ? 1 : (budget - 10));
        self->state = 3;
    }
}

static void type29_state2_dialog_open_AE26(Entity *self)
{
    if (MENU_BUTTON_LATCH != 0) {
        /* User started pressing again — reset and go back to drift. */
        DP_PIX_TILE_BAS    = 0;
        self->anim_frame_e = 0;
        POPUP_GOTO_STATE   = 0;
        sub_AFBD_compose_draw(self);
        self->state = 1;
        return;
    }
    sub_B0C9_snapshot_pos(self);
    /* Refresh the column rendered at row (frame_count/2 + $70). */
    DP_BG_ROW_CUR = (uint8_t)((DLG_FRAMECOUNT >> 1) + 0x70);
    sub_AF90_tile_blit(0);
}

/* Generic per-state blink body — used by states 3..8. */
static void type29_blink_state(Entity *self,
                               uint8_t mask_n_clocks, uint8_t anim_mask,
                               uint8_t tile_offset)
{
    sub_B0C9_snapshot_pos(self);
    if ((DP_TASK_INDEX & mask_n_clocks) == 0) {
        self->anim_frame_e = (self->anim_frame_e + 1) & anim_mask;
    }
    DP_BG_ROW_CUR = self->anim_frame_e + tile_offset;
    sub_AF90_tile_blit(0);
    if (--self->timer_10 == 0)
        self->state = 0;          /* back to hidden */
}

static void type29_state3_blink_AE4D(Entity *self)
{
    sub_B0C9_snapshot_pos(self);
    if ((DP_TASK_INDEX & 0x03) == 0) {
        uint8_t f = self->anim_frame_e + 1;
        if (f >= 6) f = 0;
        self->anim_frame_e = f;
    }
    DP_BG_ROW_CUR = self->anim_frame_e + 0x0A;
    sub_AF90_tile_blit(0);
    /* Plus: if user clicks now (bit 7 set on $60|$61), bump DLG_RESULT_LO. */
    if (DP_MENU_OPEN_LOCK == 0) {
        if ((DP_JOY_PREV_LO | DP_JOY_PREV_HI) & 0x80) {
            DLG_RESULT_LO++;
        }
    } else if (DP_MENU_HOLD_TIMER2 & 0xFF) {
        /* alternate "menu hold" path: don't increment */
    }
    if (--self->timer_10 == 0)
        self->state = 0;
}

static void type29_state4_blink_AE92(Entity *self) { type29_blink_state(self, 0x03, 0x03, 0x88); }
static void type29_state5_blink_AEB9(Entity *self) { type29_blink_state(self, 0x07, 0x01, 0x8E); }
static void type29_state6_blink_AEE0(Entity *self) { type29_blink_state(self, 0x07, 0x01, 0x08); }
static void type29_state7_blink_AF07(Entity *self) { type29_blink_state(self, 0x03, 0x01, 0x8C); }
static void type29_state8_blink_AF2E(Entity *self) { type29_blink_state(self, 0x03, 0x01, 0x74); }

/* State 9 — composite draw via sub_DB40 + sub_B0E8 row-tile DMA. */
static void type29_state9_final_AF55(Entity *self)
{
    extern const int8_t rom_B080[8];
    extern const int8_t rom_B088[8];
    sub_B0C9_snapshot_pos(self);
    uint8_t i = DLG_FRAMECOUNT & 0x07;
    DP_BG_ROW_CUR     = rom_B080[i] + 0x7C;
    DP_SPRITE_ATTR    = (uint8_t)(rom_B088[i] | 0x9B);
    DP_SPRITE_TILE_LO = 0xC0; DP_SPRITE_TILE_HI = 0;
    /* sub_DB40 with A=#$F8 (X-offset = -8), Y=#$FFF8 (Y-offset = -8) */
    sub_DB40(self, /*dx=*/(uint16_t)(int16_t)-8,
             /*dy=*/(uint16_t)(int16_t)-8);
    sub_DB9E();
    sub_B0E8_queue_row_update(DP_BG_ROW_CUR);
    if (--self->timer_10 == 0)
        self->state = 0;
}

static void type29_dispatch_AD01(Entity *self)
{
    /* Outer gate: popup not active. */
    if (POPUP_ACTIVE) return;
    /* Inner gate: popup lock active. */
    if (POPUP_LOCK)   return;

    type29_predispatch_guard(self);   /* may write anim_cursor + B0C9 */

    /* "Force-jump" request from outside? ROM uses POPUP_GOTO_STATE (full
     * byte) as the index into AD4D, not masked. The table is 12 bytes;
     * state values >= 12 read OOB, but legitimate states are 1..9. */
    if (POPUP_GOTO_STATE > 0 && POPUP_GOTO_STATE < 0x80) {
        self->state           = POPUP_GOTO_STATE;
        if (POPUP_GOTO_STATE < sizeof(type29_state_reload_AD4D))
            self->timer_10    = type29_state_reload_AD4D[POPUP_GOTO_STATE];
        POPUP_GOTO_STATE      = 0xFF;
        self->anim_frame_e    = 0;
    }

    switch (self->state) {
    case 0: type29_state0_hidden_AD6F(self);       break;
    case 1: type29_state1_drift_AD85(self);        break;
    case 2: type29_state2_dialog_open_AE26(self);  break;
    case 3: type29_state3_blink_AE4D(self);        break;
    case 4: type29_state4_blink_AE92(self);        break;
    case 5: type29_state5_blink_AEB9(self);        break;
    case 6: type29_state6_blink_AEE0(self);        break;
    case 7: type29_state7_blink_AF07(self);        break;
    case 8: type29_state8_blink_AF2E(self);        break;
    case 9: type29_state9_final_AF55(self);        break;
    default: break;
    }
}

/* ========================================================================
 * TYPE 30 — $04:B17F   (init_word=$0080)   — MOVING CURSOR
 * ------------------------------------------------------------------------
 * 6-state machine; an animated cursor/pointer that slides to a target
 * supplied via CURSOR_X/CURSOR_Y (dp[$0202/$0204]), then fires a "click"
 * animation. Init_word $0080 sets the entity's initial draw-bias.
 *
 *   state 0  idle    - wait for CURSOR_VISIBLE; on set, timer=4, advance.
 *   state 1  warmup  - countdown 4 frames. When 0, snapshot CURSOR_X/Y
 *                      into entity x/y, clear motion residues, advance.
 *   state 2  glide   - on CURSOR_VISIBLE clear, snap back to state 0.
 *                      Always update target = cursor pos; step via
 *                      sub_B114 (toward target); render via sub_B268.
 *                      When CURSOR_CLICK_COUNT == 5, set timer=$14,
 *                      advance.
 *   state 3  click_a - draw 1 of 4 (anim frame $0C), timer=$14, advance.
 *   state 4  click_b - draw 1 of 4 (anim frame $0D), advance.
 *   state 5  click_c - draw 1 of 4 (anim frame $0E). On CURSOR_VISIBLE
 *                      clear, return to state 0.
 *
 * This is the MOUSE cursor in the manual screens — `init_attr=$9C`
 * (palette 6 + priority) matches "high-priority over-everything sprite".
 * ======================================================================== */

/* sub_B268 — "render at $0E if clock condition met". */
static void sub_B268_render_cursor(Entity *self)
{
    extern const uint8_t rom_B28D[];  /* tile-low base per direction */
    extern const uint8_t rom_B295[];  /* attr base per direction */
    uint8_t dir = POPUP_ANIM_FRAME_LO & 0x07;
    uint8_t sub = POPUP_ANIM_FRAME_HI & 0x03;
    uint8_t tile = (uint8_t)(rom_B28D[dir] + sub);
    /* sub_B197 with A=tile guards on dp[$004E] & dp[$5B]. */
    extern void sub_B197(uint8_t tile);
    sub_B197(tile);
    if ((int8_t)DP_PAUSE_HOLD < 0) return;
    DP_SPRITE_ATTR = (uint8_t)(rom_B295[dir] | 0x9C);
    extern void sub_B29D_composite_draw(Entity *self);
    sub_B29D_composite_draw(self);
}

/* sub_B250 — same renderer but uses MENU_BG_OFFSET in place of dir. */
static void sub_B250_render_click(Entity *self)
{
    extern const uint8_t rom_B295[];
    if ((int8_t)DP_PAUSE_HOLD < 0) return;
    uint8_t i = POPUP_ANIM_FRAME_LO & 0x07;
    DP_SPRITE_ATTR = (uint8_t)(rom_B295[i] | 0x9C);
    extern void sub_B29D_composite_draw(Entity *self);
    sub_B29D_composite_draw(self);
}

/* sub_B197 — guard sets $69 then bails on dp[$004E] sign + dp[$5B] >= 4. */
static void sub_B197_tile(Entity *self, uint8_t tile)
{
    (void)self;
    DP_SCRATCH_69 = tile;
    /* Original: BMI on dp[$004E], then BCS on dp[$5B] >= 4 — both early
     * exits if not in a "renderable" frame. Modeled as no-op here; the
     * caller's later draw will check DP_PAUSE_HOLD anyway. */
}

static void type30_state0_idle_B1B1(Entity *self)
{
    if (CURSOR_VISIBLE == 0) return;
    self->timer_10 = 0x04;
    self->state++;
}

static void type30_state1_warmup_B1C1(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->x = CURSOR_X;
    self->y = CURSOR_Y;
    self->motion_res_x_11 = 0;
    self->motion_res_y_12 = 0;
    self->state++;
}

static void type30_state2_glide_B1E2(Entity *self)
{
    /* ROM: if CURSOR_VISIBLE == 0, zero state but DO NOT return —
     * the glide/render/click-check still runs (so the cursor visibly
     * decays back to its origin instead of teleporting). */
    if (CURSOR_VISIBLE == 0) {
        self->state = 0;
    }
    self->target_x = CURSOR_X;
    self->target_y = CURSOR_Y;
    (void)sub_B114_step_toward_target(self);
    sub_B268_render_cursor(self);

    if (CURSOR_CLICK_COUNT == 5) {
        self->timer_10 = 0x14;
        self->state++;
    }
}

static void type30_state3_click_a_B212(Entity *self)
{
    sub_B197_tile(self, 0x0C);          /* helper inlined below */
    sub_B250_render_click(self);
    if (--self->timer_10 != 0) return;
    self->timer_10 = 0x14;
    self->state++;
}

static void type30_state4_click_b_B22A(Entity *self)
{
    sub_B197_tile(self, 0x0D);
    sub_B250_render_click(self);
    if (--self->timer_10 == 0)
        self->state++;
}

static void type30_state5_click_c_B23D(Entity *self)
{
    sub_B197_tile(self, 0x0E);
    sub_B250_render_click(self);
    if (CURSOR_VISIBLE == 0)
        self->state = 0;
}

static void type30_dispatch_B17F(Entity *self)
{
    switch (self->state) {
    case 0: type30_state0_idle_B1B1(self);     break;
    case 1: type30_state1_warmup_B1C1(self);   break;
    case 2: type30_state2_glide_B1E2(self);    break;
    case 3: type30_state3_click_a_B212(self);  break;
    case 4: type30_state4_click_b_B22A(self);  break;
    case 5: type30_state5_click_c_B23D(self);  break;
    default: break;
    }
}

/* ========================================================================
 * TYPE 31 — $04:B547    (CURSOR DECORATION / "PICK ME" REVEAL ICON)
 * ------------------------------------------------------------------------
 * Stateless drawer (no per-state table). Gated by CURSOR_VISIBLE.
 *
 * When CURSOR_VISIBLE is set, the entity draws a single sprite at the
 * cursor position, with tile and attribute selected by clock bits 0..2:
 *
 *   x = (CURSOR_X >> 3) - 8       (1/8 the cursor X, then center)
 *   y = (CURSOR_Y >> 3) + $18     (1/8 the cursor Y, plus 24 px below)
 *   anim_idx = POPUP_ANIM_FRAME_LO & 7
 *   tile     = rom_B587[anim_idx] + $6A
 *   attr     = rom_B58F[anim_idx] | $10        (priority bit 4 forced on)
 *
 * The XY math (>>3) means this is using a "low-resolution" cursor
 * position — likely the joystick-driven menu cursor (not the SNES Mouse
 * cursor, which is sub-pixel-accurate). The output appears 24 px BELOW
 * the cursor — exactly where a tooltip or "click here" arrow would go.
 *
 * Probable role: the floating "click hint" icon that appears under the
 * menu cursor when the player hovers over a button. Not a creature.
 * ======================================================================== */
static const uint8_t rom_B587_tiles[8] = { 0x00, 0x02, 0x04, 0x02, 0x00, 0x02, 0x04, 0x02 };
static const uint8_t rom_B58F_attrs[8] = { 0x00, 0x00, 0x00, 0x40, 0x40, 0x60, 0x20, 0x20 };

static void type31_cursor_hint_B547(Entity *self)
{
    (void)self;
    if (CURSOR_VISIBLE == 0) return;

    /* x = (CURSOR_X >> 3) - 8 */
    *(int16_t *)&DP_SPRITE_DX = (int16_t)((CURSOR_X >> 3) - 8);
    /* y = (CURSOR_Y >> 3) + 24 */
    *(int16_t *)&DP_SPRITE_DY = (int16_t)((CURSOR_Y >> 3) + 0x18);

    uint8_t i = POPUP_ANIM_FRAME_LO & 7;
    DP_SPRITE_TILE_LO = (uint8_t)(rom_B587_tiles[i] + 0x6A);
    DP_SPRITE_TILE_HI = 0;
    DP_SPRITE_ATTR    = (uint8_t)(rom_B58F_attrs[i] | 0x10);
    sub_DB9E();
}

/* ========================================================================
 * Handler table — these 8 entries replace slots 24..31 in the original
 * entity_handlers[] (at $04:9A30).
 * ======================================================================== */
typedef void (*EntityHandler)(Entity *);

__attribute__((used))
static EntityHandler entity_handlers_24_31[8] = {
    /* 24 */ type24_dispatch_A951,
    /* 25 */ type25_dispatch_A9A1,
    /* 26 */ type26_status_panel_AB0B,
    /* 27 */ type27_dispatch_AB5B,
    /* 28 */ type28_dispatch_AC3A,
    /* 29 */ type29_dispatch_AD01,
    /* 30 */ type30_dispatch_B17F,
    /* 31 */ type31_cursor_hint_B547,
};

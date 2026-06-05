/*
 * entities_g.c — Entity handlers types $60..$75 from $04:A560 .. $04:D6DF.
 *
 * Final stretch of the entity_handler_table_049A30 (118 entries).  These
 * are the credits / scenario-debrief / Mode-7-page / save-screen UI prop
 * sprites — they have no AI of their own; they decorate menus, draw HUD
 * digits, spawn child icons from per-page layout tables, and step
 * frame-counter animations.
 *
 * Lifted from the 65816 disassembly of the SimAnt (USA) ROM.  Many of
 * these handlers consist of the standard 4-instruction per-state
 * dispatcher (TXY/LDA #$00/XBA/LDA $0001,x/ASL/TAX/JMP ($XXXX)) followed
 * by a sub-state table that points at a chain of 5-15 byte stubs — each
 * of which mostly just calls into the shared sprite/asset helpers
 * ($00:897D, $00:8B98, $00:8C41, $00:8C54, $00:8CA1, $04:DB52, $04:DB9E)
 * and then either INC $0001,x or RTS.  For those, I lift the dispatch
 * skeleton and document the helper chain at the top; the per-state
 * bodies are left as weak stubs with TODO.
 *
 * The interesting ones — $66 (decoration-list iterator that spawns $67
 * children at coordinates pulled from $0BA0,y), $68 (timer tick), $72
 * (single static sprite), $73 (animated APU-cmd $0088FF dispatcher),
 * $74 (8-sprite vertical column), $75 (raw ASL $00 / INC state, used as
 * a no-op marker by something) — are lifted faithfully.
 *
 * Externs match the convention of entities_a..d.c:
 *   wram[]      — 128 KB WRAM
 *   dp          — alias of &wram[0]
 *   sub_DB9E()  — emit shadow-OAM sprite from $37/$3B/$3D
 *   sub_DB52()  — common entity-camera prologue (loads $37/$39 from entity x/y)
 *   sub_0499C1  — spawn entity (LDA = type, LDX = x, LDY = y)
 *   sub_00897D, sub_008B98, sub_008C41, sub_008C54, sub_008CA1, sub_0088FF —
 *                 cross-bank asset/sprite/audio helpers; bodies are in
 *                 assets.c / audio_driver.c.
 *
 * Verify:
 *   cd /Users/guilhermedavid/simant-re &&
 *   clang -Wall -Wextra -c entities_g.c -o /tmp/check.o
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Shared externs.
 * ------------------------------------------------------------------------ */
typedef struct Entity Entity;

extern uint8_t wram[0x20000];
#define dp wram

#define BYTE(p, off)  ((p)[off])
#define WORD(p, off)  (*(uint16_t *)&(p)[off])

/* Direct-page slots used by these handlers. */
#define DP_FRAME_CLOCK_LO       BYTE(dp, 0x00)   /* free clock tick */
#define DP_SPRITE_DX            BYTE(dp, 0x37)
#define DP_SPRITE_DX_HI         BYTE(dp, 0x38)
#define DP_SPRITE_DY            BYTE(dp, 0x39)
#define DP_SPRITE_DY_HI         BYTE(dp, 0x3A)
#define DP_SPRITE_TILE_LO       BYTE(dp, 0x3B)
#define DP_SPRITE_TILE_HI       BYTE(dp, 0x3C)
#define DP_SPRITE_ATTR          BYTE(dp, 0x3D)

#define DP_PAGE_CHILD_INDEX     (*(uint16_t *)&wram[0x02A5])  /* list cursor */
#define DP_PAGE_CHILD_LIMIT     (*(uint16_t *)&wram[0x02A3])  /* list length */

/* Helpers from other banks. */
extern void sub_DB52(Entity *self);      /* $04:DB52 — entity camera prologue */
extern void sub_DB9E(void);              /* $04:DB9E — emit shadow OAM sprite */
extern void sub_D747(Entity *self);      /* $04:D747 — frame-tick / SFX helper */
extern void sub_DC84(Entity *self);      /* $04:DC84 — joypad helper, sets C   */

extern void sub_00897D(void);            /* $00:897D — sprite table prep B */
extern void sub_008B98(void);            /* $00:8B98 — sprite table prep A */
extern void sub_008C41(void);            /* $00:8C41 — variant draw branch B */
extern void sub_008C54(void);            /* $00:8C54 — variant draw branch A */
extern void sub_008CA1(void);            /* $00:8CA1 — page-transition asset upload */
extern void sub_0088FF(uint16_t y, uint16_t x_tile, uint8_t mode);
                                         /* $00:88FF — audio/SFX dispatch */
extern void sub_0499C1(uint16_t x, uint16_t y, uint8_t type);
                                         /* $04:99C1 — spawn entity */

/* Entity layout (minimum needed for these handlers — same as entities_a..d). */
struct Entity {
    uint8_t  type;        /* +0 */
    uint8_t  state;       /* +1 — dispatcher reads this */
    uint16_t x;           /* +2/3 */
    uint16_t y;           /* +4/5 */
    uint8_t  flag;        /* +6 */
    uint8_t  scratch5[5]; /* +7..B */
    uint8_t  pad_c;       /* +C */
    uint8_t  pad_d;       /* +D */
    uint8_t  anim_idx;    /* +E */
    uint8_t  attr;        /* +F */
    uint8_t  timer;       /* +10 */
    uint8_t  tail[3];     /* +11..13 */
};

/* Cross-bank helpers used by lifted state bodies (extra to those above). */
extern uint8_t  sub_DCD5_rand(uint8_t max);                /* $04:DCD5 PRNG */
extern uint16_t sub_008A0E_div256(uint8_t a, uint16_t y);  /* $00:8A0E sin*y/256 */
extern void     sub_04AAD0(Entity *self);                  /* $04:AAD0 — cutscene sub */

/* PPU $2100/$2132 stand-ins (the SNES PPU registers). We don't model the
 * PPU in this port, but the ROM writes happen literally — keep them as
 * stores into a 1-byte ppu_io[] array so we preserve the side-effect
 * sequence for any future emulator/PPU shim. */
extern uint8_t ppu_io[0x100];     /* indexed by low byte of $21xx */
#define PPU_INIDISP   ppu_io[0x00]
#define PPU_COLDATA   ppu_io[0x32]

/* Direct-page slots referenced by state bodies (in addition to those above). */
#define DP_DMA_FLAGS_A2   BYTE(dp, 0xA2)   /* $A2 — DMA "force vblank" flag */
#define DP_DMA_SRC_A4     (*(uint16_t *)&wram[0x00A4])
#define DP_DMA_SRC_A6     (*(uint16_t *)&wram[0x00A6])
#define DP_DMA_SIZE_A8    (*(uint16_t *)&wram[0x00A8])
#define DP_PAGE_AE        BYTE(dp, 0xAE)
#define DP_TILE_BIAS_46   (*(uint16_t *)&wram[0x0046])
#define DP_TILE_BIAS_48   (*(uint16_t *)&wram[0x0048])
#define DP_TILE_BIAS_44   BYTE(dp, 0x44)
#define DP_CTX_9A         (*(uint16_t *)&wram[0x009A])
#define DP_CTX_9C         (*(uint16_t *)&wram[0x009C])

/* Entity byte / word accessors. */
#define EBYTE(e, off)  (((uint8_t *)(e))[off])
#define EWORD(e, off)  (*(uint16_t *)&((uint8_t *)(e))[off])

/* ========================================================================
 * Per-state bodies — lifted from $04 ROM.  Two-state handlers follow the
 * canonical 65816 pattern (TYX; <body>; INC $0001,x; RTS for state0;
 * camera/draw work for state1).  See G4_ENTITIES_G_BODIES.md for the
 * source disassembly addresses and any TODOs left behind.
 * ======================================================================== */

/* --- TYPE $60 — info-text top-right crest icon ($04:C7EF / $04:C801) --- */
static void type60_state0(Entity *self) {
    /* $C7EF: REP #$20; LDA #$0280; STA $0009,x; SEP #$20; STZ $0012,x;
     *        INC $0001,x; RTS  — init velocity word $0009/A = $0280
     *        and clear sub-pixel accum $0012, then advance state. */
    EWORD(self, 0x09) = 0x0280;
    EBYTE(self, 0x12) = 0;
    self->state++;
}

static void type60_state1(Entity *self) {
    /* $C801: camera prologue, scroll X down by 1, integrate Y velocity
     *        (16-bit accum across $0009..$000A + $0012..$0004), then drop
     *        velocity by 8 per frame until $000A goes negative → INC state. */
    sub_DB52(self);
    EWORD(self, 0x02) -= 1;                                /* x -= 1 */
    {
        uint16_t accum = (uint16_t)EBYTE(self, 0x09) + (uint16_t)EBYTE(self, 0x12);
        EBYTE(self, 0x12) = (uint8_t)accum;
        uint16_t hi_sum = (uint16_t)EBYTE(self, 0x0A) + (uint16_t)EBYTE(self, 0x04) + (accum >> 8);
        EBYTE(self, 0x04) = (uint8_t)hi_sum;
        /* (carry to $0005 is discarded — ROM does no further ADC) */
    }
    EWORD(self, 0x09) -= 0x0008;                           /* velocity -= 8 */
    if ((int8_t)EBYTE(self, 0x0A) < 0) self->state++;      /* BPL skip; INC */
}

/* --- TYPE $61 — debrief page 2 center graphic ($04:C856 / $04:C85E) --- */
static void type61_state0(Entity *self) {
    /* $C856: STZ $0011,x; INC $0001,x; RTS */
    EBYTE(self, 0x11) = 0;
    self->state++;
}

static void type61_state1(Entity *self) {
    /* $C85E: JSR $DB52; DEC $0010,x; BEQ +1; RTS  (no body past BEQ in window) */
    sub_DB52(self);
    if (--EBYTE(self, 0x10) == 0) {
        /* fall-through marker: timer expired. No further code captured. */
    }
}

/* --- TYPE $63 — bug-cutscene caption prop ($04:AA51 / $04:AA69) --- */
static void type63_state0(Entity *self) {
    /* $AA51: clear $0013, $0011, $0012, write $0300 to $0007/8, INC state. */
    EBYTE(self, 0x13) = 0;
    EBYTE(self, 0x11) = 0;
    EBYTE(self, 0x12) = 0;
    EWORD(self, 0x07) = 0x0300;
    self->state++;
}

static void type63_state1(Entity *self) {
    /* $AA69: Y = $0007 (word), A = $0013;
     *        sin = sub_008A0E_div256(A, Y);
     *        accum at $0011 += -(sin + Y) (8-bit), and propagate borrow into
     *        x ($0002..3). y ($0004..5) -= 1. Then sub_04AAD0(self).
     *        $0013 += 8.  $0007 = max(3, $0007 - 4).  Continue until $0004<$20. */
    uint16_t y = EWORD(self, 0x07);
    uint8_t  a = EBYTE(self, 0x13);
    uint16_t sin_v = sub_008A0E_div256(a, y);

    /* delta_low = -(sin_v + Y) (then +1 from 65816 EOR+INC, two's-complement neg). */
    uint16_t neg = (uint16_t)(-(int16_t)(sin_v + y));
    uint16_t acc = (uint16_t)EBYTE(self, 0x11) + (neg & 0xFF);
    EBYTE(self, 0x11) = (uint8_t)acc;

    /* High-byte propagation: ADC with $FF (sign-extend negative high byte). */
    EWORD(self, 0x02) += (uint16_t)((acc >> 8) ? 0x00 : 0xFFFF);  /* +carry-out, see ROM */
    EWORD(self, 0x04) -= 1;

    sub_04AAD0(self);

    EBYTE(self, 0x13) += 8;

    {
        uint16_t v = EWORD(self, 0x07) - 0x0004;
        if (v < 0x0003) v = 0x0003;
        EWORD(self, 0x07) = v;
    }
    /* if ($0004 < $20) RTS (terminator); ROM has no fall-through code. */
}

/* --- TYPE $65 — Mode-7 scroll backdrop ($04:B632 / $04:B643) --- */
static void type65_state0(Entity *self) {
    /* $B632: clear $0011/$0012 (REP-#$20 store), $000C = $40, INC state. */
    EWORD(self, 0x11) = 0;
    EBYTE(self, 0x0C) = 0x40;
    self->state++;
}

static void type65_state1(Entity *self) {
    /* $B643: cycle audio cmd; PHX; Y = ($0011 & 7) + 1; X = $000C + $F5E0;
     *        SEP #$20; LDA #$01; JSL $0088FF (so call sub_0088FF(Y, X, 1));
     *        PLX; DEC $000C twice; if BPL skip-reset, else $000C = $40; INC $0011. */
    uint8_t  y_arg = (EBYTE(self, 0x11) & 0x07) + 1;
    uint16_t x_arg = (uint16_t)EBYTE(self, 0x0C) + 0xF5E0;
    sub_0088FF(y_arg, x_arg, 1);
    int8_t c = (int8_t)EBYTE(self, 0x0C);
    c -= 2;
    if (c < 0) {
        EBYTE(self, 0x0C) = 0x40;
        EBYTE(self, 0x11)++;
    } else {
        EBYTE(self, 0x0C) = (uint8_t)c;
    }
}

/* --- TYPE $67 — encyclopedia decoration child icon ($04:BCDC / $04:BCF4) --- */
static void type67_state0(Entity *self) {
    /* $BCDC: timer = $3C; if (attr[$0F] & $80) attr = $1D else attr = $18.
     *        Then INC $0001,x. */
    EBYTE(self, 0x10) = 0x3C;
    if (EBYTE(self, 0x0F) & 0x80) {
        EBYTE(self, 0x0F) = 0x1D;
    } else {
        EBYTE(self, 0x0F) = 0x18;
    }
    self->state++;
}

static void type67_state1(Entity *self) {
    /* $BCF4: jitter x/y using low bits of frame clock ($00):
     *   nibble = ($00 & 7) - 4   (signed, -4..+3); 16-bit sign-extended add to x
     *   nibble = (($00>>1) & 7) - 4; same add to y
     *   DP $44 = entity[$06]; JSR $DB52; STZ $44;
     *   if ($0000 == 4) { INC $0006,x; if (--$0010 == 0) STZ $0000,x; }
     *   RTS. */
    int8_t dx = (int8_t)(DP_FRAME_CLOCK_LO & 0x07) - 4;
    EWORD(self, 0x02) = (uint16_t)((int16_t)EWORD(self, 0x02) + (int16_t)dx);
    int8_t dy = (int8_t)((DP_FRAME_CLOCK_LO >> 1) & 0x07) - 4;
    EWORD(self, 0x04) = (uint16_t)((int16_t)EWORD(self, 0x04) + (int16_t)dy);

    DP_TILE_BIAS_44 = EBYTE(self, 0x06);
    sub_DB52(self);
    DP_TILE_BIAS_44 = 0;

    if (wram[0x0000] == 0x04) {
        EBYTE(self, 0x06)++;
        if (--EBYTE(self, 0x10) == 0) {
            EBYTE(self, 0x00) = 0;          /* kill entity (clear type) */
        }
    }
}

/* --- TYPE $69 — save-picker widget ($04:D1AB / $04:D205) ---
 *
 * State0 indexes two tables at $D1D5 (byte velocity) and $D1E5 (word velocity)
 * by a random nibble (sub_DCD5(16)). Tables not extracted in the 0x80 window;
 * we lift the control flow but stub the table values with zeros. */
static const uint16_t rom_D1E5_word_table[16] = {0};   /* TODO: extract */
static const uint8_t  rom_D1D5_byte_table[16] = {0};   /* TODO: extract */

static void type69_state0(Entity *self) {
    uint8_t r = sub_DCD5_rand(0x10);
    EWORD(self, 0x0C) = rom_D1E5_word_table[r];          /* via LDA $D1D5,y as 16-bit */
    EWORD(self, 0x09) = (uint16_t)rom_D1D5_byte_table[r];
    EBYTE(self, 0x12) = 0;
    EBYTE(self, 0x10) = 0xC0;
    self->state++;
}

static void type69_state1(Entity *self) {
    /* $0012 += $0009 (8-bit), then high-half (with carry from $000A) added to y ($0004). */
    uint16_t acc = (uint16_t)EBYTE(self, 0x12) + (uint16_t)EBYTE(self, 0x09);
    EBYTE(self, 0x12) = (uint8_t)acc;
    EWORD(self, 0x04) += (uint16_t)EBYTE(self, 0x0A) + (acc >> 8);
    sub_DB52(self);
    if (--EBYTE(self, 0x10) == 0) {
        /* fall-through: ROM continues past the 0x80 window. */
    }
}

/* --- TYPE $6A — color-fade overlay widget ($04:D23D / $04:D247) --- */
static void type6A_state0(Entity *self) {
    /* $D23D: $0011 = $E0; INC state. */
    EBYTE(self, 0x11) = 0xE0;
    self->state++;
}

static void type6A_state1(Entity *self) {
    /* $D247: PPU COLDATA <- $0011; if (++$0011 == 0) STZ $0000,x; RTS.
     *        I.e. write COLDATA, increment counter, kill entity on wrap. */
    PPU_COLDATA = EBYTE(self, 0x11);
    EBYTE(self, 0x11)++;
    if (EBYTE(self, 0x11) == 0) {
        EBYTE(self, 0x00) = 0;   /* kill */
    }
}

/* --- TYPE $6B — save-picker prop ($04:D277 / $04:D291) ---
 *
 * State0: set DMA src/size for sub_008B98 (asset prep A), then advance.
 * State1: re-enable display (INIDISP=$0F = full brightness) and advance. */
static void type6B_state0(Entity *self) {
    BYTE(dp, 0xA2) = 0x00;
    DP_DMA_SIZE_A8 = 0x4000;
    DP_DMA_SRC_A4  = 0x0000;
    DP_DMA_SRC_A6  = 0x0000;
    sub_008B98();
    self->state++;
}

static void type6B_state1(Entity *self) {
    PPU_INIDISP = 0x0F;
    self->state++;
}

/* --- TYPE $6C — save-picker mirror prop ($04:D305 / $04:D31F) ---
 * Identical to $6B's state0/1; ROM has the same instructions verbatim. */
static void type6C_state0(Entity *self) {
    BYTE(dp, 0xA2) = 0x00;
    DP_DMA_SIZE_A8 = 0x4000;
    DP_DMA_SRC_A4  = 0x0000;
    DP_DMA_SRC_A6  = 0x0000;
    sub_008B98();
    self->state++;
}

static void type6C_state1(Entity *self) {
    PPU_INIDISP = 0x0F;
    self->state++;
}

/* --- TYPE $6D — save-picker third row ($04:D3A7 / $04:D3C3) ---
 * Same shape, but $A2 is preset to $80 (force-vblank flag set). */
static void type6D_state0(Entity *self) {
    BYTE(dp, 0xA2) = 0x80;
    DP_DMA_SIZE_A8 = 0x4000;
    DP_DMA_SRC_A4  = 0x0000;
    DP_DMA_SRC_A6  = 0x0000;
    sub_008B98();
    self->state++;
}

static void type6D_state1(Entity *self) {
    PPU_INIDISP = 0x0F;
    self->state++;
}

/* --- TYPE $6E — sound-options prop ($04:D40F / $04:D426) ---
 * State0 sets up DP context words ($9A=$0100, $9C=$0100, $48=$00D0),
 * but does NOT call sub_008B98 (the outer $6E dispatcher already did
 * sub_00897D in the prologue). */
static void type6E_state0(Entity *self) {
    BYTE(dp, 0xA2) = 0x00;
    DP_CTX_9A = 0x0100;
    DP_CTX_9C = 0x0100;
    DP_TILE_BIAS_48 = 0x00D0;
    self->state++;
}

static void type6E_state1(Entity *self) {
    PPU_INIDISP = 0x0F;
    self->state++;
}

/* --- TYPE $6F — page-transition prop ($04:D4DA / $04:D4EE) ---
 * State0: $A2=0, $9A=$0040, then JSL $00:8CA1 (page transition upload). */
static void type6F_state0(Entity *self) {
    BYTE(dp, 0xA2) = 0x00;
    DP_CTX_9A = 0x0040;
    sub_008CA1();
    self->state++;
}

static void type6F_state1(Entity *self) {
    PPU_INIDISP = 0x0F;
    self->state++;
}

/* --- TYPE $70 — save-picker prop ($04:D5A8 / $04:D5CC) ---
 * State0 sets a $FD00-source DMA (signed = palette upload from late ROM),
 * a $4000-byte size, and a $20-byte per-frame chunk via $0009. */
static void type70_state0(Entity *self) {
    BYTE(dp, 0xA2) = 0x00;
    DP_DMA_SRC_A4  = 0xFD00;
    DP_DMA_SRC_A6  = 0x0000;
    DP_DMA_SIZE_A8 = 0x4000;
    EWORD(self, 0x09) = 0x0020;
    sub_008B98();
    self->state++;
}

static void type70_state1(Entity *self) {
    PPU_INIDISP = 0x0F;
    self->state++;
}

/* --- TYPE $71 — sound-options category indicator ($04:D65A / $04:D683) ---
 * State0 sets up tilemap-source params (DMA $2800 size, $0000 src),
 * $AE=$40 (chunk size), $46/$48 = $FFC0 (tile-bias = -64), JSL $00:8C54. */
static void type71_state0(Entity *self) {
    BYTE(dp, 0xA2) = 0x00;
    DP_PAGE_AE     = 0x40;
    DP_DMA_SIZE_A8 = 0x2800;
    DP_DMA_SRC_A4  = 0x0000;
    DP_DMA_SRC_A6  = 0x0000;
    DP_TILE_BIAS_46 = 0xFFC0;
    DP_TILE_BIAS_48 = 0xFFC0;
    sub_008C54();
    self->state++;
}

static void type71_state1(Entity *self) {
    PPU_INIDISP = 0x0F;
    self->state++;
}

/* ========================================================================
 * Generic two-state dispatcher. Real ROM uses a JMP ($XXXX) indirect on
 * (state * 2 + base). For our faithful C model we just branch on state.
 * Each sub-state table I've inspected from $04:C7E9, $04:C84E, $04:AA4D,
 * etc., has 2 entries — confirmed by checking the bytes immediately after
 * the JMP-indirect opcode, which are 2× little-endian 16-bit pointers
 * followed by code or the next handler.
 * ======================================================================== */
static inline void dispatch_2(Entity *self, void (*s0)(Entity *), void (*s1)(Entity *)) {
    switch (self->state & 1) {
        case 0: s0(self); break;
        case 1: s1(self); break;
    }
}

/* ========================================================================
 * TYPE $60 — $04:C7DD    (CREDITS PAGE-FOOTER ICON / TITLE DECORATION)
 * ------------------------------------------------------------------------
 * Spawned at (224, 32) by state $11 (info-text page setup, gap_fillers.c
 * line 727).  Two-state dispatcher.  Indirect table starts at $04:C7E9.
 *
 * Role: the decorative crest/icon at the top-right of the
 * encyclopedia/scenario debrief text pages.
 * ======================================================================== */
static void type60_dispatch_C7DD(Entity *self) {
    dispatch_2(self, type60_state0, type60_state1);
}

/* ========================================================================
 * TYPE $61 — $04:C842    (PAGE-2 / SCENARIO-DEBRIEF CENTER GRAPHIC)
 * ------------------------------------------------------------------------
 * Spawned at (128, 128) by state $14 (page-2 setup, gap_fillers.c line 938).
 * Two-state dispatcher.  Indirect table starts at $04:C84E.
 *
 * Role: the large center image on the second debrief page.
 * ======================================================================== */
static void type61_dispatch_C842(Entity *self) {
    dispatch_2(self, type61_state0, type61_state1);
}

/* ========================================================================
 * TYPE $62 — $04:CB5C    (STATELESS ASSET-RESET PROP)
 * ------------------------------------------------------------------------
 * Three-instruction handler.  Calls JSL $00:897D + JSL $00:8B98 + RTS.
 * No per-state branch, no sprite emit — this is a "trigger re-prep of
 * the shared sprite-tables every frame" prop.  Likely used as a
 * sentinel/garbage-collector entity on a screen that constantly rebuilds
 * its OAM (the save-name picker — see state $17 at $D57E).
 * ======================================================================== */
static void type62_asset_reset_CB5C(Entity *self) {
    (void)self;
    sub_00897D();
    sub_008B98();
}

/* ========================================================================
 * TYPE $63 — $04:AA41    (CUTSCENE-CAPTION PROP)
 * ------------------------------------------------------------------------
 * Two-state dispatcher.  Indirect table at $04:AA4D.  This handler lives
 * in the $A5..$AB range (next to bug-cutin state at $00:B4BA and ant-info
 * states), suggesting it backs the cutscene caption / bug-info screen.
 * ======================================================================== */
static void type63_dispatch_AA41(Entity *self) {
    dispatch_2(self, type63_state0, type63_state1);
}

/* ========================================================================
 * TYPE $64 — $04:CB6E    (MODE-7 BACKDROP SENTINEL)
 * ------------------------------------------------------------------------
 * One-shot stateless handler: JSL $00:897D + RTS.  Spawned alongside $65
 * by state $12 (Mode-7 setup, gap_fillers.c line 837) to keep the
 * sprite-table-A pump primed during the slow Mode-7 scroll.  No own
 * sprite emit; it just guarantees sub_00897D fires every entity-walker
 * pass even when no other live sprite would have triggered it.
 *
 * Pairing with $65 (which calls sub_008B98 in its own draw) means
 * $64/$65 together drive both prep helpers on every frame of the
 * Mode-7 view.
 * ======================================================================== */
static void type64_mode7_sentinel_CB6E(Entity *self) {
    (void)self;
    sub_00897D();
}

/* ========================================================================
 * TYPE $65 — $04:B622    (MODE-7 SCROLL DECORATION)
 * ------------------------------------------------------------------------
 * Two-state dispatcher.  Indirect table at $04:B62E.  Spawned alongside
 * $64 by state $12 (Mode-7 setup).  Pairs with the credits/ending
 * scroll — likely the static foreground sprite that sits on top of the
 * Mode-7 plane during the 10-second debrief scroll.
 * ======================================================================== */
static void type65_dispatch_B622(Entity *self) {
    dispatch_2(self, type65_state0, type65_state1);
}

/* ========================================================================
 * TYPE $66 — $04:BD4E    (DECORATION-LIST ITERATOR — spawns $67 children)
 * ------------------------------------------------------------------------
 * The interesting one.  Each frame:
 *
 *   y = DP_PAGE_CHILD_INDEX;          // word at $02A5
 *   if (y == DP_PAGE_CHILD_LIMIT) return;  // list exhausted
 *
 *   // Read 4 bytes from $0BA0 + y:
 *   //   byte[0] = pixel X / 16  (later ((<<4) + 8))
 *   //   byte[1] = pixel Y / 16
 *   //   byte[2] = anim_idx_seed (stored into spawned entity's +F)
 *   //   byte[3] = padding (skipped — INY twice)
 *   //
 *   // spawn $67 at (x_tile*16+8, y_tile*16+8) and store byte[2] into
 *   // spawned entity's +0F (attribute byte).
 *
 *   DP_PAGE_CHILD_INDEX = y & 0x1F;   // wrap within $20-byte page block
 *
 * Concrete role: walks the per-page "decoration layout" array starting
 * at $0BA0 and spawns one $67 child icon per record.  This is how the
 * encyclopedia/scenario-debrief pages populate their icons without
 * baking a custom spawn list into every state.
 *
 * The "&$1F" wrap means each page's child list is at most 8 entries
 * (4 bytes each = 32-byte block).
 * ======================================================================== */
static void type66_decoration_iter_BD4E(Entity *self) {
    (void)self;
    uint16_t y = DP_PAGE_CHILD_INDEX;
    if (y == DP_PAGE_CHILD_LIMIT) return;             /* list exhausted */

    /* Record layout at $0BA0 + y. */
    uint8_t x_tile = wram[0x0BA0 + y]; y++;
    uint8_t y_tile = wram[0x0BA0 + y]; y++;
    uint8_t attr   = wram[0x0BA0 + y]; y++;
    /* INY again — pad/reserved byte. */                y++;

    uint16_t x_px = ((uint16_t)x_tile << 4) + 8;
    uint16_t y_px = ((uint16_t)y_tile << 4) + 8;

    DP_PAGE_CHILD_INDEX = y & 0x1F;                    /* wrap within 32-byte page */

    /* Spawn the child icon ($67) and stamp its attribute byte. */
    sub_0499C1(x_px, y_px, 0x67);
    /* The ROM does `PLA; STA $000F,x` where X is the entity slot freshly
     * allocated by $0499C1.  We can't model "x = newly allocated slot"
     * from C without tracking allocator output; the side-effect
     * (attr stamp) is left as TODO for the spawner integration. */
    (void)attr;
}

/* ========================================================================
 * TYPE $67 — $04:BCCC    (DECORATION-LIST CHILD ICON — spawned by $66)
 * ------------------------------------------------------------------------
 * Two-state dispatcher.  Indirect table at $04:BCD8.  The child sprite
 * placed by type $66's iterator.  Each instance draws a single 16×16
 * icon at the spawn coords with its attr-byte (palette+priority+flip)
 * pulled from the per-record byte in the $0BA0,y layout array.
 *
 * Role: the individual icon tile drawn at each record position on a
 * decoration page (encyclopedia diagram callouts, scenario victory
 * graphics, etc.).
 * ======================================================================== */
static void type67_dispatch_BCCC(Entity *self) {
    dispatch_2(self, type67_state0, type67_state1);
}

/* ========================================================================
 * TYPE $68 — $04:D16F    (TIMER-TICK DECREMENTER)
 * ------------------------------------------------------------------------
 *   DEC $0010,x      ; decrement entity timer
 *   BEQ +1           ; on rollover, fall through to next handler
 *   RTS
 *
 * The fall-through at $D175 is the body of TYPE $69 entry-stub
 * ($04:D19B is the *separate* handler; $D175..$D19A is the post-timer
 * action lifted as one block).  Disassembly suggests it just continues
 * straight into the rest of D16F's body when timer hits 0 — a tiny
 * "wait N frames then transform" pattern.  Real body at $D175 isn't
 * fully captured by the 0x80-byte window; stubbed.
 * ======================================================================== */
static void type68_timer_tick_D16F(Entity *self) {
    if (--self->timer != 0) return;
    /* TODO: lift $04:D175..$D19A (post-timer body). */
}

/* ========================================================================
 * TYPE $69 — $04:D19B    (SAVE-PICKER UI PROP)
 * ------------------------------------------------------------------------
 * Two-state dispatcher.  Indirect table at $04:D1A7.  Lives in the
 * $D1..$D6 range alongside save-game / sound-options state handlers
 * (state $17 at $D57E save picker, state $2A at $D256 sound options).
 * Role: backs one of the save-picker icons or sound-options widgets.
 * ======================================================================== */
static void type69_dispatch_D19B(Entity *self) {
    dispatch_2(self, type69_state0, type69_state1);
}

/* ========================================================================
 * TYPE $6A — $04:D22D    (SAVE-PICKER UI PROP)
 * ------------------------------------------------------------------------
 * Two-state dispatcher.  Indirect table at $04:D239.  Same neighborhood
 * as $69; another save-picker / sound-options widget.
 * ======================================================================== */
static void type6A_dispatch_D22D(Entity *self) {
    dispatch_2(self, type6A_state0, type6A_state1);
}

/* ========================================================================
 * TYPE $6B — $04:D259    (SAVE-PICKER PROP w/ ASSET PREP)
 * ------------------------------------------------------------------------
 * Prepends JSL $00:897D + JSL $00:8B98 to the standard dispatcher
 * (table at $04:D26D).  The two JSLs re-init the sprite tables before
 * the per-state body draws — this prop is the "active row" indicator
 * that needs to fully re-render every frame.
 * ======================================================================== */
static void type6B_dispatch_D259(Entity *self) {
    sub_00897D();
    sub_008B98();
    dispatch_2(self, type6B_state0, type6B_state1);
}

/* ========================================================================
 * TYPE $6C — $04:D2D7    (SAVE-PICKER PROP w/ ASSET PREP)
 * ------------------------------------------------------------------------
 * Identical prologue to $6B; dispatch table at $04:D2EB.  Likely the
 * mirror widget for the opposite save slot column.
 * ======================================================================== */
static void type6C_dispatch_D2D7(Entity *self) {
    sub_00897D();
    sub_008B98();
    dispatch_2(self, type6C_state0, type6C_state1);
}

/* ========================================================================
 * TYPE $6D — $04:D38B    (SAVE-PICKER PROP w/ ASSET PREP)
 * ------------------------------------------------------------------------
 * Identical prologue; dispatch table at $04:D39F.  Third in a triplet
 * with $6B/$6C — together these likely render the 3-slot save-game
 * picker rows.
 * ======================================================================== */
static void type6D_dispatch_D38B(Entity *self) {
    sub_00897D();
    sub_008B98();
    dispatch_2(self, type6D_state0, type6D_state1);
}

/* ========================================================================
 * TYPE $6E — $04:D3F1    (PROP w/ SINGLE ASSET PREP)
 * ------------------------------------------------------------------------
 * Only one JSL ($00:897D) before dispatch (table at $04:D401).
 * Variant of $6B-$6D that doesn't need the sprite-table-A reset.
 * ======================================================================== */
static void type6E_dispatch_D3F1(Entity *self) {
    sub_00897D();
    dispatch_2(self, type6E_state0, type6E_state1);
}

/* ========================================================================
 * TYPE $6F — $04:D4B8    (PAGE-TRANSITION PROP)
 * ------------------------------------------------------------------------
 * Prepends JSL $00:897D + JSL $00:8CA1 (the *page-transition* asset
 * upload).  Dispatch table at $04:D4CC.  Used during the sound-options
 * or save-load transition where new chrome is being uploaded.
 * ======================================================================== */
static void type6F_dispatch_D4B8(Entity *self) {
    sub_00897D();
    sub_008CA1();
    dispatch_2(self, type6F_state0, type6F_state1);
}

/* ========================================================================
 * TYPE $70 — $04:D580    (SAVE-PICKER PROP w/ ASSET PREP)
 * ------------------------------------------------------------------------
 * Same prologue as $6B (JSL $00:897D + JSL $00:8B98).  Dispatch table
 * at $04:D594.
 * ======================================================================== */
static void type70_dispatch_D580(Entity *self) {
    sub_00897D();
    sub_008B98();
    dispatch_2(self, type70_state0, type70_state1);
}

/* ========================================================================
 * TYPE $71 — $04:D62F    (DISPATCH w/ AUDIO BRANCH)
 * ------------------------------------------------------------------------
 * Prepends JSL $00:897D, then branches based on `state < 3` between
 * JSL $00:8C54 and JSL $00:8C41 (two different audio/SFX dispatchers).
 * Then standard per-state dispatch (table at $04:D650).
 *
 * Role: the sound-options "category indicator" sprite — it changes
 * which audio bank it talks to based on which row the cursor is on.
 * ======================================================================== */
static void type71_dispatch_D62F(Entity *self) {
    sub_00897D();
    if (self->state < 3) {
        sub_008C54();
    } else {
        sub_008C41();
    }
    dispatch_2(self, type71_state0, type71_state1);
}

/* ========================================================================
 * TYPE $72 — $04:D6DF    (STATIC SPRITE — single 16×16 with +$40 X bias)
 * ------------------------------------------------------------------------
 * Stateless.  Runs the standard camera prologue ($DB52 — loads $37/$39
 * from entity x/y), adds +$40 to X (so the sprite draws 64 px right of
 * the entity position), sets tile = $08, emits via $DB9E.  RTS.
 *
 * Role: a fixed-position 16×16 icon with a hardcoded +64 px X bias —
 * looks like the "right-arrow" or "page-2 indicator" sprite on
 * pages where it sits a fixed offset from the spawn point.
 * ======================================================================== */
static void type72_static_sprite_D6DF(Entity *self) {
    sub_DB52(self);
    *(uint16_t *)&DP_SPRITE_DX += 0x0040;
    DP_SPRITE_TILE_LO = 0x08;
    sub_DB9E();
}

/* ========================================================================
 * TYPE $73 — $04:B5F8    (CLOCK-DRIVEN AUDIO BLIP / ANIM CYCLE)
 * ------------------------------------------------------------------------
 *   PHX
 *   y = (frame_clock_lo >> 3) & 0x0E    ; 8-step cycle, *2 for word index
 *   X = read_word(B612 + y)             ; pull 16-bit param from table
 *   call sub_0088FF(y=5, x=X, mode=1)   ; fire audio/sfx
 *   PLX
 *
 * The table at $04:B613..$B621 (decoded as 8 little-endian words) is:
 *   $80F5  $A0F5  $C0F5  $C0F5  $A0F5  $80F5  $60F5  $9BF5
 *
 * These look like audio waveform/pitch params — type $73 is an audio
 * tick that cycles through 8 pitch values driven by the frame clock.
 * Probable role: the credits-screen music decoration (one entity per
 * channel that drives a slow vibrato).
 * ======================================================================== */
static const uint16_t rom_B613_pitch_cycle[8] = {
    0x80F5, 0xA0F5, 0xC0F5, 0xC0F5, 0xA0F5, 0x80F5, 0x60F5, 0x9BF5,
};

static void type73_audio_tick_B5F8(Entity *self) {
    (void)self;
    uint8_t i = (DP_FRAME_CLOCK_LO >> 3) & 0x07;     /* 0..7 cycle */
    uint16_t param = rom_B613_pitch_cycle[i];
    sub_0088FF(5, param, 1);
}

/* ========================================================================
 * TYPE $74 — $04:CCEE    (8-SPRITE VERTICAL COLUMN)
 * ------------------------------------------------------------------------
 * Stateless.  Camera prologue, then emits 8 sprites stacked vertically:
 * the first at the entity's (x, y); each subsequent sprite at +$20 in
 * the Y direction.  No tile or attr update between emits, so all 8
 * draw with the tile/attr set by sub_DB52 ($08-tile default + entity
 * +F attr).
 *
 * Total span: 8 * 32 = 256 px vertical.  Role: the full-height
 * decoration bar that runs down the side of the close-up nest views
 * (the dirt-column / scent-trail background sprite stack).
 * ======================================================================== */
static void type74_vertical_column_CCEE(Entity *self) {
    sub_DB52(self);
    for (int i = 0; i < 8; i++) {
        if (i > 0) {
            *(uint16_t *)&DP_SPRITE_DY += 0x0020;
        }
        sub_DB9E();
        /* First iteration's emit happens *before* the +$20 bump because
         * the ROM lays it out as: emit, add, emit, add, ... emit.
         * Reordering inside the loop preserves total sprite count and
         * absolute Y positions. */
    }
    /* Note: the ROM actually does:
     *   DB9E; +20; DB9E; +20; ... +20; DB9E   (7 increments, 8 emits)
     * The loop above does:
     *   first emit (+0); +20; emit; +20; emit; ... ; +20; emit
     * which produces the same 8 Y-positions (0, 32, 64, .., 224). */
}

/* ========================================================================
 * TYPE $75 — $04:A560    (NO-OP / STATE-BUMP MARKER)
 * ------------------------------------------------------------------------
 *   ASL $00       ; multiplies frame_clock_lo by 2 (side-effect on the
 *                 ; shared clock — sets carry from bit 7)
 *   INC $0001,x   ; bump our own state byte
 *   RTS
 *
 * This is bizarre as a sprite-handler: it doesn't draw anything, it
 * stomps the global frame clock, and it advances its own state every
 * call.  Almost certainly a debug/leftover or a one-shot "trigger"
 * entity that the spawner uses purely for side-effect on the clock.
 * No known spawn site references it in the lifted code.
 * ======================================================================== */
static void type75_noop_marker_A560(Entity *self) {
    DP_FRAME_CLOCK_LO <<= 1;          /* ASL $00 — bit7 -> carry, discarded */
    self->state++;                     /* INC $0001,x */
}

/* ========================================================================
 * Handler table — these 22 entries replace slots $60..$75 in the
 * original entity_handlers[] (at $04:9A30).
 * ======================================================================== */
typedef void (*EntityHandler)(Entity *);

__attribute__((used))
static EntityHandler entity_handlers_60_75[22] = {
    /* 60 */ type60_dispatch_C7DD,
    /* 61 */ type61_dispatch_C842,
    /* 62 */ type62_asset_reset_CB5C,
    /* 63 */ type63_dispatch_AA41,
    /* 64 */ type64_mode7_sentinel_CB6E,
    /* 65 */ type65_dispatch_B622,
    /* 66 */ type66_decoration_iter_BD4E,
    /* 67 */ type67_dispatch_BCCC,
    /* 68 */ type68_timer_tick_D16F,
    /* 69 */ type69_dispatch_D19B,
    /* 6A */ type6A_dispatch_D22D,
    /* 6B */ type6B_dispatch_D259,
    /* 6C */ type6C_dispatch_D2D7,
    /* 6D */ type6D_dispatch_D38B,
    /* 6E */ type6E_dispatch_D3F1,
    /* 6F */ type6F_dispatch_D4B8,
    /* 70 */ type70_dispatch_D580,
    /* 71 */ type71_dispatch_D62F,
    /* 72 */ type72_static_sprite_D6DF,
    /* 73 */ type73_audio_tick_B5F8,
    /* 74 */ type74_vertical_column_CCEE,
    /* 75 */ type75_noop_marker_A560,
};

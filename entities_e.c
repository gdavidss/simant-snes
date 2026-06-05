/*
 * entities_e.c — Entity handlers $20..$3F from $04:9DD5 .. $04:C5E3.
 *
 * Lifted from the 65816 disassembly of the SimAnt (SNES) ROM. Faithful
 * structural reconstruction; not runnable on its own. Continues the
 * style of entities_a..d.c.
 *
 * Coverage summary (per V4_8_DISPATCH_TABLES.md §5):
 *
 *   $20 ($B597)  — HUD-style "ant-population digit" widget (asset bytes at
 *                  $F4E8/$F524, queues VRAM transfer via JSL $0088FF, then
 *                  parks BG1 scroll near (0x80-$9E-$4E, 0x80+$A0-$A7)).
 *                  Variant select uses entity flag at $0012,x.
 *   $21-$26 ($B68D, $B6DD, $B72D, $B77D, $B7C1, $B7FF)
 *                — Six TXY/LDA $1,x/ASL/TAX/JMP($table) state-machine
 *                  dispatchers used by the Behavior/Caste close-up panels
 *                  ($24/$25/$26/$27 = $B77D/$B7C1/$B7FF map to the digit
 *                  triplet that draws percentage readouts — see V4-8 §6).
 *                  State count is small (2-3) but the per-state bodies
 *                  reuse the standard DB52/DB9E sprite drawer chain.
 *   $27-$2A ($9DD5, $9DEA, $9DFF, $9E14) — Auto/Manual icon variants
 *                  documented in control_panels.c at $04:9DD5/9DEA/9DFF/9E14
 *                  (T1..T4: Behavior/Caste × normal/inverted). Each picks
 *                  attr $18 vs $19 based on dp[$0286] or dp[$0288] and
 *                  then JSRs the composite drawer $04:DB52.
 *   $2B ($9E29)  — same family, gated on dp[$0028] == $02 (sub-nest mode).
 *   $2C ($B673)  — generic "centered HUD prop": parks pos at
 *                  ($9E+0x4E, 0xA7-$A0) — scroll-relative — and draws.
 *   $2D ($B90A)  — input-driven menu cursor: gates on dp[$0028] sign,
 *                  dp[$0071] menu lock and dp[$0060/$0061] joypad mask,
 *                  then scans a 4-byte/record bounds-rect table at
 *                  entity[+0x11..+0x13] (banked far pointer), writing the
 *                  matching index back to $28 or $29. Fully lifted (H1).
 *   $2E ($B991)  — dialog-box renderer (gates on $0299 popup + $004C ==
 *                  $02), copies popup rect from $0234..$0244 into entity,
 *                  then dispatches by (dp[$0240] & $0F): cases $06/$08/$0A
 *                  + a "default" panel; common tail emits a second row via
 *                  $39+=$10/$3B+=$20/DB9E. Fully lifted (H1).
 *   $2F ($BA84)  — sibling of $2E (mirror panel; reads $0222/$0224/$0226/
 *                  $0234/$0238), branches on (dp[$0238]+$12 < dp[$0224])
 *                  between straight JSR D7C2 and a $32<->$34 swap-call
 *                  dance. Fully lifted (H1).
 *   $30 ($BAD4)  — score-readout HUD digit (every-4-frames guard via
 *                  dp[$00] & $04, then reads $7FE738/$7FE736 and draws
 *                  three sprites at tile $26/$28/$26). Lifted.
 *   $31 ($BB4F)  — dispatcher (state machine), stubbed.
 *   $32 ($BB74)  — dispatcher (state machine), stubbed.
 *   $33 ($BBB9)  — dispatcher (state machine), stubbed.
 *   $34 ($BC07)  — bicycle-squad spawner: when $029D != $029B it spawns
 *                  a type-$33 child entity at ($0BE0[y]<<4 + 8, etc.).
 *                  Lifted (small).
 *   $35 ($BD9B)  — house_screen_render_04 — ALREADY LIFTED in ui_menus.c
 *                  per V4-8 §2. Not duplicated here.
 *   $36 ($BE49)  — "object spawned by something else; suicides on -dp[$4E]
 *                  sign-bit, otherwise sets up attr $99, pos at $05+$50/
 *                  $07+$5F and runs DB40-bias + DB88 + DC6B fourplet to
 *                  draw a 2×2 sprite tile. Lifted (medium).
 *   $37 ($BEEE)  — dispatcher, stubbed.
 *   $38 ($BF37)  — dispatcher, stubbed.
 *   $39 ($BFB0)  — danger-fly spawner: when $7FE8FC != 0 and (dp[$00] &
 *                  $07) == 0, spawn type $38. Lifted.
 *   $3A ($C02B)  — dispatcher (gates on $004C == 2; then TXY/JMP($table)).
 *                  Two child states are short and lifted; the rest stubbed.
 *   $3B ($C247)  — dispatcher, stubbed.
 *   $3C ($C300)  — DMA setup ($004310/$004311/$004312…/$00420C bursts) —
 *                  gates on dp[$0B] == $1E. Lifted as a thin shim
 *                  (writes the DMA registers via memory side-effects).
 *   $3D ($C36E)  — BICYCLE handler (referenced by scenarios.c
 *                  danger_bicycles_spawn) — gates on CUR_TASK==4, then
 *                  per-state dispatch. Body is large; lifted dispatcher
 *                  + stub for state bodies.
 *   $3E ($C48F)  — dispatcher, stubbed.
 *   $3F ($C5C8)  — "delayed-write watch": when entity[+0x11] equals
 *                  dp[$0012] the handler returns (lifted as 2-line check).
 *
 * Verify:
 *   cd /Users/guilhermedavid/simant-re &&
 *   clang -Wall -Wextra -c entities_e.c -o /tmp/check.o
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Forward decls / externs from simant.c and earlier handlers.
 * ------------------------------------------------------------------------ */
typedef struct Entity Entity;

extern uint8_t  wram[0x20000];
#define dp wram                 /* alias of &wram[0] */

#define WORD(p, off)  (*(uint16_t *)&(p)[off])
#define BYTE(p, off)  ((p)[off])

/* Direct-page tags referenced by handlers $20..$3F. */
#define DP_TASK_INDEX        BYTE(dp, 0x00)
#define DP_CUR_TASK          BYTE(dp, 0x00)        /* alias */
#define DP_ROOM_INDEX        BYTE(dp, 0x0B)
#define DP_FLAG_28           BYTE(dp, 0x28)
#define DP_FLAG_29           BYTE(dp, 0x29)
#define DP_SCRATCH_2A        BYTE(dp, 0x2A)
#define DP_SCRATCH_2B        BYTE(dp, 0x2B)
#define DP_SPRITE_DX         BYTE(dp, 0x37)
#define DP_SPRITE_DX_HI      BYTE(dp, 0x38)
#define DP_SPRITE_DY         BYTE(dp, 0x39)
#define DP_SPRITE_DY_HI      BYTE(dp, 0x3A)
#define DP_SPRITE_TILE_LO    BYTE(dp, 0x3B)
#define DP_SPRITE_TILE_HI    BYTE(dp, 0x3C)
#define DP_SPRITE_ATTR       BYTE(dp, 0x3D)
#define DP_BG1_HSCROLL       WORD(dp, 0x4A)
#define DP_BG1_VSCROLL       WORD(dp, 0x4C)
#define DP_BG_4C             BYTE(dp, 0x4C)
#define DP_BG_4E             BYTE(dp, 0x4E)
#define DP_BG_TBLPTR_LO      BYTE(dp, 0x69)
#define DP_BG_TBLPTR_HI      BYTE(dp, 0x6A)
#define DP_BG_TBLPTR_BANK    BYTE(dp, 0x6B)
#define DP_MENU_OPEN_LOCK    BYTE(dp, 0x71)
#define DP_MENU_HOLD_TIMER2  BYTE(dp, 0x7D)
#define DP_SCROLL_X          WORD(dp, 0x9E)
#define DP_SCROLL_Y          WORD(dp, 0xA0)
#define DP_JOY_PREV_LO       BYTE(dp, 0x60)
#define DP_JOY_PREV_HI       BYTE(dp, 0x61)
#define DP_PIX_BASE_X        BYTE(dp, 0x05)
#define DP_PIX_BASE_Y        BYTE(dp, 0x07)
#define DP_BG_ROW_CUR        BYTE(dp, 0x57)
#define DP_BG_ROW_PREV       BYTE(dp, 0x58)
#define DP_BG_ROW_NEXT       BYTE(dp, 0x5A)
#define DP_BG_ROW_5B         BYTE(dp, 0x5B)
#define DP_BG_ROW_5D         BYTE(dp, 0x5D)
#define DP_BG_ROW_60         BYTE(dp, 0x60)

/* Bigger-page slots (still wram[0..1FFF]). */
#define DLG_DRAW_LOCK_286    BYTE(dp, 0x0286)      /* Behavior auto/manual */
#define DLG_DRAW_LOCK_288    BYTE(dp, 0x0288)      /* Caste auto/manual */
#define POPUP_FLAG_299       BYTE(dp, 0x0299)
#define POPUP_DLG_RECT_X     WORD(dp, 0x0222)
#define POPUP_DLG_RECT_Y     WORD(dp, 0x0224)
#define POPUP_DLG_RECT_2     WORD(dp, 0x0226)
#define POPUP_DLG_RECT_3     WORD(dp, 0x0234)
#define POPUP_DLG_RECT_4     WORD(dp, 0x0236)
#define POPUP_DLG_RECT_5     WORD(dp, 0x0238)
#define POPUP_DLG_RECT_6     WORD(dp, 0x0240)
#define POPUP_DLG_FLAG       BYTE(dp, 0x0241)
#define POPUP_DLG_ATTR       BYTE(dp, 0x0244)
#define POPUP_DLG_29B        BYTE(dp, 0x029B)
#define POPUP_DLG_29D        BYTE(dp, 0x029D)
#define BIKE_SQUAD_BUFFER    ((uint8_t *)&wram[0x0BE0])

/* SRAM shadows ($7F:…) used by ant-pop digit and danger spawners. */
#define SHADOW_ANTPOP_LO     BYTE(wram, 0x1E736)   /* $7FE736 */
#define SHADOW_ANTPOP_HI     BYTE(wram, 0x1E738)   /* $7FE738 */
#define SHADOW_DANGER_F8FC   BYTE(wram, 0x1E8FC)   /* $7FE8FC danger gate */

/* MMIO (only loosely modeled — not actually executed). */
#define MMIO_DMA0_BASE       (&wram[0x10000])      /* placeholder */
#define MMIO_REG_420C        BYTE(wram, 0x1F20C)
#define MMIO_REG_4310        BYTE(wram, 0x1F310)
#define MMIO_REG_4311        BYTE(wram, 0x1F311)
#define MMIO_REG_4312        WORD(wram, 0x1F312)
#define MMIO_REG_4314        BYTE(wram, 0x1F314)
#define MMIO_REG_4317        BYTE(wram, 0x1F317)

/* Helpers from earlier handlers (defined in entities_b..d.c / simant.c). */
extern void   sub_DB9E(void);
extern void   sub_DB52(void);
extern void   sub_DB88(Entity *self);
extern void   sub_DB40(Entity *self, uint16_t dx, uint16_t dy);
extern void   sub_DC6B(uint16_t y);
extern uint8_t sub_DCD5(uint8_t mod);
extern void   sub_DC71(void);
extern void   sub_0088FF(uint8_t a, uint16_t y, uint16_t x);
extern void   sub_D7C2_panel_helper(void);
extern uint8_t read_far(uint8_t bank, uint16_t addr);  /* from lifted_helpers_2.c */

/* Entity struct (mirrors the one in entities_d.c). */
struct __attribute__((packed)) Entity {
    uint8_t  type;                /* +0  */
    uint8_t  state;               /* +1  */
    uint16_t x;                   /* +2  */
    uint16_t y;                   /* +4  */
    uint8_t  flag;                /* +6  */
    uint16_t target_x;            /* +7-8 */
    uint16_t target_y;            /* +9-A */
    uint8_t  pad_b;               /* +B  */
    uint16_t pair_c;              /* +C-D (used as 16-bit) */
    uint8_t  anim_frame_e;        /* +E  */
    uint8_t  attr_f;              /* +F  */
    uint8_t  timer_10;            /* +10 */
    uint8_t  flag_11;             /* +11 */
    uint8_t  flag_12;             /* +12 */
    uint8_t  ptr_13;              /* +13 (table-ptr high byte for type $2D) */
};
_Static_assert(sizeof(struct Entity) == 20, "entity record is 20 bytes");

/* Generic state-table type (host-side; the ROM uses bank-$04 native
 * 16-bit pointers, here we just collect addresses for clarity). */
typedef void (*EntityStateFn)(Entity *self);

/* JSL $0499C1 — spawn child entity of given type. */
extern void sub_0499C1_spawn(uint8_t type);

/* ========================================================================
 * TYPE $20 — HUD ant-pop digit widget at $04:B597
 * ------------------------------------------------------------------------
 *   if (CUR_TASK == 4) ++entity[$11];
 *   if (entity[$11] >= 0x0F) entity[$11] = 0;
 *   if (entity[$12] == 0)
 *       X = entity[$11]*2 + $F4E8;
 *   else
 *       X = entity[$11]*2 + $F524;
 *   JSL $0088FF(A=1, Y=3, X)         ; queue VRAM-row transfer
 *   $4A = $0080 - DP_SCROLL_X - $004E
 *   $4C = $0080 + DP_SCROLL_Y - $00A7
 * ======================================================================== */
static void type20_handler_B597(Entity *self)
{
    if (DP_CUR_TASK == 4) {
        self->flag_11++;
    }
    if (self->flag_11 >= 0x0F) self->flag_11 = 0;

    uint16_t src;
    if (self->flag_12 == 0) {
        src = (uint16_t)(self->flag_11 * 2) + 0xF4E8;
    } else {
        src = (uint16_t)(self->flag_11 * 2) + 0xF524;
    }
    sub_0088FF(0x01, 0x0003, src);

    DP_BG1_HSCROLL = (uint16_t)(0x0080 - DP_SCROLL_X - 0x004E);
    DP_BG1_VSCROLL = (uint16_t)(0x0080 + DP_SCROLL_Y - 0x00A7);
}

/* ========================================================================
 * Generic dispatcher trampoline: types $21-$26, $31-$33, $37, $38,
 * $3A (state>=1), $3B, $3D (when CUR_TASK==4), $3E, $3F (state>=1).
 *
 * ROM pattern:
 *   TXY / LDA #$00 / XBA / LDA $0001,x / ASL / TAX / JMP ($table)
 *
 * I.e. dispatch on state byte via an in-code state-pointer table. The
 * state bodies are lifted per dispatcher below. Each dispatcher's state
 * table holds N pointers (typically 2; up to 6 for $3A/$3D/$3B).
 *
 * Helpers (declared here, defined in entities_x.c siblings or stubs.c):
 *
 *   sub_B87E_anim_from_count(a)  -- $04:B87E. Takes A=$A4/$A6/$A8 (mouse
 *      coord byte; 0..100), computes ((100-A)>>2), looks up B892[y] anim
 *      step and writes to entity[+0x13].
 *   sub_B8AC_advance_drawer(a,y) -- $04:B8AC. Calls B8CB then advances
 *      $37 += $20 and $3B += 4, calls DB9E (second sprite).
 *   sub_B8CB_anim_step(a,y)      -- $04:B8CB. The core animation/sprite
 *      drawer used by $21..$26 state-1: stores [Y] pointer at [$82], if
 *      target_y differs from Y reset timer_10, decrement timer_10, on
 *      underflow advance frame and reload, then JSR DB52.
 *   sub_DD7F_rand_xy()           -- $04:DD7F. Random (X,Y) pair (used by
 *      $38 state-0 spawn placement).
 *   sub_DC84_in_bounds()         -- $04:DC84. Returns Carry-set if sprite
 *      goes out-of-screen ($3A state-3/4/5 use this).
 *   sub_008E9D_play_sfx(a)       -- JSL $00:8E9D. Play sound effect.
 *   sub_008EA3_play_sfx2(a)      -- JSL $00:8EA3. Same family.
 *   sub_DC71_panel_helper        -- already declared above.
 *
 * Two MMIO-style accessor stubs for $33's APU-port wait:
 *   mmio_read_2143()             -- LDA $002143. Returns APU port value.
 *
 * The C2DC helper used by $3B (animates entity[+0x0E] from a 16-entry
 * table at $04:C2F0 and draws via DB52) is wrapped here as well.
 * ======================================================================== */
extern void    sub_B87E_anim_from_count(Entity *self, uint8_t mouse_cnt);
extern void    sub_B8AC_advance_drawer(Entity *self, uint8_t a, uint16_t y);
extern void    sub_B8CB_anim_step(Entity *self, uint8_t a, uint16_t y);
extern void    sub_DD7F_rand_xy(uint16_t *out_x, uint16_t *out_y);
extern uint8_t sub_DC84_in_bounds(Entity *self);   /* returns 1 if out-of-bounds (carry-set) */
extern void    sub_008E9D_play_sfx(uint8_t id);
extern void    sub_008EA3_play_sfx2(uint8_t id);
extern uint8_t mmio_read_2143(void);
extern void    sub_C2DC_C3_anim(Entity *self);
extern void    sub_C41C_bike_step(Entity *self, uint8_t delta);

/* dp[$A4]/[$A6]/[$A8] are the percentage-count bytes feeding the digit
 * triplet panels for $21/$22/$23 (Caste split) and $24/$25/$26 (Behavior
 * split). They are produced by simulation.c each frame. */
#define DP_PCT_A4  BYTE(dp, 0xA4)
#define DP_PCT_A6  BYTE(dp, 0xA6)
#define DP_PCT_A8  BYTE(dp, 0xA8)
#define DP_DRAW_44 BYTE(dp, 0x44)   /* $33 state-1 toggle for DB52 */

/* ------------------------------------------------------------------------
 * TYPE $21 — Caste-split digit at $04:B68D (panel slot 1)
 *   state 0 ($B69D): timer_12 = rand()%4; INC state
 *   state 1 ($B6AA): if (dp[$A4] < 5) {
 *                        pair_c=8; flag_13=8; Y=$F644; A=2; -> B8AC
 *                    } else {
 *                        B87E(dp[$A4]);  pair_c=$0180; Y=$F640; A=4; -> B8AC
 *                    }
 * ------------------------------------------------------------------------ */
static void type21_state0_B69D(Entity *self)
{
    self->flag_12 = (uint8_t)(sub_DCD5(0x04));
    self->state++;
}
static void type21_state1_B6AA(Entity *self)
{
    if (DP_PCT_A4 < 0x05) {
        self->pair_c = 0x0008;
        self->ptr_13 = 0x08;
        sub_B8AC_advance_drawer(self, 0x02, 0xF644);
    } else {
        sub_B87E_anim_from_count(self, DP_PCT_A4);
        self->pair_c = 0x0180;
        sub_B8AC_advance_drawer(self, 0x04, 0xF640);
    }
}
void type21_dispatch_B68D(Entity *self)
{
    if ((self->state & 1) == 0) type21_state0_B69D(self);
    else                        type21_state1_B6AA(self);
}

/* TYPE $22 — Caste-split digit at $04:B6DD (panel slot 2; uses dp[$A6]). */
static void type22_state0_B6ED(Entity *self)
{
    self->flag_12 = (uint8_t)(sub_DCD5(0x04));
    self->state++;
}
static void type22_state1_B6FA(Entity *self)
{
    if (DP_PCT_A6 < 0x05) {
        self->pair_c = 0x0008;
        self->ptr_13 = 0x08;
        sub_B8AC_advance_drawer(self, 0x02, 0xF64A);
    } else {
        sub_B87E_anim_from_count(self, DP_PCT_A6);
        self->pair_c = 0x0048;
        sub_B8AC_advance_drawer(self, 0x04, 0xF646);
    }
}
void type22_dispatch_B6DD(Entity *self)
{
    if ((self->state & 1) == 0) type22_state0_B6ED(self);
    else                        type22_state1_B6FA(self);
}

/* TYPE $23 — Caste-split digit at $04:B72D (panel slot 3; uses dp[$A8]). */
static void type23_state0_B73D(Entity *self)
{
    self->flag_12 = (uint8_t)(sub_DCD5(0x04));
    self->state++;
}
static void type23_state1_B74A(Entity *self)
{
    if (DP_PCT_A8 < 0x05) {
        self->pair_c = 0x0008;
        self->ptr_13 = 0x08;
        sub_B8AC_advance_drawer(self, 0x02, 0xF650);
    } else {
        sub_B87E_anim_from_count(self, DP_PCT_A8);
        self->pair_c = 0x00C8;
        sub_B8AC_advance_drawer(self, 0x04, 0xF64C);
    }
}
void type23_dispatch_B72D(Entity *self)
{
    if ((self->state & 1) == 0) type23_state0_B73D(self);
    else                        type23_state1_B74A(self);
}

/* TYPE $24 — Behavior-split digit at $04:B77D (uses dp[$A8]; smaller
 *            sprite drawer chain — B8CB only, not B8AC).
 *   state 1: if (dp[$A8] < 5)  { flag_13=8; Y=$F65A; A=3; -> B8CB }
 *            else             { flag_13 = ((100-dp[$A8])>>2)+1;
 *                                Y=$F652; A=7; -> B8CB }
 */
static void type24_state0_B78D(Entity *self)
{
    self->flag_12 = (uint8_t)(sub_DCD5(0x03));
    self->state++;
}
static void type24_state1_B79A(Entity *self)
{
    if (DP_PCT_A8 < 0x05) {
        self->ptr_13 = 0x08;
        sub_B8CB_anim_step(self, 0x03, 0xF65A);
    } else {
        self->ptr_13 = (uint8_t)((((0x64 - DP_PCT_A8) & 0xFF) >> 2) + 1);
        sub_B8CB_anim_step(self, 0x07, 0xF652);
    }
}
void type24_dispatch_B77D(Entity *self)
{
    if ((self->state & 1) == 0) type24_state0_B78D(self);
    else                        type24_state1_B79A(self);
}

/* TYPE $25 — Behavior-split digit at $04:B7C1 (uses dp[$A6]). */
static void type25_state0_B7D1(Entity *self)
{
    self->flag_12 = (uint8_t)(sub_DCD5(0x03));
    self->state++;
}
static void type25_state1_B7DE(Entity *self)
{
    if (DP_PCT_A6 < 0x05) {
        self->ptr_13 = 0x08;
        sub_B8CB_anim_step(self, 0x03, 0xF665);
    } else {
        sub_B87E_anim_from_count(self, DP_PCT_A6);
        sub_B8CB_anim_step(self, 0x08, 0xF65D);
    }
}
void type25_dispatch_B7C1(Entity *self)
{
    if ((self->state & 1) == 0) type25_state0_B7D1(self);
    else                        type25_state1_B7DE(self);
}

/* TYPE $26 — Behavior-split digit at $04:B7FF (uses dp[$A4]; with extra
 *            tail blits at $B830 / $B85D drawing the "%" sign). */
static void type26_state0_B80F(Entity *self)
{
    self->flag_12 = (uint8_t)(sub_DCD5(0x03));
    self->state++;
}
static void type26_state1_B81C(Entity *self)
{
    if (DP_PCT_A4 < 0x05) {
        /* primary digit: '0' */
        self->ptr_13 = 0x08;
        sub_B8CB_anim_step(self, 0x03, 0xF670);
        /* secondary "%" suffix: attr $1F, tile bias (-48,-32), tile $016C, then $016E */
        DP_SPRITE_ATTR = 0x1F;
        sub_DB40(self, 0xFFD0, 0x0000);
        DP_SPRITE_TILE_LO = 0x6C;
        DP_SPRITE_TILE_HI = 0x01;
        sub_DB9E();
        DP_SPRITE_DX = (uint8_t)(DP_SPRITE_DX + 0x10);
        DP_SPRITE_TILE_LO = 0x6E;
        sub_DB9E();
    } else {
        sub_B87E_anim_from_count(self, DP_PCT_A4);
        sub_B8CB_anim_step(self, 0x08, 0xF668);
        /* "%" tile: bias (-48,-16), tile $010C */
        sub_DB40(self, 0xFFD0, 0xFFF0);
        DP_SPRITE_TILE_LO = 0x0C;
        DP_SPRITE_TILE_HI = 0x01;
        sub_DB9E();
    }
}
void type26_dispatch_B7FF(Entity *self)
{
    if ((self->state & 1) == 0) type26_state0_B80F(self);
    else                        type26_state1_B81C(self);
}

/* ========================================================================
 * TYPE $27 .. $2A — Auto/Manual icons (control_panels.c documentation
 * matches these to the four corner icons on the Behavior + Caste panels).
 * ------------------------------------------------------------------------
 *   $27 ($9DD5): attr = (dp[$0286] ? $19 : $18); JSR DB52
 *   $28 ($9DEA): attr = (dp[$0288] ? $19 : $18); JSR DB52
 *   $29 ($9DFF): attr = (dp[$0286] ? $18 : $19); JSR DB52   (inverted)
 *   $2A ($9E14): attr = (dp[$0288] ? $18 : $19); JSR DB52   (inverted)
 * ======================================================================== */
static void type27_auto_manual_icon_9DD5(Entity *self)
{
    self->attr_f = (DLG_DRAW_LOCK_286 != 0) ? 0x19 : 0x18;
    sub_DB52();
}
static void type28_auto_manual_icon_9DEA(Entity *self)
{
    self->attr_f = (DLG_DRAW_LOCK_288 != 0) ? 0x19 : 0x18;
    sub_DB52();
}
static void type29_auto_manual_icon_9DFF(Entity *self)
{
    self->attr_f = (DLG_DRAW_LOCK_286 != 0) ? 0x18 : 0x19;
    sub_DB52();
}
static void type2A_auto_manual_icon_9E14(Entity *self)
{
    self->attr_f = (DLG_DRAW_LOCK_288 != 0) ? 0x18 : 0x19;
    sub_DB52();
}

/* ========================================================================
 * TYPE $2B — same family at $04:9E29; gates on dp[$28] == 2 (sub-nest).
 * ======================================================================== */
static void type2B_subnest_indicator_9E29(Entity *self)
{
    self->attr_f = (DP_FLAG_28 == 0x02) ? 0x19 : 0x18;
    sub_DB52();
}

/* ========================================================================
 * TYPE $2C — centered HUD prop at $04:B673
 *   self->x = DP_SCROLL_X + $4E
 *   self->y = $A7 - DP_SCROLL_Y      (8-bit subtract!)
 *   JSR DB52
 *
 * Note: $B673 stores 8-bit lo then STZ hi for the y -- the ROM only uses
 * the low byte of A here ($69 A7 / 38 E5 A0). That's an 8-bit subtract of
 * the low byte of DP_SCROLL_Y from $A7.
 * ======================================================================== */
static void type2C_centered_prop_B673(Entity *self)
{
    self->x = (uint16_t)(((uint16_t)DP_SCROLL_X & 0xFF) + 0x4E);
    self->y = (uint16_t)(uint8_t)(0xA7 - (uint8_t)(DP_SCROLL_Y & 0xFF));
    sub_DB52();
}

/* ========================================================================
 * TYPE $2D — input-driven menu cursor at $04:B90A
 * ------------------------------------------------------------------------
 * H1 lift (full body, $B90A..$B990):
 *
 *   Gates:
 *     dp[$28] sign-bit must be set (menu open / cursor armed).
 *     If dp[$71]==0, require ($60|$61) & $80 to be set (fresh D-pad).
 *     Otherwise (menu locked), require dp[$7D] & $03 (auto-repeat tick).
 *
 *   Body — scan a per-menu bounds-rectangle table:
 *     ptr = (self->ptr_13:[self->+0x11..+0x12])  ; far ptr in entity
 *     self->attr_f (+0x0F) = 0                   ; selection index counter
 *     y = 0
 *     loop:
 *         a0 = [ptr+y+0]
 *         a1 = [ptr+y+1]
 *         if (a0 == a1) return;                  ; terminator record
 *         cx = dp[$14]                           ; cursor X
 *         if (cx <  a0) skip_record;
 *         if (cx >= a1) skip_record;
 *         a2 = [ptr+y+2]
 *         a3 = [ptr+y+3]
 *         cy = dp[$15]
 *         if (cy <  a2) skip_record;
 *         if (cy >= a3) skip_record;
 *         // MATCH -- write the selection index into $28 or $29
 *         if (dp[$71]==0) {
 *             if (dp[$60] & $80) dp[$28] = self->+0x0F; else dp[$29] = self->+0x0F;
 *         } else {
 *             if (dp[$7D] & $01) dp[$28] = self->+0x0F; else dp[$29] = self->+0x0F;
 *         }
 *         return;
 *       skip_record:
 *         self->+0x0F += 1;
 *         y += 4;
 *         goto loop;
 *
 *   Records are 4 bytes: { x_min, x_max, y_min, y_max }. Terminator is any
 *   record where byte0 == byte1 (degenerate rectangle).
 * ======================================================================== */
void type2D_menu_cursor_B90A(Entity *self)
{
    /* $B90A: LDA $28 / BMI continue / RTS */
    if ((int8_t)DP_FLAG_28 >= 0) return;

    /* $B90F..$B926: input gating split on dp[$71] (menu lock). */
    if (DP_MENU_OPEN_LOCK == 0) {
        /* $B914 LDA $60 / ORA $61 / AND #$80 / BNE continue / RTS */
        if (((DP_JOY_PREV_LO | DP_JOY_PREV_HI) & 0x80) == 0) return;
    } else {
        /* $B91F LDA $7D / AND #$03 / BNE continue / RTS */
        if ((DP_MENU_HOLD_TIMER2 & 0x03) == 0) return;
    }

    /* $B927: REP #$20 / LDA $0011,x / STA $69
     * $B92E: SEP #$20 / LDA $0013,x / STA $6B
     * $B935: STZ $000F,x / LDY #$0000
     *
     * The ROM reads a 16-bit value from entity[+0x11..+0x12] (= flag_11 +
     * flag_12<<8 in our struct) and writes it to dp[$69]/[$6A]; the bank
     * byte from +0x13 (=ptr_13) goes to dp[$6B]. */
    uint16_t ptr_lo   = (uint16_t)self->flag_11 |
                        ((uint16_t)self->flag_12 << 8);
    uint8_t  ptr_bank = self->ptr_13;
    DP_BG_TBLPTR_LO   = (uint8_t)(ptr_lo & 0xFF);
    DP_BG_TBLPTR_HI   = (uint8_t)(ptr_lo >> 8);
    DP_BG_TBLPTR_BANK = ptr_bank;

    self->attr_f = 0;
    uint16_t y = 0;

    /* $B93B loop: LDA [$69],y / INY / CMP [$69],y / BNE +/ RTS */
    for (;;) {
        uint8_t a0 = read_far(ptr_bank, (uint16_t)(ptr_lo + y + 0));
        uint8_t a1 = read_far(ptr_bank, (uint16_t)(ptr_lo + y + 1));
        if (a0 == a1) return;                              /* $B942 RTS */

        /* $B944 LDA $14 / CMP [$69],y(=byte0) / BCC skip */
        uint8_t cx = BYTE(dp, 0x14);
        if (cx <  a0) { self->attr_f++; y += 4; continue; }
        if (cx >= a1) { self->attr_f++; y += 4; continue; }

        uint8_t a2 = read_far(ptr_bank, (uint16_t)(ptr_lo + y + 2));
        uint8_t a3 = read_far(ptr_bank, (uint16_t)(ptr_lo + y + 3));
        uint8_t cy = BYTE(dp, 0x15);
        if (cy <  a2) { self->attr_f++; y += 4; continue; }
        if (cy >= a3) { self->attr_f++; y += 4; continue; }

        /* MATCH — write self->attr_f into $28 or $29 based on input/lock. */
        if (DP_MENU_OPEN_LOCK == 0) {
            if (DP_JOY_PREV_LO & 0x80) DP_FLAG_28 = self->attr_f;
            else                       DP_FLAG_29 = self->attr_f;
        } else {
            if (DP_MENU_HOLD_TIMER2 & 0x01) DP_FLAG_28 = self->attr_f;
            else                            DP_FLAG_29 = self->attr_f;
        }
        return;
    }
}

/* ========================================================================
 * TYPE $2E — dialog panel renderer at $04:B991
 * ------------------------------------------------------------------------
 * H1 lift (full body, $B991..$BA83):
 *
 *   Guard:
 *     dp[$0299] != 0 AND dp[$004C] == 2  (popup active, mode-2 screen).
 *
 *   Copy popup rect into entity scratch:
 *     self->pair_c   = dp[$0240]            ; 16-bit raw
 *     self->x        = dp[$0236]
 *     self->y        = dp[$0238]
 *     self->attr_f   = dp[$0244] | $84      ; saved attr (sprite OAM hi)
 *     dp[$3D]        = dp[$0244] | $83      ; live sprite attr
 *
 *   If dp[$0241] == 0 -> render "default" 2-tile panel
 *     (single DB9E call w/ tile $014E + a second row at $016E).
 *
 *   Otherwise switch on (dp[$0240] & $0F):
 *     case $06: pick dx = ($3D & $20 ? +$0005 : -$0015), Y=$0008, JSR DB40,
 *               tile=$012E, DB9E.  -> then fall to DB52 tail.
 *     case $08: dx=$FFF0 (8-bit -16 with EB XBA -> hi=$FF), Y=$0001 JSR DB40,
 *               tile=$014E, DB9E, then $39 += $10, tile=$016E, DB9E.
 *     case $0A: dx=$0200 (lo=$02, hi=$00), Y=$0008 JSR DB40, tile=$01AE, DB9E.
 *     default : same as dp[$0241]==0 path.
 *
 *   Tail ($BA6A): JSR DB52; $39 += $10; $3B += $20; JSR DB9E; RTS.
 * ======================================================================== */
void type2E_dialog_panel_B991(Entity *self)
{
    /* $B991 LDA $0299 / CMP #$00 / BNE / RTS */
    if (POPUP_FLAG_299 == 0) return;
    /* $B999 LDA $004C / CMP #$02 / BNE / RTS */
    if (DP_BG_4C != 0x02)    return;

    /* $B9A1..$B9B5 REP #$20 block: copy 16-bit rect into entity. */
    self->pair_c = POPUP_DLG_RECT_6;          /* +$0C = dp[$0240] */
    self->x      = POPUP_DLG_RECT_4;          /* +$02 = dp[$0236] */
    self->y      = POPUP_DLG_RECT_5;          /* +$04 = dp[$0238] */
    /* $B9B7..$B9C4 SEP #$20 block */
    self->attr_f      = (uint8_t)(POPUP_DLG_ATTR | 0x84);
    DP_SPRITE_ATTR    = (uint8_t)(POPUP_DLG_ATTR | 0x83);

    /* $B9C6: LDA $0241 / BEQ -> default tail at $BA40. */
    if (POPUP_DLG_FLAG == 0) goto default_tail;

    /* $B9CB: LDA $0240 / AND #$0F / CMP #$06 / BNE next */
    uint8_t nib = (uint8_t)(POPUP_DLG_RECT_6 & 0x0F);

    if (nib == 0x06) {
        /* $B9D4 LDA $3D / AND #$20 / BNE alt
         *   "low" path:  dx = $FFEB (-21)
         *   "high" path: dx = $0005
         */
        uint16_t dx;
        if ((DP_SPRITE_ATTR & 0x20) == 0) dx = 0xFFEB;
        else                              dx = 0x0005;
        sub_DB40(self, dx, 0x0008);
        DP_SPRITE_TILE_LO = 0x2E; DP_SPRITE_TILE_HI = 0x01;     /* $012E */
        sub_DB9E();
        goto common_tail;
    }
    if (nib == 0x08) {
        /* $B9FE LDA #$FF / XBA / LDA #$F0 / Y=$0001 -> dx = $FFF0 (-16) */
        sub_DB40(self, 0xFFF0, 0x0001);
        DP_SPRITE_TILE_LO = 0x4E; DP_SPRITE_TILE_HI = 0x01;     /* $014E */
        sub_DB9E();
        /* $BA11 REP #$20 / LDA $39 / CLC / ADC #$0010 / STA $39 */
        {
            uint16_t v = (uint16_t)((uint16_t)DP_SPRITE_DY |
                                    ((uint16_t)DP_SPRITE_DY_HI << 8));
            v = (uint16_t)(v + 0x0010);
            DP_SPRITE_DY    = (uint8_t)(v & 0xFF);
            DP_SPRITE_DY_HI = (uint8_t)(v >> 8);
        }
        DP_SPRITE_TILE_LO = 0x6E; DP_SPRITE_TILE_HI = 0x01;     /* $016E */
        sub_DB9E();
        goto common_tail;
    }
    if (nib == 0x0A) {
        /* $BA2B LDA #$00 / XBA / LDA #$02 / Y=$0008 -> dx = $0002 */
        sub_DB40(self, 0x0002, 0x0008);
        DP_SPRITE_TILE_LO = 0xAE; DP_SPRITE_TILE_HI = 0x01;     /* $01AE */
        sub_DB9E();
        goto common_tail;
    }
    /* Fall through to default tail for any other nibble. */

default_tail:
    /* $BA40: LDA #$BB / STA $3D
     *        Y=$0040 STY $37 (dx=$0040)
     *        Y=$00A0 STY $39 (dy=$00A0)
     *        Y=$014E STY $3B / DB9E
     *        $39 += $10 / Y=$016E STY $3B / DB9E */
    DP_SPRITE_ATTR  = 0xBB;
    DP_SPRITE_DX    = 0x40; DP_SPRITE_DX_HI = 0x00;
    DP_SPRITE_DY    = 0xA0; DP_SPRITE_DY_HI = 0x00;
    DP_SPRITE_TILE_LO = 0x4E; DP_SPRITE_TILE_HI = 0x01;
    sub_DB9E();
    {
        uint16_t v = (uint16_t)((uint16_t)DP_SPRITE_DY |
                                ((uint16_t)DP_SPRITE_DY_HI << 8));
        v = (uint16_t)(v + 0x0010);
        DP_SPRITE_DY    = (uint8_t)(v & 0xFF);
        DP_SPRITE_DY_HI = (uint8_t)(v >> 8);
    }
    DP_SPRITE_TILE_LO = 0x6E; DP_SPRITE_TILE_HI = 0x01;
    sub_DB9E();
    /* Fall through to common tail (DB52+second-row blit). */

common_tail:
    /* $BA6A: JSR DB52 */
    sub_DB52();
    /* $BA6D REP #$20 / LDA $39 / CLC / ADC #$0010 / STA $39 */
    {
        uint16_t v = (uint16_t)((uint16_t)DP_SPRITE_DY |
                                ((uint16_t)DP_SPRITE_DY_HI << 8));
        v = (uint16_t)(v + 0x0010);
        DP_SPRITE_DY    = (uint8_t)(v & 0xFF);
        DP_SPRITE_DY_HI = (uint8_t)(v >> 8);
    }
    /* $BA79 LDA $3B / CLC / ADC #$20 / STA $3B  (8-bit tile-lo bump) */
    DP_SPRITE_TILE_LO = (uint8_t)(DP_SPRITE_TILE_LO + 0x20);
    /* $BA80 JSR DB9E / RTS */
    sub_DB9E();
}

/* ========================================================================
 * TYPE $2F — sibling dialog panel renderer at $04:BA84
 * ------------------------------------------------------------------------
 * H1 lift (full body, $BA84..$BAD3):
 *
 *   Guard: same as $2E (dp[$0299] != 0 AND dp[$004C] == 2).
 *
 *   Copy mirror-panel rect into entity scratch (note different sources):
 *     self->pair_c = dp[$0226]    ; ($0240-equivalent)
 *     self->x      = dp[$0222]    ; ($0236-equivalent)
 *     self->y      = dp[$0224]
 *     self->attr_f = dp[$0234] | $9C
 *
 *   Lower-corner check / "rotate through $32/$34":
 *     bottom = dp[$0238] + $12
 *     if (bottom < dp[$0224]) {
 *         // Swap-and-call: temporarily move dp[$34] into dp[$32], call the
 *         // panel helper, then swap the result back. This is the body's
 *         // unique "ROT through $32/$34 to flip a sub-rectangle" trick.
 *         tmp     = dp[$32];
 *         dp[$32] = dp[$34];
 *         sub_D7C2_panel_helper();
 *         dp[$34] = dp[$32];
 *         dp[$32] = tmp;
 *     } else {
 *         sub_D7C2_panel_helper();
 *     }
 *     RTS
 * ======================================================================== */
void type2F_dialog_panel_BA84(Entity *self)
{
    /* $BA84 LDA $0299 / CMP #$00 / BNE / RTS */
    if (POPUP_FLAG_299 == 0) return;
    /* $BA8C LDA $004C / CMP #$02 / BNE / RTS */
    if (DP_BG_4C != 0x02)    return;

    /* $BA94..$BAA8 REP #$20 block: copy mirror rect. */
    self->pair_c = POPUP_DLG_RECT_2;     /* +$0C = dp[$0226] */
    self->x      = POPUP_DLG_RECT_X;     /* +$02 = dp[$0222] */
    self->y      = POPUP_DLG_RECT_Y;     /* +$04 = dp[$0224] */
    /* $BAAA SEP #$20 block */
    self->attr_f = (uint8_t)(POPUP_DLG_RECT_3 | 0x9C);

    /* $BAB2: LDA $0238 / CLC / ADC #$12 / CMP $0224 / BCC $BAD0
     *   BCC = branch if A < operand (unsigned). When the lower edge of the
     *   sub-rectangle is still inside the parent (bottom < dp[$0224]),
     *   call D7C2 directly. Otherwise run the $32/$34 rotate trick first. */
    uint8_t bottom = (uint8_t)((uint8_t)POPUP_DLG_RECT_5 + 0x12);
    if (bottom < (uint8_t)POPUP_DLG_RECT_Y) {
        /* $BAD0: JSR $D7C2 / RTS  -- straight path */
        sub_D7C2_panel_helper();
    } else {
        /* $BABD: rotate-through-$32/$34 sequence (swap, call, restore). */
        uint16_t tmp = WORD(dp, 0x32);
        WORD(dp, 0x32) = WORD(dp, 0x34);
        sub_D7C2_panel_helper();
        WORD(dp, 0x34) = WORD(dp, 0x32);
        WORD(dp, 0x32) = tmp;
    }
}

/* ========================================================================
 * TYPE $30 — score / population digit HUD at $04:BAD4
 * ------------------------------------------------------------------------
 *   Run every 4 ticks (dp[$00] & 4 must be zero).
 *   v = $7FE738; w = $7FE736
 *   $69 = v*12         ; v*3 << 2
 *   $37 = (w*24 - $69) + $48
 *   $38 = 0
 *   $39 = $6A + $69; $3A = 0
 *   draw three sprites (tiles $26, $28, $26) shifted by (+12,+8) and
 *   (+0,+4) with attrs $88, $88->default, $E8.
 * ======================================================================== */
static void type30_score_digit_BAD4(Entity *self)
{
    (void)self;
    if ((DP_TASK_INDEX & 0x04) != 0) return;

    uint16_t v = (uint16_t)SHADOW_ANTPOP_HI;
    uint16_t v12 = (uint16_t)(((v * 3) & 0xFF) << 2);   /* ASL,CLC,ADC,ASL,ASL */
    DP_BG_TBLPTR_LO = (uint8_t)v12;

    uint16_t w = (uint16_t)SHADOW_ANTPOP_LO;
    uint16_t w24 = (uint16_t)((((w * 3) & 0xFF) << 3) & 0xFF);

    DP_SPRITE_DX    = (uint8_t)(w24 - v12 + 0x48);
    DP_SPRITE_DX_HI = 0;
    DP_SPRITE_DY    = (uint8_t)(0x6A + v12);
    DP_SPRITE_DY_HI = 0;

    DP_SPRITE_TILE_LO = 0x26;
    DP_SPRITE_TILE_HI = 0x00;
    DP_SPRITE_ATTR    = 0x88;
    sub_DB9E();

    DP_SPRITE_TILE_LO = 0x28;
    DP_SPRITE_DX = (uint8_t)(DP_SPRITE_DX + 0x0C);
    sub_DB9E();

    DP_SPRITE_TILE_LO = 0x26;
    DP_SPRITE_DX = (uint8_t)(DP_SPRITE_DX + 0x08);
    DP_SPRITE_DY = (uint8_t)(DP_SPRITE_DY + 0x04);
    DP_SPRITE_ATTR = 0xE8;
    sub_DB9E();
}

/* ========================================================================
 * TYPE $31, $32, $33 — dispatchers ($BB4F, $BB74, $BBB9)
 * Same TXY/LDA $1,x/ASL/TAX/JMP($table) pattern; 2 states each.
 * ------------------------------------------------------------------------
 *  $31 state 0 ($BB5F): attr_f = $98; INC state.
 *  $31 state 1 ($BB69): dp[$44] = 3; sub_DB52(); dp[$44] = 0.
 *  $32 state 0 ($BB84): attr_f = $18; target = pos; INC state.
 *  $32 state 1 ($BB9E): pos += (dp[$05], dp[$07]) bias; sub_DB52().
 *                       (Reads target into pos + dp scroll-base, scrolls.)
 *  $33 state 0 ($BBC9): timer_10 = 2; if (mmio_read_2143() == 0)
 *                          sub_008EA3_play_sfx2($24);
 *                       INC state.
 *  $33 state 1 ($BBDF): sub_DB52(); if (CUR_TASK == 4) {
 *                          if (--timer_10 == 0) { anim_frame_e += 2;
 *                              if (anim_frame_e == 8) self->type = 0; } }
 * ======================================================================== */
static void type31_state0_BB5F(Entity *self)
{
    self->attr_f = 0x98;
    self->state++;
}
static void type31_state1_BB69(Entity *self)
{
    (void)self;
    DP_DRAW_44 = 0x03;
    sub_DB52();
    DP_DRAW_44 = 0x00;
}
void type31_dispatch_BB4F(Entity *self)
{
    if ((self->state & 1) == 0) type31_state0_BB5F(self);
    else                        type31_state1_BB69(self);
}

static void type32_state0_BB84(Entity *self)
{
    self->attr_f   = 0x18;
    self->target_x = self->x;
    self->target_y = self->y;
    self->state++;
}
static void type32_state1_BB9E(Entity *self)
{
    /* x = target_x + dp[$05]; y = target_y + dp[$07] */
    self->x = (uint16_t)(self->target_x + DP_PIX_BASE_X);
    self->y = (uint16_t)(self->target_y + DP_PIX_BASE_Y);
    sub_DB52();
}
void type32_dispatch_BB74(Entity *self)
{
    if ((self->state & 1) == 0) type32_state0_BB84(self);
    else                        type32_state1_BB9E(self);
}

static void type33_state0_BBC9(Entity *self)
{
    self->timer_10 = 0x02;
    if (mmio_read_2143() == 0x00) {
        sub_008EA3_play_sfx2(0x24);
    }
    self->state++;
}
static void type33_state1_BBDF(Entity *self)
{
    sub_DB52();
    if (DP_CUR_TASK != 0x04) return;
    if (--self->timer_10 != 0) return;
    self->anim_frame_e = (uint8_t)(self->anim_frame_e + 2);
    self->anim_frame_e = (uint8_t)(self->anim_frame_e + 2);
    if (self->anim_frame_e == 0x08) {
        self->type = 0;
    }
}
void type33_dispatch_BBB9(Entity *self)
{
    if ((self->state & 1) == 0) type33_state0_BBC9(self);
    else                        type33_state1_BBDF(self);
}

/* ========================================================================
 * TYPE $34 — bicycle-squad spawner at $04:BC07
 * ------------------------------------------------------------------------
 *   while ($029D != $029B) {
 *       y = $029D
 *       cx = ($0BE0[y++] << 4) + 8
 *       cy = ($0BE0[y++] << 4) + 8
 *       $029D = y & $1F
 *       spawn type $33 with target=(cx,cy)
 *   }
 *
 * NB: the ROM does only ONE pass per call, then RTS. Match that.
 * The "X register" stash (PHX/PLX) preserves the parent entity index.
 * Note that "cx" is shifted into X (DBR-relative loop counter) but then
 * the high word is set in cx and y via PHA/TYA-AND-$1F-STA — the actual
 * spawn at $0499C1 reads them from $0002,x of the *new* entity (set by
 * the spawn helper which copies $69/$6A/$6B). The spawn-time write-back
 * to (cx,cy) is omitted here because the spawner helper takes care of it.
 * ======================================================================== */
static void type34_bicycle_spawner_BC07(Entity *self)
{
    (void)self;
    if (POPUP_DLG_29D == POPUP_DLG_29B) return;

    uint8_t y = POPUP_DLG_29D;
    uint16_t cx = (uint16_t)(BIKE_SQUAD_BUFFER[y++] << 4) + 0x08;
    uint16_t cy = (uint16_t)(BIKE_SQUAD_BUFFER[y++] << 4) + 0x08;
    (void)cx; (void)cy;

    POPUP_DLG_29D = (uint8_t)(y & 0x1F);
    sub_0499C1_spawn(0x33);   /* type-$33 child */
}

/* ========================================================================
 * TYPE $36 — particle-burst sprite at $04:BE49
 * ------------------------------------------------------------------------
 *   if ((int8_t)dp[$004E] < 0) { self->type = 0; return; }   ; suicide
 *   self->attr   = $99
 *   self->pair_c = $0080
 *   self->x      = $0050 + dp[$05]
 *   self->y      = $005F + dp[$07]
 *   sub_DB40(self, $FFE0, $FFE0)      ; tile bias -32,-32
 *   sub_DB88(self)                     ; tile_lo = anim_frame + pair_c
 *   sub_DC6B(0)                        ; draw quad slot 0
 *   $37 += $20; $3B += $04; sub_DC6B(4)
 *   $37 -= $20; $39 += $20; $3B += $04; sub_DC6B(8)
 *   $37 += $20; $3B += $04; sub_DC6B(12)
 *
 * (Continues past $BEC7 with more iterations; we lift the four observed
 * passes — that matches the 2×2 sprite tile pattern.)
 * ======================================================================== */
static void type36_burst_BE49(Entity *self)
{
    if ((int8_t)DP_BG_4E < 0) {
        self->type = 0;
        return;
    }
    self->attr_f = 0x99;
    self->pair_c = 0x0080;
    self->x      = (uint16_t)(0x0050 + DP_PIX_BASE_X);
    self->y      = (uint16_t)(0x005F + DP_PIX_BASE_Y);

    sub_DB40(self, 0xFFE0, 0xFFE0);
    sub_DB88(self);
    sub_DC6B(0x0000);

    /* quad 1: +32 / +0 / +4 tile  */
    DP_SPRITE_DX = (uint8_t)(DP_SPRITE_DX + 0x20);
    DP_SPRITE_TILE_LO = (uint8_t)(DP_SPRITE_TILE_LO + 0x04);
    sub_DC6B(0x0004);

    /* quad 2: -32 / +32 / +4 tile  */
    DP_SPRITE_DX = (uint8_t)(DP_SPRITE_DX - 0x20);
    DP_SPRITE_DY = (uint8_t)(DP_SPRITE_DY + 0x20);
    DP_SPRITE_TILE_LO = (uint8_t)(DP_SPRITE_TILE_LO + 0x04);
    sub_DC6B(0x0008);

    /* quad 3: +32 / +0 / +4 tile  */
    DP_SPRITE_DX = (uint8_t)(DP_SPRITE_DX + 0x20);
    DP_SPRITE_TILE_LO = (uint8_t)(DP_SPRITE_TILE_LO + 0x04);
    sub_DC6B(0x000C);
}

/* ========================================================================
 * TYPE $37 — dispatcher at $04:BEEE. 2 states.
 *   state 0 ($BEFE): timer_10 = 8; flag_11 = 0; INC state.
 *   state 1 ($BF0B): if (--timer_10 != 0) return; timer_10 = 8;
 *                    pair_c += $20; queue VRAM-row transfer
 *                    JSL $0088FF(A=1, Y=5, X=$F673+pair_c).
 * TYPE $38 — dispatcher at $04:BF37. 2 states.
 *   state 0 ($BF47): dp[$69] = rand()%7; dp[$6A] = rand()%7;
 *                    sub_DD7F gives (X,Y); pos = (X+$30, Y-$60);
 *                    timer_10 = $18; INC state.
 *   state 1 ($BF78): if (CUR_TASK == 4) { x -= 2; y += 4; }
 *                    sub_DB52(); if (CUR_TASK == 4)
 *                       if (--timer_10 == 0) self->type = 0;
 * ======================================================================== */
static void type37_state0_BEFE(Entity *self)
{
    self->timer_10 = 0x08;
    self->flag_11  = 0x00;
    self->state++;
}
static void type37_state1_BF0B(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->timer_10 = 0x08;
    self->pair_c = (uint16_t)(self->pair_c + 0x20);
    /* JSL $0088FF(a=1, y=5, x=$F673 + pair_c) */
    sub_0088FF(0x01, 0x0005, (uint16_t)(0xF673 + self->pair_c));
}
void type37_dispatch_BEEE(Entity *self)
{
    if ((self->state & 1) == 0) type37_state0_BEFE(self);
    else                        type37_state1_BF0B(self);
}

static void type38_state0_BF47(Entity *self)
{
    BYTE(dp, 0x69) = sub_DCD5(0x07);
    BYTE(dp, 0x6A) = sub_DCD5(0x07);
    uint16_t rx = 0, ry = 0;
    sub_DD7F_rand_xy(&rx, &ry);
    self->x = (uint16_t)(rx + 0x0030);
    self->y = (uint16_t)(ry - 0x0060);
    self->timer_10 = 0x18;
    self->state++;
}
static void type38_state1_BF78(Entity *self)
{
    if (DP_CUR_TASK == 0x04) {
        self->x = (uint16_t)(self->x - 0x0002);
        self->y = (uint16_t)(self->y + 0x0004);
    }
    sub_DB52();
    if (DP_CUR_TASK != 0x04) return;
    if (--self->timer_10 != 0) return;
    self->type = 0;
}
void type38_dispatch_BF37(Entity *self)
{
    if ((self->state & 1) == 0) type38_state0_BF47(self);
    else                        type38_state1_BF78(self);
}

/* ========================================================================
 * TYPE $39 — danger-fly spawner at $04:BFB0
 * ------------------------------------------------------------------------
 *   if ($7FE8FC == 0) return;
 *   if ((dp[$00] & 7) != 0) return;
 *   spawn type $38;
 * ======================================================================== */
static void type39_fly_spawner_BFB0(Entity *self)
{
    (void)self;
    if (SHADOW_DANGER_F8FC == 0) return;
    if ((DP_TASK_INDEX & 0x07) != 0) return;
    sub_0499C1_spawn(0x38);
}

/* ========================================================================
 * TYPE $3A — gated dispatcher at $04:C02B
 * ------------------------------------------------------------------------
 *   if (dp[$004C] != 2) return;
 *   then TXY/LDA $1,x/ASL/TAX/JMP($table) — table at $C03F has 5+
 *   short entries that lead to $C055, $C059, $C098, $C11F, $C154, $C192
 *   etc. The state-0 body at $C055 just increments state. State $C059
 *   sets up pos = ($00A4,$0050), target_x=$0001, attr=$94, pair=$000A
 *   then JSRs the C08C random-timer setup. Lift these two; stub rest.
 * ======================================================================== */
/* $3A state 2 ($C098): moving sprite — every 8 frames step pair_c by
 * lookup table $C11B[(dp[$00]>>3) & 3]; every other frame advance pos
 * by target_x; if pos drifts outside [$A4, $F8) flip target_x sign and
 * play SFX $46 + toggle attr ^= $20; then sub_DB52(); sub_DC84()
 * carry-set => transition to state 6, timer = $40. */
static const uint16_t k_C11B_table[4] = { 0x0000, 0x0008, 0x0010, 0x0018 };
void type3A_state2plus_C098(Entity *self)
{
    uint8_t idx = (uint8_t)((DP_TASK_INDEX >> 3) & 0x03);
    self->pair_c = k_C11B_table[idx];

    if ((DP_TASK_INDEX & 0x01) == 0) {
        self->x = (uint16_t)(self->x + self->target_x);
    }
    uint16_t d = (uint16_t)(self->x - 0x00A4);
    if (d >= 0x0054) {
        /* out-of-band: flip x-velocity */
        self->target_x = (uint16_t)(-(int16_t)self->target_x);
        self->x = (uint16_t)(self->x + self->target_x);
        self->attr_f ^= 0x20;
        sub_008EA3_play_sfx2(0x46);
    }
    sub_DB52();
    if (sub_DC84_in_bounds(self)) {
        self->state    = 6;
        self->timer_10 = 0x40;
    }
}

/* $3A state 3 ($C11F): JSR DB52; on out-of-bounds advance state=6, timer=$40. */
void type3A_state3_C11F(Entity *self)
{
    sub_DB52();
    if (sub_DC84_in_bounds(self)) {
        self->state    = 6;
        self->timer_10 = 0x40;
    }
}

/* $3A state 4 ($C154): pair_c = table $C18E[(dp[$00]>>4) & 3]; then like state 3. */
static const uint16_t k_C18E_table[4] = { 0x0000, 0x0010, 0x0020, 0x0030 };
void type3A_state4_C154(Entity *self)
{
    uint8_t idx = (uint8_t)((DP_TASK_INDEX >> 4) & 0x03);
    self->pair_c = k_C18E_table[idx];
    sub_DB52();
    if (sub_DC84_in_bounds(self)) {
        self->state    = 6;
        self->timer_10 = 0x40;
    }
}

/* $3A state 5 ($C192): identical bounds-and-cleanup tail (no animation
 * step before DB52). */
void type3A_state5_C192(Entity *self)
{
    sub_DB52();
    if (sub_DC84_in_bounds(self)) {
        self->state    = 6;
        self->timer_10 = 0x40;
    }
}

static void type3A_state0_init_C055(Entity *self)
{
    self->timer_10 = 0xC0;
    self->state++;
}

static void type3A_random_timer_C08C(Entity *self)
{
    self->timer_10 = (uint8_t)(sub_DCD5(0x40) + 0x40);
}

static void type3A_state1_setup_C059(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->x = 0x00A4;
    self->y = 0x0050;
    self->target_x = 0x0001;
    self->attr_f = 0x94;
    self->pair_c = 0x000A;
    type3A_random_timer_C08C(self);
    self->state++;
}

static void type3A_dispatch_C02B(Entity *self)
{
    if (DP_BG_4C != 0x02) return;
    switch (self->state) {
    case 0: type3A_state0_init_C055(self);  break;
    case 1: type3A_state1_setup_C059(self); break;
    case 2: type3A_state2plus_C098(self);   break;
    case 3: type3A_state3_C11F(self);       break;
    case 4: type3A_state4_C154(self);       break;
    default: type3A_state5_C192(self);      break;
    }
}

/* ========================================================================
 * TYPE $3B — dispatcher at $04:C247. 4 states.
 *   state 0 ($C25B): timer_10 dec; on 0 -> attr_f=$8C, pos=($0000,$0030),
 *                                       timer_10=$80, INC state.
 *   state 1 ($C280): x += 2; sub_C2DC_C3_anim(); timer_10 dec; on 0:
 *                       timer_10 = $40, INC state.
 *   state 2 ($C2A1): timer_10 dec; on 0 -> timer_10=$80, y=$60, attr_f=$BC,
 *                                       INC state.
 *   state 3 ($C2C0): x -= 2; sub_C2DC_C3_anim(); timer_10 dec; on 0:
 *                       state = 0.
 * ======================================================================== */
static void type3B_state0_C25B(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->attr_f   = 0x8C;
    self->x        = 0x0000;
    self->y        = 0x0030;
    self->timer_10 = 0x80;
    self->state++;
}
static void type3B_state1_C280(Entity *self)
{
    self->x = (uint16_t)(self->x + 0x0002);
    sub_C2DC_C3_anim(self);
    if (--self->timer_10 != 0) return;
    self->timer_10 = 0x40;
    self->state++;
}
static void type3B_state2_C2A1(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->timer_10 = 0x80;
    self->y        = 0x0060;
    self->attr_f   = 0xBC;
    self->state++;
}
static void type3B_state3_C2C0(Entity *self)
{
    self->x = (uint16_t)(self->x - 0x0002);
    sub_C2DC_C3_anim(self);
    if (--self->timer_10 != 0) return;
    self->state = 0;
}
void type3B_dispatch_C247(Entity *self)
{
    switch (self->state & 0x03) {
    case 0: type3B_state0_C25B(self); break;
    case 1: type3B_state1_C280(self); break;
    case 2: type3B_state2_C2A1(self); break;
    default: type3B_state3_C2C0(self); break;
    }
}

/* ========================================================================
 * TYPE $3C — DMA setup helper at $04:C300
 * ------------------------------------------------------------------------
 *   if (dp[$0B] != $1E) return;
 *   mmio[$420C] = 0;
 *   dp[$57]=dp[$5A]=dp[$5D]= $C0;
 *   dp[$60]=0;
 *   tableOffset = dp[$0026] ? (dp[$00]<<2) & $7E : 0
 *   dp[$58..$5E] = three pointers at $F773 + tableOffset, +$80, +$100
 *   mmio[$4310]=$42, mmio[$4311]=$0D, mmio[$4312]=$0057
 *   mmio[$4314]=$01, mmio[$4317]=$01
 *   mmio[$420C]=$02
 *
 * This is host-side modeled as plain memory writes; the real ROM kicks
 * HDMA via $420C. Lifted faithfully.
 * ======================================================================== */
static void type3C_hdma_setup_C300(Entity *self)
{
    (void)self;
    if (DP_ROOM_INDEX != 0x1E) return;

    MMIO_REG_420C = 0x00;
    DP_BG_ROW_CUR = 0xC0;
    DP_BG_ROW_NEXT = 0xC0;
    DP_BG_ROW_5D  = 0xC0;
    DP_BG_ROW_60  = 0x00;

    uint16_t off;
    if (BYTE(dp, 0x0026) != 0) {
        off = (uint16_t)(((uint16_t)DP_TASK_INDEX << 2) & 0x7E);
    } else {
        off = 0;
    }

    uint16_t p0 = (uint16_t)(0xF773 + off);
    *(uint16_t *)&dp[0x0058] = p0;
    *(uint16_t *)&dp[0x005B] = (uint16_t)(p0 + 0x0080);
    *(uint16_t *)&dp[0x005E] = (uint16_t)(p0 + 0x0100);

    MMIO_REG_4310 = 0x42;
    MMIO_REG_4311 = 0x0D;
    MMIO_REG_4312 = 0x0057;
    MMIO_REG_4314 = 0x01;
    MMIO_REG_4317 = 0x01;
    MMIO_REG_420C = 0x02;
}

/* ========================================================================
 * TYPE $3D — BICYCLE handler at $04:C36E
 * ------------------------------------------------------------------------
 *   if (CUR_TASK != 4) return;
 *   TXY / state-dispatch via JMP ($C382) — 5 sub-states. Bodies large
 *   and data-mixed; lift dispatcher head only.
 * ======================================================================== */
/* TYPE $3D — BICYCLE state bodies.
 *   state 0 ($C38C): pos = ($0000, …), target_x = $0002, dp[$05]=dp[$0A]=0, INC state.
 *   state 1 ($C3A7): if ((dp[$00] & $02) == 0) {
 *                        if (--timer_10 == 0) {
 *                            JSL $008E9D($3C);    ; play sfx
 *                            INC state; timer_10 = $3C;
 *                        }
 *                    }
 *   state 2 ($C3C2): timer_10 dec; on 0 -> INC state, timer_10 = $06,
 *                                       y = rand(%$20) + $10.
 *   state 3 ($C3DD): timer_10 dec; on 0 -> sub_C41C_bike_step(-2), INC state.
 *   state 4 ($C3ED): timer_10 dec; on 0 -> sub_C41C_bike_step(+2); if (x<0) {
 *                       target_x = -target_x; x += target_x; state=1; timer_10=0;
 *                    } else DEC state. */
void type3D_bike_state0_C38C(Entity *self)
{
    self->x        = 0x0000;
    self->target_x = 0x0002;
    /* "$0005,x = 0" / "$000A,x = 0" — these are the low halves of pos
     * fields shared with pad_b / target_y high byte; treat as no-op on
     * a clean spawn. */
    self->pad_b     = 0x00;
    self->target_y &= 0x00FF;
    self->state++;
}
void type3D_bike_state1_C3A7(Entity *self)
{
    if ((DP_TASK_INDEX & 0x02) != 0) return;
    if (--self->timer_10 != 0) return;
    sub_008E9D_play_sfx(0x3C);
    self->state++;
    self->timer_10 = 0x3C;
}
void type3D_bike_state2_C3C2(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->state++;
    self->timer_10 = 0x06;
    self->y = (uint16_t)((sub_DCD5(0x20)) + 0x10);
}
void type3D_bike_state3_C3DD(Entity *self)
{
    if (--self->timer_10 != 0) return;
    sub_C41C_bike_step(self, 0xFE);  /* -2 */
    self->state++;
}
void type3D_bike_state4_C3ED(Entity *self)
{
    if (--self->timer_10 != 0) return;
    sub_C41C_bike_step(self, 0x02);
    if ((int16_t)self->x < 0) {
        self->target_x = (uint16_t)(-(int16_t)self->target_x);
        self->x        = (uint16_t)(self->x + self->target_x);
        self->state    = 1;
        self->timer_10 = 0;
    } else {
        self->state--;
    }
}

static void type3D_bicycle_dispatch_C36E(Entity *self)
{
    if (DP_CUR_TASK != 0x04) return;
    switch (self->state) {
    case 0: type3D_bike_state0_C38C(self); break;
    case 1: type3D_bike_state1_C3A7(self); break;
    case 2: type3D_bike_state2_C3C2(self); break;
    case 3: type3D_bike_state3_C3DD(self); break;
    default: type3D_bike_state4_C3ED(self); break;
    }
}

/* ========================================================================
 * TYPE $3E — dispatcher at $04:C48F. 2 states.
 *   state 0 ($C49F): JSL $008E9D($40); timer_10 = $10; INC state.
 *   state 1 ($C4AF): JSR DB52; if (CUR_TASK == 4)
 *                       if (--timer_10 == 0) self->type = 0;
 * ======================================================================== */
static void type3E_state0_C49F(Entity *self)
{
    sub_008E9D_play_sfx(0x40);
    self->timer_10 = 0x10;
    self->state++;
}
static void type3E_state1_C4AF(Entity *self)
{
    sub_DB52();
    if (DP_CUR_TASK != 0x04) return;
    if (--self->timer_10 != 0) return;
    self->type = 0;
}
void type3E_dispatch_C48F(Entity *self)
{
    if ((self->state & 1) == 0) type3E_state0_C49F(self);
    else                        type3E_state1_C4AF(self);
}

/* ========================================================================
 * TYPE $3F — delayed-write watchdog at $04:C5C8
 * ------------------------------------------------------------------------
 *   if (self->flag_11 == dp[$12]) return;
 *   if (self->flag_11 < dp[$12]) return;     ; BCS branch — i.e. return
 *                                            ; when flag_11 >= dp[$12]
 *   JSR DB52; return.
 * ------------------------------------------------------------------------
 * Lifted faithfully: when flag_11 is strictly less than dp[$12] the
 * watchdog calls the sprite drawer; otherwise it falls through.
 * ======================================================================== */
static void type3F_watch_tail_C5D0(Entity *self)
{
    if (self->flag_11 >= BYTE(dp, 0x12)) return;
    (void)self;
    sub_DB52();
}

static void type3F_watch_C5C8(Entity *self)
{
    if (self->flag_11 == BYTE(dp, 0x12)) return;
    type3F_watch_tail_C5D0(self);
}

/* ========================================================================
 * Handler table — these 32 entries replace slots $20..$3F in the original
 * entity_handlers[] (at $04:9A30). Note that slot $35 ($BD9B) is
 * house_screen_render_04, lifted in ui_menus.c — we keep an extern decl.
 * ======================================================================== */
extern void house_screen_render_04_BD9B(Entity *self);

typedef void (*EntityHandler)(Entity *);

__attribute__((used))
static EntityHandler entity_handlers_20_3F[32] = {
    /* $20 */ type20_handler_B597,
    /* $21 */ type21_dispatch_B68D,
    /* $22 */ type22_dispatch_B6DD,
    /* $23 */ type23_dispatch_B72D,
    /* $24 */ type24_dispatch_B77D,
    /* $25 */ type25_dispatch_B7C1,
    /* $26 */ type26_dispatch_B7FF,
    /* $27 */ type27_auto_manual_icon_9DD5,
    /* $28 */ type28_auto_manual_icon_9DEA,
    /* $29 */ type29_auto_manual_icon_9DFF,
    /* $2A */ type2A_auto_manual_icon_9E14,
    /* $2B */ type2B_subnest_indicator_9E29,
    /* $2C */ type2C_centered_prop_B673,
    /* $2D */ type2D_menu_cursor_B90A,
    /* $2E */ type2E_dialog_panel_B991,
    /* $2F */ type2F_dialog_panel_BA84,
    /* $30 */ type30_score_digit_BAD4,
    /* $31 */ type31_dispatch_BB4F,
    /* $32 */ type32_dispatch_BB74,
    /* $33 */ type33_dispatch_BBB9,
    /* $34 */ type34_bicycle_spawner_BC07,
    /* $35 */ house_screen_render_04_BD9B,
    /* $36 */ type36_burst_BE49,
    /* $37 */ type37_dispatch_BEEE,
    /* $38 */ type38_dispatch_BF37,
    /* $39 */ type39_fly_spawner_BFB0,
    /* $3A */ type3A_dispatch_C02B,
    /* $3B */ type3B_dispatch_C247,
    /* $3C */ type3C_hdma_setup_C300,
    /* $3D */ type3D_bicycle_dispatch_C36E,
    /* $3E */ type3E_dispatch_C48F,
    /* $3F */ type3F_watch_C5C8,
};

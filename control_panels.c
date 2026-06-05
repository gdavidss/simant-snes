/*
 * SimAnt (SNES) — BEHAVIOR & CASTE CONTROL PANELS
 *
 * WIKI: see wiki/12-control-panels.md for the full triangle-joystick math
 *       walkthrough (D034 forward / D074 inverse), the Auto-vs-Manual data
 *       flow, the %/# display toggle, and the state $24/$25/$26/$27
 *       dispatch wiring.
 *
 * Lifted from bank $00 / $01:
 *
 *   $00:C28A             panel setup helper (BG mode 9, load Workers/...)
 *   $00:CA96  state $24  Behavior Control Panel — setup
 *   $00:CA96  state $26  Caste Control Panel    — setup (shared body, branches on dp[$0B])
 *   $00:CCD0  state $25/$27  per-frame run loop (shared, branches on dp[$0B])
 *   $00:CDBA  icon-click sub-state dispatch table
 *   $00:CDC6  Auto-icon clicked
 *   $00:CDE5  Manual-icon clicked
 *   $00:CE04  Percent / Absolute-count toggle icon
 *   $00:CE20..CE86  joystick book-keeping (backup / restore cursor & %)
 *   $00:CE87  Behavior auto path: read $028E/$028C/$028A -> A4/A6/A8 -> snap cursor
 *   $00:CE9A  Behavior commit: A4/A6/A8 -> $028E/$028C/$028A
 *   $00:CEAA  Caste auto path: read $027E/$0280/$0282/$0284 -> $029X -> snap cursor
 *   $00:CEC0  Caste auto-pack: $027E/$0280/($0282+$0284) -> $0290/$0292/$0294
 *   $00:CEDB  Caste commit (Manual mode)
 *   $00:CF05  per-frame redraw (% or # of ants, depending on dp[$0044])
 *   $00:CF6D  draw 3 numbers from working A4/A6/A8 at tile positions
 *   $00:CF8A  convert Behavior %-> count for Absolute-count display
 *   $00:CFB9  helper:  count = (% * pop_total[$7F:EB60]) / 100
 *   $00:CFDF  convert Caste %-> count for Absolute-count display
 *   $00:D034  TRIANGLE JOYSTICK FORWARD: cursor (x,y) -> 3 barycentric percentages
 *   $00:D074  TRIANGLE JOYSTICK INVERSE: 3 percentages -> cursor (x,y) lattice point
 *   $00:8D05  32 x 32 -> 64 bit multiply (used by D074)
 *   $00:8D41  32 / 32 -> 32 bit divide  (used by D034)
 *   $00:9D48  icon menu master dispatch (called from view-run states)
 *   $00:9D60  6-entry icon table  (View / Scent / Control / Save / Eval / Options)
 *   $00:9DE3  icon[2] = Control Panel  -> Behavior/Caste submenu @ $01:8809
 *   $00:9187  popup submenu helper (count + Y-ptr-table; returns BCC = picked, $1A = index)
 *   $00:9700..9745  state $1A initial seed of $028A..$028E / $027E..$0284
 *   $01:8800  Control-Panel submenu: count=02, ptr[0]=$880E "Behavior",
 *             ptr[1]=$8817 "Caste"
 *   $04:9DD5  entity handler T1 — Auto/Manual icon (Behavior side)
 *   $04:9DEA  entity handler T2 — Auto/Manual icon (Caste side)
 *   $04:9DFF  entity handler T3 — Auto/Manual icon (inverted; "current" highlight)
 *   $04:9E14  entity handler T4 — Auto/Manual icon (Caste, inverted)
 *
 * Conventions match simant.c / states_gameplay.c.
 */

#include <stdint.h>

/* ========================================================================
 * EXTERNAL DEPS — declared in simant.c / states_gameplay.c
 * ======================================================================== */
extern uint8_t  wram[];
extern volatile uint8_t mmio[];
#define dp wram
#define MMIO8(addr)  (*(volatile uint8_t *)&mmio[(addr) & 0xFFFF])
#define INIDISP    MMIO8 (0x2100)
#define OBSEL      MMIO8 (0x2101)
#define BGMODE     MMIO8 (0x2105)
#define JOY1L      MMIO8 (0x4218)
#define JOY1H      MMIO8 (0x4219)

/* yield to scheduler / various lifted helpers */
extern void sub_8976(void);
extern void sub_877D(void);
extern void sub_C318(void);
extern void sub_C398(void);
extern void sub_8F08(void);
extern void sub_8D94(void);
extern void sub_8D7E(uint8_t count, uint16_t src);
extern void sub_8ACC(uint16_t length, uint16_t vram_dst);
extern void sub_8AED(uint8_t a, uint16_t y);
extern void sub_867F(uint8_t a, uint16_t x);
extern void sub_499C1(uint16_t x, uint16_t y, uint8_t a);
extern void sub_8E88(uint8_t a);
extern void sub_8EA3(uint8_t a);
extern void sub_A734(void);
extern void sub_A243(void);
extern void sub_A0D2(void);
extern void sub_A3BD(void);
extern void sub_A3D6(void);
extern void sub_8642(void);
extern int  sub_DF79(void);                              /* carry-clear = normal */
extern void sub_8D05(void);                              /* 32-bit mul: BE:C0 * C2:C4 -> C6:C8:CA:CC */
extern void sub_8D41(void);                              /* 32-bit div: BE:C0 / C2:C4 -> C6:C8, rem CE:D0 */
extern void sub_8CE0(void);                              /* 16-bit mul: BE * C2 -> C8:C6 */
extern void sub_C516(uint16_t xy, uint16_t value);       /* draw 2-digit number at tile (x,y) */
extern void sub_C4D8(uint16_t xy, uint16_t y_ptr);       /* draw label string */

/* Bank-$01 string-table pointer (Workers/Soldiers/... tile labels at $01:CF08). */
/* extern const uint8_t rom_01_CF08_caste_labels[]; */

/* Where the colony's TOTAL POPULATION lives (read by the "% -> # of ants" path). */
#define COLONY_TOTAL_POP_7FEB60   (*(uint16_t *)&wram[0x1EB60])

/* ========================================================================
 * WRAM LAYOUT — Control-Panel state
 * ========================================================================
 *
 * All values are 8-bit-as-16-bit (the code does REP #$20 / LDA $028A on
 * load, so we treat them as 16-bit cells; in practice they hold 0..100).
 *
 *   $0044    "%/#" display-mode flag (0 = absolute # of ants, 1 = percent).
 *            Toggled by the bottom-row icon. Both panels share this flag.
 *
 *   $0286    BEHAVIOR Auto/Manual flag (0 = Manual, 1 = Auto).
 *   $0288    CASTE    Auto/Manual flag (0 = Manual, 1 = Auto).
 *
 *   Behavior TARGET percentages (each 0..100, sum-to-100 invariant maintained
 *   by the triangle math):
 *     $028A   Behavior TOP-vertex   weight ("Forage" — see manual p.14;
 *             default seed = $003C = 60)
 *     $028C   Behavior LEFT-vertex  weight ("Dig"   — default = $0014 = 20)
 *     $028E   Behavior RIGHT-vertex weight ("Nurse" — default = $0014 = 20)
 *
 *   Caste TARGET percentages (filled by CEDB on Manual write, by CEC0 on
 *   Auto display):
 *     $0290   Caste LEFT-vertex   weight ("Workers"  — copy of $027E)
 *     $0292   Caste RIGHT-vertex  weight ("Soldiers" — copy of $0280)
 *     $0294   Caste TOP-vertex    weight ("Breeders" — $0282 + $0284)
 *
 *   Caste raw colony state (counts/proportions written by simulation; in
 *   Auto mode the panel just reflects these):
 *     $027E   Workers count     (default seed = $003C = 60)
 *     $0280   Soldiers count    (default seed = $0028 = 40)
 *     $0282   Breeders, group A (default = 0)
 *     $0284   Breeders, group B (default = 0)
 *
 *   Cursor-side scratch (used by the joystick loop, NOT persisted):
 *     $14/$15        raw cursor (x,y) in screen pixels (8-bit each).
 *     $9E/$A0        triangle-space cursor (x,y).
 *     $9A/$9C        saved triangle-space cursor for restore-on-invalid.
 *     $A4            working RIGHT-vertex weight (16-bit; 0..100).
 *     $A6            working LEFT-vertex  weight.
 *     $A8            working TOP-vertex   weight.
 *     $AA/$AC/$AE    backup of $A4/$A6/$A8 for restore-on-invalid.
 *     $BE/$C0/$C2/$C4/$C6/$C8 — 32-bit mul/div scratch (D034 / D074).
 *
 *   The TWO triangle origins on screen (both panels share):
 *     base-X origin = $4E (78 px)   — left edge of triangle's bounding box
 *     base-Y origin = $A7 (167 px)  — bottom edge
 *     width        = 100 px (= 100% per side)
 *     height       = 87 px  (= 100 * sin(60°), rounded)
 *
 *   Constants:
 *     RATIO = $0001BB67 / $00010000 = 1.73218... ≈ √3
 *     The division (y * 65536) / 113511  ≈ y / √3  (the "axial → screen" ratio).
 * ======================================================================== */

/* WRAM helpers (16-bit, native endianness OK for x86 host port). */
#define DP_U8(off)   wram[(off)]
#define DP_U16(off)  (*(uint16_t *)&wram[(off)])

#define CP_PCT_DISPLAY    DP_U8 (0x0044)                 /* 0=#, 1=%       */
#define CP_BEHAVIOR_AUTO  DP_U8 (0x0286)
#define CP_CASTE_AUTO     DP_U8 (0x0288)

#define CP_BHV_TOP        DP_U16(0x028A)  /* Forage  */
#define CP_BHV_LEFT       DP_U16(0x028C)  /* Dig     */
#define CP_BHV_RIGHT      DP_U16(0x028E)  /* Nurse   */

#define CP_CASTE_LEFT     DP_U16(0x0290)  /* Workers  */
#define CP_CASTE_RIGHT    DP_U16(0x0292)  /* Soldiers */
#define CP_CASTE_TOP      DP_U16(0x0294)  /* Breeders */

#define CP_COLONY_WORKERS   DP_U16(0x027E)
#define CP_COLONY_SOLDIERS  DP_U16(0x0280)
#define CP_COLONY_BREEDERS_A DP_U16(0x0282)
#define CP_COLONY_BREEDERS_B DP_U16(0x0284)

#define DP_CURSOR_X       DP_U8 (0x0014)
#define DP_CURSOR_Y       DP_U8 (0x0015)
/* Triangle-space cursor cells. The ROM accesses them with REP #$20
 * (16-bit) in D034/D074 — they're 16-bit cells. Treating them as 8-bit
 * left the high byte stale and produced garbage scaled_y values when
 * the high byte was non-zero from a previous step. */
#define DP_TRI_X          DP_U16(0x009E)
#define DP_TRI_Y          DP_U16(0x00A0)
#define DP_TRI_X_SAVED    DP_U16(0x009A)
#define DP_TRI_Y_SAVED    DP_U16(0x009C)

#define DP_W_A4           DP_U16(0x00A4)  /* working "RIGHT" weight */
#define DP_W_A6           DP_U16(0x00A6)  /* working "LEFT"  weight */
#define DP_W_A8           DP_U16(0x00A8)  /* working "TOP"   weight */
#define DP_W_AA           DP_U16(0x00AA)
#define DP_W_AC           DP_U16(0x00AC)
#define DP_W_AE           DP_U16(0x00AE)

#define DP_BE             DP_U16(0x00BE)
#define DP_C0             DP_U16(0x00C0)
#define DP_C2             DP_U16(0x00C2)
#define DP_C4             DP_U16(0x00C4)
#define DP_C6             DP_U16(0x00C6)
#define DP_C8             DP_U16(0x00C8)

/* State-machine word lifted from simant.c. */
#define DP_STATE          DP_U8 (0x000B)
#define DP_INPUT_SUBSTATE DP_U8 (0x0028)    /* icon index from popup; $FF = none */
#define DP_PAUSED         DP_U8 (0x002A)
#define DP_MENU_LOCK      DP_U8 (0x0071)
#define DP_MENU_INPUT     DP_U8 (0x007B)

/* Triangle origin / size — both panels use the same screen rectangle. */
#define TRI_BASE_X    0x4E    /* 78 px */
#define TRI_BASE_Y    0xA7    /* 167 px (cursor_y is subtracted FROM this) */
#define TRI_W         100     /* 100% along the base */
#define TRI_H         87      /* 87 px ≈ 100 * sin(60°) */

#define RATIO_NUM     0x1BB67U                          /* = 113511 */
#define RATIO_DENOM_FIXED  0x10000U                     /* = 65536  */

/* ========================================================================
 * sub_D034 ($00:D034) — TRIANGLE JOYSTICK FORWARD MAP
 * ------------------------------------------------------------------------
 *   Input:  ($9E, $A0) = cursor pos in triangle space  (0..99, 0..86)
 *   Output: $A4, $A6, $A8 = three barycentric weights, each 0..100,
 *           summing to 100 by construction.
 *
 * The geometry: an equilateral-ish triangle with base at y=0 (along the
 * bottom of the panel), left vertex at (0,0), right vertex at (99,0),
 * top vertex at (50, 86). The math expresses (x,y) in oblique
 * barycentric coordinates:
 *
 *     scaled_y = y / √3            (via integer division y*65536 / 113511)
 *     A4 (RIGHT) = x       - scaled_y
 *     A6 (LEFT ) = 100 - x - scaled_y
 *     A8 (TOP  ) = 100 - A4 - A6     (= 2*scaled_y, by construction)
 *
 * That way A4 + A6 + A8 = 100 always.
 * ======================================================================== */
/* WIKI: wiki/12-control-panels.md §2a (Forward: Cursor → Percentages). */
static void cp_triangle_xy_to_weights_D034(void)
{
    /* C4:C2 = 32-bit divisor = $0001BB67. */
    DP_C2 = 0xBB67;
    DP_C4 = 0x0001;

    /* BE:C0 = 32-bit dividend = y << 16  (BE = 0; C0 = y). */
    DP_BE = 0x0000;
    DP_C0 = DP_TRI_Y;

    sub_8D41();                       /* C8:C6 := BE:C0 / C4:C2 = scaled_y */

    uint16_t scaled_y = DP_C6;        /* low word of quotient is enough */

    /* A4 = x - scaled_y */
    DP_W_A4 = (uint16_t)(DP_TRI_X - scaled_y);
    /* A6 = 100 - x - scaled_y */
    DP_W_A6 = (uint16_t)(100 - DP_TRI_X - scaled_y);
    /* A8 = 100 - A4 - A6  (= 2*scaled_y) */
    DP_W_A8 = (uint16_t)(100 - DP_W_A4 - DP_W_A6);
}

/* ========================================================================
 * sub_D074 ($00:D074) — TRIANGLE JOYSTICK INVERSE MAP
 * ------------------------------------------------------------------------
 *   Input:  $A4, $A6, $A8 = three weights, each 0..100, sum 100.
 *   Output: ($9E, $A0) = cursor pos in triangle space.
 *
 *   cursor_x = A4 + (A8 / 2)                 = right + half of top
 *   cursor_y = (A8 * 113511) >> 16 / 2       = A8 * √3 / 2 ≈ A8 * 0.866
 *
 * Used after the player moves: D034 maps cursor → percentages, then D074
 * SNAPS the cursor back to the exact lattice point that represents those
 * (integer) percentages. This is what gives the cross-cursor its
 * "click into place" feel.
 * ======================================================================== */
/* WIKI: wiki/12-control-panels.md §2b (Inverse: Percentages → Cursor snap). */
static void cp_triangle_weights_to_xy_D074(void)
{
    /* x = A4 + A8 / 2 */
    DP_BE       = DP_W_A8;                              /* BE = top weight */
    uint16_t a8 = DP_W_A8;
    DP_TRI_X    = (uint16_t)(DP_W_A4 + (a8 >> 1));      /* cursor_x */

    /* C4:C2 = $0001BB67, BE:C0 = $00:A8 (top weight as 32-bit). */
    DP_C0 = 0x0000;
    DP_C2 = 0xBB67;
    DP_C4 = 0x0001;

    sub_8D05();    /* C8:C6 := BE:C0 * C4:C2 = A8 * 0x1BB67 (32-bit product
                    * is bits 0..47 in C6:C8:CA; we use only the C8 word) */

    /* cursor_y = C8 / 2.  ROM does `LDA $C8; LSR; STA $A0` in 16-bit mode,
     * so the result is a full 16-bit value (we lost it earlier by casting
     * to uint8_t). */
    DP_TRI_Y = (uint16_t)(DP_C8 >> 1);
}

/* ========================================================================
 * sub_CE20 ($00:CE20) — BACKUP working weights
 * ======================================================================== */
static void cp_backup_weights_CE20(void)
{
    DP_W_AA = DP_W_A4;
    DP_W_AC = DP_W_A6;
    DP_W_AE = DP_W_A8;
}

/* ========================================================================
 * sub_CE31 ($00:CE31) — BACKUP working triangle cursor
 * ======================================================================== */
static void cp_backup_cursor_CE31(void)
{
    DP_TRI_X_SAVED = DP_TRI_X;
    DP_TRI_Y_SAVED = DP_TRI_Y;
}

/* ========================================================================
 * sub_CE3E ($00:CE3E) — VALIDATE working weights ∈ [0,100]
 *   Returns 1 if all <= 100; 0 if any > 100 (and restores from AA/AC/AE).
 *   ROM compares against $0065 = 101 with BCS — i.e. valid when A < 101,
 *   so 100 is INCLUDED in the valid range. The earlier C used `< 100`,
 *   which rejected the corners of the triangle (e.g. 100/0/0).
 * ======================================================================== */
static int cp_validate_weights_CE3E(void)
{
    if (DP_W_A4 <= 100 && DP_W_A6 <= 100 && DP_W_A8 <= 100) return 1;
    /* Out-of-triangle: restore. */
    DP_W_A4 = DP_W_AA;
    DP_W_A6 = DP_W_AC;
    DP_W_A8 = DP_W_AE;
    return 0;
}

/* ========================================================================
 * sub_CE6B ($00:CE6B) — VALIDATE triangle cursor: X<=100, Y<87
 *
 *   ROM:  LDA $9E; CMP #$65 (=101); BCS reject  ; X < 101  -> X <= 100
 *         LDA $A0; CMP #$57 (= 87); BCS reject  ; Y < 87
 *
 * Earlier C used `DP_TRI_X < 100`, which rejected the exact right edge
 * of the triangle.
 *
 * NOTE: the ROM does 8-bit LDA here (no REP) — so this is one of the
 * few places that DOES treat $9E/$A0 as 8-bit. Since the bound checks
 * are 0..100 / 0..87, the low byte is sufficient — the cast in C is safe.
 * ======================================================================== */
static int cp_cursor_in_bounds_CE6B(void)
{
    return ((uint8_t)DP_TRI_X < 101) && ((uint8_t)DP_TRI_Y < TRI_H);
}

/* ========================================================================
 * sub_CE79 ($00:CE79) — RESTORE cursor from backup
 * ======================================================================== */
static void cp_restore_cursor_CE79(void)
{
    DP_TRI_X = DP_TRI_X_SAVED;
    DP_TRI_Y = DP_TRI_Y_SAVED;
}

/* ========================================================================
 * sub_CE87 ($00:CE87) — BEHAVIOR Auto-mode load
 *   Read the live Behavior percentages (top=Forage, left=Dig,
 *   right=Nurse) into the working A4/A6/A8 slots, then snap the cursor
 *   to match.
 * ======================================================================== */
static void cp_behavior_load_for_display_CE87(void)
{
    DP_W_A4 = CP_BHV_RIGHT;       /* RIGHT vertex = Nurse  */
    DP_W_A6 = CP_BHV_LEFT;        /* LEFT  vertex = Dig    */
    DP_W_A8 = CP_BHV_TOP;         /* TOP   vertex = Forage */
    cp_triangle_weights_to_xy_D074();
}

/* ========================================================================
 * sub_CE9A ($00:CE9A) — BEHAVIOR commit (Manual mode write)
 *   After the player moves the joystick to a new lattice point, write
 *   the working A4/A6/A8 back into the persistent $028A/$028C/$028E.
 * ======================================================================== */
static void cp_behavior_commit_CE9A(void)
{
    CP_BHV_RIGHT = DP_W_A4;
    CP_BHV_LEFT  = DP_W_A6;
    CP_BHV_TOP   = DP_W_A8;
}

/* ========================================================================
 * sub_CEC0 ($00:CEC0) — CASTE Auto-mode pack
 *   Convert the 4 raw colony cells into 3 panel percentages. The
 *   simulation may store "breeders" in two halves (e.g. male/female
 *   alates or two life-stage buckets); the panel sums them.
 * ======================================================================== */
static void cp_caste_auto_pack_CEC0(void)
{
    CP_CASTE_LEFT  = CP_COLONY_WORKERS;
    CP_CASTE_RIGHT = CP_COLONY_SOLDIERS;
    CP_CASTE_TOP   = (uint16_t)(CP_COLONY_BREEDERS_A + CP_COLONY_BREEDERS_B);
}

/* ========================================================================
 * sub_CEAA ($00:CEAA) — CASTE Auto-mode load (display path)
 *   Refresh $029X from $027E..$0284, then put them into working slots
 *   and snap cursor.
 * ======================================================================== */
static void cp_caste_load_for_display_CEAA(void)
{
    cp_caste_auto_pack_CEC0();
    DP_W_A8 = CP_CASTE_TOP;       /* TOP = Breeders */
    DP_W_A6 = CP_CASTE_LEFT;      /* LEFT = Workers */
    DP_W_A4 = CP_CASTE_RIGHT;     /* RIGHT = Soldiers */
    cp_triangle_weights_to_xy_D074();
}

/* ========================================================================
 * sub_CEDB ($00:CEDB) — CASTE commit (Manual mode write)
 *   Write A4/A6/A8 → $0290/$0292/$0294, then mirror back into the colony
 *   cells $027E/$0280 (and split Breeders half/half into $0282/$0284).
 *   This lets the simulation pick up the new caste TARGETS.
 * ======================================================================== */
/* WIKI: wiki/12-control-panels.md §4b (Manual-mode Caste commit + mirror). */
static void cp_caste_commit_CEDB(void)
{
    CP_CASTE_TOP   = DP_W_A8;
    CP_CASTE_LEFT  = DP_W_A6;
    CP_CASTE_RIGHT = DP_W_A4;

    /* Mirror to colony cells. Breeders gets LSR (split in two). */
    CP_COLONY_WORKERS  = CP_CASTE_LEFT;
    CP_COLONY_SOLDIERS = CP_CASTE_RIGHT;
    uint16_t breeders_half = CP_CASTE_TOP >> 1;
    CP_COLONY_BREEDERS_A = breeders_half;
    CP_COLONY_BREEDERS_B = breeders_half;
}

/* ========================================================================
 * sub_CFB9 ($00:CFB9) — convert "percentage" to "absolute count of ants"
 *   count = (percent * COLONY_TOTAL_POP_7FEB60) / 100
 * Used by the "#" display mode. The lifted code stages the operands in
 * C2 (percent), BE (total), then JSRs $8CE0 to multiply, then JSRs
 * $8D41 to divide by 100. The full 32-bit product survives in C6:C8.
 *
 * The caller reads the result from $C6 (low 16 bits).
 * ======================================================================== */
static uint16_t cp_pct_to_count_CFB9(uint16_t percent)
{
    /* percent * total_pop */
    DP_C2 = percent;
    DP_BE = COLONY_TOTAL_POP_7FEB60;
    sub_8CE0();          /* leaves product in C6:C8 */
    /* Divide by 100. */
    DP_BE = DP_C6;
    DP_C0 = DP_C8;
    DP_C2 = 100;
    DP_C4 = 0;
    sub_8D41();
    return DP_C6;
}

/* ========================================================================
 * sub_CF6D ($00:CF6D) — draw the 3 working numbers in their fixed
 * triangular layout.  (Each call to sub_C516 writes a 2-digit number to
 * the BG1 tilemap at the given tile coord.)
 * ======================================================================== */
static void cp_draw_working_numbers_CF6D(void)
{
    DP_U8(0x008C) = 0x30;                  /* palette byte */
    sub_C516(0x0719, DP_W_A8);             /* TOP   number at tile col 7, row 25 */
    sub_C516(0x131B, DP_W_A4);             /* RIGHT number at col 27, row 19    */
    sub_C516(0x1304, DP_W_A6);             /* LEFT  number at col 4,  row 19    */
}

/* ========================================================================
 * sub_CF8A ($00:CF8A) — pack the Behavior percentages into working slots
 *   *after* converting them to absolute counts (% / # toggle = #).
 * ======================================================================== */
static void cp_behavior_pct_to_count_CF8A(void)
{
    DP_W_A8 = cp_pct_to_count_CFB9(CP_BHV_TOP);
    DP_W_A6 = cp_pct_to_count_CFB9(CP_BHV_LEFT);
    DP_W_A4 = cp_pct_to_count_CFB9(CP_BHV_RIGHT);
}

/* sub_CFDF — caste version. */
static void cp_caste_pct_to_count_CFDF(void)
{
    DP_W_A4 = cp_pct_to_count_CFB9(CP_CASTE_RIGHT);
    DP_W_A6 = cp_pct_to_count_CFB9(CP_CASTE_LEFT);
    DP_W_A8 = cp_pct_to_count_CFB9(CP_CASTE_TOP);
}

/* ========================================================================
 * sub_CF05 ($00:CF05) — REDRAW the panel each frame
 * ------------------------------------------------------------------------
 * If $0044 != 0  (Percent mode):
 *   - the three numbers shown ARE the current working A4/A6/A8.
 *   - the per-frame loop just calls CF6D + sets palette and exits.
 *
 * If $0044 == 0  (Absolute count mode):
 *   - save working A4/A6/A8 via CE20.
 *   - convert each stored % to a count via CFB9 (Behavior or Caste).
 *   - draw the counts.
 *   - restore working values via CE59 (which uses AA/AC/AE).
 *
 * That way the live cross-cursor still tracks (working values) but the
 * displayed digits are pop-scaled counts.
 * ======================================================================== */
extern void sub_CE59(void);                              /* restore AA/AC/AE -> A4/A6/A8 */

static void cp_redraw_CF05(void)
{
    DP_U8(0x0089) = 0x70;                 /* row scroll base, reset for redraw */

    if (CP_PCT_DISPLAY) {
        /* Percent mode: numbers ARE the working weights. */
        cp_draw_working_numbers_CF6D();
        DP_U8(0x008C) = 0x31;
        /* The original code then re-paints the "%" suffix tile (tile $9B6F)
         * next to each number, at three fixed positions: */
        sub_C4D8(0x081C, 0x9B6F);
        sub_C4D8(0x141E, 0x9B6F);
        sub_C4D8(0x1407, 0x9B6F);
    } else {
        /* Absolute count mode. */
        cp_backup_weights_CE20();
        if (DP_STATE < 0x26)
            cp_behavior_pct_to_count_CF8A();
        else
            cp_caste_pct_to_count_CFDF();
        cp_draw_working_numbers_CF6D();
        DP_U8(0x008C) = 0x31;
        /* Suffix tile $9B71 = "of " for absolute counts ("X of Y ants"). */
        sub_C4D8(0x081C, 0x9B71);
        sub_C4D8(0x141E, 0x9B71);
        sub_C4D8(0x1407, 0x9B71);
        sub_CE59();      /* restore working from backup so the cursor is
                          * still correct after the digit recompute */
    }

    DP_U8(0x0089) = 0x78;                 /* restore row scroll */
}

/* ========================================================================
 * sub_CDC6 ($00:CDC6) — Auto-button substate
 * ======================================================================== */
static void cp_substate_auto_CDC6(void)
{
    if (DP_STATE == 0x25)
        CP_BEHAVIOR_AUTO = 1;
    else
        CP_CASTE_AUTO    = 1;
    sub_8EA3(0x2C);                        /* APU "click" SFX */
    DP_INPUT_SUBSTATE = 0xFF;
}

/* sub_CDE5 — Manual-button substate. */
static void cp_substate_manual_CDE5(void)
{
    if (DP_STATE == 0x25)
        CP_BEHAVIOR_AUTO = 0;
    else
        CP_CASTE_AUTO    = 0;
    sub_8EA3(0x2C);
    DP_INPUT_SUBSTATE = 0xFF;
}

/* sub_CDA5 — sound-only substate (purpose unclear; just plays a click). */
static void cp_substate_click_CDA5(void)
{
    sub_8EA3(0x2C);
    DP_INPUT_SUBSTATE = 0xFF;
}

/* sub_CE04 — % / # toggle substate. */
static void cp_substate_toggle_pct_count_CE04(void)
{
    /* dp[$0044] = 1 - dp[$0044]   (toggle 0 ↔ 1) */
    CP_PCT_DISPLAY = (uint8_t)(1 - CP_PCT_DISPLAY);
    sub_8EA3(0x2C);
    DP_INPUT_SUBSTATE = 0xFF;
    sub_877D();
    cp_redraw_CF05();
}

/* Sub-state dispatch table at $00:CDBA — 6 entries.  Indexed by the
 * "icon you clicked" (which is stored in $0028 by the popup helper). */
typedef void (*cp_substate_fn)(void);
static cp_substate_fn const cp_substate_table_CDBA[6] = {
    cp_substate_auto_CDC6,         /* 0 -> Auto */
    cp_substate_manual_CDE5,       /* 1 -> Manual */
    cp_substate_click_CDA5,        /* 2 -> click-only */
    cp_substate_toggle_pct_count_CE04, /* 3, 4, 5 all reuse $CE04 */
    cp_substate_toggle_pct_count_CE04,
    cp_substate_toggle_pct_count_CE04,
};

/* ========================================================================
 * state_24/$26 setup ($00:CA96)
 * ------------------------------------------------------------------------
 * Builds the Behavior (state $24) or Caste (state $26) panel layout:
 *   - BG mode 9 with the same OAM tile bank as the views
 *   - palette/tile loads at $E371, $B380 (Behavior) / $B671 (Caste),
 *     $F76C, $B975, $FEEC (Behavior) / $A03E (Caste), and the font at
 *     $8AE3.
 *   - spawns:
 *       * the cursor (type $02)
 *       * three caste-icon entities at the left edge:
 *           Behavior:  $27 (Forage icon), $29 (Dig icon), $2B (Nurse icon)
 *           Caste:     $28 (Worker icon), $2A (Soldier icon), $2B (Breeder)
 *       * the Auto/Manual highlight pair:
 *           Behavior: type $24 at (0x70, 0x38) and (0x90, 0x38)
 *           Caste:    type $23 at (0x70, 0x30)
 *       * the bottom-row toggle icons (% / # / etc.)
 *       * the "end" sentinels $2C and $20
 *
 * After spawning, the setup calls $CE87 (Behavior auto-draw) or $CEAA
 * (Caste auto-draw) once to seed the cross-cursor from the persistent
 * percentages, then advances $0B (so state $25 / $27 takes over).
 * ======================================================================== */
static void cp_state_setup_CA96(void)
{
    sub_8976();
    INIDISP = 0x80;
    DP_U8(0x004F) = 0;
    sub_C318();
    DP_U8(0x0023) = 0x02;
    sub_8D94();
    DP_U8(0x0019) = 0xB0;
    DP_U8(0x0064) = 0x02;
    BGMODE = 0x09;
    sub_C398();
    OBSEL = 0x62;
    sub_8F08();
    DP_U8(0x008C) = 0x2C;
    DP_U8(0x0089) = 0x78;
    DP_U16(0x008A) = 0x01E0;

    sub_499C1(0, 0, 0x02);                 /* cursor */
    DP_U8(0x0049) = 0;
    DP_U8(0x004B) = 0x18;
    sub_499C1(0, 0, 0x2D);                 /* "click hint" cursor */

    /* The two paths differ in WHICH entity types end up at the same
     * screen coordinates. The 0x9B55 + 0x01 stamp at $0011/$0013 is the
     * label-string pointer per panel (encoded by sub_C28A). */
    *(uint16_t *)&wram[0x0011] = 0x9B55;
    DP_U8(0x0013) = 0x01;

    DP_INPUT_SUBSTATE = 0xFF;

    if (DP_STATE == 0x24) {
        /* Behavior icons (Forage / Dig / Nurse): types 27/29/2B. */
        sub_499C1(0x0024, 0x002C, 0x27);
        sub_499C1(0x0024, 0x003C, 0x29);
        sub_499C1(0x0024, 0x0054, 0x2B);
        /* Auto/Manual icon pair: type $24 at (0x70, 0x38) and (0x90, 0x38) */
        sub_499C1(0x0070, 0x0038, 0x24);
        sub_499C1(0x0090, 0x0038, 0x24);
        /* Bottom-row toggle icons: types 25 / 25 / 26. */
        sub_499C1(0x0010, 0x00C8, 0x25);
        sub_499C1(0x0030, 0x00C8, 0x25);
        sub_499C1(0x00F0, 0x00C8, 0x26);
        sub_499C1(0, 0, 0x2C);
        sub_499C1(0, 0, 0x20);
        DP_U8(0x0012) = 0x00;             /* panel-id = Behavior */
    } else {
        /* Caste icons (Workers / Soldiers / Breeders): types 28/2A/2B. */
        sub_499C1(0x0024, 0x002C, 0x28);
        sub_499C1(0x0024, 0x003C, 0x2A);
        sub_499C1(0x0024, 0x0054, 0x2B);
        sub_499C1(0x0070, 0x0030, 0x23);
        sub_499C1(0x0010, 0x00C8, 0x22);
        sub_499C1(0x00D0, 0x00C8, 0x21);
        sub_499C1(0, 0, 0x2C);
        sub_499C1(0, 0, 0x20);
        DP_U8(0x0012) = 0x01;             /* panel-id = Caste */
    }

    /* Asset loads — these decompress tile data into VRAM. */
    sub_8D7E(0x16, 0xE371); sub_8ACC(0x4000, 0x0000);   /* BG tiles */
    if (DP_STATE == 0x24) {
        sub_8D7E(0x07, 0xB380);                          /* Behavior palette */
    } else {
        sub_8D7E(0x07, 0xB671);                          /* Caste palette */
    }
    sub_8ACC(0x0800, 0x7000);
    sub_8D7E(0x16, 0xF76C); sub_8ACC(0x2000, 0x3000);
    sub_8D7E(0x07, 0xB975); sub_8ACC(0x0800, 0x7400);
    if (DP_STATE == 0x24) {
        sub_8D7E(0x16, 0xFEEC); sub_8ACC(0x2000, 0x4000);
        sub_8D7E(0x17, 0x8F2F);                          /* "Forage/Dig/Nurse" labels */
    } else {
        sub_8D7E(0x17, 0xA03E); sub_8ACC(0x2000, 0x4000);
        sub_8D7E(0x17, 0xB1E9);                          /* "Workers/Soldiers/Breeders" */
    }
    sub_8ACC(0x2000, 0x5000);
    sub_8D7E(0x10, 0x8AE3); sub_8ACC(0x1000, 0x6000);    /* font */
    sub_8AED(0x07, (DP_STATE == 0x24) ? 0x9000 : 0x9200);

    sub_867F(0x00, 0x7800);                              /* clear OAM strip */

    /* First snap of cursor from persistent values. */
    if (DP_STATE == 0x24) cp_behavior_load_for_display_CE87();
    else                  cp_caste_load_for_display_CEAA();
    cp_redraw_CF05();

    /* APU command — different SFX track per panel. */
    sub_8E88((DP_STATE == 0x24) ? 0x0C : 0x0E);

    DP_U8(0x0025) = 0x08;
    DP_U8(0x0026) = 0x18;
    /* (sub_896D, sub_8629, etc. — see states_gameplay.c for the tail.) */
    DP_U8(0x001E) = DP_U8(0x0016);
    DP_U8(0x0026)++;
    DP_STATE++;                                          /* -> state $25/$27 */
}

/* ========================================================================
 * state_25/$27 run loop ($00:CCD0)
 * ------------------------------------------------------------------------
 * Per-frame:
 *   1. JSR $877D (yield); if paused, JSR $A0D2.
 *   2. If sub_DF79 returns "vsync wait" (carry), call $A3D6 and retry.
 *   3. JSR $A243 (entity step).
 *   4. If $0B is no longer $25/$27, bail to exit cleanup.
 *   5. JSR $A734 — universal tail; if it returned carry-set, bail.
 *   6. If $0028 ≥ 0 (= "popup picked an icon"), JMP through the
 *      sub-state table at $CDBA — that's the Auto/Manual/Toggle handlers.
 *   7. Otherwise:
 *        - if the panel is in AUTO mode, just refresh the cross-cursor
 *          from the live percentages (CE87/CEAA) + redraw (CF05).
 *        - if MANUAL mode AND A is held over the triangle:
 *            * compute triangle-space cursor (x = $14 - $4E, y = $A7 - $15)
 *            * if (x, y) is INSIDE the triangle:
 *                - backup weights via CE20 + cursor via CE31
 *                - JSR D034   (cursor -> weights)
 *                - validate weights ∈ [0, 100)  (CE3E)
 *                - if valid: JSR D074 (snap cursor); commit (CE9A/CEDB);
 *                            JSR CF05 redraw
 *                - if invalid: restore cursor via CE79
 *            * if A is NOT held or out of bounds: skip to button-poll
 *        - poll JOY1H: SELECT (bit 7) -> exit. Y (bit 6) -> recenter
 *          cursor to (0x24, 0x2C).
 *   8. Loop.
 * ======================================================================== */
/* WIKI: wiki/12-control-panels.md §5 ("Per-Frame Run Loop"). */
static void cp_state_run_CCD0(void)
{
    for (;;) {
        sub_877D();
        if (DP_PAUSED) sub_A0D2();
        DP_U8(0x02E3) = 0;
        if (sub_DF79()) {
            sub_A3D6();
            continue;
        }
        sub_A243();

        if (DP_STATE != 0x25 && DP_STATE != 0x27) goto exit_panel;
        sub_A734();
        if (DP_STATE != 0x25 && DP_STATE != 0x27) goto exit_panel;

        /* Sub-state dispatch — icon clicked from the popup. */
        if ((int8_t)DP_INPUT_SUBSTATE >= 0) {
            unsigned idx = DP_INPUT_SUBSTATE & 0x07;
            if (idx < 6 && cp_substate_table_CDBA[idx])
                cp_substate_table_CDBA[idx]();
            continue;
        }

        /* AUTO path */
        uint8_t auto_flag = (DP_STATE == 0x25) ? CP_BEHAVIOR_AUTO : CP_CASTE_AUTO;
        if (auto_flag) {
            if (DP_STATE == 0x25) cp_behavior_load_for_display_CE87();
            else                  cp_caste_load_for_display_CEAA();
            cp_redraw_CF05();
            goto poll_buttons;
        }

        /* MANUAL path: A must be held. */
        if (DP_MENU_LOCK == 0) {
            if ((JOY1L & 0x80) == 0) goto poll_buttons;
        } else if ((DP_MENU_INPUT & 0x01) == 0) {
            goto poll_buttons;
        }

        /* Compute triangle-space cursor. */
        cp_backup_cursor_CE31();
        DP_TRI_X = (uint16_t)((uint16_t)DP_CURSOR_X - TRI_BASE_X);
        DP_TRI_Y = (uint16_t)((uint16_t)TRI_BASE_Y - DP_CURSOR_Y);

        if (!cp_cursor_in_bounds_CE6B()) goto poll_buttons;

        cp_backup_weights_CE20();
        cp_triangle_xy_to_weights_D034();
        if (cp_validate_weights_CE3E()) {
            cp_triangle_weights_to_xy_D074();           /* snap cursor */
            cp_redraw_CF05();
            if (DP_STATE == 0x25) cp_behavior_commit_CE9A();
            else                  cp_caste_commit_CEDB();
        } else {
            cp_restore_cursor_CE79();
        }

poll_buttons:
        if (DP_MENU_LOCK == 0) {
            if (JOY1H & 0x80) goto exit_panel;          /* SELECT */
            if (JOY1H & 0x40) {                         /* Y: re-center cursor */
                DP_CURSOR_X = 0x24;
                DP_CURSOR_Y = 0x2C;
            }
        } else if (DP_MENU_INPUT & 0x02) {
            goto exit_panel;
        }
    }

exit_panel:
    sub_A3BD();
    DP_U8(0x0026) = 0;
    sub_8642();
    DP_U8(0x004F) = 0x01;
}

/* ========================================================================
 * AUTO-MODE policy
 * ========================================================================
 *
 * The dispatch in the run-loop (above) only shows the auto flags being
 * READ by the panel — there is no auto-adjustment code in bank $00.
 * The actual write paths to the persistent percentage cells:
 *
 *   $028A/$028C/$028E (Behavior) — written ONLY by:
 *       (a) the user via the joystick in Manual mode (CE9A)
 *       (b) the initial seed at $9700-$972E in state $1A
 *           (default: 60% Forage, 20% Dig, 20% Nurse)
 *
 *   $027E/$0280/$0282/$0284 (Caste) — written by:
 *       (a) the user via CEDB (Manual)
 *       (b) the initial seed at $9700-$9745
 *           (default: 60 Workers, 40 Soldiers, 0 Breeders)
 *       (c) the colony simulation: every time a new ant is born or one
 *           dies, the appropriate cell is bumped. This per-ant accounting
 *           lives in bank $04's entity step (handler T1-T4 readers at
 *           $04:9DD5/$04:9DEA/$04:9DFF/$04:9E14 detect the Auto flag and
 *           swap between the two ICON variants — that's the visible
 *           "Auto highlight" on the panel).
 *
 * Therefore "Auto mode" for the panels is implicit: when the flag is
 * set, the panel simply DISPLAYS the live simulation state (via CE87 /
 * CEAA) without overriding it; when the flag is clear, the user's
 * stored target overrides the simulation hint. There is no separate
 * Auto-balance algorithm — the simulation tick IS the auto algorithm,
 * and the panel just becomes a read-only view of it.
 *
 * For a port, the "Auto" rule is:
 *
 *   if (CP_BEHAVIOR_AUTO) {
 *       // The triangle reads $028A/$028C/$028E (which the sim updates).
 *       // Don't write them from the panel.
 *   } else {
 *       // User drags the cross; CE9A writes the targets.
 *   }
 *
 *   if (CP_CASTE_AUTO) {
 *       // Same idea, but for $027E/$0280/$0282/$0284.
 *   } else {
 *       // CEDB writes both $029X AND mirrors back to $027E etc.
 *   }
 *
 * The "Caste -> $027E/$0280/$0282/$0284 mirror" path in CEDB is what
 * makes Manual mode actually re-target the colony: it nudges the
 * simulation toward the new caste ratios by overwriting the raw counts.
 * ======================================================================== */

/* ========================================================================
 * ICON-MENU TIE-IN
 * ------------------------------------------------------------------------
 * The "Control Panel" icon in the bottom HUD is icon #2 of 6. From the
 * view-run states (e.g. $1E for Surface Overview), the cursor is read
 * each frame; if it clicks on the icon row at the right Y, $00:9D48 is
 * called. That function reads dp[$28] as the icon index, then jumps
 * through the table at $00:9D60:
 *
 *   icon 0 -> $9D6C  View    (submenu @ $01:8756, 6 items)
 *   icon 1 -> $9DC3  Scent   (submenu @ $01:87C9, 5 items)
 *   icon 2 -> $9DE3  CONTROL (submenu @ $01:8809, 2 items: Behavior, Caste)
 *   icon 3 -> $9DFE  Save    (submenu @ $01:881D, 2 items: Save Game, Main Menu)
 *   icon 4 -> $9FB2  Evaluation (multiple sub-screens)
 *   icon 5 -> $9FFA  Options
 *
 * Icon 2's body (at $00:9DE3) calls sub_9187 with:
 *   X = $0A05            (popup X=5, Y=10 — tile coords)
 *   Y = $8809            (count byte + ptr table)
 *   A = $0A              (popup tile-width or row count)
 *
 * sub_9187 paints the submenu and waits. On dismiss (BCS) the icon
 * handler RTSes; on confirm (BCC) it reads dp[$1A] (selection index)
 * and sets $0B to:
 *   $24  if index == 0  (Behavior)
 *   $26  if index == 1  (Caste)
 *
 * Setting $0B fires state $24 / $26 (the panel setup) on the NEXT
 * dispatch tick, which advances to $25 / $27 (the panel run loop).
 *
 * The submenu data at $01:8800 is:
 *   $8800: ff                    (terminator of previous submenu's strings)
 *   $8809: 02                    (count of items)
 *   $880A: 0E 88                 (ptr -> $880E = "Behavior")
 *   $880C: 17 88                 (ptr -> $8817 = "Caste")
 *   $880E: "Behavior" + FF
 *   $8817: "Caste"   + FF
 * ======================================================================== */

void cp_icon2_control_pick(void)
{
    /* Real code: JSR $9187 (popup); on success $1A = 0|1 -> $0B = 0x24|0x26. */
    /* The body here is a model — the harness in states_gameplay sets $0B
     * via the popup helper. We just expose the dispatch outcome. */
    extern uint8_t sub_9187_popup(uint8_t a, uint16_t y, uint16_t x);
    uint8_t carry = sub_9187_popup(/*a=*/0x0A, /*y=*/0x8809, /*x=*/0x0A05);
    if (carry) return;                              /* user cancelled */
    DP_STATE = (DP_U8(0x001A) == 0) ? 0x24 : 0x26;
}

/* ========================================================================
 * STATE-1A INITIAL SEED  ($00:9700-$9745)
 * ------------------------------------------------------------------------
 * This runs once when a brand-new (or load-from-empty) game starts. It
 * puts both panels in AUTO mode and seeds the percentages and counts
 * with the documented defaults:
 *
 *   Behavior:  Forage 60%, Dig 20%, Nurse 20%   (sum = 100)
 *   Caste:     Workers 60, Soldiers 40, Breeders 0
 *
 * The "$0044 = 0" default means the panel comes up in "# of ants" mode;
 * the user toggles to % via the bottom icon.
 * ======================================================================== */
/* WIKI: wiki/12-control-panels.md §7 ("Initial Seed"). */
void cp_seed_initial_9700(void)
{
    DP_U8(0x004C) = 0;
    CP_BEHAVIOR_AUTO = 1;
    CP_CASTE_AUTO    = 1;

    /* (state-1A also zeros $02BB/$02BD/$02A7 — not control-panel state.) */

    CP_BHV_TOP   = 0x003C;       /* Forage = 60 */
    CP_BHV_LEFT  = 0x0014;       /* Dig    = 20 */
    CP_BHV_RIGHT = 0x0014;       /* Nurse  = 20 */

    CP_COLONY_SOLDIERS  = 0x0028;       /* 40 */
    CP_COLONY_WORKERS   = 0x003C;       /* 60 */
    CP_COLONY_BREEDERS_A = 0x0000;
    CP_COLONY_BREEDERS_B = 0x0000;

    DP_U8(0x0081) = 0x01;        /* "world initialized" flag */
}

/* ========================================================================
 * PUBLIC ENTRY POINTS — match the dispatch table in simant.c
 * ======================================================================== */

void state_24_behavior_panel_setup(void) { cp_state_setup_CA96(); }
void state_25_behavior_panel_run(void)   { cp_state_run_CCD0(); }
void state_26_caste_panel_setup(void)    { cp_state_setup_CA96(); }
void state_27_caste_panel_run(void)      { cp_state_run_CCD0(); }

/* ========================================================================
 * NOTES FOR A FUTURE PORT
 * ------------------------------------------------------------------------
 * 1. The triangle joystick math (D034 / D074) is COORDINATE-FREE: the
 *    triangle has base 100 and height 87, but those are convenient
 *    integers — the actual constant that drives the slope is
 *    $1BB67 / $10000 = 1.73218..., i.e. √3. To port to a different
 *    pixel-resolution triangle, recompute the constant as
 *
 *        K = (1 << 16) / sqrt(3) * (height_px / 87)
 *
 *    and replace 0xBB67 / 0x0001 accordingly.
 *
 * 2. The cross-cursor is drawn by the cursor entity (type $02), not by
 *    this panel. The panel only WRITES the cursor position via $14/$15
 *    (which are the cursor's screen coords in dp). The entity step in
 *    bank $04 picks those up.
 *
 * 3. The Y-button (re-center) bounces the cursor to (0x24, 0x2C), which
 *    is the *icon column*, not the center of the triangle. That's
 *    intentional — pressing Y on the panel jumps the cursor off the
 *    joystick onto the Auto/Manual icons, so the user can switch modes
 *    with one button-press.
 *
 * 4. SELECT exits the panel (back to the underlying view). B doesn't
 *    do anything on the panel — it's reserved for the higher-level
 *    pause toggle.
 *
 * 5. The percentage display reads tile $9B6F ("%") in % mode and $9B71
 *    ("of") in count mode. The Absolute-count mode draws "X of Y"
 *    where Y is the total live ant population at $7F:EB60.
 * ======================================================================== */

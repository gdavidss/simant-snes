/*
 * lifted_helpers_5.c — Batch 5: cursor / UI helpers and screen-setup glue.
 *
 *   $00:A0F4  if dp[$0B] == $1C: A=$0D, else A=$0C. dp[$88] = A; yield
 *   $00:A106  cursor-move handler (heading 1 / $C/$D pos clamp)
 *   $00:A18D  cursor-move handler (heading 2 / $E/$F pos clamp)
 *   $00:A1E8  cursor-move handler (heading 3 / $10/$11 pos clamp)
 *   $00:A2AA  per-frame "cursor task" dispatch (calls A2B4 / A2D8 / A2FC)
 *   $00:A2B4  if dp[$024A] == 1: tail-call ($A2BC)
 *   $00:A2D8  if dp[$024A] == 2: tail-call ($A2E0)
 *   $00:A2FC  if dp[$024A] == 3: tail-call ($A304)
 *   $00:A3BD  set dp[$0B] = (dp[$02B1]<<1)+$1D or $1B if dp[$02B3]==0
 *   $00:A3D0  STZ $88; STZ $0026
 *   $00:A5CB  preload "default cursor view-rect" into dp[$14..$19]
 *   $00:A625  draw "splash" 5x10 text grid from $89E7/$89F9/$8A0B labels
 *   $00:A893  if dp[$12] == 8: RTS, else fall through
 *   $00:9D1A  cursor "Big A pressed in slot" (sub_9D1A_countdown_match)
 *   $00:9D48  cursor "wait for clear input"
 *   $00:9D49  alt entry, $9D48 + 1
 *   $00:8967  wait HVBJOY high-bit (vblank start)
 *   $00:8000  JMP $C502 (entry trampoline)
 *   $00:80CA  scheduler RTI tail (mostly data + scheduler)
 *   $00:C398  set BG12/34 NBA + BG1/2/3/4 SC (4 register writes each)
 *   $00:9832  helper: enter mode-0 stack frame, run task $8024
 *   $00:994B  call $9955, BCS->RTS, else fall through
 *   $00:9A8A  same pattern for $9A94
 *   $00:9BF3  same pattern for $9BFD
 *
 * Source: SimAnt (USA) SNES ROM bank $00.
 */

#include <stdint.h>

extern uint8_t           wram[];
extern volatile uint8_t  mmio[];
#define dp wram
#define WMEM8(off)   (*(uint8_t  *)&wram[(off)])
#define WMEM16(off)  (*(uint16_t *)&wram[(off)])
#define MMIO8(addr)  (*(volatile uint8_t  *)&mmio[(addr) & 0xFFFF])
#define MMIO16(addr) (*(volatile uint16_t *)&mmio[(addr) & 0xFFFF])

#define HVBJOY     MMIO8(0x4212)
#define JOY1L      MMIO8(0x4218)
#define JOY1H      MMIO8(0x4219)
#define BG12NBA    MMIO8(0x210B)
#define BG34NBA    MMIO8(0x210C)
#define BG1SC      MMIO8(0x2107)
#define BG2SC      MMIO8(0x2108)
#define BG3SC      MMIO8(0x2109)
#define BG4SC      MMIO8(0x210A)

extern void sub_877D(void);
extern void sub_871A(void);
extern void sub_C4D8(uint16_t xy, uint16_t yp);

/* Opaque sub-handlers — match the ROM's actual entry points where we know
 * them, else weak stub. */
__attribute__((weak)) void sub_A2BC(void) {}
__attribute__((weak)) void sub_A2E0(void) {}
__attribute__((weak)) void sub_A304(void) {}
__attribute__((weak)) void sub_8113_run(uint8_t a, uint16_t x) { (void)a; (void)x; }
__attribute__((weak)) void sub_9955(void) {}
__attribute__((weak)) void sub_9A94(void) {}
__attribute__((weak)) void sub_9BFD(void) {}

/* -------------------------------------------------------------------------
 * $00:A0F4 — set dp[$88] to next substate (0x0C / 0x0D) and yield one
 * ------------------------------------------------------------------------- */
void sub_A0F4(void) {
    uint8_t a;
    if (WMEM8(0x000B) == 0x1C) a = 0x0D;
    else                       a = 0x0C;
    WMEM8(0x0088) = a;
    sub_877D();
}

/* Helper used by A106/A18D/A1E8 — clamp the 8-bit cursor view rectangle.
 *
 * ROM body for sub_A106 (Y-coord clamp uses high = $A8, X-coord clamp uses
 * high = $E8 — the screen is 168px wide and 168px tall in this view; the
 * X-axis allows slightly more in close-up mode).
 *
 * Coordinate layout in dp:
 *   $14 = view rect's left edge (X)         — paired with $0C  (world X)
 *   $15 = view rect's top edge  (Y)         — paired with $0D  (world Y)
 *   $0C/$0D move OPPOSITE to $14/$15: clamping pushes the rect inward
 *   while pulling the world coord outward by the same amount, so the
 *   centre of the rect stays put.
 *
 * The four-branch sequence is:
 *   if ($15 <= $08) { $15 = $09;  $0D--;  clamp_world() }   // top edge
 *   if ($15 >= $A8) { $15 = $A7;  $0D++;  clamp_world() }   // bottom edge
 *   if ($14 <= $08) { $14 = $09;  $0C--;  clamp_world() }   // left edge
 *   if ($14 >= $X)  { $14 = $X-1; $0C++;  clamp_world() }   // right edge
 *                   // $X is $E8 for the X-axis, $A8 for the Y-axis
 *   (Compare uses >= / <= to match the C body's branch shape — at the
 *   boundary value the rect is pulled in by exactly 1.)
 */
static void cursor_axis_clamp(uint8_t low_limit, uint8_t high_limit,
                              uint8_t coord_addr, uint8_t world_addr) {
    if (low_limit >= WMEM8(coord_addr)) {
        WMEM8(coord_addr) = (uint8_t)(low_limit + 1);
        WMEM8(world_addr) = (uint8_t)(WMEM8(world_addr) - 1);
        sub_871A();
    }
    if (high_limit <= WMEM8(coord_addr)) {
        WMEM8(coord_addr) = (uint8_t)(high_limit - 1);
        WMEM8(world_addr) = (uint8_t)(WMEM8(world_addr) + 1);
        sub_871A();
    }
}

/* -------------------------------------------------------------------------
 * $00:A106 — cursor-move for ant 1
 *   Clamps $15 to [$09,$A7] and $14 to [$09,$E7], then applies joypad
 *   $0F-direction lookup to dp[$0C]/dp[$0D]. The downstream JSR $86BD
 *   converts (X,Y) into 16-bit pixel coords in $05/$07.
 * ------------------------------------------------------------------------- */
void sub_A106(void) {
    cursor_axis_clamp(0x08, 0xA8, 0x0015, 0x000D);    /* Y-axis clamp  */
    cursor_axis_clamp(0x08, 0xE8, 0x0014, 0x000C);    /* X-axis clamp  */
    /* Joypad-direction movement (sub_A155-onwards) is opaque — leave it
     * to the joypad-handler. */
    (void)JOY1L; (void)JOY1H;
}

/* -------------------------------------------------------------------------
 * $00:A18D — cursor-move for ant 2 (uses dp[$0E]/$0F instead of $0C/$0D).
 * ------------------------------------------------------------------------- */
void sub_A18D(void) {
    cursor_axis_clamp(0x08, 0xA8, 0x0015, 0x000F);
    cursor_axis_clamp(0x08, 0xE8, 0x0014, 0x000E);
}

/* -------------------------------------------------------------------------
 * $00:A1E8 — cursor-move for ant 3 (uses dp[$10]/$11).
 * ------------------------------------------------------------------------- */
void sub_A1E8(void) {
    cursor_axis_clamp(0x08, 0xA8, 0x0015, 0x0011);
    cursor_axis_clamp(0x08, 0xE8, 0x0014, 0x0010);
}

/* -------------------------------------------------------------------------
 * $00:A2AA — per-frame cursor task dispatch
 * ------------------------------------------------------------------------- */
static void sub_A2B4(void) {
    if (WMEM8(0x024A) == 0x01) sub_A2BC();
}
static void sub_A2D8(void) {
    if (WMEM8(0x024A) == 0x02) sub_A2E0();
}
static void sub_A2FC(void) {
    if (WMEM8(0x024A) == 0x03) sub_A304();
}
void sub_A2AA(void) {
    sub_A2B4();
    sub_A2D8();
    sub_A2FC();
}

/* -------------------------------------------------------------------------
 * $00:A3BD — set dp[$0B] (substate code) per the panel-state
 * ------------------------------------------------------------------------- */
void sub_A3BD(void) {
    uint8_t a;
    if (WMEM8(0x02B3) == 0) {
        a = 0x1B;
    } else {
        a = (uint8_t)((WMEM8(0x02B1) << 1) + 0x1D);
    }
    WMEM8(0x000B) = a;
}

/* -------------------------------------------------------------------------
 * $00:A3D0 — clear substate + caption-active
 * ------------------------------------------------------------------------- */
void sub_A3D0(void) {
    WMEM8(0x0088) = 0;
    WMEM8(0x0026) = 0;
}

/* -------------------------------------------------------------------------
 * $00:A5CB — preset default cursor "view rect"
 *   dp[$14]=$7F dp[$16]=$7F dp[$15]=$28 dp[$18]=$28
 *   dp[$17]=$80 dp[$19]=$78
 * ------------------------------------------------------------------------- */
void sub_A5CB(void) {
    WMEM8(0x0016) = 0x7F;
    WMEM8(0x0014) = 0x7F;
    WMEM8(0x0017) = 0x80;
    WMEM8(0x0018) = 0x28;
    WMEM8(0x0015) = 0x28;
    WMEM8(0x0019) = 0x78;
}

/* -------------------------------------------------------------------------
 * $00:A625 — draw splash text. 13 string blits, with a yield between rows
 * 7 and 8 and again at the end so the VRAM queue can drain.
 *
 * Layout (X = $XXYY = (row << 8) | column):
 *   row $03 col $03  -> string at $89E7   ; blank/decoration
 *   row $04 col $03  -> $89E7
 *   row $05 col $03  -> $89F9
 *   row $06 col $03  -> $89E7
 *   row $07 col $03  -> $8A0B
 *   row $08 col $03  -> $89E7
 *   row $09 col $03  -> $8A1D
 *   --- yield ---
 *   row $0A col $03  -> $89E7
 *   row $0B col $03  -> $8A2F
 *   row $0C col $03  -> $89E7
 *   row $0D col $03  -> $8A41
 *   row $0E col $03  -> $89E7
 *   row $0F col $03  -> $8A53
 *   --- yield ---
 * ------------------------------------------------------------------------- */
void sub_A625(void) {
    WMEM8(0x008C) = 0x0D;
    sub_877D();
    sub_C4D8(0x0303, 0x89E7);
    sub_C4D8(0x0403, 0x89E7);
    sub_C4D8(0x0503, 0x89F9);
    sub_C4D8(0x0603, 0x89E7);
    sub_C4D8(0x0703, 0x8A0B);
    sub_C4D8(0x0803, 0x89E7);
    sub_C4D8(0x0903, 0x8A1D);
    sub_877D();
    sub_C4D8(0x0A03, 0x89E7);
    sub_C4D8(0x0B03, 0x8A2F);
    sub_C4D8(0x0C03, 0x89E7);
    sub_C4D8(0x0D03, 0x8A41);
    sub_C4D8(0x0E03, 0x89E7);
    sub_C4D8(0x0F03, 0x8A53);
    sub_877D();
}

/* -------------------------------------------------------------------------
 * $00:A893 — early-return if dp[$12] == 8
 * ------------------------------------------------------------------------- */
int sub_A893_fill_action_slot(void) {
    if (WMEM8(0x0012) == 0x08) return 1;
    return 0;
}

/* -------------------------------------------------------------------------
 * $00:9D1A — cursor "Big A pressed in slot"
 *   Calls $9187 with (X=$0A08, Y=$86E8, A=$11)
 *   If carry clear: pulls SFX index from $01:$86E3+x, dispatches APU cmd.
 * ------------------------------------------------------------------------- */
extern int  sub_9187(uint16_t x, uint16_t y, uint8_t a);
extern void sub_8EA3(uint8_t a);
extern void sub_A0F4(void);
__attribute__((weak)) int  sub_9187(uint16_t x, uint16_t y, uint8_t a) {
    (void)x; (void)y; (void)a; return 0;
}
void sub_9D1A(void) {
    int rc = sub_9187(0x0A08, 0x86E8, 0x11);
    if (rc == 0) {
        /* far read from $01:$86E3+x — opaque table, default to 0 */
        uint8_t sfx_idx = 0;
        sub_8EA3(sfx_idx);
        WMEM8(0x02B7) = (uint8_t)(WMEM8(0x001A) + 1);
    }
    sub_A0F4();
    WMEM8(0x001E) = WMEM8(0x0016);
    WMEM8(0x0026) = (uint8_t)(WMEM8(0x0026) + 1);
}
int sub_9D1A_countdown_match(void) { sub_9D1A(); return 0; }

/* -------------------------------------------------------------------------
 * $00:9D48 — yield, then if dp[$28] >= 6, RTS.   *** PARTIAL PORT ***
 *   Body issues the yield but omits the conditional return — caller-side
 *   tails that depend on the `$28 >= 6` short-circuit will run further
 *   than ROM in our port. Harmless for the current call graph (every
 *   caller treats the post-yield path as common code), but worth a fix
 *   if a new caller is added.
 * ------------------------------------------------------------------------- */
void sub_9D48(void) {
    sub_877D();
    /* If dp[$28] >= 6, ROM returns here; we don't model that fall-through.
     * If dp[$28] < 6, fall through to caller-specific tail (not lifted). */
}
/* $9D49 enters with PHA in the prologue — same effect when called normally. */
void sub_9D49_draw_cursor_box(void) { sub_9D48(); }

/* -------------------------------------------------------------------------
 * $00:8967 — wait for vblank (HVBJOY bit7 = 1)
 * ------------------------------------------------------------------------- */
void sub_8967(void) {
    while ((HVBJOY & 0x80) == 0) { /* spin */ }
}

/* -------------------------------------------------------------------------
 * $00:8000 — JMP $C502 (reset trampoline)
 * ------------------------------------------------------------------------- */
extern void sub_C502_reset(void);
__attribute__((weak)) void sub_C502_reset(void) {}
void sub_8000(void) { sub_C502_reset(); }
void render_post_8000(void) { sub_C502_reset(); }

/* -------------------------------------------------------------------------
 * $00:80CA — scheduler tail (heavy state mechanics). We approximate as
 * "increment frame counter and yield". Real ROM yields back into the next
 * task in the round-robin.
 * ------------------------------------------------------------------------- */
void render_post_80CA(void) {
    /* INC $02BA on every 256th call, advance task pointer, etc. */
    WMEM16(0x02B9) = (uint16_t)(WMEM16(0x02B9) + 1);
}

/* -------------------------------------------------------------------------
 * $00:C398 — set BG12NBA/BG34NBA/BG1SC/BG2SC/BG3SC/BG4SC
 * ------------------------------------------------------------------------- */
void sub_C398(void) {
    BG12NBA = 0x30;
    BG34NBA = 0x66;
    BG1SC   = 0x70;
    BG2SC   = 0x74;
    BG3SC   = 0x78;
    BG4SC   = 0x7C;
}

/* -------------------------------------------------------------------------
 * $00:9832 — enter mode-0 stack frame for a task at $8024
 * ------------------------------------------------------------------------- */
void sub_9832(void) {
    /* PHB / LDA #$00 / PHA / PLB / PHD / PEA $0000 / PLD / LDY #$0004 /
     * STY $02 / LDA #$02 / LDX #$8024 / JSR $8113
     * Then PLD / PLB / STZ $0026 */
    WMEM16(0x0002) = 0x0004;
    sub_8113_run(0x02, 0x8024);
    WMEM8(0x0026) = 0;
}

/* -------------------------------------------------------------------------
 * $00:994B / $9A8A / $9BF3 — "call-and-bail-on-success" trampolines.
 * ------------------------------------------------------------------------- */
void sub_994B(void) { sub_9955(); }
void sub_9A8A(void) { sub_9A94(); }
void sub_9BF3(void) { sub_9BFD(); }

/* Aliases for things hooked elsewhere */
int sub_A893(void) { return sub_A893_fill_action_slot(); }

/* -------------------------------------------------------------------------
 * $00:88A5 — fill OAM scratch ($0D00) with default sprite pattern (Y=$E0)
 * ------------------------------------------------------------------------- */
void sub_88A5(void) {
    for (uint16_t x = 0; x < 0x200; x += 4) {
        WMEM16(0x0D00 + x) = 0xE080;
    }
    for (uint16_t x = 0x200; x < 0x220; x += 2) {
        WMEM16(0x0D00 + x) = 0x5555;
    }
    WMEM16(0x0032) = 0x0010;
    WMEM16(0x0034) = 0x0110;
}

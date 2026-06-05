/*
 * render_helpers.c — small per-view / per-frame helpers and the two
 * "evaluation" data renderers (History Graph + Population Graph).
 *
 * Lifted faithfully from bank $00 of the SimAnt (USA) SNES ROM.
 *
 * Conventions match simant.c / states_gameplay.c:
 *   - wram[] is the 128 KB SNES WRAM; dp == wram[] for direct page.
 *   - 16-bit reads/writes use WMEM16 over wram[]; bank $7F mirrors bank
 *     $7E for addresses >= $E000, so the absolute-long stores the ROM
 *     uses ($7F:Exxx, $7F:Fxxx) land in wram[$Exxx]/wram[$Fxxx] of our
 *     128 KB image.
 *   - The 8-bit MMIO macros (INIDISP, VMADDL, etc.) come from simant.c
 *     via the same extern volatile mmio[] table.
 *
 * Routines lifted here (in ROM-address order):
 *
 *   $00:A0D2  sub_A0D2  pause-toggle internal (RUN/PAUSE music command)
 *   $00:A243  sub_A243  per-frame "view needs run" pump + cursor-action
 *                       dispatch (X-button -> A2AA -> per-icon back-out)
 *   $00:A734  sub_A734  universal per-frame tail — jumps through a
 *                       4-entry table at $A740 keyed by dp[$0299]
 *                       (active view-mode index)
 *   $00:C103  sub_C103  per-view OAM init: stamps two BG palette
 *                       templates ($9A8F/$9A90) then bakes six color-
 *                       math frames (one C6B0 call per round-robin
 *                       channel).
 *   $00:C3B7  sub_C3B7  cursor / row-of-icons gate. Spawns the 6 HUD
 *                       icons at (X=$0018, Y=$0018..$0068) and the
 *                       cursor-tail entity ($2D) with template $9A92.
 *   $00:C6B0  sub_C6B0  per-view BG color-math register write. Round-
 *                       robins through dp[$09] (0..5) into a 6-entry
 *                       table; each entry writes one BG channel's
 *                       gradient via the queue at $0C00.
 *   $00:CE87  sub_CE87  B-Nest interior renderer entry. Copies the
 *                       caste counts ($028A/$028C/$028E) into the
 *                       triangle scratch ($A4/$A6/$A8) and runs the
 *                       barycentric forward transform (D074).
 *   $00:CEAA  sub_CEAA  R-Nest interior renderer entry. Same as CE87
 *                       but uses $0290/$0292/$0294 (already mirrored
 *                       from the caste totals by CEC0).
 *   $00:CF05  sub_CF05  Nest scroll commit — picks the manual/auto
 *                       palette set ($9B6F vs $9B71) per dp[$0044]
 *                       and writes 3 tiles ($081C, $141E, $1407).
 *   $00:D470  D438/D470  History Graph "highlight current sample"
 *                       cursor + axis-label writer
 *   $00:D4AE  hist_render_all_D4AE  History Graph: replot all 4
 *                       channels by walking dp[$0045..$0048]
 *   $00:D4F1  hist_plot_one_D4F1   History Graph: plot ONE channel's
 *                       64-sample circular buffer
 *   $00:DCC1  pop_grid_draw_DCC1   Population Graph (House Screen):
 *                       7x7 grid of bar-chart cells for the 49 areas
 *                       (B colony = $EA46, R colony = $EAC6, with
 *                       per-area XY transform at DD40)
 *   $00:DE70  sub_DE70  Redraw all six views — first pass (template
 *                       $A03C) then call DE83
 *   $00:DE83  sub_DE83  Redraw all six views — middle pass; picks
 *                       template from the 6-entry table at $DEAA via
 *                       dp[$02B1] / dp[$02B4]
 *   $00:DEB6  sub_DEB6  Redraw all six views — final scrub: if both
 *                       $02B1 and $02B4 are clear, run 4x4 fast-frame;
 *                       otherwise one shot template write.
 */

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * External deps (declared in simant.c, states_gameplay.c, etc.).
 * ------------------------------------------------------------------------- */
extern uint8_t           wram[];
extern volatile uint8_t  mmio[];
#define dp wram
#define WMEM8(off)   (*(uint8_t  *)&wram[(off)])
#define WMEM16(off)  (*(uint16_t *)&wram[(off)])
#define MMIO8(addr)  (*(volatile uint8_t  *)&mmio[(addr) & 0xFFFF])

#define INIDISP    MMIO8 (0x2100)
#define VMADDL_R   (*(volatile uint16_t *)&mmio[0x2116])
#define VMDATAL_R  (*(volatile uint16_t *)&mmio[0x2118])

/* Helpers used here, defined elsewhere (simant.c / bank-04). */
extern void     sub_877D(void);                                  /* yield 1 frame                  */
extern void     sub_C516(uint16_t xy, uint16_t value);           /* draw 2-digit number            */
extern void     sub_C4D8(uint16_t xy, uint16_t y_ptr);           /* draw label string              */
extern void     sub_C593(uint16_t xy, uint8_t a);                /* per-pixel byte to queue        */
extern void     sub_C596(uint16_t xy, uint8_t a);                /* "lo half of word" emit         */
extern void     sub_C625(uint16_t xy, uint8_t a);                /* "hi half of word" emit         */
extern void     sub_C91F(uint16_t x, uint16_t y, uint8_t a);     /* template at (X,Y) w/ count A   */
extern void     sub_C9BB(uint16_t x, uint16_t y, uint8_t a);     /* template, alt entry            */
extern void     sub_C4BB(void);                                  /* unpack base palette idx        */
extern void     sub_8A9A(void);                                  /* AB <- 16-bit /10 (BCD helper)  */
extern void     sub_DFCD(uint8_t a);                             /* play SFX A                     */
extern void     sub_DF0A(void);                                  /* per-view "scroll one step"     */
extern void     sub_E260(void);                                  /* "RUN mode" common              */
extern void     sub_499C1(uint16_t x, uint16_t y, uint8_t a);    /* spawn entity (X=x,Y=y,A=type)  */
extern void     sub_8E88(uint8_t a);                             /* APUIO0 command (BGM)           */
extern void     sub_8EA3(uint8_t a);                             /* APUIO0 command (SFX)           */
extern void     sub_8AED(uint8_t a, uint16_t y);                 /* DMA palette block              */
extern void     sub_CE20(void);                                  /* save triangle scratch          */
extern void     sub_CE59(void);                                  /* restore triangle scratch       */
extern void     sub_CE6B(void);                                  /* check 2D cursor inside box     */
extern void     sub_D034(void);                                  /* triangle: (x,y)->3 percent     */
extern void     sub_D074(void);                                  /* triangle: 3 percent->(x,y)     */
extern void     sub_A2AA(void);                                  /* X-button -> cursor-action      */

/* Bank-04 "line plotter" helpers. The graph code talks to them via JSL. */
extern void     sub_490D2(void);                                 /* set DBR = $7E (start of plot)  */
extern void     sub_490DB(void);                                 /* restore DBR (end of plot)      */
extern void     sub_490E0(uint8_t a);                            /* expand A's 8 bits -> $E5..$EC  */
extern void     sub_490E2(uint8_t a);                            /* set "alt mask" $E6 for E4==4   */
extern void     sub_4911B(void);                                 /* init bg3 pixel-plot state      */
extern void     sub_4914F(uint8_t a, uint8_t y);                 /* plot first point  (X=color)    */
extern void     sub_493EF(uint8_t a, uint8_t y);                 /* plot next point   (line draw)  */
extern void     sub_49617(uint8_t a, uint8_t y);                 /* per-bar pixel write            */

/* The set of A0D2-style tails the dispatch table at $A740 lands on. */
static int      view_tail_select_A746(void);   /* +/- SELECT -> $0B=$16     */
static int      view_tail_save_A755(void);     /* Y-button   -> $0B=$23     */
static int      view_tail_action_A787(void);   /* X-button   -> $0B=$23/$28 */
static int      view_tail_landing_A7AD(void);  /* mark spot picked          */

/* Forward decls for the renderer-internal helpers (definitions later in
 * the file). */
void        sub_C6B0(void);
static void gradient_emit_C7A7(uint16_t xy_unused, uint8_t a);
static void sub_CF6D_nest_numbers(void);
void        sub_DE83(void);
extern void sub_CF8A_bounds_check(void);     /* lifted in control_panels.c */
extern void sub_CFDF_bounds_check(void);     /* lifted in control_panels.c */

/* ===========================================================================
 *  $00:A0D2  sub_A0D2  — pause toggle internal
 * ---------------------------------------------------------------------------
 *  Flips dp[$0002] between 4 and 6 (RUN vs PAUSE). On RUN ($0002 != 4) it
 *  also calls sub_E260 (the "RUN mode" common entry). Then it sends APU
 *  command $30 (BGM pause/resume) and zeros dp[$002A] (the pause-pending
 *  flag set by START).
 * ========================================================================= */
void sub_A0D2(void)
{
    uint8_t mode = dp[0x0002];
    if (mode != 0x04) {
        dp[0x0002] = 0x04;
        sub_E260();
    } else {
        dp[0x0002] = 0x06;
    }
    sub_8EA3(0x30);                /* JSL $00:8EA3 — APU cmd 30 (toggle BGM) */
    dp[0x002A] = 0;
}

/* ===========================================================================
 *  $00:A243  sub_A243  — per-frame "view needs run" pump
 * ---------------------------------------------------------------------------
 *  Early-out unless dp[$02B5] (the "view-state changed" trigger) is set.
 *  On entry it clears that flag, then re-gates on dp[$02A9] (the
 *  "view is alive" flag, set by the view-setup state). If alive, run
 *  the cursor-action dispatcher (A2AA), then re-stamp dp[$0B] (state
 *  index) from a small per-view fixup table indexed by dp[$024A] (which
 *  icon-row the cursor is on right now).
 *
 *  The fixup table is open-coded as a chain of inline JMPs at $A260,
 *  $A27E, $A294: each tests dp[$0B] for a specific "run" code; if
 *  matched it returns (the run-state is already correct), otherwise it
 *  overwrites $0B with the matching "re-render" state and sets
 *  dp[$02B1]/dp[$02B3] (per-view "pending refresh" flags) to seed the
 *  next frame's view-switch landing.
 * ========================================================================= */
void sub_A243(void)
{
    if (dp[0x02B5] == 0) return;
    dp[0x02B5] = 0;
    if (dp[0x02A9] == 0) return;

    sub_A2AA();                                       /* X-button gate */

    /* Per-view "icon row" fixup. dp[$024A] selects which of three icon
     * rows the cursor lives on; each row has its own "run" state that
     * we want to land on. (The ROM uses an indirect JMP through a small
     * "next row" table at $A260/$A27E/$A294; we open-code the cases.) */
    switch (dp[0x024A]) {
    case 0:                                           /* row 0 (Surface) */
        if (dp[0x0B] == 0x1E) return;
        dp[0x0B] = 0x1D;
        dp[0x02B1] = 0x00;
        dp[0x02B3] = 0x01;
        return;
    case 1:                                           /* row 1 (B-Nest) */
        if (dp[0x0B] == 0x20) return;
        dp[0x0B] = 0x1F;
        dp[0x02B1] = 0x01;
        dp[0x02B3] = 0x01;
        return;
    case 2:                                           /* row 2 (R-Nest) */
        if (dp[0x0B] == 0x22) return;
        dp[0x0B] = 0x21;
        dp[0x02B1] = 0x02;
        dp[0x02B3] = 0x01;
        return;
    default:
        return;
    }
}

/* ===========================================================================
 *  $00:A734  sub_A734  — universal per-frame view tail
 * ---------------------------------------------------------------------------
 *  Every view-run state ends with a JSR to this routine. ROM:
 *      REP #$20
 *      LDA $0299
 *      ASL
 *      TAX
 *      SEP #$20
 *      JMP ($A740,x)        ; opcode 7C — indexed indirect
 *
 *  Table at $00:A740 has only THREE 16-bit pointers (idx 0/1/2):
 *      [0] -> $A746   ICON_ACTION_MODE 0 — "SELECT-only" tail
 *      [1] -> $A755   ICON_ACTION_MODE 1 — main in-view dispatcher
 *      [2] -> $A787   ICON_ACTION_MODE 2 — submenu-confirmation
 *
 *  Each handler returns SEC if it consumed a state change, CLC otherwise.
 *  The caller uses the carry flag to decide whether to break the
 *  view-run inner loop. */
int sub_A734(void)
{
    static int (*const view_tail_table_A740[3])(void) = {
        view_tail_select_A746,   /* [0] $A746 — SELECT (bit $20) -> $0B=$16 */
        view_tail_save_A755,     /* [1] $A755 — bits $08/$10/$01 dispatch    */
        view_tail_action_A787,   /* [2] $A787 — bit $10 with sub-mode        */
    };
    uint8_t i = dp[0x0299];
    if (i >= 3) return 0;         /* defensive — shouldn't happen           */
    return view_tail_table_A740[i]();
}

/* ---- $A746 — entry [0]: SELECT-only -------------------------------------
 *  if (joypad_event & $20)  -> dp[$0B] = $16,  carry-set, return
 *  else                                       carry-clear, return         */
static int view_tail_select_A746(void)
{
    if (dp[0x02A7] & 0x20) {
        dp[0x0B] = 0x16;
        return 1;
    }
    return 0;
}

/* ---- $A755 — entry [1]: B-button -> $23 (with SFX $2B/$25/$2C) ----------
 *  if (event & $08)  SFX $2B, $0B=$23, carry-set
 *  if (event & $10)  SFX $25, $0B=$23, carry-set
 *  if (event & $01)  SFX $2C, $0B=$23, carry-set
 *  else                              carry-clear                          */
static int view_tail_save_A755(void)
{
    uint8_t e = dp[0x02A7];
    if (e & 0x08) { sub_DFCD(0x2B); dp[0x0B] = 0x23; return 1; }
    if (e & 0x10) { sub_DFCD(0x25); dp[0x0B] = 0x23; return 1; }
    if (e & 0x01) { sub_DFCD(0x2C); dp[0x0B] = 0x23; return 1; }
    return 0;
}

/* ---- $A787 — entry [2]: X-button (with sub-modes for dp[$02ED]) ---------
 *  if (event & $10):
 *      if (dp[$02ED] < 2)              SFX $28, $0B=$23, carry-set
 *      else                            SFX $2A, $0B=$28, REP $20
 *                                      stamp $0054=1, $7F:E744=1, clear
 *                                      dp[$02A7], carry-set
 *  else if (no other event branch)     fall through to default
 *  Common tail at $A7C4:
 *      if (dp[$02EF]==0 && dp[$02ED]==$31)
 *          SFX $29, $0B=$23, carry-set
 *      else                            carry-clear                         */
static int view_tail_action_A787(void)
{
    uint8_t e = dp[0x02A7];
    if (e & 0x10) {
        if (dp[0x02ED] < 0x02) {
            sub_DFCD(0x28);
            dp[0x0B] = 0x23;
            return 1;
        }
        sub_DFCD(0x2A);
        dp[0x0B]   = 0x28;
        WMEM16(0x0054)   = 0x0001;
        WMEM16(0x1E744)  = 0x0001;   /* $7F:E744 -> wram[$1E744]           */
        dp[0x02A7] = 0x00;
        return 1;
    }
    /* Common tail at $A7C4. */
    if (dp[0x02EF] == 0 && dp[0x02ED] == 0x31) {
        sub_DFCD(0x29);
        dp[0x0B] = 0x23;
        return 1;
    }
    return 0;
}

/* ---- $A7AD — NOT a separate dispatch entry. This is inline continuation
 *  of $A787's "Exit to Menu" path (the SEC + RTS tail). Kept as a static
 *  no-op for documentation but no longer reached via the A740 table. */
__attribute__((unused))
static int view_tail_landing_A7AD(void)
{
    WMEM16(0x0054)  = 0x0001;
    WMEM16(0x1E744) = 0x0001;
    dp[0x02A7]      = 0x00;
    return 1;
}

/* ===========================================================================
 *  $00:C103  sub_C103  — per-view OAM init
 * ---------------------------------------------------------------------------
 *  Runs at view-entry to initialise the OAM allocators and bake six
 *  color-math channel frames into the queue at $0C00.
 *
 *  Step 1: dp[$8C]=$2C — "BG palette template kind"
 *  Step 2: sub_C91F(0x1901, 0x9A8F, 0x1E) — stamp template $9A8F at
 *          tile (X=$1901, Y=$9A8F) — this is the cursor sprite tile.
 *  Step 3: prime dp[$6C]=6 and loop 6+1 times, each time:
 *          - yield one frame (sub_877D)
 *          - run sub_C6B0 (the color-math write for the current channel)
 *  Step 4: stamp template $9A90 at $1D01 (the "cursor-shadow" tile).
 * ========================================================================= */
void sub_C103(void)
{
    dp[0x8C] = 0x2C;
    sub_C91F(0x1901, 0x9A8F, 0x1E);

    dp[0x6C] = 0x06;
    do {
        sub_877D();
        sub_C6B0();
        dp[0x6C]--;
    } while ((int8_t)dp[0x6C] >= 0);

    sub_877D();
    sub_C91F(0x1D01, 0x9A90, 0x1E);
}

/* ===========================================================================
 *  $00:C6B0  sub_C6B0  — per-view BG color-math register write
 * ---------------------------------------------------------------------------
 *  Round-robins through 6 BG color-math channels via dp[$09] (0..5),
 *  then dispatches into a 6-entry table at $00:C6C4:
 *    [0] $C6D0  scroll the surface BG gradient (uses $7F:E776)
 *    [1] $C6F1  scroll the B-nest BG gradient   (uses $7F:E778)
 *    [2] $C70E  scroll the R-nest BG gradient   (uses $7F:E940)
 *    [3] $C722  "food bar" 16-bit value at $7F:EB60 (B colony) -> tile
 *               column 4 row 19 / 5 row 19 / 2 row 19 (status overlay)
 *    [4] $C74D  "food bar" 16-bit value at $7F:EB62 (R colony) -> tile
 *               column 1B row 19 / 1C row 19 / 19 row 19
 *    [5] $C77C  "warning" pulsing — if dp[$02A9] < 0, blink the
 *               warning glyph; else show the colony-health byte
 *
 *  Cases [0..2] use the common subroutine at $00:C7A7 (a gradient
 *  emitter that writes a tile pattern to the VRAM queue at $0C00 in
 *  steps of 8 — the ROM's "shift-7" line builder).
 * ========================================================================= */
void sub_C6B0(void)
{
    /* dp[$09]++ then clamp to 0..5. */
    uint8_t ch = dp[0x09] + 1;
    if (ch >= 6) ch = 0;
    dp[0x09] = ch;

    switch (ch) {
    case 0:                                 /* $C6D0 — Surface BG       */
        dp[0x8C] = 0x2C;
        sub_C4D8(0x1A01, 0x9B4D);
        dp[0x27] = 0x30;
        dp[0x8C] = 0x24;
        /* gradient emitter at $C7A7 takes A = $7F:E776 >> 1 and writes
         * to the VRAM queue at $0C00 starting at column $1A03. */
        gradient_emit_C7A7(0x1A03, WMEM8(0x1E776) >> 1);
        break;
    case 1:                                 /* $C6F1 — B-Nest BG        */
        dp[0x8C] = 0x20;
        sub_C4D8(0x1A15, 0x9B4A);
        dp[0x27] = 0x30;
        gradient_emit_C7A7(0x1A17, WMEM8(0x1E778) >> 1);
        break;
    case 2:                                 /* $C70E — R-Nest BG        */
        dp[0x8C] = 0x28;
        dp[0x27] = 0x30;
        gradient_emit_C7A7(0x1A0E, WMEM8(0x1E940) >> 1);
        break;
    case 3: {                               /* $C722 — B colony food    */
        dp[0x8C] = 0x2C;
        uint16_t v = WMEM16(0x1EB60);
        WMEM16(0xBE) = v;
        sub_8A9A();                         /* AB <- v / 10             */
        sub_C625(0x1904, dp[0xC7]);         /* hi digit                 */
        sub_C596(0x1905, dp[0xC6]);         /* lo digit                 */
        sub_C4D8(0x1902, 0x9B4D);
        break;
    }
    case 4: {                               /* $C74D — R colony food    */
        dp[0x8C] = 0x2C;
        uint16_t v = WMEM16(0x1EB62);
        WMEM16(0xBE) = v;
        sub_8A9A();
        sub_C625(0x191B, dp[0xC7]);
        sub_C596(0x191C, dp[0xC6]);
        dp[0x8C] = 0x20;
        sub_C4D8(0x1919, 0x9B4A);
        break;
    }
    case 5:                                 /* $C77C — colony health    */
        dp[0x8C] = 0x2C;
        if ((int8_t)dp[0x02A9] < 0) {       /* warning blinking         */
            sub_C4D8(0x1A0C, 0x9B52);
        } else {
            sub_C593(0x1A0B, dp[0x02A9]);
        }
        dp[0x8C] = 0x28;
        sub_C4D8(0x1A0A, 0x9B4A);
        break;
    }
}

/* ---- $00:C7A7 — gradient emitter (uses dp[$27] as upper bound) ----------
 *  if (a > dp[$27]) a = dp[$27];
 *  dp[$65] = a; dp[$66] = a >> 3;
 *  sub_C4BB();  // unpack base palette idx
 *  while (true) {
 *      dp[$65] -= 8;
 *      if (dp[$65] underflowed) break;
 *      queue write at $0C00,x: $FF, dp[$8C], x+=2
 *      dec dp[$66]; if zero -> tail
 *  }
 *  // tail: write (a-1) as remainder, then dp[$8C], then $F7-pad pairs */
static void gradient_emit_C7A7(uint16_t xy_unused, uint8_t a)
{
    (void)xy_unused;
    if (a > dp[0x27]) a = dp[0x27];
    dp[0x65] = a;
    dp[0x66] = a >> 3;
    sub_C4BB();

    uint16_t x = WMEM16(0x2C);              /* queue tail index */
    for (;;) {
        int16_t left = (int16_t)dp[0x65] - 8;
        if (left < 0) {
            /* Remainder pair, then dp[$8C], then F7-pad pairs. */
            uint8_t rem = (uint8_t)(0xFF + dp[0x65]);
            wram[0x0C00 + x++] = rem;
            wram[0x0C00 + x++] = dp[0x8C];
            dp[0x66]--;
            while (dp[0x66] != 0) {
                wram[0x0C00 + x++] = 0xF7;
                wram[0x0C00 + x++] = dp[0x8C];
                dp[0x66]--;
            }
            break;
        }
        dp[0x65] = (uint8_t)left;
        wram[0x0C00 + x++] = 0xFF;
        wram[0x0C00 + x++] = dp[0x8C];
        dp[0x66]--;
        if (dp[0x66] == 0) break;
    }
    /* End-of-list sentinel pair (two $FF $FF bytes). */
    wram[0x0C00 + x++] = 0xFF;
    wram[0x0C00 + x++] = 0xFF;
    WMEM16(0x2C) = x;
}

/* ===========================================================================
 *  $00:CE87  sub_CE87  — B-Nest interior renderer entry
 * ---------------------------------------------------------------------------
 *  Loads the per-caste counts (Workers/Soldiers/Breeders at $028A/$028C/
 *  $028E) into the triangle-math scratch ($A4/$A6/$A8) and runs the
 *  forward barycentric transform (sub_D074) to derive the cursor screen
 *  position. The caller then plots the cursor sprite from dp[$9E]/$A0.
 * ========================================================================= */
void sub_CE87(void)
{
    WMEM16(0xA4) = WMEM16(0x028E);          /* Breeders                       */
    WMEM16(0xA6) = WMEM16(0x028C);          /* Soldiers                       */
    WMEM16(0xA8) = WMEM16(0x028A);          /* Workers                        */
    sub_D074();
}

/* ===========================================================================
 *  $00:CEAA  sub_CEAA  — R-Nest interior renderer entry
 * ---------------------------------------------------------------------------
 *  Same shape as CE87 but reads from the *mirrored* caste counts at
 *  $0290/$0292/$0294. Those mirrors were populated by sub_CEC0 at the
 *  start of every R-nest frame from the raw R-colony fields at
 *  $027E/$0280/($0282+$0284).
 * ========================================================================= */
void sub_CEAA(void)
{
    /* First, refresh the mirrors (sub_CEC0). */
    WMEM16(0x0290) = WMEM16(0x027E);                  /* B-mirror -> A6 src   */
    WMEM16(0x0292) = WMEM16(0x0280);                  /* R-mirror -> A4 src   */
    WMEM16(0x0294) = WMEM16(0x0282) + WMEM16(0x0284); /* Breeders summed      */

    /* Then load + transform like CE87. */
    WMEM16(0xA8) = WMEM16(0x0294);
    WMEM16(0xA6) = WMEM16(0x0290);
    WMEM16(0xA4) = WMEM16(0x0292);
    sub_D074();
}

/* ===========================================================================
 *  $00:CF05  sub_CF05  — Nest scroll commit
 * ---------------------------------------------------------------------------
 *  Called at the end of every nest-view frame. Reads dp[$0044] (the
 *  "auto vs manual" flag — same one used by Behavior / Caste panels).
 *  In MANUAL mode (dp[$0044] != 0) it just refreshes the three caste
 *  number tiles at fixed positions ($081C, $141E, $1407). In AUTO mode
 *  it first saves the triangle scratch (sub_CE20), runs the in-panel
 *  bounds-check (sub_CF8A for $0B<$26, sub_CFDF otherwise), refreshes
 *  the numbers, then restores the saved scratch (sub_CE59).
 *
 *  Either way, dp[$89] is set to the priority byte ($70 on entry, $78
 *  on exit) so the cursor sprite ends up on top of the redraw.
 * ========================================================================= */
void sub_CF05(void)
{
    dp[0x89] = 0x70;
    if (dp[0x0044] != 0) {
        /* MANUAL: just refresh the three caste numbers using the "%" tile. */
        sub_CF6D_nest_numbers();
        dp[0x8C] = 0x31;
        sub_C4D8(0x081C, 0x9B6F);
        sub_C4D8(0x141E, 0x9B6F);
        sub_C4D8(0x1407, 0x9B6F);
    } else {
        /* AUTO: save scratch, run state-dependent bounds, refresh, restore. */
        sub_CE20();
        if (dp[0x0B] < 0x26) {
            sub_CF8A_bounds_check();
        } else {
            sub_CFDF_bounds_check();
        }
        sub_CF6D_nest_numbers();
        dp[0x8C] = 0x31;
        sub_C4D8(0x081C, 0x9B71);
        sub_C4D8(0x141E, 0x9B71);
        sub_C4D8(0x1407, 0x9B71);
        sub_CE59();
    }
    dp[0x89] = 0x78;
}

/* ---- $00:CF6D — emit the three caste numbers as 2-digit tiles ----------- */
static void sub_CF6D_nest_numbers(void)
{
    dp[0x8C] = 0x30;
    sub_C516(0x0719, WMEM16(0xA8));         /* TOP    (Workers)               */
    sub_C516(0x131B, WMEM16(0xA4));         /* RIGHT  (Breeders)              */
    sub_C516(0x1304, WMEM16(0xA6));         /* LEFT   (Soldiers)              */
}

/* (sub_CF8A_bounds_check / sub_CFDF_bounds_check declared near top.) */

/* ===========================================================================
 *  $00:C3B7  sub_C3B7  — cursor / icon-row gate
 * ---------------------------------------------------------------------------
 *  Spawns the row of 6 HUD icons at (X=$18, Y=$18..$68) — each one is
 *  entity type $32 (icon-button), with dp[$0E,x] holding the icon-table
 *  index (02, 04, 06, 08, 0A, 0C — even, since the icon table at
 *  $9D60 uses 2-byte entries).
 *
 *  After the icons, it spawns the "cursor tail" entity (type $2D) and
 *  pokes its instance fields:
 *      $0011,x = $9A92    (template ptr — the cursor sprite-tile data)
 *      $0013,x = $01      (template length / row count)
 *
 *  Finally it stamps dp[$28]=$FF and dp[$29]=$FF — the "no icon
 *  selected yet" sentinel for the cursor-input dispatcher.
 * ========================================================================= */
void sub_C3B7(void)
{
    static const struct { uint16_t y; uint8_t icon_off; } row[6] = {
        { 0x0018, 0x02 }, { 0x0028, 0x04 }, { 0x0038, 0x06 },
        { 0x0048, 0x08 }, { 0x0058, 0x0A }, { 0x0068, 0x0C },
    };

    /* The ROM packs the spawn-and-poke into 6 unrolled copies; we keep
     * the same observable effect with a small loop. The X-register's
     * "current entity record" output of sub_499C1 is implicit in our
     * model (returned by reference via the entity dispatch wrapper),
     * so we re-read dp[$22..] for the new slot's offset each time. */
    for (unsigned i = 0; i < 6; ++i) {
        sub_499C1(0x0018, row[i].y, 0x32);
        /* dp[$22..23] now holds the freshly-allocated slot's X-base;
         * the ROM stores into $000E,x — we re-derive that here as
         * "the last spawned entity's $0E field". */
        uint16_t ent = WMEM16(0x0022);
        wram[ent + 0x0E] = row[i].icon_off;
    }

    /* Cursor tail entity ($2D). */
    sub_499C1(0, 0, 0x2D);
    uint16_t ent = WMEM16(0x0022);
    WMEM16(ent + 0x11) = 0x9A92;            /* template ptr                   */
    wram[ent + 0x13]   = 0x01;              /* template count                 */

    dp[0x28] = 0xFF;
    dp[0x29] = 0xFF;
}

/* ===========================================================================
 *  $00:DE70  sub_DE70  — Redraw all six views (first pass)
 * ---------------------------------------------------------------------------
 *  Stamps the "$2C" BG palette template kind, then runs template
 *  $A03C at tile origin $0101 (count=$1E). This is the "common
 *  frame" template that all six view-switch landings share.
 *  Then chains to sub_DE83 for the middle pass.
 * ========================================================================= */
void sub_DE70(void)
{
    dp[0x8C] = 0x2C;
    sub_C91F(0x0101, 0xA03C, 0x1E);
    sub_DE83();
}

/* ===========================================================================
 *  $00:DE83  sub_DE83  — Redraw all six views (middle pass)
 * ---------------------------------------------------------------------------
 *  Picks one of 6 sub-templates from the table at $DEAA, indexed by
 *  either dp[$02B1] (5 if non-zero, used by the "post-switch" pass) or
 *  dp[$02B4] (the active view ID, 0..5). Stamps the chosen template at
 *  $0112 with count $0C, then bumps dp[$0030] (the "redraw round-trip"
 *  counter) so the caller knows we're done with this pass.
 * ========================================================================= */
static const uint16_t view_redraw_template_DEAA[6] = {
    0xA03E,    /* [0] Surface OV                                              */
    0xA06E,    /* [1] Surface close-up                                        */
    0xA086,    /* [2] B-Nest OV                                               */
    0xA09E,    /* [3] B-Nest close-up                                         */
    0xA0B6,    /* [4] R-Nest OV                                               */
    0xA056,    /* [5] post-switch transition (used when $02B1 != 0)           */
};

void sub_DE83(void)
{
    dp[0x8C] = 0x2C;
    dp[0x0030] = 0;
    uint8_t i = (dp[0x02B1] != 0) ? 5 : dp[0x02B4];
    uint16_t tmpl = view_redraw_template_DEAA[i & 0x07];
    sub_C91F(0x0112, tmpl, 0x0C);
    dp[0x0030]++;
}

/* ===========================================================================
 *  $00:DEB6  sub_DEB6  — Redraw all six views (final scrub)
 * ---------------------------------------------------------------------------
 *  If either pending-flag ($02B1 or $02B4) is set, we're in a real
 *  view-switch landing: stamp the post-switch template ($A0CE) at
 *  $1501 (count $1E), then run 4 yield/refresh pairs (4 outer x 2
 *  inner = 8 frames) to let the scroll catch up.
 *
 *  Otherwise it's an "idle" scrub: just stamp $A10C at $1501 with
 *  count $1E using the alternate template entry sub_C9BB.
 * ========================================================================= */
void sub_DEB6(void)
{
    /* ROM at $DEB6:
     *   LDA $02B1; BNE DEC0      ; if $02B1 != 0 -> jump to loop (DEC0)
     *   LDA $02B4; BNE DEE2      ; else if $02B4 != 0 -> alt path
     *   (fall through to DEC0)   ; else also do loop
     * So: alt path only when $02B1==0 AND $02B4!=0.                     */
    if (dp[0x02B1] == 0 && dp[0x02B4] != 0) {
        sub_C9BB(0x1501, 0xA10C, 0x1E);
        return;
    }
    /* Loop path (DEC0): big template at $A0CE then 4x4 yield/refresh. */
    sub_C91F(0x1501, 0xA0CE, 0x1E);
    dp[0x6C] = 0x03;
    do {
        dp[0x6D] = 0x01;
        do {
            sub_877D();
            sub_DF0A();
            dp[0x6D]--;
        } while ((int8_t)dp[0x6D] >= 0);
        dp[0x6C]--;
    } while ((int8_t)dp[0x6C] >= 0);
}

/* ===========================================================================
 *  HISTORY GRAPH RENDERER
 * ---------------------------------------------------------------------------
 *  The History Graph (state $2A, evaluation-icon submenu pick) plots 4
 *  concurrent line graphs over a 64-sample sliding window. Each metric
 *  has its own circular buffer of 16-bit values; the renderer reads
 *  the buffer from wrap-around-newest, clamps to 100%, scales to the
 *  graph pixel coordinate space, and emits a per-pixel polyline to
 *  the bank-04 BG3 plot machinery.
 *
 *  Circular buffer layout (at $7E:F6D7 onwards — wram[$F6D7]):
 *      Each channel = 64 16-bit samples = 128 bytes.
 *      Channel 0 ("B.Food")  -> $F6D7
 *      Channel 1 ("R.Food")  -> $F757 (+ 0x80)
 *      Channel 2 ("Health")  -> $F7D7 (+ 0x100)
 *      Channel 3 ("Population")-> $F857 (+ 0x180)
 *      (Plus 4 more channels for the 8-channel buffer, but only 4 are
 *       drawn in the History Graph UI; the others feed Status Screen.)
 *
 *  Index machinery:
 *      $F6D3 = 16-bit write cursor (0..63, wraps mod 64)
 *      $F6D5 = 16-bit sample count, clamped to 0x3F (63)
 *
 *  Which channel index lives in dp[$0045..$0048]:
 *      $0045 = "currently animating sample" for channel 0, or $FF if
 *              the channel is in steady-state (uses normal redraw).
 *      $0046, $0047, $0048 = same idea for channels 1..3.
 *      A non-FF byte means "this channel just got a new sample, redraw
 *      it with the bright/highlight palette via D470."
 *
 *  Plot pipeline:
 *      JSL $04:90D2  set DBR = $7E
 *      JSL $04:90E0  expand "color mask" A -> 8 palette-index bytes
 *      JSL $04:914F  plot first point (col 0)
 *      JSL $04:93EF  plot next point (line draw from prev to next)
 *      JSL $04:90DB  restore DBR
 *
 *  After each channel re-plot, the colour-mask palette is set so that
 *  this channel's pixel pattern is XOR'd into the existing BG3 plot
 *  layer — so multiple lines can overlap visually without one erasing
 *  the others.
 * ========================================================================= */

/* ---- $00:D470 — write the "current sample" label tile ------------------
 *  ROM: reads two parallel 12-entry tables at $00:D496 (X tile column,
 *  one of $01/$09/$11/$19 — 4 columns) and $00:D4A2 (Y tile row,
 *  $15/$17/$19 — 3 rows), indexed by dp[$28] (channel slot 0..11).
 *  X is loaded with the X-column byte, Y with the string ptr =
 *  $9BAC + (channel * 7) — each label string is 7 bytes wide. Then
 *  JSR $C4D8 renders the label at that XY in the queue. dp[$8C] is
 *  set by the caller (D438) to (palette << 2) | $20. */
static const uint8_t rom_00_D496_tile_x[12] = {
    0x01, 0x09, 0x11, 0x19,
    0x01, 0x09, 0x11, 0x19,
    0x01, 0x09, 0x11, 0x19,
};
static const uint8_t rom_00_D4A2_tile_y[12] = {
    0x15, 0x15, 0x15, 0x15,
    0x17, 0x17, 0x17, 0x17,
    0x19, 0x19, 0x19, 0x19,
};
void hist_label_emit_D470(void)
{
    uint8_t i = dp[0x28];
    /* Pack (Y << 8) | X for the C4D8 tile-XY argument.
     * NOTE: the ROM puts X in X-register and uses dp[$8C] for the colour
     * byte previously stamped by D438. */
    uint16_t tile_xy = ((uint16_t)rom_00_D4A2_tile_y[i & 0x0F] << 8)
                     |  (uint16_t)rom_00_D496_tile_x[i & 0x0F];
    /* String pointer = $9BAC + channel * 7 (7 bytes per label entry). */
    uint16_t str_ptr = (uint16_t)(0x9BAC + (uint16_t)i * 7u);
    sub_C4D8(tile_xy, str_ptr);
}

/* ---- $00:D438 — replot the "highlight current sample" caret for ch $28
 *  Walks dp[$0045..$0048] looking for the channel that contains $28;
 *  if found, sets dp[$8C] to the channel's "bright" palette (palette
 *  index 4/5/6/7 for channels 0..3, else 3). Then calls D470 to write
 *  the label tile.                                                       */
void hist_caret_replot_D438(void)
{
    uint8_t i = dp[0x28];
    uint8_t palette;
    if      (dp[0x0045] == i) palette = 4;
    else if (dp[0x0046] == i) palette = 5;
    else if (dp[0x0047] == i) palette = 6;
    else if (dp[0x0048] == i) palette = 7;
    else                     palette = 3;
    dp[0x8C] = (uint8_t)(((palette << 2) | 0x20));
    hist_label_emit_D470();
}

/* ---- $00:D4F1 — plot ONE channel's 64-sample circular buffer ---------
 *  dp[$72] = colour byte (the same value the caller passed to
 *            JSL $04:90E0 just before calling us).
 *  Source: $7F:F6D7 + (dp[$72] >> 1) * 2  (channel base; the LSR
 *          packs "channel index" by halving)
 *          Wait — the actual indexing is:
 *              A = dp[$72]; XBA; A = 0; REP $20; LSR; ADC #$F6D7
 *          That builds [$1C..$1E] = $7F:(F6D7 + (dp[$72] * 0x80) / 2)
 *          i.e. for dp[$72] = 0 -> $F6D7, dp[$72] = 1 -> $F717 (channel 1
 *          offset $40), dp[$72] = 2 -> $F757, etc. So each channel is
 *          0x40 words = 0x80 bytes apart -- the buffer holds 64
 *          16-bit samples per channel.
 *
 *  Cursor: dp[$F6D5] is the count of valid samples (max 63), dp[$F6D3]
 *          is the next-write index. We walk backward from (D3-D5) for
 *          D5 samples, wrapping mod 64.
 *
 *  Plot:   for each sample we clamp to 0xFF (one byte), then map to the
 *          Y pixel coordinate:
 *              Y = (sample >> 1) - 0x9F     (negation -> top-of-graph)
 *              Y = (Y EOR $FF) + 1          (== -Y, two's complement)
 *          and feed (X, Y) to the plot machinery.
 *
 *  Early-out: if dp[$F6D5] < 2, return — no line to draw from <2 samples. */
void hist_plot_one_D4F1(uint8_t channel_color)
{
    /* ROM: LDA $7FF6D5; CMP #$02; BCS continue; RTS  — need >=2 samples. */
    uint16_t sample_count = WMEM16(0x1F6D5);
    if (sample_count < 2) return;

    /* ROM addressing of per-channel buffer:
     *   LDA $72 (channel/color); XBA; LDA #$00; REP #$20  -> A = ch << 8
     *   LSR (16-bit)                                       -> A = ch << 7
     *   CLC ADC #$F6D7                                     -> base in bank $7F
     *   STA $1C; LDA #$7F STA $1E (long pointer at $1C..$1E)
     * So channel k's base in WRAM is $7F:(F6D7 + k * 0x80). */
    uint16_t rom_base = 0xF6D7 + (uint16_t)channel_color * 0x80;
    /* Map $7F:rom_base into our flat wram[] image (bank $7F at +$10000). */
    uint32_t wram_base = 0x10000u + (uint32_t)rom_base;

    /* Walk backward (newest-first); start at (cursor - count) & 0x3F
     * (word index), then ASL to get a byte index for [$1C],y reads.
     * ROM: LDA $7FF6D3; SEC SBC $7FF6D5; AND #$3F; ASL; TAY. */
    uint16_t cursor = WMEM16(0x1F6D3);
    uint16_t idx_byte = (uint16_t)(((cursor - sample_count) & 0x3F) << 1);

    /* First point at X=0 (dp[$9E] zeroed). */
    uint16_t s = *(uint16_t *)&wram[wram_base + idx_byte];
    if (s > 0xFF) s = 0xFF;
    /* Y = ((s >> 1) - 0x9F) EOR 0xFF + 1 — i.e. negate and offset, in 8-bit. */
    uint8_t y = (uint8_t)((uint8_t)((s >> 1) - 0x9F) ^ 0xFF);
    y = (uint8_t)(y + 1);

    sub_490D2();                           /* DBR = $7E                       */
    sub_4914F(0, y);                       /* first point at X=0              */

    /* Subsequent points: dp[$6E] = count-1; loop INY INY AND #$7E + emit. */
    uint16_t x_dst = 0;
    for (uint16_t n = sample_count - 1; n != 0; --n) {
        x_dst = (uint16_t)(x_dst + 4);     /* dp[$9E] += 4 per sample         */
        idx_byte = (uint16_t)((idx_byte + 2) & 0x7E); /* wrap in [0, 0x7E]    */
        s = *(uint16_t *)&wram[wram_base + idx_byte];
        if (s > 0xFF) s = 0xFF;
        y = (uint8_t)((uint8_t)((s >> 1) - 0x9F) ^ 0xFF);
        y = (uint8_t)(y + 1);
        sub_493EF((uint8_t)x_dst, y);
    }

    sub_490DB();                           /* restore DBR                     */
}

/* ---- $00:D4AE — replot all 4 history channels --------------------------
 *  Walks dp[$6C] from 3 down to 0; for each k it loads dp[$0045+k] (the
 *  "channel selector" byte). BMI skips if bit 7 set (i.e. >= 0x80, used
 *  as "no channel in this slot"). Otherwise copies the byte to dp[$72]
 *  then calls $04:90E0 with A = k+1 to set up the colour mask, then
 *  calls hist_plot_one_D4F1. Tail sets dp[$88]=$0E (the DMA queue
 *  priority byte) so the bank-04 pixel plot makes it to VRAM next frame.
 *  Note: hist_plot_one_D4F1 reads dp[$72] for the channel index — pass
 *  it through dp not the C arg. */
void hist_render_all_D4AE(void)
{
    for (int8_t k = 3; k >= 0; --k) {
        uint8_t flag = dp[0x0045 + k];
        if (flag & 0x80) continue;         /* BMI: bit 7 set -> skip slot     */
        dp[0x72] = flag;                   /* hist_plot_one reads this        */
        sub_490E0((uint8_t)(k + 1));       /* colour mask = slot+1            */
        hist_plot_one_D4F1(flag);          /* arg used to compute channel ofs */
    }
    dp[0x88] = 0x0E;
}

/* ---- $00:D4D2 — "all 4 channels, mask=0" pre-pass ---------------------
 *  Same as D4AE but every channel is plotted with mask=0 (i.e. ERASE
 *  the old line first). The ROM does this immediately before D4AE so
 *  that the new sample's polyline replaces the old one without
 *  doubling-up. */
void hist_erase_all_D4D2(void)
{
    for (int8_t k = 3; k >= 0; --k) {
        uint8_t flag = dp[0x0045 + k];
        if (flag & 0x80) continue;         /* BMI                             */
        dp[0x72] = flag;
        sub_490E0(0);                      /* mask = 0 -> erase old line      */
        hist_plot_one_D4F1(flag);
    }
}

/* ===========================================================================
 *  POPULATION GRAPH RENDERER  ($00:DCC1 / $00:DB90 / $00:DD40)
 *
 *  WIKI: see wiki/11-house-screen-ui.md §5 for the prose write-up of the
 *        iso-bar transform, palette selection, lattice lines, and the
 *        V2-D diagonal-bar / palette-swap fixes.
 * ---------------------------------------------------------------------------
 *  The "Population Graph" is the House Screen's 7x7 grid showing which
 *  of the 49 game-world areas are owned by which colony. Two 49-cell
 *  arrays at $7F:EA46 (B colony per-area population) and $7F:EAC6 (R
 *  colony per-area population) drive the cells:
 *
 *      cell[i][j] = pop_B at (i,j), pop_R at (i,j)
 *
 *  Each cell is rendered as a colored bar via JSL $04:9617 with a
 *  palette set by the (B,R) presence pair:
 *      both 0       -> palette 3 (empty/grey)
 *      B>0, R==0    -> palette 1 (blue)
 *      B==0, R>0    -> palette 2 (red)
 *      both >0      -> palette 4 (purple, contested) — and the bar
 *                      pattern is "striped" via $04:90E2 alt-mask.
 *
 *  Grid lines (the "lattice") are drawn by sub_DB90, which calls the
 *  same $04:914F / $04:93EF plot pair as the History Graph — but along
 *  rectangular paths instead of one polyline.
 *
 *  Per-cell coord transform (sub_DD40 returns):
 *      X = $54 - i*12 + j*24
 *      Y = $6F + i*12
 *  in pixel coordinates within the BG3 pixel-plot buffer.
 * ========================================================================= */

/* ---- $00:DD40 — cell (i=$93, j=$94) -> (X,Y) pixel coords -------------
 *  Returns X in the CPU's X register and Y in the CPU's Y register. We
 *  model that with two output pointers.
 *
 *  Transform per ROM (LoROM bank 00:DD40):
 *      X = 24*i - 12*j + $54     (varies +24 with i, -12 with j)
 *      Y = $6F + 12*j            (varies   0 with i, +12 with j)
 *  i.e. varying i moves cells HORIZONTALLY along the (24,0) axis; varying
 *  j moves cells DIAGONALLY along the (-12,+12) axis. */
/* WIKI: wiki/11-house-screen-ui.md §5a (i,j → px,py iso transform). */
void pop_cell_xy_DD40(uint8_t i_row, uint8_t j_col, uint8_t *out_x, uint8_t *out_y)
{
    uint8_t k = (uint8_t)(j_col * 12);                 /* dp[$65] = j*12      */
    int     x = (int)i_row * 24 - (int)k + 0x54;       /* 24*i - 12*j + 0x54  */
    *out_x = (uint8_t)x;
    *out_y = (uint8_t)(0x6F + k);                      /* 0x6F + 12*j         */
}

/* ---- $00:DCE4 — render one cell (i=dp[$93], j=dp[$94]) ----------------
 *  Picks the palette index from the (B,R) presence pair, sets the
 *  colour mask via $04:90E0, transforms (i,j) to (X,Y), then walks an
 *  11-step diagonal "bar" via $04:9617. Per-step the ROM does INX/INY
 *  in pixel space: X decreases by 1 each step (DEX), Y increases by 1
 *  (INY), then the plot is offset by +$17 in X. Net: bar pixels along
 *  the diagonal (px+$17, py+1), (px+$16, py+2), ... For palette 4
 *  (contested) the alt-mask via $04:90E2 flips per step. */
/* WIKI: wiki/11-house-screen-ui.md §5b-c (palette + diagonal 11-step bar). */
static void pop_cell_render_DCE4(uint8_t row_i, uint8_t col_j)
{
    /* Build the per-cell offset into both 49-entry arrays.
     * ROM: LDA $94 / ASL ASL ASL / ADC $93 / ASL / TAX -> X = (8*j+i)*2. */
    unsigned cell = ((unsigned)col_j * 8 + row_i) * 2;
    uint16_t pop_B = WMEM16(0x1EA46 + cell);
    uint16_t pop_R = WMEM16(0x1EAC6 + cell);

    uint8_t palette;
    int     contested = 0;
    if (pop_B == 0) {
        palette = (pop_R == 0) ? 3 : 2;
    } else {
        palette = (pop_R == 0) ? 1 : 4;
        if (pop_R != 0) contested = 1;
    }
    sub_490E0(palette);

    /* Transform (i,j) -> (px,py). The ROM does an INX immediately after
     * DD40 (X++ in pixel space), then the body of the loop does DEX/INY
     * BEFORE each plot. So at iteration 0: px_eff = px+1-1 = px,
     * py_eff = py+1. At iteration k: px_eff = px - k, py_eff = py + k + 1. */
    uint8_t px, py;
    pop_cell_xy_DD40(row_i, col_j, &px, &py);

    /* Loop runs counter 0x0A down to 0 inclusive (BPL) — 11 iterations. */
    dp[0x6E] = 0x0A;
    for (uint8_t step = 0; step <= 0x0A; ++step) {
        if (contested) {
            /* dp[$E4] == 4 (palette 4) triggers the alt-mask reset
             * every other row: $90E2 with A = (($6E & 1) + 1).
             * Note $6E counts DOWN from 0x0A, so iteration k has
             * $6E = 0x0A - k -> (((0x0A-k) & 1) + 1). */
            uint8_t cnt = (uint8_t)(0x0A - step);
            sub_490E2((uint8_t)((cnt & 1) + 1));
        }
        /* Per ROM: DEX (px--), INY (py++), then plot at (X + $17, Y).
         * Iteration k plots at (px - k + $17, py + 1 + k). */
        sub_49617((uint8_t)(px + 0x17 - step), (uint8_t)(py + 1 + step));
        if (dp[0x6E]-- == 0) break;
    }
}

/* ---- $00:DB90 — draw the 8 grid lines (the lattice) ------------------
 *  For each counter k=7..0, the ROM draws two lines per iteration:
 *    "horizontal" — vary i from 0 to 7 at fixed j=k. With X=24i-12k+$54 and
 *                   Y=$6F+12k, this is a true horizontal segment (Y const,
 *                   X moves by +168 across the run).
 *    "vertical"   — vary j from 7 to 0 at fixed i=k. With X=24k-12j+$54
 *                   and Y=$6F+12j, this is the diagonal/iso "vertical".
 *  Plot pair: $04:914F (start point) + $04:93EF (next/line draw), palette 1. */
/* WIKI: wiki/11-house-screen-ui.md §5d (lattice lines · palette 1). */
static void pop_grid_lattice_DB90(void)
{
    sub_490E0(1);
    for (int8_t k = 7; k >= 0; --k) {
        uint8_t px, py;
        /* "Horizontal": i=0 -> i=7 with j=k constant. */
        pop_cell_xy_DD40(0, (uint8_t)k, &px, &py);
        sub_4914F(px, py);
        pop_cell_xy_DD40(7, (uint8_t)k, &px, &py);
        sub_493EF(px, py);
        /* "Vertical" (iso): j=7 -> j=0 with i=k constant. */
        pop_cell_xy_DD40((uint8_t)k, 7, &px, &py);
        sub_4914F(px, py);
        pop_cell_xy_DD40((uint8_t)k, 0, &px, &py);
        sub_493EF(px, py);
    }
}

/* ---- $00:DCC1 — Population Graph entry: 7x7 grid + lattice ----------- */
/* WIKI: wiki/11-house-screen-ui.md §5 ("Population Graph" entry point). */
void pop_grid_draw_DCC1(void)
{
    sub_490D2();                                    /* DBR = $7E              */
    sub_4911B();                                    /* init BG3 plot state    */

    /* Render all 49 cells (i=0..6 outer, j=0..6 inner). */
    for (int8_t i = 6; i >= 0; --i) {
        for (int8_t j = 6; j >= 0; --j) {
            pop_cell_render_DCE4((uint8_t)i, (uint8_t)j);
        }
    }
    pop_grid_lattice_DB90();                        /* the 8x8 grid lines    */

    sub_490DB();                                    /* restore DBR           */
}

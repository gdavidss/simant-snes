/*
 * player_actions.c — SimAnt (SNES) PLAYER-ACTION layer.
 *
 * See wiki/13-player-actions.md for the high-level page covering the
 * three-layer dispatch model (per-view run loop, per-entity click test,
 * popup state machine).
 *
 * This file lifts (and where the body isn't reachable from clean code in
 * the lifted regions, documents) the gameplay-layer translation from
 * "A button / B button on cursor at (X, Y)" into "Yellow Ant takes a
 * game action": move, dig, pick up, eat, attack, recruit, lay egg, dig
 * new nest. It also wires up the on-screen Recruit and Queen popup
 * menus to their effects on simulation state.
 *
 * Scope as commissioned (manual pages 10..13):
 *   A button (move/dig/select)
 *   B button (pickup/cancel; click-Yellow opens Recruit menu;
 *             click food/red ant/etc. while in Worker form runs
 *             a context-action: pick up, eat, attack, trophallaxis)
 *   Recruit menu (Recruit 5/10/All, Release 1/2/All)
 *   Queen menu  (Dig New Nest, Lay Eggs)
 *
 * Where things live in ROM:
 *   - Cursor A/B detection per entity:           $04:DC84  sub_DC84_clicked
 *   - Menu-cursor (type 1) "press confirms":     $04:9DB9  cursor_confirm_action
 *   - Surface CLOSE-UP A-button trigger:         $00:CD30  in state_25/27 run
 *   - Recruit-menu string table:                 $01:86E8  (count + 5 ptrs)
 *   - Queen-menu string table:                   $01:872F  (count + 2 ptrs;
 *                                                  byte at $872E is the
 *                                                  $FF terminator of the
 *                                                  previous string)
 *   - Popup state machine (entity type 29):      $04:AD01  type29_dispatch
 *   - Cursor visual (entity type 30):            $04:B17F  type30_dispatch
 *   - Tutorial message bodies (Yellow Ant ...):  $01:A215 .. $01:E5B3
 *   - DIG NEW NEST excavator (entity type 20):   $04:A6C5  type20_handler
 *   - Hunger meter in simulation summary:        $7E:E7B8  SUMM_HUNGER_B8
 *   - Hunger volatile feeder:                    $7E:E7D2  FEED_HUNGER_D2
 *
 * Conventions match the rest of the decomp:
 *   - `wram[0x20000]` aliases SNES WRAM; `dp` is wram[0..0xFF].
 *   - 16-bit reads/writes via W16 / SW16.
 *   - Entity is the 20-byte struct from simant.c.
 *   - All hex addresses kept in symbol names for grep against disasm.
 *
 * Build:
 *   cd /Users/guilhermedavid/simant-re && \
 *   clang -Wall -Wextra -c player_actions.c -o /tmp/pa.o
 */

#include <stdint.h>

/* ========================================================================
 * Shared aliases (extern from simant.c).
 * ======================================================================== */
extern uint8_t wram[0x20000];
#define dp wram

static inline uint16_t W16(unsigned a)            { return *(uint16_t *)&wram[a]; }
static inline void     SW16(unsigned a, uint16_t v){ *(uint16_t *)&wram[a] = v; }

/* Entity layout — matches the canonical struct used by entities_a..d.c. */
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
    uint8_t  scratch10;
    uint8_t  scratch11;
    uint8_t  scratch12;
    uint8_t  scratch13;
} Entity;
_Static_assert(sizeof(Entity) == 20, "20-byte entity record");

/* Entity table at $04:0600 (= wram[0x0600]). 64 slots max. */
#define ENTITY_TABLE       ((Entity *)&wram[0x0600])
#define ENTITY_TABLE_END   (0x0600 + (64 * 20))   /* parentheses matter! */

/* MMIO joypad shadows used by per-frame action chains. */
#define MMIO8(addr)  (*(volatile uint8_t *)&wram[(addr) & 0xFFFF])
#define JOY1L_4218   MMIO8(0x4218)   /* bit 7 = A, bit 6 = X (held)  */
#define JOY1H_4219   MMIO8(0x4219)   /* bit 7 = B, bit 6 = Y (held)  */

/* ========================================================================
 * DIRECT-PAGE MAP (player-action layer)
 * ------------------------------------------------------------------------
 * These are the wram cells the player-action machinery reads and writes
 * across the cursor / popup / entity stack. The ones below are CONFIRMED
 * by reading from a unique site in the lifted code or the disasm.
 * ======================================================================== */

/* --- Cursor & input state (per-NMI, edge-detected by sub_88xx pipeline) -- */
#define DP_CURSOR_X            0x14    /* cursor screen X (0..0xFF)           */
#define DP_CURSOR_Y            0x15    /* cursor screen Y (0..0xFF)           */
#define DP_CURSOR_X_MIN        0x16    /* cursor X clip lo / hi               */
#define DP_CURSOR_X_MAX        0x17
#define DP_CURSOR_Y_MIN        0x18
#define DP_CURSOR_Y_MAX        0x19

#define DP_CAMERA_X            0x05    /* camera origin (subtracted on draw)  */
#define DP_CAMERA_Y            0x07

#define DP_JOY_NEW_LO          0x60    /* edge-detected: $80 = A pressed      */
#define DP_JOY_NEW_HI          0x61    /* edge-detected: $80 = B / $10 = Y    */
#define DP_MOUSE_CLICK_LATCH   0x7D    /* SNES Mouse left/right (bits 0,1)    */
#define DP_MENU_HOLD_TIMER     0x7B    /* sub_DC84 alternate-click path       */
#define DP_MENU_OPEN_LOCK      0x71    /* 0 = world input live; non-0 = lock  */

/* --- Popup-machine state (entity type 29 at $04:AD01) ------------------- */
#define DP_POPUP_BG_X_0246     0x0246  /* popup BG anchor X                   */
#define DP_POPUP_BG_Y_0248     0x0248  /* popup BG anchor Y                   */
#define DP_MENU_TICK_CMP_024A  0x024A  /* selected menu slot (1..N)           */
#define DP_DLG_FRAMECOUNT_024C 0x024C  /* slow heartbeat                      */
#define DP_MENU_BTN_LATCH_0250 0x0250  /* current "down" button mask          */
#define DP_VIEW_CHANGED_02B3   0x02B3  /* "view-changed" flag                 */
#define DP_VIEW_PREV_02B1      0x02B1  /* previous view (0..5)                */
#define DP_POPUP_ACTIVE_02A7   0x02A7  /* gate for type-29                    */
#define DP_POPUP_LOCK_02E1     0x02E1
#define DP_POPUP_GOTO_02E3     0x02E3

/* --- Close-up coordinate scratch (state $23 / $25 / $27 run loops) ------- */
#define DP_NEST_CURX_9E        0x9E    /* cursor X - $4E (panel-relative)     */
#define DP_NEST_CURY_A0        0xA0    /* stores $A7 - dp[$15] (Y inverted)   */
#define DP_NEST_TGTA_A4        0xA4    /* candidate chamber coord A           */
#define DP_NEST_TGTB_A6        0xA6    /* candidate chamber coord B           */
#define DP_NEST_TGTC_A8        0xA8    /* candidate chamber coord C           */
#define DP_NEST_BAKA_AA        0xAA    /* backup of A4 for rollback           */
#define DP_NEST_BAKB_AC        0xAC
#define DP_NEST_BAKC_AE        0xAE
#define DP_NEST_BAK9E_9A       0x9A    /* backup of 9E/A0 (sub_CE31)          */
#define DP_NEST_BAKA0_9C       0x9C

/* --- B-nest / R-nest scroll commit (sub_CE9A / sub_CEDB) ----------------- */
#define DP_BNEST_ZOOM_0286     0x0286  /* B-Nest "interior visible" zoom flag */
#define DP_RNEST_ZOOM_0288     0x0288  /* R-Nest "interior visible" zoom flag */
#define DP_NEST_CHAMBER_A_028A 0x028A  /* committed chamber A                 */
#define DP_NEST_CHAMBER_B_028C 0x028C  /* committed chamber B                 */
#define DP_NEST_CHAMBER_C_028E 0x028E  /* committed chamber C                 */
#define DP_RNEST_CHA_0290      0x0290  /* R-Nest equivalents                  */
#define DP_RNEST_CHB_0292      0x0292
#define DP_RNEST_CHC_0294      0x0294

/* --- Simulation summary cells (per-tick aggregator output at $7E:E7Bx) --- */
#define WRAM_HUNGER_E7B8       0xE7B8  /* hunger snapshot 0..0xFF             */
#define WRAM_HUNGER_FEED_E7D2  0xE7D2  /* hunger volatile feeder              */
#define WRAM_FOOD_TOTAL_E770   0xE770  /* total food in current area          */
#define WRAM_AREA_BFOOD_EB60   0xEB60  /* B-colony food stock                 */
#define WRAM_AREA_RFOOD_EB62   0xEB62  /* R-colony food stock                 */
#define WRAM_EGGS_LAID_E80E    0xE80E  /* total eggs laid                     */
#define WRAM_FEED_DEAD_E7DE    0xE7DE
#define WRAM_FIGHTS_B_WON_E844 0xE844
#define WRAM_PLAY_MODE         0xE700  /* 0=Tutorial 1=Scenario 2=Full        */
#define WRAM_WALK_ACTIVE_E8BE  0xE8BE  /* "Yellow-Ant walking" flag           */
#define WRAM_WALK_TILE_X_E8C0  0xE8C0  /* walking ant world tile X            */
#define WRAM_WALK_TILE_Y_E8C2  0xE8C2  /* walking ant world tile Y            */
#define WRAM_WALK_LIVES_E8C4   0xE8C4  /* "lives remaining" — rebirth count   */
#define WRAM_WALK_DIR_E8C6     0xE8C6  /* walking direction (0..3)            */

/* ========================================================================
 * EXTERNAL HELPERS (extern from entities_a..d.c, simant.c, simulation.c)
 * ======================================================================== */

/* $04:DC84 — "is the player clicking THIS entity right now?"
 * Returns non-zero iff:
 *   - no menu lockout (dp[$0071] == 0), the OR of (JOY1L | JOY1H) & $C0 is
 *     non-zero — i.e. ANY of A (JOY1L bit 7), B (JOY1H bit 7),
 *     X (JOY1L bit 6), or Y (JOY1H bit 6) is held — AND entity within a
 *     $20-wide (-16..+15 px on each axis) box around cursor.
 *   OR
 *   - menu lockout (dp[$0071] != 0) but dp[$007B] & $03 is non-zero (the
 *     menu-internal "fast tick" click is firing) AND entity in the same
 *     -16..+15 box.
 * The 16-px claim from earlier docs is half-right: the test is a 32-px
 * window centered on the cursor (right/bottom edge exclusive).
 *
 * Reads entity.x at $0002,x and entity.y at $0004,x, cursor at dp[$14]/[$15],
 * camera at dp[$05]/[$07]. The X/Y reads are 16-bit (REP #$20); the high
 * byte of (entity_xy - camera_xy) must be 0 to even consider the entity
 * (early reject via BNE before the +16/CMP $20 check). */
extern int sub_DC84_entity_clicked(const Entity *e);

/* $00:8EA3 — schedule SPC700 SFX command. */
extern void apu_play_sfx_008EA3(uint8_t sfx);

/* $04:99C1 — spawn a new entity (X=screen X, Y=screen Y, A=type byte). */
extern void entity_spawn_0499C1(uint16_t sx, uint16_t sy, uint8_t type);

/* $04:99BB — reset entity-table cursor to $0600 (start of frame). */
extern void entity_table_reset_0499BB(void);

/* $00:A2AA — "L-shoulder interact" sub. Sets the type 3/4/5 selection-box
 * marker positions based on dp[$024A] (which selection corner we're on). */
extern void selection_box_set_A2AA(void);

/* $00:A3BD — recompute view-state on SELECT press. */
extern void view_switch_A3BD(void);

/* sub_DCD5 — pseudo-random byte ANDed with mask. */
extern uint8_t sub_DCD5_rand(uint8_t mask);

/* ========================================================================
 * HOW THE PLAYER-ACTION LAYER IS WIRED (overview)
 * ------------------------------------------------------------------------
 * SimAnt distributes player-input handling across THREE concentric loops:
 *
 *   1) THE PER-VIEW RUN LOOP (states_gameplay.c).
 *      Each of the six views' run state ($1E / $20 / $22 / $23 / $25 / $27)
 *      polls the joypad each iteration:
 *        - JOY1H bit 7 = B button  -> exit close-up (in $25/$27) OR
 *                                     state-specific cancel
 *        - JOY1H bit 6 = X button  -> recenter cursor to (0x18, 0x18)
 *        - JOY1H bit 5 = SELECT    -> view-switch to state $1B
 *        - JOY1L bit 6 = L button  -> sub_A2AA selection-box draw
 *        - JOY1L bit 7 = A button  -> close-up: nest-scroll target compute
 *                                     (other views: per-state handling)
 *
 *      In OVERVIEW views ($1E/$20/$22) the cursor + A-press do NOT yet
 *      have a "select ant" action lifted; the player-action there is the
 *      drag-selection rectangle (types 3/4/5 in entities_a.c) plus the
 *      eventual icon-menu click which we don't have body-lifted.
 *
 *      In NEST CLOSE-UP ($25/$27) the lifted run handler at $00:CCD0 has
 *      a CONFIRMED chain: when JOY1L bit 7 is set (A held), it computes
 *        dp[$9E] = dp[$14] - $4E         (cursor X panel-relative)
 *        dp[$A0] = $A7 - dp[$15]         (cursor Y flipped — nest Y is
 *                                          inverted in screen space)
 *      runs sub_CE6B to range-check < $65/$57, then via a backup/restore
 *      pair (sub_CE20, sub_D034, sub_CE3E) commits the resulting
 *      chamber coords to dp[$0286..0294]. This is the SCROLL TARGET for
 *      panning the camera through the nest; it doesn't move the ant.
 *
 *   2) THE PER-ENTITY CLICK CHECK (entities_a..d.c).
 *      Every CLICKABLE entity calls sub_DC84_entity_clicked(self) each
 *      frame inside its state-1 (active) handler. If clicked, the entity
 *      self-mutates: e.g. type 9/10/11 advance to state 2 (death-fade)
 *      which is "the player picked me up / ate me" for those drifting
 *      food/larva sprites.
 *
 *      The walking ants (types 14/15) currently DO NOT consume DC84 —
 *      they're pure wanderers in the lifted code. Player-controlled
 *      Yellow-Ant interaction with workers (pick up / recruit / attack)
 *      is documented in tutorial strings ($01:BB28 "will pick up the
 *      pebble", $01:9C78 "Yellow Ant to call and recruit") but the
 *      consumption-side code isn't located in the lifted regions yet.
 *
 *   3) THE POPUP STATE MACHINE (entity type 29 at $04:AD01).
 *      When dp[$02A7] != 0, type 29 owns the screen. It implements a
 *      10-state machine for: drift -> dialog-open -> 6 blink frames ->
 *      final commit. The "Recruit" and "Queen" menus surface here.
 *      $01:86E8 holds the recruit menu pointer-table (5 options),
 *      $01:872F holds the queen menu table (count + 2 ptrs); $01:874A
 *      holds the alternate Queen menu A (count + 1 ptr = "Lay Eggs" only).
 *
 * The KEY OBSERVATION: clicking on YELLOW ANT in surface close-up opens
 * a popup whose action depends on the Yellow Ant's caste. If Worker, it
 * shows the Recruit menu. If Queen (in Full Game), it shows the Queen
 * menu. The lifted code at $00:DFA4 reads from a per-mode jump table at
 * $00:DFCA when initiating these popups — it sets dp[$0250] (the menu
 * button latch) and triggers the type-29 popup via STA #$80 to the APU
 * latch ($2140) then JSR $DFCD to play the open-popup SFX.
 *
 * The 5 lifted modes vs the Yellow Ant context table at $00:DFCA:
 *   $DFCA: 10 30 20 85 72 ... (data follows; the first 5 are popup-mode
 *   IDs, where mode 0 = "Yellow Ant Worker", 1 = "Yellow Ant Queen", etc.)
 * ======================================================================== */

/* ========================================================================
 * STAGE 1 — CURSOR + INPUT DISPATCH (lifted from $00:CD30..$00:CD81)
 * ------------------------------------------------------------------------
 * The per-frame in-game cursor input handler in close-up nest views.
 * Called from state_view_nest_closeup_run_CCD0() in states_gameplay.c
 * when dp[$28] is non-negative (no sub-state active) and dp[$0286] or
 * dp[$0288] zoom flag is 0 (we're in the "click-to-pan" close-up).
 *
 * Inputs:
 *   dp[$14], dp[$15]  cursor screen position
 *   dp[$71]           menu-open lock
 *   dp[$007B]         menu-internal click latch (when lock is set)
 *   JOY1L bit 7       A button held
 *   dp[$0B]           current state ($25 = B-Nest, $27 = R-Nest)
 *
 * Outputs (when A is held AND cursor is in-panel):
 *   dp[$9A]/[$9C]  backup of dp[$9E]/[$A0]
 *   dp[$AA]/[$AC]/[$AE]  backup of dp[$A4]/[$A6]/[$A8]
 *   if validation passes:
 *      dp[$0286] or dp[$0288] receives new "scroll target" via
 *      sub_CE9A / sub_CEDB
 *   else:
 *      dp[$A4..A8] restored from dp[$AA..AE]
 *
 * NOTE: This is NOT "send ant to (X, Y)" — it's "pan the NEST view to
 * the chamber under the cursor". In SimAnt the close-up nest view is
 * navigable; clicking a different chamber scrolls there. Sending an ant
 * is a separate action handled in the SURFACE close-up (state $23) which
 * we haven't fully lifted body — see the surface_closeup_action stub
 * below.
 * ======================================================================== */
extern void sub_CE31_save_cursor_9E_to_9A(void);    /* dp[$9A/$9C] = dp[$9E/$A0] */
extern int  sub_CE6B_validate_panel_xy(void);       /* C=1 if dp[$9E]<$65 && dp[$A0]<$57 */
extern void sub_CE20_save_chamber_to_AA(void);      /* dp[$AA..AE] = dp[$A4..A8] */
extern void sub_D034_compute_chamber_coords(void);  /* dp[$A4],[$A6],[$A8] from dp[$9E]/[$A0] */
extern int  sub_CE3E_validate_chamber(void);        /* C=1 if dp[$A4],[$A6],[$A8] all < $65 */
extern void sub_D074_finalize_chamber(void);        /* commit dp[$A4/$A6/$A8] into dp[$9E/$A0] */
extern void sub_CF05_redraw_nest(void);             /* trigger BG redraw */
extern void sub_CE9A_commit_bnest_target(void);     /* dp[$028A..028E] = dp[$A4..A8] */
extern void sub_CEDB_commit_rnest_target(void);     /* dp[$0290..0294] = dp[$A4..A8] (with extra rule) */
extern void sub_CE79_restore_chamber(void);         /* dp[$A4..A8] = dp[$AA..AE] */

/* close_up_nest_a_button_action_CD30 — the A-button handler in the close-
 * up nest view's run loop. Returns 1 if the click was consumed, 0 if no
 * commit happened (cursor was outside the nest panel).
 *
 * NOTE: the original $00:CD30..$00:CDA2 routine also polls SELECT/Y at
 * $CD81 onwards (SELECT = exit, Y = re-center cursor). This C lift covers
 * ONLY the A-button section ($CD30..$CD7D). A caller that wants the full
 * routine's behavior must additionally implement the button-polling tail
 * (see control_panels.c::cp_state_run_CCD0 for an equivalent pattern). */
int close_up_nest_a_button_action_CD30(void)
{
    /* dp[$71] gating: if menu-open AND dp[$007B] & 1 not set, no click. */
    if (dp[DP_MENU_OPEN_LOCK] == 0) {
        if ((JOY1L_4218 & 0x80) == 0) return 0;          /* A not held    */
    } else if ((dp[DP_MENU_HOLD_TIMER] & 0x01) == 0) {
        return 0;
    }

    /* Capture cursor in panel coords. */
    sub_CE31_save_cursor_9E_to_9A();
    dp[DP_NEST_CURX_9E] = (uint8_t)(dp[DP_CURSOR_X] - 0x4E);
    dp[DP_NEST_CURY_A0] = (uint8_t)(0xA7 - dp[DP_CURSOR_Y]);

    if (!sub_CE6B_validate_panel_xy()) return 0;          /* outside panel */

    sub_CE20_save_chamber_to_AA();
    sub_D034_compute_chamber_coords();

    if (!sub_CE3E_validate_chamber()) {
        /* Off-grid — restore previous chamber and abort. */
        sub_CE79_restore_chamber();
        return 0;
    }

    sub_D074_finalize_chamber();
    sub_CF05_redraw_nest();

    /* B-Nest vs R-Nest commit. */
    if (dp[0x0B] == 0x25) sub_CE9A_commit_bnest_target();
    else                  sub_CEDB_commit_rnest_target();

    return 1;
}

/* ========================================================================
 * STAGE 2 — SURFACE CLOSE-UP A-BUTTON: "WALK YELLOW ANT TO (X,Y)"
 * ------------------------------------------------------------------------
 * State $23 is the surface close-up dispatcher; its 3-entry table at
 * $00:A806 hands off to sub-state handlers at $A80C/$A824/$A86A.
 *
 *   $A80C — close-up SETUP (asset upload + sub-state $16)
 *   $A824 — close-up RUN with bit-3 of $02A7 checked: when set, fire
 *           popup; otherwise advance dp[$12] (the "click-build" counter
 *           that fills the entity table slot for the player-issued
 *           action) until it hits 8, at which point the action commits
 *           and the popup-state transitions to $3E.
 *   $A86A — close-up RUN with bit-4 of $02A7 checked: alternative entry
 *           used when dp[$02EF]==0 && dp[$02ED]==$31 — the "yellow ant
 *           selected himself" path that opens the popup at state $36.
 *
 * What we know (read off disasm):
 *   - dp[$02A7] is the "popup gate" bit-field set by clicking on
 *     specific entity types in the surface view. Bits encode WHICH
 *     popup will open:
 *        bit 3 (0x08) = "Yellow Ant in Worker role" -> Recruit menu
 *        bit 4 (0x10) = "Yellow Ant in Queen role"  -> Queen menu
 *   - dp[$12] and dp[$13] form a "build" pair that fills 8 entity-record
 *     fields ($0B00,x onward) for the player-action queue:
 *
 *        slot 0..6 : action target XY, source, action-type byte
 *        slot 7    : commit flag — when dp[$12] == 8 the action fires
 *
 *   - The popup state transitions ($0B = $30..$3E) are the popup-screen
 *     handlers; we have not fully lifted their bodies.
 *
 * The skeleton lifted below assumes:
 *   - Click on EMPTY space  -> set "walk-to" target via build-counter
 *   - Click on DIRT tile    -> set "dig" target (same encoding, type=DIG)
 *   - Click on ICON         -> set "select-icon" target (state $30)
 *
 * The full disassembly at $00:A824 is heavily byte-mixed with data and
 * isn't cleanly recovered here. The COVERAGE table already marks
 * "Move (cursor click -> ant walks)" as PARTIAL — that's still true.
 * ======================================================================== */

/* Surface close-up entity-action build-state. Reset by sub_A86A entry. */
typedef struct __attribute__((packed)) ActionBuildSlot {
    /* These bytes live at $0B00,x in WRAM where x = dp[$12] * 2 (paired) */
    uint16_t target_x;      /* derived from dp[$0246]/dp[$14] */
    uint16_t target_y;      /* derived from dp[$0248]/dp[$15] */
    uint8_t  action_kind;   /* 0=walk 1=dig 2=pickup 3=eat 4=attack 5=recruit */
    uint8_t  source_caste;  /* 0=Worker 1=Soldier 2=Queen */
    uint8_t  pad[10];
} ActionBuildSlot;

#define ACTION_BUILD_AT_0B00  ((ActionBuildSlot *)&wram[0x0B00])
#define DP_BUILD_COUNTER_12   0x12      /* current build slot index (0..7) */
#define DP_BUILD_TARGET_13    0x13      /* "expected" slot when complete   */

/* surface_closeup_a_press_A824 — the "A button while in surface close-up"
 * handler. This is the public entry from sub_state 1 of state $23.
 *
 * Behavior (best-effort lift from $00:A824..$A85E):
 *   - APU silence: STZ $2140  (acknowledged-input click sound)
 *   - if dp[$02A7] & 8 (the "popup-cued" bit):
 *       dp[$0B] = $30                      ; jump to popup state
 *       if (dp[$13] == dp[$12]):
 *           if dp[$12] != 7:  A = $2F      ; sound code 0x2F (cancel)
 *           else:             A = $31      ; sound code 0x31 (commit)
 *           sub_DFCD(A);                   ; play sound
 *           spawn cursor sprite (type 2)
 *           dp[$12]++
 *           sub_A893();                    ; build next entity slot
 *           sub_499BB();                   ; reset entity table cursor
 *           if dp[$12] == 8:
 *               dp[$0B] = $3E              ; final-commit popup state
 *   - else (no popup cue): bring dp[$0B] back to $3C (idle state).
 */
void surface_closeup_a_press_A824(void)
{
    /* APUIO0 = 0; */                /* STZ $2140 — silence click cue */
    *(volatile uint8_t *)&wram[0x4140] = 0;

    if ((dp[DP_POPUP_ACTIVE_02A7] & 0x08) == 0) {
        /* No popup cued — play "cancel" sound, go to idle. */
        extern void sub_DFCD_play_popup_sfx(uint8_t sfx);
        sub_DFCD_play_popup_sfx(0x30);
        dp[0x0B] = 0x3C;             /* idle */
        return;
    }

    dp[0x0B] = 0x30;                  /* popup is active */

    if (dp[DP_BUILD_TARGET_13] != dp[DP_BUILD_COUNTER_12]) return;

    extern void sub_DFCD_play_popup_sfx(uint8_t sfx);
    extern void sub_A893_fill_action_slot(void);

    if (dp[DP_BUILD_COUNTER_12] != 7)
        sub_DFCD_play_popup_sfx(0x2F);    /* step sound */
    else
        sub_DFCD_play_popup_sfx(0x31);    /* commit sound */

    entity_spawn_0499C1(0, 0, 0x02);      /* spawn / refresh cursor */
    dp[DP_BUILD_COUNTER_12]++;
    sub_A893_fill_action_slot();
    entity_table_reset_0499BB();

    if (dp[DP_BUILD_COUNTER_12] == 8)
        dp[0x0B] = 0x3E;                  /* terminal -> commit */
}

/* surface_closeup_b_press_A86A — the "B button (or Y) while in surface
 * close-up" handler. The popup-active bit consulted here is bit 4 (the
 * "Queen menu" path).
 *
 * Behavior (from $00:A86A..$A892):
 *   - STZ $2140  (silence)
 *   - if dp[$02A7] & $10 (the Queen-popup cue):
 *       sub_DFCD($2E)                ; "open menu" sound code
 *       dp[$0B] = $3C                ; go to idle/menu state
 *   - if dp[$02EF] == 0 && dp[$02ED] == $31 (close-up done message?):
 *       sub_DFCD($2D)                ; "close menu" sound
 *       dp[$0B] = $36                ; Queen-menu opening state
 */
void surface_closeup_b_press_A86A(void)
{
    *(volatile uint8_t *)&wram[0x4140] = 0;

    if (dp[DP_POPUP_ACTIVE_02A7] & 0x10) {
        extern void sub_DFCD_play_popup_sfx(uint8_t sfx);
        sub_DFCD_play_popup_sfx(0x2E);
        dp[0x0B] = 0x3C;
    }

    if (dp[0x02EF] == 0 && dp[0x02ED] == 0x31) {
        extern void sub_DFCD_play_popup_sfx(uint8_t sfx);
        sub_DFCD_play_popup_sfx(0x2D);
        dp[0x0B] = 0x36;
    }
}

/* ========================================================================
 * STAGE 3 — PER-ENTITY CLICK HANDLER (B button = pickup / cancel)
 * ------------------------------------------------------------------------
 * Each clickable entity calls sub_DC84_entity_clicked(self) in its
 * per-frame state body. The lifted entities that already do this:
 *
 *   types 9, 10, 11, 22  (food crumbs / particles)   — on click, advance
 *                                                      to "death-fade"
 *   type  12             (drifting food prop)        — on click, skip
 *                                                      physics this frame
 *   type  13             (UI banner)                 — passive (skip phys)
 *   type  16, 17         (ant lion / spider)         — collision-only
 *   type  20             (dig-new-nest excavator)    — collision turns
 *   type  27             (caterpillar?)              — on click, fade
 *
 * The MISSING per-entity click consumers (per coverage.md):
 *   types 14, 15         (walking workers/soldiers)  — should bind
 *                                                      "pick up me" /
 *                                                      "recruit me" /
 *                                                      "fight me" actions
 *   type  18, 19         (queens)                    — should bind
 *                                                      "open Queen menu"
 *
 * The B-button + cursor-over-Yellow-Ant -> Recruit menu flow:
 *
 *   When the player's yellow-marked ant entity (the special one with
 *   walking-ant body + a "controlled" flag) is clicked, the entity's
 *   state-1 handler should:
 *     1) check sub_DC84_entity_clicked(self)
 *     2) if clicked AND no popup already active (dp[$02A7] == 0):
 *        - set dp[$02A7] |= 0x08 (Worker form) OR |= 0x10 (Queen form)
 *        - sub_DFCD(0x2E) — the open-menu sound
 *        - dp[$02EF] = 0x00 (clear "menu denial" flag)
 *        - the next frame, the surface close-up dispatcher at $A824
 *          / $A86A will fire and transition dp[$0B] to a popup state
 *
 * The detection threshold ($20x$20 box around cursor) inside sub_DC84
 * means the click must be NEAR the entity. The player can use the
 * cursor to "select" a specific worker because each worker has its own
 * entity record and its own sub_DC84 call.
 *
 * The lift below is the SKELETON of "what types 14/15 SHOULD do" if
 * they were to participate in the click economy.
 * ======================================================================== */

/* worker_click_handler — what types 14/15 would call from their state-1
 * (walking) handler to participate in the player-action layer. NOTE: this
 * is the inferred shape based on the manual + tutorial strings ($01:9C78
 * "Yellow Ant to call and recruit", $01:BB28 "will pick up the pebble").
 * The original ROM body for this isn't in the lifted regions yet — see
 * coverage.md row for types 14/15.
 *
 * Returns 1 if the click was consumed (caller should skip this frame's
 * normal motion); 0 if no click happened. */
int worker_click_handler_pseudo(Entity *self,
                                int is_yellow_ant,
                                int self_is_red_colony)
{
    if (!sub_DC84_entity_clicked(self)) return 0;

    /* Don't open menus on top of menus. */
    if (dp[DP_POPUP_ACTIVE_02A7]) return 1;

    if (is_yellow_ant) {
        /* Clicking the Yellow Ant himself opens the Recruit menu. */
        dp[DP_POPUP_ACTIVE_02A7] |= 0x08;
        apu_play_sfx_008EA3(0x2E);          /* menu-open SFX */
        return 1;
    }

    /* Clicking a red ant -> Yellow Ant attacks.
     * Clicking a black ant while hungry -> trophallaxis (red ant gives food).
     * Clicking a black ant otherwise   -> no-op (already same colony).
     *
     * The hunger threshold is read from the simulation summary cell
     * SUMM_HUNGER_B8 ($7E:E7B8); when below 0x30 (manual: "hungry"), the
     * trophallaxis path fires. */
    if (self_is_red_colony) {
        /* Yellow Ant attacks. The action is queued via the build-counter
         * machine (Stage 2): walk-target = entity position, action=ATTACK. */
        ActionBuildSlot *as = &ACTION_BUILD_AT_0B00[dp[DP_BUILD_COUNTER_12]];
        as->target_x = self->x;
        as->target_y = self->y;
        as->action_kind = 4;                /* attack */
        return 1;
    }

    /* Black ant — trophallaxis if hungry. */
    if (W16(WRAM_HUNGER_E7B8) < 0x30) {
        /* tutorial: "The Yellow Ant was fed by its nestmate" ($01:A215) */
        ActionBuildSlot *as = &ACTION_BUILD_AT_0B00[dp[DP_BUILD_COUNTER_12]];
        as->target_x = self->x;
        as->target_y = self->y;
        as->action_kind = 5;                /* receive trophallaxis */
        /* Eat: hunger gets reset to ~0xFF.  See stage 6 below. */
        return 1;
    }

    return 1;
}

/* food_click_handler — similar shape for stationary food entities.
 *
 * Clicking on food while NOT carrying:
 *   - if HUNGRY: eat immediately (decrement hunger, despawn food)
 *   - else:      pick up (action queued, food becomes "carried")
 *
 * Clicking on food while ALREADY CARRYING: drop the current item, pick up
 * the new one (instant swap in the action queue).
 *
 * Source intel:
 *   - tutorial $01:B860 "ate and regained its strength"  — eat success
 *   - tutorial $01:BB30 "will pick up the pebble"         — pickup
 *   - HUD attribute at dp[$0F00,x] tracks carry-item per ant slot
 *     (the $0C/$0D init_word slot in the entity record is repurposed
 *     for "carry-item-type" when state == carrying)
 */
extern int  yellow_ant_is_worker(const Entity *yellow);
extern void simulate_pickup_food_for_yellow(Entity *food);
extern void simulate_eat_food_for_yellow(Entity *food);

int food_click_handler_pseudo(Entity *self)
{
    if (!sub_DC84_entity_clicked(self)) return 0;
    if (dp[DP_POPUP_ACTIVE_02A7]) return 1;

    Entity *yellow = &ENTITY_TABLE[/*yellow_index*/0];   /* index TBD */
    if (!yellow_ant_is_worker(yellow)) {
        /* Queens can't pick up — Yellow as Queen ignores food clicks. */
        return 1;
    }

    if (W16(WRAM_HUNGER_E7B8) < 0x30) {
        /* Hungry — eat directly. */
        simulate_eat_food_for_yellow(self);
        /* Tutorial message: $01:B860 fires through the message-queue. */
    } else {
        simulate_pickup_food_for_yellow(self);
        /* Tutorial: $01:BB28 fires. */
    }
    return 1;
}

/* ========================================================================
 * STAGE 4 — RECRUIT MENU  (see wiki/13-player-actions.md §4)
 *
 * SUPERSEDED: see player_actions_full.c::recruit_apply_02A1F4 for the
 * ROM-verified body. The pseudocode below was inferred from the manual
 * and has at least one wrong assumption: it gates on a $40-pixel ESCORT
 * radius around the Yellow Ant, but the ROM at $02:A1F4 does NOT check
 * position — it iterates ALL ants in the colony and accepts any
 * non-fighting Worker/Soldier (or Breeder/Queen in mating-flight mode)
 * whose state isn't already 6. Recruit-All (slot 3) passes desired=$03E8
 * to drain the entire colony.
 * ------------------------------------------------------------------------
 * Table at $01:86E8 (5 entries):
 *
 *   $01:86E8: 05                ; count = 5
 *   $01:86E9: F3 86             ; ptr -> "Recruit 5  "    @ $86F3
 *   $01:86EB: FF 86             ; ptr -> "Recruit 10 "    @ $86FF
 *   $01:86ED: 0B 87             ; ptr -> "Recruit All"    @ $870B
 *   $01:86EF: 17 87             ; ptr -> "Release 1/2"    @ $8717
 *   $01:86F1: 23 87             ; ptr -> "Release All"    @ $8723
 *
 * On commit (dp[$024A] holds the selected slot 1..5), the menu-action
 * dispatcher runs one of these effects on the "escort group" of workers
 * following the Yellow Ant. The escort group is the set of entity slots
 * whose +B byte (state_scratch) has bit-RECRUITED set.
 *
 * Recruit 5  — pull up to 5 nearby workers into the escort group
 * Recruit 10 — pull up to 10 nearby workers
 * Recruit All— pull every worker in the current area
 * Release 1/2— release half of the escort group
 * Release All— release every escort
 *
 * The "current area" cap matches simulation.c::AREA_B_POP cap (0xFA=250).
 * "Nearby" = within $40 px in both axes (4x the click-target radius).
 * ======================================================================== */
#define RECRUIT_FLAG_BIT       0x80    /* entity scratch[4] bit 7 */
#define ESCORT_RADIUS          0x40    /* px around Yellow Ant    */

/* recruit_menu_apply — entry from the popup-machine when the player
 * confirms a Recruit-menu choice. selected_slot is 1..5. */
void recruit_menu_apply_pseudo(uint8_t selected_slot)
{
    extern Entity *yellow_ant_get_self(void);
    Entity *yellow = yellow_ant_get_self();
    if (!yellow) return;

    int desired_count_to_add = 0;
    int desired_count_to_release = 0;
    int release_all = 0;

    switch (selected_slot) {
    case 1: desired_count_to_add = 5;   break;        /* Recruit 5  */
    case 2: desired_count_to_add = 10;  break;        /* Recruit 10 */
    case 3: desired_count_to_add = 250; break;        /* Recruit All*/
    case 4: desired_count_to_release = 2; break;      /* Release 1/2 — semantics:
                                                       * "release half" = drop
                                                       * floor(count/2). The
                                                       * "1/2" in the label is
                                                       * the FRACTION not a
                                                       * literal "1 or 2 ants". */
    case 5: release_all = 1; break;                   /* Release All*/
    default: return;
    }

    /* Walk the entity table; for each worker (type 14) inside the escort
     * radius, set or clear the RECRUIT_FLAG_BIT. */
    int added = 0;
    int existing_recruits = 0;

    for (uint8_t *p = (uint8_t *)ENTITY_TABLE;
         p < (uint8_t *)&wram[ENTITY_TABLE_END];
         p += sizeof(Entity)) {
        Entity *e = (Entity *)p;
        if (e->type != 14) continue;                    /* only workers */
        if (e == yellow) continue;
        int already = (e->scratch[4] & RECRUIT_FLAG_BIT) != 0;
        if (already) existing_recruits++;
    }

    if (release_all) {
        for (uint8_t *p = (uint8_t *)ENTITY_TABLE;
             p < (uint8_t *)&wram[ENTITY_TABLE_END];
             p += sizeof(Entity)) {
            Entity *e = (Entity *)p;
            if (e->type != 14) continue;
            e->scratch[4] &= (uint8_t)~RECRUIT_FLAG_BIT;
        }
        return;
    }

    if (desired_count_to_release) {
        /* Release half (rounded down). */
        int to_drop = existing_recruits / 2;
        for (uint8_t *p = (uint8_t *)ENTITY_TABLE;
             to_drop > 0 && p < (uint8_t *)&wram[ENTITY_TABLE_END];
             p += sizeof(Entity)) {
            Entity *e = (Entity *)p;
            if (e->type != 14) continue;
            if (e->scratch[4] & RECRUIT_FLAG_BIT) {
                e->scratch[4] &= (uint8_t)~RECRUIT_FLAG_BIT;
                to_drop--;
            }
        }
        return;
    }

    /* Recruit path: pull nearby workers up to the requested count. */
    for (uint8_t *p = (uint8_t *)ENTITY_TABLE;
         added < desired_count_to_add &&
         p < (uint8_t *)&wram[ENTITY_TABLE_END];
         p += sizeof(Entity)) {
        Entity *e = (Entity *)p;
        if (e->type != 14) continue;
        if (e == yellow) continue;
        if (e->scratch[4] & RECRUIT_FLAG_BIT) continue;   /* already in escort */

        int16_t dx = (int16_t)e->x - (int16_t)yellow->x;
        int16_t dy = (int16_t)e->y - (int16_t)yellow->y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > ESCORT_RADIUS || dy > ESCORT_RADIUS) continue;

        e->scratch[4] |= RECRUIT_FLAG_BIT;
        added++;
    }
}

/* ========================================================================
 * STAGE 5 — QUEEN MENU  (see wiki/13-player-actions.md §5)
 *
 * SUPERSEDED: see player_actions_full.c::queen_menu_open_009CF0 plus
 * dig_action_03D7EA / neighbour_action_03D808 for the ROM-verified flow.
 * The pseudocode below assumes "click Queen menu -> directly call
 * entity_spawn for type 20 / type 24". The ROM is indirected: the menu
 * commit at $00:9D38 writes the slot index to dp[$02B7], and the colony
 * tick dispatcher at $03:D792 reads dp[$02B7] one tick later, runs the
 * matching slot ($03:D7EA = DIG, $03:D808 = LAY/NEIGHBOUR), then zeroes
 * $02B7 ($02:8054-8059) so the action fires exactly once.
 *
 * The Dig handler gates on dp[$4A]==1 (Worker form, NOT Queen as the
 * stub assumed) and routes to $03:B7A7 — i.e. the player is in Worker
 * form digging into dirt, NOT a Queen excavating a new nest. The "Dig
 * New Nest" Queen action is actually slot 9 ($D808) which gates on
 * dp[$4A]==2 (Queen) AND dp[$48]>=3 (mature).
 * ------------------------------------------------------------------------
 * Table at $01:872F (2 entries):
 *
 *   $01:872E: FF                ; (terminator of preceding string)
 *   $01:872F: 02                ; count = 2
 *   $01:8730: 34 87             ; ptr -> "Dig New Nest"   @ $8734
 *   $01:8732: 41 87             ; ptr -> "Lay Eggs"       @ $8741
 *
 * The Queen menu opens only when the Yellow Ant is currently the Queen
 * (only possible in Full Game mode after the mating flight). Trigger
 * detection (per the manual p.20):
 *   - PLAY_MODE == 2 (Full Game) AND
 *   - Yellow Ant's caste byte = 2 (Queen)
 *
 * Effects:
 *
 *   Dig New Nest:
 *     - Spawn a type-20 entity at the Yellow Ant's position.
 *     - Type 20's carve_A704 routine zeroes 4 layer rows in WRAM bank $7F
 *       starting at $7F:4000 — the world tile bitmap — for each tile the
 *       entity walks over, "digging a new chamber". (See entities_c.c
 *       type20_state1_carve_A704.)
 *     - Tutorial confirmation: $01:B14D "Press B and Dig New Nest"
 *
 *   Lay Eggs:
 *     - INC WRAM_EGGS_LAID_E80E (the running count that feeds the
 *       Status Screen's "Eggs Hatched %" metric).
 *     - Spawn a type-24 entity (egg visual) at the Yellow Ant's position
 *       — type-24 state-1 has the "fall in + land + stand forever" body
 *       that matches an immobile egg.
 *     - The hatching is handled by the slow tick in simulation.c
 *       (round_robin_slow_ABEF phase 0 / 2 — see EGGS_HATCHED_E80C).
 *     - Tutorial confirmation: $01:B16D "Lay Eggs"
 *
 * Both menu options consume one Queen action; the queen can't issue more
 * actions until the popup closes (the popup-machine clears dp[$02A7]).
 * ======================================================================== */

void queen_menu_apply_pseudo(uint8_t selected_slot)
{
    extern Entity *yellow_ant_get_self(void);
    Entity *yellow = yellow_ant_get_self();
    if (!yellow) return;

    /* Verify we're in Full Game AND yellow is queen. */
    if (wram[WRAM_PLAY_MODE] != 0x02) return;
    if (yellow->scratch[4] != 0x02 /* QUEEN_CASTE */) return;

    switch (selected_slot) {
    case 1: /* Dig New Nest */
        /* Spawn the dig-excavator entity at yellow's position. */
        entity_spawn_0499C1((uint16_t)yellow->x, (uint16_t)yellow->y, 20);
        apu_play_sfx_008EA3(0x2C);          /* "dig start" SFX */
        break;

    case 2: /* Lay Eggs */
        SW16(WRAM_EGGS_LAID_E80E, (uint16_t)(W16(WRAM_EGGS_LAID_E80E) + 1));
        entity_spawn_0499C1((uint16_t)yellow->x, (uint16_t)yellow->y, 24);
        apu_play_sfx_008EA3(0x2B);          /* "lay egg" SFX */
        break;

    default:
        break;
    }
}

/* ========================================================================
 * STAGE 6 — HUNGER / EAT / TROPHALLAXIS  (see wiki/13-player-actions.md
 *                                          §7 "Eating — no dispatcher" and
 *                                          §8 "Trophallaxis")
 * ------------------------------------------------------------------------
 * Hunger model (from simulation.c::SUMM_HUNGER_B8):
 *
 *   - The hunger meter at $7E:E7B8 is a 16-bit value 0..0xFF (8-bit
 *     effective). It DECREMENTS over time via the per-tick FEED_HUNGER_D2
 *     volatile feeder at $7E:E7D2 (which the live-stats-summary copies
 *     into SUMM_HUNGER_B8 once per sim tick).
 *
 *   - "Hungry" threshold (per tutorial $01:B07E "is HUNGRY!!!"):
 *       hunger < 0x30        -> the warning fires
 *
 *   - "Eat" raises hunger meter back toward 0xFF and INCs total food
 *     eaten (EATEN_COUNTER_E764 — gets fed into SUMM_EATEN_C0 via the
 *     live-stats-summary).
 *
 *   - "Trophallaxis" (food sharing) = a black ant gives food to Yellow:
 *     same effect as Eat but the message is different. Tutorial $01:A219
 *     "was fed by its nestmate".
 *
 *   - "Pick up food" attaches the food to Yellow's carry slot. Hunger
 *     keeps ticking down even while carrying — the manual specifically
 *     says "the Yellow Ant can carry food but can't eat what it's
 *     carrying without putting it down first" (paraphrased). To eat
 *     while carrying, the player must B-click the food in hand: it
 *     drops, immediately followed by an Eat on the dropped food.
 *
 * The carry-item state lives in TWO places, depending on whether the
 * entity is the Yellow Ant or a regular worker:
 *
 *   - For the Yellow Ant entity (the "controlled" ant), the carried
 *     item type is in entity scratch[3] = byte +10 in the record (the
 *     same byte we name `scratch10` — repurposed as "carry tag"
 *     instead of "countdown timer" when state == carrying).
 *     Values: 0 = nothing
 *             1 = pebble (rock)
 *             2 = food crumb
 *             3 = egg
 *             4 = larva
 *             5 = pupa
 *
 *   - For regular worker ants (types 14/15), they also use scratch10 the
 *     same way — that's why the type 14/15 spawn helpers set scratch10 to
 *     "60-frame timer" only in the NON-carrying state.
 *
 * The "drop" action is a state transition: scratch10 -> 0 and spawn a
 * new entity at the ant's position with the appropriate type.
 * ======================================================================== */

#define CARRY_NONE      0
#define CARRY_PEBBLE    1
#define CARRY_FOOD      2
#define CARRY_EGG       3
#define CARRY_LARVA     4
#define CARRY_PUPA      5

/* Eat: refill hunger meter and clear the food entity. */
void simulate_eat_food_for_yellow_lift(Entity *food)
{
    extern Entity *yellow_ant_get_self(void);
    Entity *y = yellow_ant_get_self();

    /* Refill hunger to max. */
    SW16(WRAM_HUNGER_E7B8, 0x00FF);

    /* Push hunger-feeder so the next sim-tick consumes it. */
    SW16(WRAM_HUNGER_FEED_E7D2, (uint16_t)(W16(WRAM_HUNGER_FEED_E7D2) + 0xFF));

    /* Tally "eaten" — fed into SUMM_EATEN_C0. */
    SW16(0xE764, (uint16_t)(W16(0xE764) + 1));

    /* Despawn the food entity. */
    food->type = 0;

    /* If yellow was carrying food and we just ate the FLOOR food, the
     * carry doesn't change. If we ate the carried food (by drop-then-eat),
     * scratch10 will be cleared by the caller's drop step. */
    (void)y;

    apu_play_sfx_008EA3(0x4E);              /* "munch" SFX */
}

/* Pick up: attach food to yellow's carry slot. */
void simulate_pickup_food_for_yellow_lift(Entity *food)
{
    extern Entity *yellow_ant_get_self(void);
    Entity *y = yellow_ant_get_self();
    if (!y) return;

    /* Drop whatever was being carried (if anything) at the old position. */
    if (y->scratch10 != CARRY_NONE) {
        /* Spawn a "dropped" sprite of the previous carry-type. */
        static const uint8_t carry_to_type[6] = {
            0,                   /* none */
            16,                  /* pebble -> type 16 (rock) */
            9,                   /* food   -> type 9 (drifting food) */
            24,                  /* egg    -> type 24 (egg visual)   */
            25,                  /* larva  -> type 25                 */
            12,                  /* pupa   -> type 12                 */
        };
        entity_spawn_0499C1(y->x, y->y, carry_to_type[y->scratch10]);
    }

    y->scratch10 = CARRY_FOOD;
    food->type = 0;                          /* consume floor food */
    apu_play_sfx_008EA3(0x44);               /* "pickup" SFX */
}

/* Trophallaxis: black ant feeds Yellow Ant (same effect as Eat, different
 * confirmation message). */
void simulate_trophallaxis_for_yellow(Entity *donor)
{
    (void)donor;
    SW16(WRAM_HUNGER_E7B8, 0x00FF);
    SW16(WRAM_HUNGER_FEED_E7D2, (uint16_t)(W16(WRAM_HUNGER_FEED_E7D2) + 0x80));

    apu_play_sfx_008EA3(0x4F);               /* "feed-from-nestmate" SFX */

    /* The tutorial-message queue would fire $01:A215 here:
     *   "The Yellow Ant was fed by its nestmate" */
}

/* ========================================================================
 * STAGE 7 — ATTACK (B-click on red ant)  (see wiki/13-player-actions.md §6,
 *                                          and wiki/15-dangers.md §3 for the
 *                                          shared rect-sweep kernel.)
 *
 * SUPERSEDED: see player_actions_full.c::rect_sweep_action_03EE66 for the
 * ROM-verified body. The pseudocode below assumes a single per-ant kill
 * with an IN_FIGHT_BIT marker; the ROM at $03:EE66 is actually a
 * RECTANGULAR SWEEP: it iterates the B-colony parallel-array, kills every
 * ant whose (X, Y) falls inside the dp[$E5]+dp[$EB] / dp[$E7]+dp[$E9]
 * rectangle by calling kill_resolver $02:C379. There is no "is red?" gate
 * — the kill is colour-blind, which is why Cat's Paw / Mower / B-click
 * all share the path: each just supplies a different rectangle size.
 * ------------------------------------------------------------------------
 * When the Yellow Ant is in Worker/Soldier form and the player clicks a
 * red (enemy-colony) ant, the action queue is filled with an ATTACK
 * directive. The walking-ant state machine (entities_b.c types 14/15)
 * already has state 4 = "attack pose"; the player-directed attack just
 * forces the entity into state 4 with the red ant as target.
 *
 * Resolution of the fight is centralized in simulation.c's
 * fight_resolver_96D7 (called every sim tick). It compares attacker /
 * defender strength stats and chooses a winner. The winner's
 * FIGHTS_*_WON counter gets bumped; the loser's type byte goes to 0
 * (despawn).
 *
 * For the IMMEDIATE visible effect: the attacker enters state 3->4 of
 * its walking-ant AI (the "lunge" pose), plays SFX $4E (the same "hit"
 * cue used by the queen on collision), and the next sim tick may resolve
 * the fight if both ants are still alive on the same tile.
 * ======================================================================== */
void simulate_attack_red_for_yellow(Entity *target_red)
{
    extern Entity *yellow_ant_get_self(void);
    Entity *y = yellow_ant_get_self();
    if (!y) return;

    /* Yellow Ant approaches in state-3 (pose-before-attack), then state-4.
     * The walking-ant tables already handle this animation. */
    y->state = 3;
    y->scratch10 = 0x1E;         /* 30-frame timer (state-3 reload) */
    y->pad_e    = 0x03;          /* sprite lane = "attacking" */

    /* Mark target as "engaged" — fight_resolver_96D7 will see both ants
     * within range and resolve. */
    target_red->scratch[4] |= 0x40;   /* IN_FIGHT_BIT */

    apu_play_sfx_008EA3(0x4E);
}

/* ========================================================================
 * STAGE 8 — DEATH + REBIRTH
 * ------------------------------------------------------------------------
 * From manual p.13: when the Yellow Ant dies (hunger to 0 OR killed in
 * combat OR squished by a danger), the player is reincarnated into the
 * next available egg/larva/pupa. The body type doesn't change — Worker
 * stays Worker — but a fresh entity record is allocated.
 *
 * The trigger:
 *   - hunger == 0  -> set "yellow died of starvation" event, SUMM_STARVE_C2
 *                     increments
 *   - HP == 0 from combat -> SUMM_KILLED_C4 increments
 *
 * The rebirth path scans the entity table for an entity of type 24/25
 * (egg) or 12 (pupa drifting) belonging to the same colony, "captures"
 * it (mutates type to 14/15 worker/soldier), and assigns the new entity
 * as the Yellow Ant. The tutorial message $01:A34F "The Yellow Ant has
 * just lost its last [life]" fires when no rebirthable host remains.
 * ======================================================================== */
void simulate_yellow_ant_dies(int cause /* 0=starve 1=combat 2=danger */)
{
    extern Entity *yellow_ant_get_self(void);
    Entity *y = yellow_ant_get_self();
    if (!y) return;

    /* Bump death-cause counters. */
    if      (cause == 0) SW16(0xE7C2, (uint16_t)(W16(0xE7C2) + 1)); /* STARVE */
    else if (cause == 1) SW16(0xE7C4, (uint16_t)(W16(0xE7C4) + 1)); /* KILLED */
    SW16(WRAM_FEED_DEAD_E7DE, (uint16_t)(W16(WRAM_FEED_DEAD_E7DE) + 1));

    /* Drop carry. */
    if (y->scratch10 != CARRY_NONE) {
        static const uint8_t carry_to_type[6] = { 0, 16, 9, 24, 25, 12 };
        entity_spawn_0499C1(y->x, y->y, carry_to_type[y->scratch10]);
        y->scratch10 = CARRY_NONE;
    }

    /* Despawn old body. */
    y->type = 0;

    /* Decrement lives counter. If 0, game over. */
    uint16_t lives = W16(WRAM_WALK_LIVES_E8C4);
    if (lives == 0) {
        /* Game-over event — handled by simulation.c queue. */
        SW16(WRAM_WALK_ACTIVE_E8BE, 0);
        return;
    }
    SW16(WRAM_WALK_LIVES_E8C4, lives - 1);

    /* Scan entity table for a rebirthable host — first egg/larva/pupa
     * belonging to the player's colony. */
    for (uint8_t *p = (uint8_t *)ENTITY_TABLE;
         p < (uint8_t *)&wram[ENTITY_TABLE_END];
         p += sizeof(Entity)) {
        Entity *e = (Entity *)p;
        if (e->type == 24 || e->type == 25 ||      /* egg variants */
            e->type == 12) {                       /* drifting pupa */
            e->type = 14;                          /* become Worker */
            e->state = 0;                          /* spawn anim    */
            e->scratch10 = 0x3C;                   /* 60-f timer    */
            /* Mark this entity as the new Yellow Ant — done by setting
             * a "controlled" bit elsewhere. */
            extern void yellow_ant_assign_to(Entity *e);
            yellow_ant_assign_to(e);
            return;
        }
    }

    /* No rebirthable host — tutorial $01:A34F fires. */
}

/* ========================================================================
 * STAGE 9 — TOP-LEVEL DISPATCH (the canonical "A-click / B-click router")
 * ------------------------------------------------------------------------
 * Wires the per-view loops to per-action handlers. Called from the
 * surface close-up's run state ($23 sub-state 1) when sub_DC84 returns
 * non-zero against the cursor's NEAREST entity in the entity table.
 *
 * This is the unifying entry point that the COVERAGE.md target row
 * "Move (cursor click → ant walks)" should resolve to once the per-view
 * jump-tables are fully understood.
 * ======================================================================== */

/* Find the entity nearest to the cursor and within DC84's hit-box.
 * Returns 0 if no entity is under the cursor (player clicked empty space
 * or terrain). */
Entity *cursor_pick_entity(void)
{
    for (uint8_t *p = (uint8_t *)ENTITY_TABLE;
         p < (uint8_t *)&wram[ENTITY_TABLE_END];
         p += sizeof(Entity)) {
        Entity *e = (Entity *)p;
        if (e->type == 0) continue;
        /* Use DC84 against this entity. */
        if (sub_DC84_entity_clicked(e)) return e;
    }
    return 0;
}

/* Probe a dirt tile under the cursor (for dig actions). World tile bitmap
 * is at WRAM $7F:4000..$7F:5FFF (8 KB of bit-packed tile flags). */
extern int  world_tile_is_dirt(uint16_t world_x, uint16_t world_y);
extern void world_set_dirt(uint16_t world_x, uint16_t world_y, int dirt);

/* a_button_action — the master "A button at (cursor X, Y)" router.
 * See wiki/13-player-actions.md §2 "A button — cursor confirm / select / dig". */
void player_a_button_action(void)
{
    /* dp[$71] gates everything — if a menu is open, the menu owns input. */
    if (dp[DP_MENU_OPEN_LOCK]) return;

    Entity *target = cursor_pick_entity();
    if (target) {
        /* Icon clicks: types 26, 30, 31 are HUD entities. Ignore here. */
        if (target->type == 26 || target->type == 30 || target->type == 31)
            return;
        /* Click on an in-world entity with the A button = select / lock
         * camera. The cursor highlights the entity and the camera follows
         * until B-cancel or click-elsewhere. (Manual p.6 "A button:
         * select".) */
        SW16(0x0202, target->x);                 /* cursor lock-target X */
        SW16(0x0204, target->y);                 /* cursor lock-target Y */
        wram[0x0200] = 0x01;                     /* CURSOR_VISIBLE      */
        return;
    }

    /* No entity under cursor — interpret as "walk-to" or "dig". */
    uint16_t world_x = (uint16_t)(dp[DP_CURSOR_X] + W16(DP_CAMERA_X));
    uint16_t world_y = (uint16_t)(dp[DP_CURSOR_Y] + W16(DP_CAMERA_Y));

    if (world_tile_is_dirt(world_x, world_y)) {
        /* Dig: queue a DIG action for Yellow Ant. */
        ActionBuildSlot *as = &ACTION_BUILD_AT_0B00[dp[DP_BUILD_COUNTER_12]];
        as->target_x = world_x;
        as->target_y = world_y;
        as->action_kind = 1;     /* DIG */
        return;
    }

    /* Walk-to-empty-space. Tutorial: $01:B7D5 "will walk toward [...]" */
    ActionBuildSlot *as = &ACTION_BUILD_AT_0B00[dp[DP_BUILD_COUNTER_12]];
    as->target_x = world_x;
    as->target_y = world_y;
    as->action_kind = 0;         /* WALK */
}

/* b_button_action — the master "B button at (cursor X, Y)" router.
 * See wiki/13-player-actions.md §3 "B button — pickup / cancel / context menu". */
void player_b_button_action(void)
{
    if (dp[DP_MENU_OPEN_LOCK]) {
        /* Menu open — B cancels. */
        dp[DP_POPUP_GOTO_02E3] = 0;             /* request "close" */
        return;
    }

    Entity *target = cursor_pick_entity();
    if (!target) {
        /* B on empty -> no-op (no "drop carry on empty" path; carry can
         * only be released by dropping on a valid surface, which lives
         * in the per-tile handler at world_set_dirt etc.) */
        return;
    }

    extern Entity *yellow_ant_get_self(void);
    Entity *yellow = yellow_ant_get_self();
    int yellow_is_worker = yellow_ant_is_worker(yellow);

    /* B-click on YELLOW ANT himself: open caste-specific menu. */
    if (target == yellow) {
        if (yellow_is_worker) {
            dp[DP_POPUP_ACTIVE_02A7] |= 0x08;   /* recruit-menu cue */
        } else {
            dp[DP_POPUP_ACTIVE_02A7] |= 0x10;   /* queen-menu cue */
        }
        return;
    }

    /* B-click on a food/egg/larva/pupa/rock: pick up (or eat if hungry).
     * Types per the manual:
     *   food crumb -> type 9 or 10 (drifting)
     *   egg        -> type 24 / 25
     *   larva      -> type 25 (mirror)
     *   pupa       -> type 12
     *   rock       -> type 16
     */
    if (target->type == 9 || target->type == 10 ||
        target->type == 12 || target->type == 16 ||
        target->type == 24 || target->type == 25) {
        if (yellow_is_worker) {
            if (W16(WRAM_HUNGER_E7B8) < 0x30 && target->type == 9) {
                simulate_eat_food_for_yellow_lift(target);
            } else {
                simulate_pickup_food_for_yellow_lift(target);
            }
        }
        return;
    }

    /* B-click on a red ant: Yellow Ant attacks. */
    if (target->type == 15) {                     /* red soldier/worker */
        simulate_attack_red_for_yellow(target);
        return;
    }

    /* B-click on a black ant: trophallaxis if hungry, else no-op. */
    if (target->type == 14) {
        if (W16(WRAM_HUNGER_E7B8) < 0x30) {
            simulate_trophallaxis_for_yellow(target);
        }
        return;
    }
}

/* ========================================================================
 * Compile-anchor — keeps the linker from pruning the dispatch entrypoints
 * even when no other TU references them yet.
 * ======================================================================== */
/* F6 wiring fix: the recruit/queen "*_pseudo" handlers are superseded by the
 * ROM-verified bodies in player_actions_full.c (per V4-2). The anchor table
 * now references the *_full equivalents so the linker keeps the real bodies
 * live instead of the stub pseudos.
 *
 *   recruit_menu_apply_pseudo  -> recruit_apply_02A1F4           (STAGE 4)
 *   queen_menu_apply_pseudo    -> player_action_dispatch_03D792  (STAGE 9 dispatcher,
 *                                                                 includes Lay/Dig)
 *
 * worker_click_handler_pseudo and food_click_handler_pseudo have no _full
 * replacement yet (V4-2: "still active, real lift partial"), so they stay
 * here and remain MAJOR TODOs. */
extern void recruit_apply_02A1F4(uint16_t desired);
extern void player_action_dispatch_03D792(void);

__attribute__((used))
static void *const _player_actions_refs[] = {
    (void *)close_up_nest_a_button_action_CD30,
    (void *)surface_closeup_a_press_A824,
    (void *)surface_closeup_b_press_A86A,
    (void *)recruit_apply_02A1F4,           /* was recruit_menu_apply_pseudo */
    (void *)player_action_dispatch_03D792,  /* was queen_menu_apply_pseudo  */
    (void *)simulate_eat_food_for_yellow_lift,
    (void *)simulate_pickup_food_for_yellow_lift,
    (void *)simulate_trophallaxis_for_yellow,
    (void *)simulate_attack_red_for_yellow,
    (void *)simulate_yellow_ant_dies,
    (void *)cursor_pick_entity,
    (void *)player_a_button_action,
    (void *)player_b_button_action,
    (void *)worker_click_handler_pseudo,    /* TODO: no _full lift yet */
    (void *)food_click_handler_pseudo,      /* TODO: no _full lift yet */
};

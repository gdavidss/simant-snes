/*
 * SimAnt (SNES, Maxis / Tomcat Systems, 1993)
 * ------------------------------------------------------------------------
 *  WIKI: see wiki/10-territory-49areas.md for the full prose write-up of
 *        the 49-area Full-Game world, the Mating Flight trigger, the
 *        Mass Exodus per-tick loop, and the SNES-port "one area at a
 *        time" finding.
 *
 *  TERRITORY / FULL-GAME AREA MECHANICS
 *
 *  This file documents the manual's "House Screen" (the 7x7 = 49-area
 *  yard+house map), the Status-Screen percentage formulas, the Marriage
 *  Flight & Mass Exodus colony-expansion events, and the area-state
 *  byte encoding used by the per-area sprite renderer.
 *
 *  All routines lifted from the 65816 disassembly of the ROM and
 *  cross-checked against:
 *    - SimAnt instruction booklet pages 18-20 (Full Game expansion)
 *      and pages 29-32 (Evaluation Screens)
 *    - simulation.c (the per-tick subsystem layout)
 *    - ui_menus.c (the live area-display layer)
 *
 *  Key WRAM addresses used here (cross-reference simulation.c):
 *
 *      $7E:E736   CUR_AREA_X        16-bit  current area X coord (0..6)
 *      $7E:E738   CUR_AREA_Y        16-bit  current area Y coord (0..6)
 *      $7E:E776   COLONY_B_HEALTH   16-bit  0..100; -1 per food-starved
 *      $7E:E778   COLONY_R_HEALTH   16-bit  0..100
 *      $7E:E79C   POP_B_BREEDER     16-bit  B-colony winged-breeder count
 *      $7E:E79E   POP_R_BREEDER     16-bit  R-colony winged-breeder count
 *      $7E:E844   FIGHTS_B_WON      16-bit  cumulative B-side win count
 *      $7E:E848   FIGHTS_R_WON      16-bit  cumulative R-side win count
 *      $7E:E80C   EGGS_HATCHED      16-bit  cumulative
 *      $7E:E80E   EGGS_LAID         16-bit  cumulative
 *      $7E:E7EA   FEED_B_FOOD_HOME  16-bit  B food returned to nest
 *      $7E:E7EE   FEED_R_FOOD_HOME  16-bit  R food returned to nest
 *      $7E:EA46   AREA_B_POP_MAP    16x16   8x8 grid, 2 bytes each, 7x7 used
 *      $7E:EAC6   AREA_R_POP_MAP    16x16   8x8 grid, 2 bytes each, 7x7 used
 *      $7E:EB60   AREA_B_POP_LIVE   16-bit  current-area B pop (cap=250)
 *      $7E:EB62   AREA_R_POP_LIVE   16-bit  current-area R pop (cap=250)
 *      $7E:EB5C   AREA_R_PRESENCE   16-bit  count of 49 areas with R>0
 *      $7E:EB5E   AREA_B_PRESENCE   16-bit  count of 49 areas with B>0
 *      $7E:EB48   AREA_B_SURPLUS    16-bit  B ants queued to split outward
 *      $7E:EB4A   AREA_R_SURPLUS    16-bit  R ants queued to split outward
 *      $7E:EC94   MARRIAGE_COOLDOWN 16-bit  200-tick re-arm timer
 *      $7F:E87E   AREA_COUNT        8-bit   live-area cursor (0..5)
 *      $7F:E880   AREA_LAST_IDX     8-bit   index of "current" live entry
 *      $7F:E882   AREA_X_TABLE[12]  per-live-area screen X (units of 16)
 *      $7F:E88E   AREA_Y_TABLE[12]  per-live-area screen Y
 *      $7F:E89A   AREA_FLAGS_A[12]  per-live-area "active/scratch" byte
 *      $7F:E8A6   AREA_FLAGS_B[12]  per-live-area countdown to state-advance
 *      $7F:E8B2   AREA_STATE[12]    per-live-area sprite-state byte (0..7)
 *      $7E:F0D3   AREA_TARGET_X     8-bit   new-area X target (mating flight)
 *      $7E:F0D5   AREA_TARGET_Y     8-bit   new-area Y target
 *
 *  Reference index for grep against disasm.txt:
 *      marriage_flight_trigger     $02:9E35
 *      colony_health_grade         $02:9E62
 *      caste_percent_status        $02:9419  (worker/soldier/breeder/queen %)
 *      behavior_percent_status     $02:94E1  (forage/dig/nurse %)
 *      per_area_colony_score       $02:9EEB  (caste-share within area)
 *      per_area_action_chooser     $02:A033  (foraging/digging selector)
 *      per_area_food_tick          $03:E4DB
 *      area_grid_scan              $03:F02A
 *      mass_exodus_cap             $03:F050
 *      area_spread_split           $03:F1F3
 *      area_split_to_neighbour     $03:F358
 *      neighbour_balance_count     $03:F2D9
 *      area_offset_helper          $02:F5B2
 *      area_signed_random          $02:F38D
 *      area_xy_compare_sfx         $04:8000  (current-area SFX cue)
 *      house_screen_setup          $00:B2B0  (state $0E)
 *      house_screen_renderer       $04:BD9B  (entity type 0x35)
 *      house_area_append           $03:96B0
 *      area_state_advance          $03:9930
 *      area_state_random_redraw    $03:9959
 *      area_state_set_BLACK_init   $03:9888
 *      area_state_set_RED_init     $03:9967
 *      area_message_dispatch       $00:DFCD
 *      area_msg_pointer_table      $00:E026
 *      "You can only enter..." str $01:A550 (msg-code 18..24)
 * ======================================================================== */

#include <stdint.h>

/* External WRAM/dp from the rest of the decomp. */
extern uint8_t wram[0x20000];
#define dp wram

#define WMEM16(off)  (*(uint16_t *)&wram[off])
#define WMEM8(off)   (*(uint8_t  *)&wram[off])

/* ========================================================================
 *  AREA STATE BYTE ENCODING ($7F:E8B2[i] — per LIVE area, 12 entries)
 *  ------------------------------------------------------------------------
 *  Agent K's earlier guess was: 0=empty / 1=BLACK / 2=RED / 3=STRIPED,
 *  bit-7 = FLASHING. Lifting $03:9930..$9967 (state-advance) and the
 *  per-state tile lookup at $04:BE41 reveals the encoding is RICHER:
 *
 *      AREA_STATE byte value -> sprite-tile-base via $04:BE41[state]:
 *          0  -> $42  ("no special tile" sentinel — only background draws)
 *          1  -> $48  BLACK (filled black square)
 *          2  -> $4A  RED   (filled red square)
 *          3  -> $48  BLACK (state-advance pass)
 *          4  -> $4A  RED   (state-advance pass)
 *          5  -> $4C  STRIPED v1
 *          6  -> $4E  STRIPED v2
 *          7  -> $48  (rare — wrap)
 *
 *  And the state-advance ROM table at $04:BE41 is:
 *      42 48 4A 48 4A 4C 4E 48
 *
 *  $03:9930 INCs the byte while it stays < 4, otherwise rolls into a
 *  random redraw at 3..6. So "established" colonies cycle 1->2->3->4 then
 *  rerandomize. There is no explicit bit-7 "FLASHING" flag — the
 *  flashing/current-area effect comes from the renderer drawing a
 *  separate composite tile (0x95+0x20+0x42+0x24) on top of the base. The
 *  enum from ui_menus.c thus needs a tweak: FLASHING is *not* a bit on
 *  the state byte, it's a property of the AREA_LAST_IDX entry.
 *
 *  Also: only 8 sprite values are valid (0..7). The renderer at $04:BDC1
 *  masks `state & 7` is NOT explicit — it always reads up to 7 bytes
 *  forward, so any value above 7 would index past the table. The state
 *  byte is only ever set to 0..6 by the lifted code, so this is safe.
 *
 *  Manual cross-check (p.30):
 *      "Each area can be colored green (empty), black, red, striped
 *       (both colonies), and the current area flashes."
 *  Tile bytes $48 ↔ black, $4A ↔ red, $4C/$4E ↔ striped variants —
 *  matches the manual exactly.
 * ======================================================================== */
enum AreaState {
    AREA_STATE_EMPTY        = 0x00,
    AREA_STATE_BLACK        = 0x01,
    AREA_STATE_RED          = 0x02,
    AREA_STATE_BLACK_ALT    = 0x03,   /* state-advance pass (still BLACK) */
    AREA_STATE_RED_ALT      = 0x04,   /* state-advance pass (still RED)   */
    AREA_STATE_STRIPED_A    = 0x05,
    AREA_STATE_STRIPED_B    = 0x06,
};

/* The per-state base-tile lookup table at $04:BE41 (8 bytes). */
static const uint8_t area_state_tile_BE41[8] = {
    0x42, 0x48, 0x4A, 0x48, 0x4A, 0x4C, 0x4E, 0x48,
};

/* Helper: does an AREA_STATE byte mean B is present? */
int area_state_has_B(uint8_t state)
{
    /* States 1, 3, 5, 6 all have a black component. */
    return state == AREA_STATE_BLACK || state == AREA_STATE_BLACK_ALT ||
           state == AREA_STATE_STRIPED_A || state == AREA_STATE_STRIPED_B;
}
int area_state_has_R(uint8_t state)
{
    return state == AREA_STATE_RED || state == AREA_STATE_RED_ALT ||
           state == AREA_STATE_STRIPED_A || state == AREA_STATE_STRIPED_B;
}

/* ========================================================================
 *  AREA-OFFSET HELPER  ($02:F5B2)
 *  ------------------------------------------------------------------------
 *  Lifted verbatim:
 *      STX $F1                   ; save X (caller's X coord)
 *      TYA                       ; A = Y
 *      ASL : ASL : ASL           ; A = Y * 8
 *      ADC $F1                   ; A = Y*8 + X
 *      ASL                       ; A = (Y*8 + X) * 2
 *      TAX
 *      RTL
 *  Returns into X. The 49-area map is laid out as an 8x8 = 64-slot grid
 *  (last column/row are padding), 16 bits per entry. The 7x7 valid area
 *  range is X in 0..6, Y in 0..6 (the same as in $03:F2D9's bounds-test).
 * ======================================================================== */
static inline uint16_t area_offset_F5B2(uint8_t x, uint8_t y)
{
    return (uint16_t)((((uint16_t)y << 3) + x) << 1);
}

/* AREA_B_POP / AREA_R_POP macros use the same offset; both maps live at
 * $7E:EA46 (B) and $7E:EAC6 (R). */
#define AREA_B_POP(x, y)   WMEM16(0xEA46 + area_offset_F5B2(x, y))
#define AREA_R_POP(x, y)   WMEM16(0xEAC6 + area_offset_F5B2(x, y))

/* Current area cursor (constant in the SNES port — see note below). */
#define CUR_AREA_X         WMEM16(0xE736)
#define CUR_AREA_Y         WMEM16(0xE738)

/* Per-area current-area pops (NOT the global map; these get aggregated
 * from the live entity list each tick by $02:923B and propagated into
 * the 49-area map at $03:F050+). */
#define AREA_B_POP_LIVE    WMEM16(0xEB60)
#define AREA_R_POP_LIVE    WMEM16(0xEB62)

/* Per-area presence counts (computed by $03:F02A loop). */
#define AREA_B_PRESENCE    WMEM16(0xEB5E)
#define AREA_R_PRESENCE    WMEM16(0xEB5C)

/* Per-area surplus queues (for the area-split mechanic). */
#define AREA_B_SURPLUS     WMEM16(0xEB48)
#define AREA_R_SURPLUS     WMEM16(0xEB4A)

/* Caste / behavior tallies (from $02:923B). */
#define POP_B_BREEDER      WMEM16(0xE79C)
#define POP_R_BREEDER      WMEM16(0xE79E)

/* Fight + egg cumulative tallies. */
#define FIGHTS_B_WON       WMEM16(0xE844)
#define FIGHTS_R_WON       WMEM16(0xE848)
#define EGGS_HATCHED       WMEM16(0xE80C)
#define EGGS_LAID          WMEM16(0xE80E)
#define FEED_B_FOOD_HOME   WMEM16(0xE7EA)
#define FEED_R_FOOD_HOME   WMEM16(0xE7EE)

/* Colony health (decremented by per-tick chain when food runs out). */
#define COLONY_B_HEALTH    WMEM16(0xE776)
#define COLONY_R_HEALTH    WMEM16(0xE778)

/* Marriage-flight cooldown. */
#define MARRIAGE_COOLDOWN  WMEM16(0xEC94)

/* PLAY_MODE: 0/1=Tutorial/Scenario, 2=Full Game, 3=AntInfo. Used as a
 * gate on territory-related mechanics. */
#define PLAY_MODE          WMEM8 (0x99)

/* The live-area display tables in bank $7F. */
#define LIVE_AREA_COUNT          WMEM8 (0x1E87E)
#define LIVE_AREA_LAST_IDX       WMEM8 (0x1E880)
#define LIVE_AREA_X(i)           WMEM8 (0x1E882 + (i))
#define LIVE_AREA_Y(i)           WMEM8 (0x1E88E + (i))
#define LIVE_AREA_FLAGS_A(i)     WMEM8 (0x1E89A + (i))
#define LIVE_AREA_FLAGS_B(i)     WMEM8 (0x1E8A6 + (i))
#define LIVE_AREA_STATE(i)       WMEM8 (0x1E8B2 + (i))

/* External helpers used by this file. */
extern uint16_t rand_modulo_F3BD(uint16_t bound);     /* $02:F3BD */
extern void     queue_event_F65A(uint8_t event_id);   /* $02:F65A */


/* ========================================================================
 *  1. STATUS-SCREEN PERCENTAGE FORMULAS
 *  ------------------------------------------------------------------------
 *  All six percentages computed by the lifted code use the SNES hardware
 *  multiply/divide trampoline: store inputs in $E71A (num) / $E71E (den),
 *  call $02:F420 (16x16->32 multiply by 100) then $02:F4BF (32/16->16
 *  divide). The C reduction is `(num * 100) / den` with denominator-
 *  zero guard returning 0.
 *
 *  Manual page 32 specifies six readouts; mapped to ROM counters:
 *
 *      Colony Health %  = COLONY_B_HEALTH                  (0..100 in $E776)
 *      Foraging %       = FEED_B_FOOD_HOME * 100 /
 *                              (FEED_B_FOOD_HOME + FEED_R_FOOD_HOME)
 *      Eggs Hatched %   = EGGS_HATCHED  * 100 / EGGS_LAID
 *      Fights Won %     = FIGHTS_B_WON  * 100 /
 *                              (FIGHTS_B_WON + FIGHTS_R_WON)
 *      B.Ant Occupation = #areas-with-B * 100 / 49
 *      R.Ant Occupation = #areas-with-R * 100 / 49
 *
 *  IMPORTANT correction over Agent K: the manual's "Foraging %" is
 *  defined as "food brought home by black ants as a percentage of all
 *  food brought home by both colonies" — NOT "ants currently foraging
 *  vs total population". The cumulative food-home counters at $E7EA
 *  (B) and $E7EE (R) are exactly the numerators the manual references.
 *  These get incremented inside the fight/eat resolver chain (see
 *  $02:AC64 in simulation.c which builds SUMM_B_FOOD_BA from $E7EA).
 *
 *  Likewise "Fights Won %" is "percentage of fights won by the black
 *  ants" — NUMERATOR = B wins, DENOMINATOR = (B wins + R wins). The
 *  draws counter ($E84C) is NOT included; the manual is silent on
 *  draws and Agent I's history-channel layout treats them as a separate
 *  series.
 * ======================================================================== */
struct StatusPercents {
    uint8_t colony_health;     /* 0..100 */
    uint8_t foraging;          /* 0..100 */
    uint8_t eggs_hatched;      /* 0..100 */
    uint8_t fights_won;        /* 0..100 */
    uint8_t b_ant_occupation;  /* 0..100 */
    uint8_t r_ant_occupation;  /* 0..100 */
};

/* The exact ($02:F420 + $02:F4BF) reduction. */
static uint8_t pct100(uint16_t num, uint16_t den)
{
    if (den == 0) return 0;
    uint32_t q = ((uint32_t)num * 100u) / den;
    if (q > 100u) q = 100u;
    return (uint8_t)q;
}

/* Count of 49-area cells where B (or R) population is > 0. */
uint8_t area_b_occupied_count(void)
{
    uint8_t n = 0;
    for (uint8_t y = 0; y < 7; ++y)
        for (uint8_t x = 0; x < 7; ++x)
            if (AREA_B_POP(x, y) != 0) n++;
    return n;
}
uint8_t area_r_occupied_count(void)
{
    uint8_t n = 0;
    for (uint8_t y = 0; y < 7; ++y)
        for (uint8_t x = 0; x < 7; ++x)
            if (AREA_R_POP(x, y) != 0) n++;
    return n;
}

void status_screen_compute_territory(struct StatusPercents *out)
{
    /* Colony Health: $E776 is already a 0..100 byte (initialised to
     * $0064 in $03:8507 then DEC'd by the slow tick on food starvation).
     * Just clamp. */
    uint16_t hb = COLONY_B_HEALTH;
    if (hb > 100) hb = 100;
    out->colony_health = (uint8_t)hb;

    /* Foraging %: B-food-home / (B-food-home + R-food-home). */
    uint16_t bf = FEED_B_FOOD_HOME;
    uint16_t rf = FEED_R_FOOD_HOME;
    out->foraging = pct100(bf, (uint16_t)(bf + rf));

    /* Eggs Hatched %: hatched / laid (both cumulative). */
    out->eggs_hatched = pct100(EGGS_HATCHED, EGGS_LAID);

    /* Fights Won %: B / (B + R). The ROM stores B and R wins in
     * separate 16-bit counters at $E844 and $E848. Draws ($E84C) are
     * tracked separately and not counted toward either side. */
    uint16_t bw = FIGHTS_B_WON;
    uint16_t rw = FIGHTS_R_WON;
    out->fights_won = pct100(bw, (uint16_t)(bw + rw));

    /* Ant Occupation %: #areas-with-population / 49 (NOT 64; the 8x8
     * grid is padded but only the central 7x7 is valid play area). */
    out->b_ant_occupation = pct100(area_b_occupied_count(), 49);
    out->r_ant_occupation = pct100(area_r_occupied_count(), 49);
}

/* ========================================================================
 *  2. COLONY-HEALTH 0..5 GRADE  (lifted at $02:9E62..$9EEA)
 *  ------------------------------------------------------------------------
 *  The manual's "Colony Health" widget on the Status Screen also exposes
 *  a 0..5 GRADE (most icons reflect this rather than the raw percentage).
 *  The chain at $02:9E62 picks the grade:
 *
 *      if (COLONY_B_HEALTH < 10) {
 *          if ((AREA_B_POP_LIVE / 2) == AREA_R_POP_LIVE) return 0;
 *          if ((AREA_B_POP_LIVE / 2)  < AREA_R_POP_LIVE) return 0;
 *          if (AREA_R_POP_LIVE == 0)        return 0;
 *          if ((int16_t)AREA_R_POP_LIVE < 0) return 0;
 *          if (STARVED_COUNTER_E766 == 0 ||
 *              (int16_t)STARVED_COUNTER_E766 < 0) return 0;
 *          return 0;     ; "dead" grade
 *      }
 *      if (COLONY_B_HEALTH < 30) return 5;   ; "Crisis"
 *      if (COLONY_B_HEALTH < 50) return 4;   ; "Struggling"
 *      if (EVENT_THRESH_E746 < AREA_B_POP_LIVE) return 3;  ; "Hungry"
 *      if (AREA_B_POP_LIVE * 2 < EVENT_THRESH_E746) return 2;  ; "Comfortable"
 *      if (AREA_B_POP_LIVE < 100) return 1;  ; "Stable"
 *      else                       return 0;  ; "Thriving"
 *
 *  Returns value into A, called as: `JSL $029E35` -> JMP $9EEA which
 *  RTLs with A = grade. Caller at $02:9E21 stores into $EE34.
 *
 *  NOTE: the lifted chain in simulation.c::colony_health_grade_9E62 has
 *  this CORRECT in its lifted form (verified by re-disasm at $9E88+).
 * ======================================================================== */


/* ========================================================================
 *  3. MARRIAGE FLIGHT TRIGGER  ($02:9E35..$9E62, lifted)
 *  ------------------------------------------------------------------------
 *  Verbatim from the disassembly:
 *
 *      02:9E35  LDA $99             ; PLAY_MODE
 *      02:9E39  CMP #$0002
 *      02:9E3A  BNE skip            ; only in Full Game
 *      02:9E3C  LDA $EB60           ; AREA_B_POP_LIVE
 *      02:9E3F  CMP #$0064          ; >= 100?
 *      02:9E42  BCC skip
 *      02:9E44  LDA $E79C           ; POP_B_BREEDER
 *      02:9E47  CLC
 *      02:9E48  ADC $E79E           ; + POP_R_BREEDER
 *      02:9E4B  CMP #$0014          ; >= 20?
 *      02:9E4E  BCC skip
 *      02:9E50  LDA $EC94           ; cooldown
 *      02:9E53  BNE arm_only
 *      02:9E55  LDA #$004B          ; event #$4B = marriage flight
 *      02:9E58  JSL $02F65A         ; queue_event
 *      02:9E5C arm_only:
 *               LDA #$00C8          ; 200 ticks (~25 sec at 8.5Hz)
 *      02:9E5F  STA $EC94
 *      02:9E62  ...                 ; continues into colony_health_grade
 *
 *  IMPORTANT CORRECTIONS:
 *
 *  (a) The breeder threshold uses BOTH B and R breeder counts summed,
 *      NOT just the B count. This is a SNES-port simplification of the
 *      manual's "20 breeders" — in the original Mac/Apple II, it's per-
 *      colony. The SNES uses a combined check.
 *
 *  (b) The cooldown is RE-ARMED to 200 EVERY TIME the trigger is
 *      evaluated and the conditions are met, not just when the event
 *      fires. So once breeders+pop exceed thresholds, marriage flights
 *      fire at most once per 200 sim-ticks (~25 sec wall clock).
 *
 *  Note: "where do new queens land?" — the marriage-flight event $4B
 *  doesn't directly write to any area-state byte. The actual landing
 *  is implemented as a queen-entity (type 0x12 in entity table) being
 *  spawned during state $0E (the post-flight cinematic). The "expand
 *  to a new area" is then realized by the area-split tick at $03:F050+
 *  / $03:F358 which spreads excess B-population to adjacent areas. So
 *  the marriage flight is mostly a NARRATIVE event in this port — the
 *  visible territory change comes from the per-tick spread loop, not
 *  from a deterministic "place a new queen at (x,y)" handler. See
 *  area_split_to_neighbour_F358 below for the actual area-state change.
 * ======================================================================== */
/* NOTE: The canonical body lives in simulation.c::marriage_flight_trigger_9E35.
 * This file documents the SAME behavior with the more thorough investigation
 * notes above. Renaming local function to avoid duplicate-symbol on link. */
/* WIKI: wiki/10-territory-49areas.md §4 ("Mating Flight Trigger"). */
void territory_marriage_flight_trigger_9E35(void)
{
    if (PLAY_MODE != 0x02) return;                  /* Full Game only */
    if (AREA_B_POP_LIVE < 100) return;
    if ((POP_B_BREEDER + POP_R_BREEDER) < 20) return;
    if (MARRIAGE_COOLDOWN == 0) {
        queue_event_F65A(0x4B);                     /* marriage-flight event */
    }
    MARRIAGE_COOLDOWN = 200;                        /* re-arm */
}


/* ========================================================================
 *  4. MASS EXODUS CAP + SPREAD  ($03:F050..$F1F2)
 *  ------------------------------------------------------------------------
 *  Once every 32 ticks (gated by `$E878 & 0x1F == 0` at $03:F022), the
 *  area subsystem:
 *
 *    (a) Caps the live current-area populations at 250 ($00FA):
 *         - AND'd with $03FF first (sanity 10-bit clip)
 *         - If > 250 -> 250
 *         - Written to AREA_B_POP / AREA_R_POP for (CUR_AREA_X, Y)
 *    (b) Resets AREA_B_PRESENCE / AREA_R_PRESENCE to 0
 *    (c) Sets PRESENCE flags for the current area if pop > 0
 *    (d) Walks the 7x7 grid (X=0..6, Y=0..6 with skip on current):
 *         - For each non-empty cell, calls neighbour_balance_count
 *           (returns signed "B - R" weighted by 3 per neighbour)
 *         - If B pop in that cell + balance >= 250, calls
 *           area_split_to_neighbour with color=0 (B), THEN if the new
 *           cell is at the bottom-right corner (6,6), seeds 20 R ants
 *           into a random other area (the "queen escapes" behavior).
 *         - For R cells, splits with color=1 (R).
 *         - Also INCs AREA_B_PRESENCE / AREA_R_PRESENCE per occupied
 *           cell, used by the game-end check at $03:F25B.
 *    (e) After the grid walk, $03:F1F3 spreads queued SURPLUS:
 *         - If EE62 (loaded-game flag) AND surplus B (EB48) > 0 AND
 *           E8F8 counter has expired -> split B from current area.
 *         - Same for R.
 *
 *  This is the manual's "Mass Exodus" mechanic — when a colony fills
 *  a section to 250, it begins overflowing to adjacent sections. The
 *  exodus is NOT a one-shot teleport; it's a continuous trickle from
 *  the capped area outward.
 *
 *  Where does the queen go? The QUEEN entity itself (type 18) stays
 *  put — the SNES port has only ONE active area at a time (CUR_AREA_XY
 *  are never written; see note at $03:F0E9 / $E73C). The "Mass Exodus"
 *  visually shown on the House Screen is the per-tick AREA_B_POP map
 *  update, not a literal queen-relocation.
 *
 *  The dispersion algorithm:
 *      X' = clamp(X + randInt(-1..+1), 0, 6)
 *      Y' = clamp(Y + randInt(-1..+1), 0, 6)
 *      if (X', Y') != (X, Y):
 *          AREA_B_POP(X', Y')++   (or AREA_R_POP++)
 *      else: drop (no spread this tick).
 *
 *  Random offsets come from $02:F38D which is `JSL F3BD(2)` (giving
 *  0 or 1) then sign-flip on second random call — yielding -1, 0, or
 *  +1 with equal probability (well, +1 has 1/2 chance because the
 *  sign-flip on 0 leaves it 0; spread is X' in {-1, 0, +1}).
 * ======================================================================== */

/* Cap-current-area + the colony pop-presence reset (called every /32 ticks).
 * Verbatim lift of $03:F050..F0EF (the cap+presence subset).
 * WIKI: wiki/10-territory-49areas.md §5 ("Mass Exodus"). */
void mass_exodus_cap_and_presence_F050(void)
{
    /* B cap. */
    uint16_t bv = AREA_B_POP_LIVE & 0x03FF;
    if (bv > 0x00FA) bv = 0x00FA;
    AREA_B_POP((uint8_t)CUR_AREA_X, (uint8_t)CUR_AREA_Y) = bv;

    /* R cap. */
    uint16_t rv = AREA_R_POP_LIVE & 0x03FF;
    if (rv > 0x00FA) rv = 0x00FA;
    AREA_R_POP((uint8_t)CUR_AREA_X, (uint8_t)CUR_AREA_Y) = rv;
}

/* Compute the signed "B - R" balance from a cell's 4 cardinal neighbors.
 * Each neighbor that has B > 0 adds 3; each that has R > 0 subtracts 3.
 * Verbatim from $03:F2D9 — note the 2 zero-delta entries at the end of
 * the 6-entry table act as no-op extras (same cell counted, but pop is
 * the cell's own, so the balance is incremented once for self+B-pop and
 * decremented once for self+R-pop, cancelling for STRIPED cells).
 *
 * Lifted tables:
 *     $03:F340 X-deltas = { 0, +1,  0, -1, 0, 0 }
 *     $03:F34C Y-deltas = { -1, 0, +1,  0, 0, 0 }
 */
int16_t neighbour_balance_F2D9(uint8_t cx, uint8_t cy)
{
    static const int8_t dx[6] = {  0, +1,  0, -1, 0, 0 };
    static const int8_t dy[6] = { -1,  0, +1,  0, 0, 0 };
    int16_t bal = 0;
    for (int i = 0; i < 6; ++i) {
        int nx = (int)cx + dx[i];
        int ny = (int)cy + dy[i];
        if (nx < 0 || ny < 0 || nx >= 7 || ny >= 7) continue;
        if (AREA_B_POP((uint8_t)nx, (uint8_t)ny) != 0) bal += 3;
        if (AREA_R_POP((uint8_t)nx, (uint8_t)ny) != 0) bal -= 3;
    }
    return bal;
}

/* Split one ant from (cx, cy) into a random adjacent cell.
 * color = 0 -> B, color = 1 -> R.
 * Lifted from $03:F358:
 *
 *     LDA color -> $F6A3
 *     STX cx -> $F69F
 *     STY cy -> $F6A1
 *     LDA #$0002 ; JSL $02F38D -> A in {-1, 0, +1}
 *     ADC $F69F   ; nx = cx + delta
 *     if (nx < 0) nx = 0; if (nx > 6) nx = 6
 *     ; same for ny
 *     if ((nx, ny) == (cx, cy)) return     ; "no-op" split
 *     compute offset; if color=R: INC AREA_R_POP; else INC AREA_B_POP
 *     (also INC $E78E / $E790 if neighbor was previously 0 — new-area flag)
 * ======================================================================== */
extern int16_t rand_signed_F38D(uint8_t bound);   /* $02:F38D, returns -bound..+bound */

void area_split_to_neighbour_F358(uint8_t color, uint8_t cx, uint8_t cy)
{
    int16_t dx = rand_signed_F38D(2);
    int nx = (int)cx + dx;
    if (nx < 0) nx = 0;
    else if (nx > 6) nx = 6;
    /* The ROM has an interesting quirk: it tests CMP #$06 then BEQ skip
     * BCC skip ELSE clamp — meaning a result of EXACTLY 6 is allowed
     * unchanged (BEQ skips clamp), >6 gets clamped to 6, <0 gets clamped
     * to 0. Our `> 6` check captures the >6 case; the BEQ-on-6 is implicit
     * in our `<= 6` flow. */

    int16_t dy_ = rand_signed_F38D(2);
    int ny = (int)cy + dy_;
    if (ny < 0) ny = 0;
    else if (ny > 6) ny = 6;

    /* "No-op" split — random walked back to the source. */
    if ((uint8_t)nx == cx && (uint8_t)ny == cy) return;

    if (color != 0) {
        /* R colony. */
        if (AREA_R_POP((uint8_t)nx, (uint8_t)ny) == 0)
            WMEM16(0xE790)++;                            /* new R area count */
        AREA_R_POP((uint8_t)nx, (uint8_t)ny)++;
    } else {
        /* B colony. */
        if (AREA_B_POP((uint8_t)nx, (uint8_t)ny) == 0)
            WMEM16(0xE78E)++;                            /* new B area count */
        AREA_B_POP((uint8_t)nx, (uint8_t)ny)++;
    }
}


/* ========================================================================
 *  5. AREA-GRID SCAN  ($03:F02A..$F1F2)
 *  ------------------------------------------------------------------------
 *  The /32-tick grid scan that drives both the area-state map update
 *  and the "is the player winning?" presence count.
 *
 *  Pseudocode (verbatim from disassembly):
 *      if (E878 & 0x1F != 0) return;
 *      EE60 = 1;
 *      sub_03EFA2();             ; (per-area food redistribution)
 *      EB5E = 0;                 ; B presence count reset
 *      EB5C = 0;                 ; R presence count reset
 *      E73C = 0;                 ; reset "danger split" counter
 *      if (AREA_B_POP_LIVE > 0) EB5E++;
 *      if (AREA_R_POP_LIVE > 0) EB5C++;
 *      mass_exodus_cap_and_presence_F050();   ; per current area
 *      for (X=0..6) {
 *          for (Y=0..6) {
 *              if ((X,Y) == (CUR_AREA_X, CUR_AREA_Y)) continue;
 *              b = AREA_B_POP(X, Y); r = AREA_R_POP(X, Y);
 *              if (b == 0 && r == 0) continue;
 *              if (Full Game AND X < 2)            INC $E73C; ; left-edge wins
 *              if (Full Game AND X == 3 AND Y < 5) INC $E73C; ; "yard" wins
 *              balance = neighbour_balance_F2D9(X, Y);
 *              if (b != 0) {
 *                  EB5E++;
 *                  b += balance;
 *                  if (b <= 0) {
 *                      ; colony at (X,Y) died this tick
 *                      INC E794 (death-count); STZ AREA_B_POP(X,Y);
 *                  } else if (b >= 250) {
 *                      AREA_B_POP(X,Y) = 250;
 *                      if (rand10() == 0) area_split_to_neighbour(0, X, Y);
 *                  } else {
 *                      AREA_B_POP(X,Y) = b;
 *                  }
 *              }
 *              if (r != 0) {
 *                  EB5C++;
 *                  r -= balance;       ; sign-flipped (R loses against B)
 *                  if (r <= 0) {
 *                      INC E792; STZ AREA_R_POP(X,Y);
 *                      if ((X,Y) == (6,6)) {
 *                          ; "queen escapes to a random other cell"
 *                          rx = rand(5) + 2; AREA_R_POP(rx, 0) = 20;
 *                      }
 *                  } else if (r >= 250) {
 *                      AREA_R_POP(X,Y) = 250;
 *                      if (((joypad_shadow >> 1) & 7) == 0)
 *                          area_split_to_neighbour(1, X, Y);
 *                  } else {
 *                      AREA_R_POP(X,Y) = r;
 *                  }
 *              }
 *          }
 *      }
 *      ; spread queued surplus
 *      if (game_loaded) {
 *          while (B_SURPLUS > 0) {
 *              if (random < timer) area_split_to_neighbour(0, ...);
 *              dec B_SURPLUS;
 *          }
 *      }
 *      while (R_SURPLUS > 0) { ... same for R ... }
 *
 *  Notice: B and R use slightly different random-trigger thresholds
 *  for the cap-overflow split. B uses `rand(10) == 0` (10%); R uses
 *  the joypad shadow byte at $000100 (bit 1..3 == 0, so ~12.5%).
 * ======================================================================== */
extern void per_area_food_redistribute_EFA2(void);    /* $03:EFA2 */

/* WIKI: wiki/10-territory-49areas.md §5 + §7 (per-32-tick walk; game-end). */
void area_grid_scan_F02A(void)
{
    /* Reset presence counts. */
    AREA_B_PRESENCE = 0;
    AREA_R_PRESENCE = 0;
    WMEM16(0xE73C) = 0;

    /* Current-area presence (the only cell never visited by the loop). */
    if (AREA_B_POP_LIVE > 0) AREA_B_PRESENCE++;
    if (AREA_R_POP_LIVE > 0) AREA_R_PRESENCE++;

    /* Cap current area + propagate live counts. */
    mass_exodus_cap_and_presence_F050();

    /* Walk the 7x7 grid. */
    for (uint8_t x = 0; x < 7; ++x) {
        for (uint8_t y = 0; y < 7; ++y) {
            /* Skip the current area — already handled by F050. */
            if (x == (uint8_t)CUR_AREA_X && y == (uint8_t)CUR_AREA_Y) continue;

            uint16_t b = AREA_B_POP(x, y);
            uint16_t r = AREA_R_POP(x, y);
            if (b == 0 && r == 0) continue;

            int16_t bal = neighbour_balance_F2D9(x, y);

            /* B colony at this cell. */
            if (b != 0) {
                AREA_B_PRESENCE++;
                int32_t nb = (int32_t)b + bal;
                if (nb <= 0) {
                    WMEM16(0xE794)++;
                    AREA_B_POP(x, y) = 0;
                } else if (nb >= 250) {
                    AREA_B_POP(x, y) = 250;
                    if (rand_modulo_F3BD(10) == 0)
                        area_split_to_neighbour_F358(0, x, y);
                } else {
                    AREA_B_POP(x, y) = (uint16_t)nb;
                }
            }

            /* R colony at this cell (sign-flipped balance). */
            if (r != 0) {
                AREA_R_PRESENCE++;
                int32_t nr = (int32_t)r - bal;
                if (nr <= 0) {
                    WMEM16(0xE792)++;
                    AREA_R_POP(x, y) = 0;
                    /* Queen escapes from corner (6, 6). ROM at $03:F1A6:
                     *   LDA #$0004; JSL $02F3BD; ADC #$0002 -> X in 2..5.
                     * (An earlier draft passed bound=5, giving 2..6 instead
                     * of the ROM's 2..5.) */
                    if (x == 6 && y == 6) {
                        uint8_t rx = (uint8_t)(rand_modulo_F3BD(4) + 2);
                        AREA_R_POP(rx, 0) = 20;
                    }
                } else if (nr >= 250) {
                    AREA_R_POP(x, y) = 250;
                    /* R uses the joypad shadow as RNG (bits 1..3). */
                    if (((WMEM8(0x100) >> 1) & 7) == 0)
                        area_split_to_neighbour_F358(1, x, y);
                } else {
                    AREA_R_POP(x, y) = (uint16_t)nr;
                }
            }
        }
    }

    /* Surplus dispersal (queued from prior over-cap events). The lifted
     * loop at $03:F1F3 doesn't appear in this skeleton — it just calls
     * area_split_to_neighbour_F358 once per surplus while a per-game
     * countdown ($E8F8) is positive. The exact gating is in the disasm
     * around $03:F1FB..$F221. */
}


/* ========================================================================
 *  6. HOUSE SCREEN "CLICK TO MOVE" GATE
 *  ------------------------------------------------------------------------
 *  Manual p.19: "Remember: you can only move to sections that already
 *  have a black colony." String "You can only enter areas with black
 *  ant colonies." lives at $01:A550 and is reached via the per-state
 *  message dispatch at $00:DFCD.
 *
 *  Message-code 18..24 (decimal) — i.e. dp[$72] & 0x1F in 0x12..0x18 —
 *  all map to that string. The dispatcher at $00:E000:
 *
 *      LDA $72 ; AND #$1F ; STA $72
 *      TAY ; LDA $E066,Y ; JSL $008EA3  ; play SFX
 *      LDA $72 ; ASL ; TAX
 *      LDY $E026,X      ; pointer to text
 *      LDX #$0608       ; popup window position
 *      LDA #$12         ; popup window width
 *      JSR $C91F        ; show text popup
 *      JSR $8804        ; ??
 *      RTS
 *
 *  So the "click and got rejected" message is fired by setting dp[$72]
 *  to an 18..24 value before calling $00:DFCD. The setter is at
 *  $00:D480..$D4CD: the cursor's idle handler walks dp[$45..$50] (a
 *  scratch array of per-area messages) and triggers the first valid
 *  message (high bit clear).
 *
 *  The "is there a black colony here?" gate itself is NOT a lifted
 *  function — the SNES port simplifies the manual's behavior because
 *  the simulation always lives in ONE area at a time (CUR_AREA_X /
 *  CUR_AREA_Y are constants set at game init by $03:8507 and never
 *  modified by any other code). So the "click to move" mechanic
 *  reduces to a UI overlay: if the clicked area's per-live-area
 *  STATE byte is in the "has B" set, the click triggers the area
 *  transition (which is mostly a renderer-level effect — no live
 *  simulation state migrates between areas).
 *
 *  The proper SNES-port test for "is this area a valid move target?":
 *      area_state = LIVE_AREA_STATE[clicked_idx];
 *      valid = area_state_has_B(area_state);
 *
 *  And the message-code-set-on-fail is dp[$72] = 18 (an arbitrary
 *  pick within the 18..24 range that maps to the same string).
 * ======================================================================== */
/* WIKI: wiki/10-territory-49areas.md §6 + wiki/11-house-screen-ui.md §6
 * ("Click-to-Move Gate"). */
int house_can_move_to_clicked(uint8_t clicked_live_idx)
{
    if (clicked_live_idx >= 12) return 0;
    return area_state_has_B(LIVE_AREA_STATE(clicked_live_idx));
}

void house_click_reject_with_msg(void)
{
    extern void msg_dispatch_DFCD(uint8_t msg);
    /* msg-code 18 == "You can only enter areas with black ant
     * colonies." (table index in $00:E026, see entries [18..24]). */
    msg_dispatch_DFCD(18);
}


/* ========================================================================
 *  7. AREA TRANSITION — WHAT CHANGES WHEN PLAYER MOVES TO A NEW AREA
 *  ------------------------------------------------------------------------
 *  In the SNES port: NOT MUCH. Specifically:
 *
 *  (a) CUR_AREA_X ($E736) / CUR_AREA_Y ($E738) are NEVER written by
 *      lifted code (verified by exhaustive scan: only LDA/LDX/LDY/CMP
 *      access; no STA/STZ/INC/DEC). They're set to (3, 3) at $03:8507
 *      ("place B/R colony seed in area $20" = 4,4 in the 8x8 padded
 *      grid, which is (3, 3) in the 7x7 valid range) and stay there.
 *
 *  (b) The per-LIVE-area display state (LIVE_AREA_X / Y / STATE) IS
 *      updated by $03:96B0 (the "append" routine) when the game wants
 *      to add a new area to the House Screen visualization. AREA_TARGET
 *      ($F0D3/F0D5) are also never written by any lifted code — likely
 *      the new-area X/Y are seeded by a state-machine handler that
 *      lives in unlifted territory (probably bank $15 or $13, given
 *      the per-state references found).
 *
 *  (c) The actual SIMULATION area never changes — all per-tick AI runs
 *      against the single set of entities living in the current area.
 *      The 49-area map IS read by area_grid_scan_F02A but only to
 *      compute presence counts and to update the rendered map; it
 *      doesn't drive entity spawning.
 *
 *  This is consistent with the SNES port being a SIMPLIFIED FULL GAME:
 *  the manual describes 49 simultaneous simulations with the player
 *  free to switch between them, but the SNES (with its limited RAM
 *  and CPU) reduces that to ONE active simulation plus a per-area
 *  abstract presence map.
 *
 *  What DOES change visually on a "transition":
 *      - LIVE_AREA_COUNT increments (capped at 5)
 *      - LIVE_AREA_X / Y / FLAGS_A / FLAGS_B / STATE for the new entry
 *        all get freshly initialised:
 *           AREA_X_TABLE[i] = new_X (read from $F0D3)
 *           AREA_Y_TABLE[i] = new_Y (read from $F0D5)
 *           FLAGS_A[i] = 0
 *           FLAGS_B[i] = 0
 *           STATE[i]   = AREA_STATE_EMPTY (0)
 *      - The previous "last area" index ($E880) advances.
 *      - The map-state byte for the new (x,y) is randomly set to one
 *        of the AREA_STATE_BLACK / AREA_STATE_RED states based on
 *        scenario logic in $03:9880+
 *
 *  Per-area STATE byte progression after transition:
 *      0 (EMPTY) -> 1 (BLACK) on $03:9880 init      -> $03:988B STA E8B2,x
 *      1 -> 2 -> 3 -> 4 on each $03:9930 tick (when < 4)
 *      4 (or beyond): if FLAGS_B byte's bit 0 set, randomize 3..6 via
 *         `LDA #$04 ; JSL F3BD ; ADC #$03` -> new state 3..6
 *      6 -> rollover handled by the rand redraw
 *
 *  In short: the LIVE_AREA_STATE byte cycles, and the renderer at
 *  $04:BD9B uses it to pick which tile (BLACK / RED / STRIPED variant)
 *  to draw for that area on the 7x7 House Screen. The "current" area
 *  is drawn with an extra cursor sprite by the second compositing pass
 *  inside $04:BDD4..$BE34 (the four extra tile draws at +/-16 px
 *  around the area center).
 * ======================================================================== */
extern uint8_t rom_F0D3_new_X(void);
extern uint8_t rom_F0D5_new_Y(void);

/* The lifted version of $03:96B0 — the area-append. Re-lifted here so
 * this file is self-contained, but it's identical to ui_menus.c's
 * house_area_append_039_6B0 (kept in sync). */
void area_transition_append_96B0(void)
{
    uint8_t i = LIVE_AREA_COUNT;
    if (i >= 5) return;                 /* live cap is 5 visible areas */
    LIVE_AREA_X(i)        = rom_F0D3_new_X();
    LIVE_AREA_Y(i)        = rom_F0D5_new_Y();
    LIVE_AREA_FLAGS_A(i)  = 0;
    LIVE_AREA_FLAGS_B(i)  = 0;
    LIVE_AREA_STATE(i)    = AREA_STATE_EMPTY;
    LIVE_AREA_COUNT       = i + 1;
}


/* ========================================================================
 *  8. AREA STATE ADVANCE  ($03:9930..$9967)
 *  ------------------------------------------------------------------------
 *  Called periodically (probably from the per-LIVE-area visit tick to
 *  animate the House Screen sprite cycling). For a given live-area
 *  index x (already ASL'd):
 *
 *      LDA E8B2,x          ; current state byte
 *      CMP #$0004
 *      BCS roll            ; if >= 4, roll into random redraw
 *      INC E8B2,x          ; otherwise, advance state 0->1, 1->2, etc.
 *      BRA done
 *  roll:
 *      LDA E8A6,x          ; FLAGS_B (countdown)
 *      BEQ reseed          ; if 0 (or negative), force "STRIPED" reseed
 *      BMI reseed
 *      DEC E8A6,x          ; otherwise tick the countdown down
 *      LDA E8A6,x ; AND #$01 ; BEQ done
 *      LDA #$04 ; JSL F3BD ; ADC #$03    ; A in 3..6
 *      STA E8B2,x          ; reseed state
 *      BRA done
 *  reseed:
 *      LDA #$02 ; STA E89A,x             ; FLAGS_A = 2 (some redraw flag)
 *      LDA #$04 ; STA E8B2,x             ; state = 4 (RED_ALT)
 *  done:
 *      ...
 *
 *  This produces the animated tile cycling visible on the House Screen
 *  (manual p.30) where established areas slowly oscillate between
 *  BLACK and STRIPED states to indicate "this area is busy."
 * ======================================================================== */
void area_state_advance_9930(uint8_t live_idx_word)
{
    /* live_idx_word is the ASL'd index (2 bytes per slot). */
    uint8_t idx = live_idx_word;
    uint8_t state = LIVE_AREA_STATE(idx);
    if (state < 4) {
        LIVE_AREA_STATE(idx) = state + 1;
        return;
    }
    uint8_t fb = LIVE_AREA_FLAGS_B(idx);
    if (fb == 0 || (int8_t)fb < 0) {
        /* Reseed: force back to RED_ALT state with new redraw flag. */
        LIVE_AREA_FLAGS_A(idx) = 2;
        LIVE_AREA_STATE(idx)   = AREA_STATE_RED_ALT;
        return;
    }
    LIVE_AREA_FLAGS_B(idx) = fb - 1;
    if ((LIVE_AREA_FLAGS_B(idx) & 0x01) == 0) return;
    /* Generate new state in 3..6. */
    uint16_t r = rand_modulo_F3BD(4);
    LIVE_AREA_STATE(idx) = (uint8_t)((r + 3) & 0xFF);
}


/* ========================================================================
 *  9. PER-AREA "VISIT TICK" $02:9D96 NOTES
 *  ------------------------------------------------------------------------
 *  Called every sim tick by simulation.c::sim_tick. Builds the displayed
 *  per-area-message scratch in $EE64..$EE68. The relevant bits:
 *
 *      - At entry, zeros $EE64 ("there is a problem here" message flag)
 *      - If dp[$4A] == 1 (the "evaluating ant" mode), generates a
 *        random pair of (worker?, soldier?) targets in $EE66/$EE68
 *        based on the live ant counts at dp[$46]/dp[$48]
 *      - Calls marriage_flight_trigger_9E35 at offset 02:9E1D and
 *        captures the result at $EE34 (the "what happened" flag)
 *      - Calls a behavior-classifier at $02:A0F9 -> $EE32
 *      - Calls per_area_colony_score_9EEB (computes caste-share %)
 *      - Calls per_area_action_chooser_A033 (picks the active task)
 *
 *  This block is what gives the House Screen its per-area "info dot"
 *  display (the per-area messages that pop up when the player hovers
 *  over a section).
 * ======================================================================== */


/* ========================================================================
 * 10. AREA-BORDER WIN CHECK  ($03:F25B..$F2CE)
 *  ------------------------------------------------------------------------
 *  Called as the tail of area_grid_scan_F02A. Tests for game-over:
 *
 *      if (EB5C == 0 AND $E902 != 0 AND EB4E != 0)
 *          ; "no R colonies AND someone clicked AND scenario active"
 *          ; -> no-op for Full Game mode
 *
 *      if (EB4E != 0 AND EB5C == 0)
 *          ; scenario victory: store result, set bit dp[$A7] |= $08,
 *          ;   halt sim via dp[$E1] = 2.
 *          ED = EB5E ; EF = EB5C
 *          dp[$A7] |= 0x08
 *          dp[$E1] = 2
 *
 *      else if (EB5E < 2 AND $E7A0 == 0 AND $E796/$E79C/$E79E all == 0)
 *          ; FULL-GAME defeat: B has presence in fewer than 2 areas AND
 *          ; no live entities of any caste -> the player's colony died.
 *          ED = EB5E ; EF = EB5C
 *          dp[$A7] |= 0x10
 *          dp[$E1] = 2
 *
 *  Bit $08 in dp[$A7] is "victory"; bit $10 is "defeat". The state-1A
 *  level-completion handler reads these on next render-cycle.
 *
 *  This pairs with the player-only-can-move-to-B gate above: it's the
 *  only mechanism by which the Full-Game world ENDS. Without a manual
 *  "EXIT" action, the player just plays until B is squeezed off the
 *  map by R's expansion.
 * ======================================================================== */


/* ========================================================================
 * 11. MISC HELPERS — addresses for grep
 *  ------------------------------------------------------------------------
 *  These don't need bodies here; documented for reference.
 *
 *      $02:F3BD  rand_modulo_F3BD(bound) -> A in [0, bound-1]
 *      $02:F38D  rand_signed_F38D(bound) -> A signed, |A| < bound
 *      $02:F5B2  area_offset_F5B2 -> X (= ((Y<<3)+X)<<1)
 *      $02:F65A  queue_event_F65A(id) -> ring buffer at $000FE0
 *      $00:DFCD  msg_dispatch_DFCD(id) -> shows popup text + SFX
 *      $00:E026  area_msg_pointer_table[32] -> 16-bit text pointers
 *      $00:E066  area_msg_sfx_table[32] -> per-msg SFX byte
 *      $04:BE41  area_state_tile_table[8] = {42,48,4A,48,4A,4C,4E,48}
 *      $03:9880  area_state_seed_BLACK   (sets STATE = 1)
 *      $03:9930  area_state_advance      (this file)
 *      $03:9959  area_state_random_redraw  (3..6)
 *      $03:9967  area_state_set_RED_init (sets STATE = 4)
 *      $03:96B0  area_transition_append  (this file)
 *      $03:F02A  area_grid_scan          (this file)
 *      $03:F050  mass_exodus_cap         (this file)
 *      $03:F1F3  area_surplus_disperse
 *      $03:F25B  area_border_win_check
 *      $03:F358  area_split_to_neighbour (this file)
 *      $03:F2D9  neighbour_balance_count (this file)
 * ======================================================================== */

/* Touch all file-scope routines so -Wunused-function stays quiet. */
__attribute__((used))
static void const * const _territory_doc_refs[] = {
    (void const *)pct100,
    (void const *)area_b_occupied_count,
    (void const *)area_r_occupied_count,
    (void const *)status_screen_compute_territory,
    (void const *)area_state_has_B,
    (void const *)area_state_has_R,
    (void const *)territory_marriage_flight_trigger_9E35,
    (void const *)mass_exodus_cap_and_presence_F050,
    (void const *)neighbour_balance_F2D9,
    (void const *)area_split_to_neighbour_F358,
    (void const *)area_grid_scan_F02A,
    (void const *)house_can_move_to_clicked,
    (void const *)house_click_reject_with_msg,
    (void const *)area_transition_append_96B0,
    (void const *)area_state_advance_9930,
    (void const *)area_state_tile_BE41,
};

/* End of territory.c —
 * Investigates manual p.18-20, 29-32 mechanics:
 *   - 6 Status Screen percentage formulas (with food-home Foraging % fix)
 *   - 7-value area-state byte encoding (with corrected enum vs Agent K's)
 *   - Marriage Flight trigger (PLAY_MODE/POP/BREEDER/COOLDOWN check)
 *   - Mass Exodus cap+presence (per-tick at $F050)
 *   - Area-split to neighbour (rand walk +/-1 each axis, clamped 0..6)
 *   - 7x7 grid scan with B/R balance + cap + cell-death + queen escape
 *   - House Screen click-to-move gate ("must have B colony" msg-code 18)
 *   - Area transition / live-area append (mostly cosmetic in SNES port)
 * Every routine cross-referenced to its ROM address for grep.
 */

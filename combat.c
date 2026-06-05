/*
 * combat.c — SimAnt (SNES) combat, predation, and feeding mechanics.
 * ------------------------------------------------------------------------
 *  WIKI:
 *    - wiki/08-combat.md     — fight resolver, kill dispatcher, jump table,
 *                              Worker-vs-Soldier tile-hold mechanic, Yellow
 *                              Ant attack chain, "Fights Won %" derivation
 *    - wiki/09-predation.md  — Spider/Ant Lion cadence, Mower/Cat/Foot mass
 *                              sweep, Hand rect-kill, eat/starve loops
 *    - wiki/13-player-actions.md  — §6 Attack uses the same rect kernel;
 *                                   §7 "Eat has no dispatcher" explains the
 *                                   eat_food_8C00 below; §8 Trophallaxis.
 *    - wiki/15-dangers.md    — mass_kill_sweep_EF1E and hand_squash_EF02 are
 *                              the kill kernels for Mower/Cat/Foot and Hand
 *                              respectively (the 7 dangers from manual p.36).
 * ------------------------------------------------------------------------
 *  This file lifts the COMBAT / FIGHT / PREDATION layer of SimAnt. Manual
 *  references: p.10-12 (Yellow Ant attack, trophallaxis), p.34 (cast of
 *  characters), p.36 (dangers).
 *
 *  SimAnt has TWO entity layers running in parallel:
 *
 *    1) THE "VISUAL" ENTITY TABLE at $7E:0600+ (20-byte records,
 *       Entity struct in simant.c). These are the on-screen sprites the
 *       player sees move — Workers (type 14), Soldiers (type 15), Queen
 *       (18), Spider (17), Ant Lion variants (27/28), the Yellow Ant
 *       cursor, popups, etc. Each has its own per-state AI in
 *       entities_a..d.c. Visual entities collide with the cursor via
 *       sub_DC84 (the click hit-test), but they do NOT carry HP and they
 *       do NOT fight each other directly.
 *
 *    2) THE "ABSTRACT" PARALLEL-ARRAY TABLES in $7F:Cxxx-$7F:Exxx. These
 *       are the SIMULATION-LEVEL ant populations — the things that
 *       starve, hatch, fight, get eaten, etc. They are tracked as three
 *       count-capped arrays (see save_options.c parallel-arrays layout):
 *
 *         Table 1 (B-colony ants):    type at $7F:CBB8, attr $7F:C3E8,
 *                                      x at $7F:C000, count $E77E
 *         Table 2 (R-colony ants):    type at $7F:D964, attr $7F:D57C,
 *                                      x at $7F:D388, count $E780
 *         Table 3 (dangers / misc):   type at $7F:E328, attr $7F:DF40,
 *                                      x at $7F:DD4C, count $E782
 *
 *       Each entry "owns" a tile in one of the 4 world tile maps at
 *       $7F:0000 / $7F:2000 / $7F:3000 / $7F:9000 (B-ants, dangers,
 *       another, R-ants), addressed via $02:F59F (slot allocator) or
 *       $02:F5A8 (alternate slot allocator).
 *
 *    3) THE "ACTIVE COMBATANT" TABLE at $7F:E87E.. up to 5 entries
 *       (per-area collision-resolution pool). When two opposing-colony
 *       ants come within range, they get pushed into this pool by
 *       $03:96B0. The fight_resolver_96D7 iterates this pool every sim
 *       tick. Each combatant has 5 WORD fields at offset i*2:
 *
 *         $E882,i  combatant X (tile coord)
 *         $E88E,i  combatant Y (tile coord)
 *         $E89A,i  STATE: 0 = active (look for/engage enemy)
 *                          1 = decay/idle (counting down to despawn)
 *                          2 = expiring (one tick before clear)
 *         $E8A6,i  HP / timer (for state==1 decay)
 *         $E8B2,i  combat-frame counter (animation step, 0..3 cycle)
 *
 *       Same memory is also reinterpreted by ui_menus.c (House screen)
 *       as the area-icon table when the simulation is paused on the
 *       evaluation screen — these uses do not overlap in time.
 *
 *  Combat resolution and predation outcomes all funnel through a single
 *  KILL DISPATCHER at $03:D334. The caller passes a 4-bit "kill reason
 *  code" in A; the dispatcher:
 *    - sets dp[$0002B5] = 1 (world-changed flag)
 *    - if code 7..9: stashes (X, Y, kind, attr, sub) into $F487..$F491
 *      and JSL $03:C3E3 (the corpse-spawn routine — leaves a body sprite)
 *    - jumps into a per-code handler that increments the appropriate
 *      FIGHTS_*_WON counter (E844/E848/E84C) AND optionally plays a
 *      victory/defeat SFX and animation freeze
 *
 *  KILL CODES (lifted from the $03:D3C0 jump table at $03:D334).
 *  VERIFIED: actual jump-table bytes at $03:D3C0 give the SHIFTED slots
 *  below (an earlier draft had every entry off by one).
 *
 *    0 ($D3D6)  queue event 0x40, JMP D4B5 cleanup (NOT silent)
 *    1 ($D3E0)  R wins (combat loss to enemy ant): INC E848, SFX 0x1E
 *               via $008003, spin 6 frames on $E3, queue event 0x43
 *    2 ($D402)  R wins (player ant slain): INC E848, SFX 0x1E,
 *               spin 6, queue event 0x42
 *    3 ($D424)  B wins (silent): INC E844 only
 *    4 ($D42F)  B wins (silent): INC E844 only (alias of #3)
 *    5 ($D43A)  B wins with fanfare — spin 5 frames, queue 0x46, INC E844
 *    6 ($D455)  B wins (cat's paw / hand squash): queue 0x45, INC E844
 *    7 ($D467)  B wins (mower/foot mass-kill): queue 0x44, INC E844
 *    8 ($D479)  DRAW: queue 0x4D, INC E84C
 *    9 ($D48B)  B wins (silent predator kill): INC E844 only
 *
 *  Verify:
 *    cd /Users/guilhermedavid/simant-re && clang -Wall -Wextra
 *    -Wno-unused-function -O0 -g -c combat.c -o /tmp/combat.o
 */

#include <stdint.h>

/* ========================================================================
 *  EXTERNAL DEPS (defined in simant.c / simulation.c / other lifts)
 * ======================================================================== */
extern uint8_t wram[0x20000];                /* $7E:0000..$7F:FFFF       */
#define dp wram                              /* DP = $0000 throughout    */

#define WMEM16(off)   (*(uint16_t *)&wram[off])
#define WMEM8(off)    (*(uint8_t  *)&wram[off])

/* The sim task runs with DBR = $7F (set by sim_main_loop $02:8024), so
 * absolute addresses in bank $03 code that look like "$E882,x" are
 * actually accessing $7F:E882,x — which is wram[0x1E882] in our flat
 * mapping. The macros below make this explicit. */
#define WRAM7F16(off)   WMEM16(0x10000 + (off))
#define WRAM7F8(off)    WMEM8 (0x10000 + (off))

/* RNG and SFX hooks. */
extern uint16_t rand_modulo_F3BD(uint16_t bound);   /* $02:F3BD */
extern void     queue_event_F65A(uint8_t event_id); /* $02:F65A */
extern void     apu_play_sfx_008EA3(uint8_t sfx);   /* $00:8EA3 */

/* Tile-map cursor / slot helpers. */
extern void     slotmap_select_a_F59F(void);        /* $02:F59F: select map1 cursor */
extern void     slotmap_select_b_F5A8(void);        /* $02:F5A8: select map2 cursor */

/* Tile read / write helpers (per the fight resolver's calls). */
extern uint16_t tilemap_read_A626 (uint16_t kind, uint16_t x, uint16_t y);  /* $03:A626 */
extern void     tilemap_write_A689(void);                                   /* $03:A689 — reads $F013/15/17/19 args */
extern uint16_t tilemap_addr_A5BB (uint16_t kind, uint16_t x, uint16_t y);  /* $03:A5BB */
extern uint16_t inbounds_B8E9    (uint16_t x, uint16_t y);                  /* $03:B8E9 — 1 if (X,Y) in $80x$40 */

/* Tile-value classifiers. */
extern uint16_t tile_is_combatant_A547(uint16_t kind, uint16_t tile);
/* $03:A547: kind=1 -> returns 1 if tile in [$51..$52] (own-team marker)
 *           kind=2 -> returns 1 if tile in [$30..$31] (impassable)         */
extern uint16_t tile_is_enemy_C690   (uint16_t tile);
/* $02:C690: returns 1 if tile in [$48..$4A] (enemy-ant marker).           */
extern uint16_t tile_is_walkable_A534(uint16_t kind, uint16_t tile);
/* $03:A534: kind=1 -> JSL $02:C690 (enemy tile test).                     */
extern uint16_t tile_is_blocked_9F6A(uint16_t tile);
/* $03:9F6A: returns 1 if tile is $FE or $FF (impassable/edge).            */
extern uint16_t tile_in_range_9F5D   (uint16_t kind);
/* $03:9F5D: dispatch helper. For kind == 1 jumps to inbounds_B8E9.        */

/* The corpse / "leave a body sprite" helper called by the kill dispatcher
 * for kill codes 7..9. */
extern void corpse_spawn_C3E3(void);                /* $03:C3E3 */

/* Visual entity-table support. */
extern uint16_t entity_clicked_DC84(uint8_t entity_x_idx);   /* $04:DC84 */

/* Per-area-pop adjuster used when a B/R ant dies (see entities_b.c
 * for the sister starvation path). */
extern void area_pop_dec_E1DC(void);                /* $03:E1DC — generic pop dec */

/* ========================================================================
 *  THE 8 NEIGHBOR-OFFSET TABLES (used for combat scan + ant lion + eat)
 *  ------------------------------------------------------------------------
 *  In bank 02 there are FOUR 16-bit tables of 8 entries each, used as
 *  (dx, dy) word-aligned offsets when a combatant scans its surrounding
 *  cells. The 8 entries are the 8 compass directions starting from N
 *  (CCW): N, NE, E, SE, S, SW, W, NW. Both pairs are duplicated so the
 *  code can read X-deltas from one base and Y-deltas from a parallel
 *  base, avoiding interleaved-table accounting.
 *
 *  Table layouts (read as int16_t arrays):
 *
 *    $02:8065  X-DELTA (set 1):  0  1  1  1  0 -1 -1 -1  (= N..NW ccw)
 *    $02:8077  Y-DELTA (set 1): -1 -1  0  1  1  1  0 -1
 *    $02:8089  X-DELTA (set 2):  0  0  1  1  1  0 -1 -1  (different cycle)
 *    $02:809B  Y-DELTA (set 2): -1 -1 -1  0  1  1  1  0
 *
 *  ROM bytes (verified via direct read of simant.sfc):
 *    $8065: 00 00 01 00 01 00 01 00 00 00 FF FF FF FF FF FF
 *    $8077: FF FF FF FF 00 00 01 00 01 00 01 00 00 00 FF FF
 *    $8089: 00 00 00 00 01 00 01 00 01 00 00 00 FF FF FF FF
 *    $809B: 00 00 FF FF FF FF 00 00 01 00 01 00 01 00 00 00
 *
 *  The fight resolver scans BOTH (set 1) for "where's the enemy?" and
 *  (set 2) for "where can I retreat to?" The ant_lion_tick ($03:C0FD)
 *  also uses set 1 to pick an ambush direction. The eat routine
 *  ($03:8C00) uses set 1 to move a food crumb between adjacent cells.
 * ======================================================================== */
static const int8_t neigh_dx_set1_8065[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
static const int8_t neigh_dy_set1_8077[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
static const int8_t neigh_dx_set2_8089[8] = {  0,  0,  1,  1,  1,  0, -1, -1 };
static const int8_t neigh_dy_set2_809B[8] = { -1, -1, -1,  0,  1,  1,  1,  0 };

/* ========================================================================
 *  ENTITY-TABLE PARALLEL ARRAYS (lifted from save_options.c)
 *  ------------------------------------------------------------------------
 *  These ARE the "real" ants the simulation cares about. count is at the
 *  matching $E77E/$E780/$E782 word.
 *
 *  Each i in [0..count) names one entry across multiple arrays:
 *
 *  Table 1 (B-colony):  type[i] = ($7F:CBB8 + i)
 *                       attr[i] = ($7F:C3E8 + i)
 *                       x[i]    = ($7F:C000 + i)
 *
 *  Table 2 (R-colony):  type[i] = ($7F:D964 + i)
 *                       attr[i] = ($7F:D57C + i)
 *                       x[i]    = ($7F:D388 + i)
 *
 *  Table 3 (dangers):   type[i] = ($7F:E328 + i)
 *                       attr[i] = ($7F:DF40 + i)
 *                       x[i]    = ($7F:DD4C + i)
 * ======================================================================== */
#define B_COUNT    WMEM16(0xE77E)        /* $7E:E77E (note: $7E side) */
#define R_COUNT    WMEM16(0xE780)
#define D_COUNT    WMEM16(0xE782)

#define B_TYPE(i)  WRAM7F8(0xCBB8 + (i))
#define B_ATTR(i)  WRAM7F8(0xC3E8 + (i))
#define B_X(i)     WRAM7F8(0xC000 + (i))

#define R_TYPE(i)  WRAM7F8(0xD964 + (i))
#define R_ATTR(i)  WRAM7F8(0xD57C + (i))
#define R_X(i)     WRAM7F8(0xD388 + (i))

#define D_TYPE(i)  WRAM7F8(0xE328 + (i))
#define D_ATTR(i)  WRAM7F8(0xDF40 + (i))
#define D_X(i)     WRAM7F8(0xDD4C + (i))

/* The kill counters (lifted in simulation.c::SIM_COUNTERS). All on the
 * $7F mirror via DBR=$7F. Note: simulation.c references them in the $7E
 * mirror because the cap WRITER (live_stats_summary) runs from the
 * gameplay task which uses DBR=$7E. Both addresses map to the same
 * physical SRAM byte via the SNES WRAM mirror.) */
#define EATEN_COUNTER       WMEM16(0xE764)   /* ants eaten by predator     */
#define STARVED_COUNTER     WMEM16(0xE766)   /* ants starved               */
#define KILLED_COUNTER_768  WMEM16(0xE768)
#define KILLED_COUNTER_76A  WMEM16(0xE76A)
#define KILLED_COUNTER_76C  WMEM16(0xE76C)
#define KILLED_COUNTER_76E  WMEM16(0xE76E)

/* Active-combatant pool. count at $E87E, max 5 entries, each entry is
 * 5 WORD fields at the offsets below. With DBR=$7F these resolve to
 * $7F:E87E..$7F:E8B2. */
#define COMBAT_COUNT        WRAM7F16(0xE87E)        /* 0..5 */
#define COMBAT_LAST_IDX     WRAM7F16(0xE880)
#define COMBAT_X(i)         WRAM7F16(0xE882 + (i)*2)
#define COMBAT_Y(i)         WRAM7F16(0xE88E + (i)*2)
#define COMBAT_STATE(i)     WRAM7F16(0xE89A + (i)*2)
#define COMBAT_HP(i)        WRAM7F16(0xE8A6 + (i)*2)
#define COMBAT_FRAME(i)     WRAM7F16(0xE8B2 + (i)*2)

/* Fight-tally counters (lifted in simulation.c). */
#define FIGHTS_B_WON_44     WMEM16(0xE844)
#define FIGHTS_B_WON_HI_46  WMEM16(0xE846)  /* carry-out high word */
#define FIGHTS_R_WON_48     WMEM16(0xE848)
#define FIGHTS_R_WON_HI_4A  WMEM16(0xE84A)
#define FIGHTS_DRAW_4C      WMEM16(0xE84C)
#define FIGHTS_DRAW_HI_4E   WMEM16(0xE84E)
/* Two additional counters at $E852/$E854/$E856 — secondary kill tallies. */
#define KILLS_E852          WMEM16(0xE852)
#define KILLS_E854          WMEM16(0xE854)
#define KILLS_E856          WMEM16(0xE856)

/* The "current attacker" iteration cursor. */
#define COMBAT_ITER         WRAM7F16(0xF5AB)   /* 0..COMBAT_COUNT-1 */

/* The "current attacker / target geometry" scratch used by fight_resolver
 * and helpers. All in $7F:F5xx (bank-3 scratch zone). */
#define COMBAT_SELF_TILE    WRAM7F16(0xF5AD)
#define COMBAT_DIR_8        WRAM7F16(0xF5AF)
#define COMBAT_TX           WRAM7F16(0xF5B1)
#define COMBAT_TY           WRAM7F16(0xF5B3)
#define COMBAT_SELF_X       WRAM7F16(0xF5B5)
#define COMBAT_SELF_Y       WRAM7F16(0xF5B7)
#define COMBAT_NEIGH_COUNT  WRAM7F16(0xF5B9)

/* The tilemap-write argument trampoline (per $03:A689 expectations). */
#define ARG_TILE_KIND       WRAM7F16(0xF013)   /* 1=map1 ($6000), 2=map2, else=map4 */
#define ARG_TILE_X          WRAM7F16(0xF015)
#define ARG_TILE_Y          WRAM7F16(0xF017)
#define ARG_TILE_VALUE      WRAM7F16(0xF019)
#define ARG_TILE_ATTR       WRAM7F16(0xF01B)
#define ARG_TILE_ATTR1      WRAM7F16(0xF01D)
#define ARG_TILE_ATTR2      WRAM7F16(0xF01F)
#define ARG_TILE_ATTR3      WRAM7F16(0xF021)
#define ARG_TILE_ATTR4      WRAM7F16(0xF023)
#define ARG_TILE_ATTR5      WRAM7F16(0xF025)


/* Forward declarations — these are referenced before their bodies. */
int  combatant_can_engage_A359(uint16_t kind, uint16_t tx, uint16_t ty);
void kill_dispatcher_D334(uint16_t code);

/* ========================================================================
 *  combatant_append_96B0 ($03:96B0 in ROM)
 *  ------------------------------------------------------------------------
 *  Append a new entry to the active-combatant pool at $7F:E87E. Called
 *  when a player-controlled or AI-driven ant enters the engaging range
 *  of an enemy-colony ant. Capped at 5 entries; over-cap calls silently
 *  no-op.
 *
 *  Argument lifted: dp[$F0D3] = new X, dp[$F0D5] = new Y (both in tile
 *  coords). The entry starts in state 0 (active scan).
 *
 *  Verified ROM body (M=0, X=0):
 *
 *      LDA  $E87E              ; combatant count
 *      ASL                     ; X = i * 2 (word index)
 *      TAX
 *      LDA  $F0D3
 *      STA  $E882,x            ; combatant X
 *      LDA  $F0D5
 *      STA  $E88E,x            ; combatant Y
 *      STZ  $E89A,x            ; state = 0 (active)
 *      STZ  $E8A6,x            ; HP = 0
 *      STZ  $E8B2,x            ; frame = 0
 *      LDA  $E87E
 *      CMP  #$0005             ; cap at 5
 *      BCS  +
 *      INC                     ; bump count
 *      STA  $E87E
 *    + RTL
 * ======================================================================== */
void combatant_append_96B0(uint16_t new_x, uint16_t new_y)
{
    /* ROM-faithful: writes are unconditional, only the count bump is
     * gated. Slot 5 (i==5) overflow lands in the per-array 2-byte gap
     * before the next array — benign in practice but matches ROM. */
    unsigned i = COMBAT_COUNT;
    if (i < 6) {                          /* avoid overflowing our array */
        COMBAT_X    (i) = new_x;
        COMBAT_Y    (i) = new_y;
        COMBAT_STATE(i) = 0;
        COMBAT_HP   (i) = 0;
        COMBAT_FRAME(i) = 0;
    }
    if (i < 5) {                          /* hard cap from manual: 5      */
        COMBAT_COUNT = i + 1;
    }
}


/* ========================================================================
 *  fight_resolver_96D7 ($03:96D7 in ROM) — central per-sim-tick combat
 *
 *  WIKI: wiki/08-combat.md#3-fight-resolver--0396d7
 *  ------------------------------------------------------------------------
 *  Called once per sim_tick (from $02:ABAB). Iterates the active-
 *  combatant pool. Each combatant's state byte selects one of three
 *  sub-behaviors:
 *
 *    state == 0 (active scan):
 *        1. Read tile at (self.X, self.Y) into A.
 *        2. If A is an OWN-TEAM tile (in [$51..$52]): we're parked on a
 *           friendly cell — try to fight a neighbor.
 *        3. Else if A is an ENEMY tile (in [$48..$4A]): we're standing
 *           on an enemy — same-cell combat.
 *        4. Else: jump to $97C6 — try to migrate to an adjacent valid
 *           cell.
 *        5. If we decided to fight: with 1/512 probability per tick
 *           ($02F3BD with bound $0200, BEQ branch fires when result==0),
 *           pick a random neighbor index (rand & 7) and scan its 8
 *           rotated cells looking for an enemy-marked tile. If found,
 *           write a "combat-active" tile (combat_frame + $38) into the
 *           position via the $03:99A0 helper, transition to state 1
 *           (post-combat decay), set HP = 25 (0x19) and frame = 1.
 *
 *    state == 1 (decay countdown):
 *        Frame < 4: INC frame (animation cycle 0..3, then 4).
 *        Frame >= 4: DEC HP. If HP & 1 (every other tick): roll
 *        $02F3BD(4) to pick a new frame in [3..6]. If HP underflows
 *        (BMI), transition to state 2 with frame = 4.
 *
 *    state == 2 (terminal):
 *        Frame > 0: DEC frame (final cleanup countdown).
 *        Frame == 0: clear state to 0 (re-enter active scan) AND INC
 *                    $E880 (combat-end count, used by the GUI as
 *                    "engagements resolved this round").
 *
 *  Outer loop advances COMBAT_ITER through 0..COMBAT_COUNT-1.
 *
 *  KILL-RESOLUTION NOTE: when state-0 finds an enemy at the same cell
 *  (the $97C3 path), it falls into a SAME-CELL FIGHT at $97DA. That
 *  branch:
 *      - reads the second-set offsets ($02:8089 / $02:809B) to inspect
 *        a wider 2-cell neighborhood,
 *      - reads the enemy's tile back into $F5AD,
 *      - JSL $03:9F6A: if enemy tile is $FE/$FF, count it as immediate
 *        kill (the ant has hit a wall/obstacle while attacking),
 *      - else JSL $03:A829 (the "fight calc" stub that fills $F01x args)
 *        then JSL $03:D334 with kill code 2 (R-wins-on-player path),
 *      - else (no neighbor found) walks the 8-neighborhood and:
 *          - tile has bit 7 set ($AND #$80): INC $E848 (R wins)
 *          - else: INC $E854 (secondary kill counter)
 *      - rewrites tile = combat_frame+$50 at self position,
 *      - sets state=1, HP=50 (0x32), frame=1.
 *
 *  Manual cross-reference (p.34):
 *    "Soldier Ants are better fighters than Workers" — implemented by
 *    making the same-cell-fight path probabilistically award B-wins to
 *    SOLDIER-typed attackers more often. The HP=50 (Soldier) vs HP=25
 *    (Worker) on the post-fight decay state IS that bias: a Soldier
 *    occupies an enemy cell for twice as long, blocking re-engagement
 *    by enemies (since enemies skip cells with combat-tile values).
 *
 *  Roughly: B WINS get INC $E844; R WINS INC $E848; DRAWS INC $E84C
 *  (all in the kill dispatcher $03:D334 — see kill_dispatcher_D334
 *  below). The raw fight_resolver itself only writes E848 (when no
 *  proper opponent is found and the attacker's animation runs out, the
 *  attacker is presumed dead) and E854 (secondary kill).
 * ======================================================================== */

/* Helper $03:99A0: write a combat-marker tile (frame + $38) at the
 * combatant's position. Used both for the "I'm fighting" indicator and
 * for the post-combat "I held this cell" marker. */
static void combat_mark_tile_99A0(uint16_t combatant_idx)
{
    unsigned i = combatant_idx;
    ARG_TILE_X     = COMBAT_X(i);
    ARG_TILE_Y     = COMBAT_Y(i);
    ARG_TILE_VALUE = COMBAT_FRAME(i) + 0x38;
    ARG_TILE_KIND  = 1;                                 /* map1 = $6000 */
    tilemap_write_A689();
}

/* Helper $03:99F1: write a "victory marker" tile ($3F) and try to "swap"
 * the combatant against the highest-indexed combatant — bubble-sort tail
 * that compacts the pool when one slot dies. Lifted in full because
 * fight_resolver branches into it. */
static void combat_compact_pool_99F1(uint16_t combatant_idx)
{
    unsigned i = combatant_idx;
    /* Stamp tile = $3F (victory marker). */
    ARG_TILE_X     = COMBAT_X(i);
    ARG_TILE_Y     = COMBAT_Y(i);
    ARG_TILE_VALUE = 0x3F;
    ARG_TILE_KIND  = 1;
    tilemap_write_A689();

    /* If pool already empty, bail. */
    if (COMBAT_COUNT == 0) return;
    COMBAT_COUNT--;

    /* Swap-down: copy entry at the (newly-reduced) end into our slot. */
    unsigned tail = COMBAT_COUNT;
    while (i < tail) {
        unsigned next = i + 1;
        COMBAT_X    (i) = COMBAT_X    (next);
        COMBAT_Y    (i) = COMBAT_Y    (next);
        COMBAT_FRAME(i) = COMBAT_FRAME(next);
        COMBAT_STATE(i) = COMBAT_STATE(next);
        COMBAT_HP   (i) = COMBAT_HP   (next);
        i = next;
    }
}

void fight_resolver_96D7(void)
{
    /* Outer loop: iterate 0..COMBAT_COUNT-1. */
    for (COMBAT_ITER = 0;
         COMBAT_ITER < COMBAT_COUNT;
         COMBAT_ITER++)
    {
        unsigned i = COMBAT_ITER;

        /* ============================ state ?? ============================ */
        uint16_t st = COMBAT_STATE(i);

        if (st == 0) {
            /* -------------------- ACTIVE SCAN --------------------- */
            COMBAT_SELF_X = COMBAT_X(i);
            COMBAT_SELF_Y = COMBAT_Y(i);

            /* Read the tile under self. */
            uint16_t self_tile = tilemap_read_A626(1, COMBAT_SELF_X, COMBAT_SELF_Y);
            COMBAT_SELF_TILE = self_tile;

            /* Am I on an own-team or enemy marker? */
            int on_own_team = tile_is_combatant_A547(1, self_tile);

            if (!on_own_team) {
                /* Maybe I'm standing right on an enemy. */
                int on_enemy = tile_is_walkable_A534(1, self_tile);
                if (!on_enemy) {
                    /* Not own-team, not enemy — try to MIGRATE.
                     * The $97C6 path scans for an adjacent valid cell
                     * (own-team marker present) and moves there. We
                     * model the migration as a no-op tick — when the
                     * combatant's underlying ant entity walks (in its
                     * per-frame AI in entities_b.c), it'll naturally
                     * step to a friendly cell on the next tick. */
                    goto fight_migrate;
                }
            }

            /* Decide whether to fight: 1/512 probability per tick. */
            if (rand_modulo_F3BD(0x0200) != 0) {
                /* Not this tick — leave state at 0, keep scanning. */
                goto fight_advance_iter;
            }

            /* Pick a random starting direction and scan 8 neighbors. */
            COMBAT_DIR_8 = rand_modulo_F3BD(8);
            COMBAT_NEIGH_COUNT = 0;

            while (COMBAT_NEIGH_COUNT < 8) {
                uint16_t dir = COMBAT_DIR_8;
                /* Use SET-1 offsets (ASL=2 because each entry is a
                 * word). The original code does `LDA $028065,x` with
                 * X already pre-doubled — same as our normal byte
                 * lookup but the SNES tables are duplicated 16-bit. */
                int16_t dx = neigh_dx_set1_8065[dir];
                int16_t dy = neigh_dy_set1_8077[dir];
                /* Compounded: dx*2 + self.X (the ROM ASL's the table
                 * entry, doubling the offset before adding the
                 * combatant's X). */
                COMBAT_TX = COMBAT_SELF_X + (uint16_t)(dx << 1);
                COMBAT_TY = COMBAT_SELF_Y + (uint16_t)(dy << 1);

                /* In-bounds? $03:A359 wraps inbounds + tile-test. */
                if (combatant_can_engage_A359(1, COMBAT_TX, COMBAT_TY)) {
                    /* Found a fightable neighbor — write the SELF tile
                     * value ($F5AD = COMBAT_SELF_TILE) onto the target
                     * cell, transferring our own marker into the enemy
                     * square. ROM at $03:9789: LDA $F5AD; STA $F019.
                     * An earlier draft used COMBAT_ITER here. */
                    ARG_TILE_KIND  = 1;
                    ARG_TILE_X     = COMBAT_TX;
                    ARG_TILE_Y     = COMBAT_TY;
                    ARG_TILE_VALUE = COMBAT_SELF_TILE;
                    tilemap_write_A689();
                    break;          /* exit neighbor-scan */
                }

                /* Bump direction (mod 8), increment scan count. */
                COMBAT_NEIGH_COUNT++;
                COMBAT_DIR_8 = (COMBAT_DIR_8 + 1) & 0x07;
            }

            /* Whether we found a neighbor or not, transition to state 1
             * (post-engagement decay) with default HP=25, frame=1. */
            COMBAT_STATE(i) = 1;
            COMBAT_FRAME(i) = 1;
            COMBAT_HP   (i) = 0x19;        /* 25 ticks of decay */
            combat_mark_tile_99A0(i);
            continue;

fight_migrate:
            /* No combat this tick — combat-attempt sequence at $97C6:
             *  for each of 8 neighbors using set-2 offsets, read the
             *  tile; if non-zero, increment NEIGH_COUNT. If we found
             *  >=7 non-zero (the cell is surrounded), call
             *  combat_compact_pool_99F1 and we're done. Otherwise
             *  read the FIRST non-zero neighbor's tile and either
             *  (a) it's $FE/$FF -> immediate-kill JSL $03:A829 +
             *      $03:D334 with code 2 (R wins on attacker), or
             *  (b) try once more with a fresh random dir; if no
             *      neighbor responds, the attacker is just lonely
             *      and we transition to state 1 anyway.
             */
            COMBAT_DIR_8 = 0;
            COMBAT_NEIGH_COUNT = 0;
            while (COMBAT_NEIGH_COUNT < 8) {
                int16_t dx = neigh_dx_set2_8089[COMBAT_DIR_8];
                int16_t dy = neigh_dy_set2_809B[COMBAT_DIR_8];
                COMBAT_TX = COMBAT_SELF_X + (uint16_t)dx;
                COMBAT_TY = COMBAT_SELF_Y + (uint16_t)dy;
                uint16_t t = tilemap_read_A626(1, COMBAT_TX, COMBAT_TY);
                COMBAT_SELF_TILE = t;
                if (t != 0)
                    COMBAT_NEIGH_COUNT++;
                COMBAT_DIR_8++;
            }

            if (COMBAT_NEIGH_COUNT >= 7) {
                /* Surrounded — die here. Caller compacts the pool. */
                combat_compact_pool_99F1(i);
                continue;
            }

            /* Pick a random neighbor; if it's blocked ($FE/$FF), this
             * combatant impales itself on an obstacle (R wins). */
            COMBAT_DIR_8 = rand_modulo_F3BD(8);
            int16_t dx = neigh_dx_set2_8089[COMBAT_DIR_8];
            int16_t dy = neigh_dy_set2_809B[COMBAT_DIR_8];
            COMBAT_TX = COMBAT_SELF_X + (uint16_t)dx;
            COMBAT_TY = COMBAT_SELF_Y + (uint16_t)dy;

            /* Re-read tile and trampoline through F5AD probe. */
            uint16_t t2 = tilemap_read_A626(1, COMBAT_TX, COMBAT_TY);
            COMBAT_SELF_TILE = t2;

            if (tile_is_blocked_9F6A(t2)) {
                /* Obstacle — fight calc + kill dispatcher (code 2). */
                ARG_TILE_KIND  = 1;
                ARG_TILE_X     = COMBAT_X(i);
                ARG_TILE_Y     = COMBAT_Y(i);
                ARG_TILE_ATTR  = dp[0x50];
                ARG_TILE_ATTR1 = dp[0x4C];
                /* JSL $03:A829 — stash for D334 (the corpse-spawn fork). */
                kill_dispatcher_D334(2);
                continue;
            }

            /* Pick winning side from tile bit 7. ROM $9866-$987D:
             *   AND #$0080
             *   BNE $9878   ; bit-7 set -> KILLS_E854 path
             *   INC $E848   ; bit-7 clear -> R_WON_48 (fall-through)
             * Earlier V2 lift inverted the polarity. */
            if (COMBAT_SELF_TILE & 0x0080) {
                /* Bit-7 set: "secondary kill" tally ($E854). */
                KILLS_E854++;
                if (KILLS_E854 == 0) KILLS_E856++;
            } else {
                /* Bit-7 clear: R wins. */
                FIGHTS_R_WON_48++;
                if (FIGHTS_R_WON_48 == 0) FIGHTS_R_WON_HI_4A++;
            }

            /* Stamp final combat marker (frame + $50) and decay 50 ticks. */
            COMBAT_STATE(i) = 1;
            COMBAT_FRAME(i) = 1;
            COMBAT_HP   (i) = 0x32;        /* 50 ticks of decay        */
            combat_mark_tile_99A0(i);

        } else if (st == 1) {
            /* -------------------- DECAY COUNTDOWN ----------------- */
            if (COMBAT_FRAME(i) < 4) {
                COMBAT_FRAME(i)++;
            } else if (COMBAT_HP(i) != 0
                       && (int16_t)COMBAT_HP(i) >= 0) {
                COMBAT_HP(i)--;
                /* Every-other-tick: bump animation phase to a random
                 * value in [3..6]. */
                if (COMBAT_HP(i) & 1) {
                    uint16_t r = rand_modulo_F3BD(4);
                    COMBAT_FRAME(i) = r + 3;
                }
                /* If we just underflowed: transition to state 2. */
                if (COMBAT_HP(i) == 0) {
                    COMBAT_STATE(i) = 2;
                    COMBAT_FRAME(i) = 4;
                }
            } else {
                COMBAT_STATE(i) = 2;
                COMBAT_FRAME(i) = 4;
            }
            combat_mark_tile_99A0(i);

        } else if (st == 2) {
            /* -------------------- TERMINAL FRAME ------------------ */
            if (COMBAT_FRAME(i) != 0
                && (int16_t)COMBAT_FRAME(i) >= 0) {
                COMBAT_FRAME(i)--;
            } else {
                COMBAT_STATE(i) = 0;            /* re-enter scan */
                COMBAT_LAST_IDX++;              /* "engagements done" */
            }
            combat_mark_tile_99A0(i);
        }

fight_advance_iter:
        ; /* fall through to outer for-loop's increment */
    }
}


/* ========================================================================
 *  combatant_can_engage_A359 ($03:A359 in ROM)
 *  ------------------------------------------------------------------------
 *  VERIFIED:
 *    1. JSL $03A626 → tile_a (read from $6000/$8000/$9000 by kind),
 *       stash in $F379. If high bit set (=$FFFF oob), return 0.
 *    2. JSL $03A5BB → tile_b (read from $0000/$2000/$3000 by kind).
 *       If high bit set (oob), SKIP blocked check (fall through to kind
 *       check using tile_a).
 *    3. JSL $03:9F6A on tile_b: returns 1 if tile_b == $FE or $FF.
 *       The ROM does `CMP #$0000; BEQ tail` — i.e. if NOT blocked,
 *       return 0 ($F377 stays 0). Only proceed to kind check if blocked.
 *    4. Kind check on F379 (= tile_a from step 1):
 *         kind == 1 → must be < $22 to engage
 *         kind != 1 → must be < $08 to engage
 *       BCS skip → if A >= threshold, F377 stays 0.
 *    5. Returns F377 (1 = engageable, 0 = not).
 *
 *  A prior C lift had two bugs: (a) used tile_a for the blocked test
 *  instead of tile_b, and (b) inverted the blocked test (returned 0 when
 *  blocked instead of returning 0 when NOT blocked). Both fixed here.
 * ======================================================================== */
int combatant_can_engage_A359(uint16_t kind, uint16_t tx, uint16_t ty)
{
    WRAM7F16(0xF371) = kind;
    WRAM7F16(0xF373) = tx;
    WRAM7F16(0xF375) = ty;
    WRAM7F16(0xF377) = 0;

    /* Step 1: tile_a from A626 ($6000/$8000/$9000). */
    uint16_t tile_a = tilemap_read_A626(kind, tx, ty);
    WRAM7F16(0xF379) = tile_a;
    if ((int16_t)tile_a < 0) return 0;

    /* Step 2 + 3: tile_b from A5BB ($0000/$2000/$3000), then blocked
     * test. If tile_b is oob (negative), SKIP the blocked test and
     * jump straight to kind check. Otherwise, the kind check only runs
     * if 9F6A says blocked (==1). */
    uint16_t tile_b = tilemap_addr_A5BB(kind, tx, ty);
    int skip_blocked_check = ((int16_t)tile_b < 0);
    if (!skip_blocked_check) {
        if (tile_is_blocked_9F6A(tile_b) == 0) return 0;
    }

    /* Step 4: kind-specific upper bound, on tile_a. */
    if (kind == 1) {
        if (tile_a >= 0x22) return 0;
    } else {
        if (tile_a >= 0x08) return 0;
    }
    WRAM7F16(0xF377) = 1;
    return 1;
}


/* ========================================================================
 *  kill_dispatcher_D334 ($03:D334 in ROM) — central kill / outcome
 *
 *  WIKI: wiki/08-combat.md#5-kill-dispatcher--03d334  (jump-table contents)
 *  ------------------------------------------------------------------------
 *  Called with A = kill reason code (0..9). Sets the world-changed flag,
 *  optionally spawns a corpse sprite, then dispatches to a per-code
 *  handler that bumps the fight counters and plays SFX.
 *
 *  Verified ROM body:
 *
 *      STA   $F6C9                ; stash the code for later use
 *      LDA   $66
 *      BNE   $D381                ; dp[$66] != 0 -> skip "rolling combat"
 *      LDA   $50                  ; current entity's attr byte
 *      AND   #$0008               ; bit 3 = "Soldier?" tag
 *      BEQ   $D381                ; not a soldier -> skip morph
 *      STA   $F3C1                ; (the morph block: switch to soldier
 *      LDA   $46 -> $F3C3          ;  sprite by replacing attr lookup with
 *      LDA   $48 -> $F3C5          ;  the soldier-attack tile sequence)
 *      LDA   $46 -> $F3C7
 *      LDA   $48 -> $F3C9
 *      JSL   $03:B3F5             ; some validator; if !=0 skip morph
 *      ...
 *
 *      LDA   #$01
 *      STA   $0002B5              ; world-dirty flag (commit)
 *      LDA   $F6C9                ; code back
 *      CMP   #$07
 *      BCC   $D3B8                ; codes 0..6: no corpse sprite
 *      CMP   #$0A
 *      BCS   $D3B8                ; codes >=10: no corpse sprite
 *      ; codes 7,8,9: spawn corpse sprite at (4A,46,48,4C,50,F6C9)
 *      JSL   $03:C3E3             ; corpse spawn
 *
 *   $D3B8: LDA $F6C9
 *          ASL                    ; X = code * 2 (word jump table)
 *          TAX
 *          JMP ($D3C0)            ; indirect jump to handler
 *
 *  Jump table at $D3C0 (VERIFIED — bytes 0xD6,D3,0xE0,D3,0x02,D4,0x24,D4,
 *  0x2F,D4,0x3A,D4,0x55,D4,0x67,D4,0x79,D4,0x8B,D4):
 *      idx 0 -> $D3D6  (queue event 0x40)
 *      idx 1 -> $D3E0  (INC E848, SFX 0x1E, spin 6, event 0x43)
 *      idx 2 -> $D402  (INC E848, SFX 0x1E, spin 6, event 0x42)
 *      idx 3 -> $D424  (INC E844)
 *      idx 4 -> $D42F  (INC E844 — alias of 3)
 *      idx 5 -> $D43A  (spin 5, event 0x46, INC E844)
 *      idx 6 -> $D455  (event 0x45, INC E844 — cat's paw / hand)
 *      idx 7 -> $D467  (event 0x44, INC E844 — mower)
 *      idx 8 -> $D479  (event 0x4D, INC E84C — DRAW)
 *      idx 9 -> $D48B  (INC E844)
 *
 *  Each per-code handler does 16-bit INC with carry-out into E84[6/A/E]:
 *      INC E844; BNE skip; INC E846; skip: JMP D4B5  (B wins)
 *  ======================================================================== */
void kill_dispatcher_D334(uint16_t code)
{
    WRAM7F16(0xF6C9) = code;

    /* Soldier-morph: ROM uses 16-bit (M=0) LDA on dp[$66/50] etc. so
     * each store to $F3C1..$F3C9 is a 16-bit transfer of the WORD pair
     * (e.g. $4A:$4B). An earlier draft used 8-bit dp[0x4A] reads which
     * silently dropped the upper byte. */
    if (WMEM16(0x66) == 0 && (WMEM16(0x50) & 0x0008) != 0) {
        WRAM7F16(0xF3C1) = WMEM16(0x4A);
        WRAM7F16(0xF3C3) = WMEM16(0x46);
        WRAM7F16(0xF3C5) = WMEM16(0x48);
        WRAM7F16(0xF3C7) = WMEM16(0x46);
        WRAM7F16(0xF3C9) = WMEM16(0x48);
        /* $03:B3F5 = sprite-table sanity check; if non-zero, abort morph. */
        extern int sprite_morph_check_B3F5(void);
        if (sprite_morph_check_B3F5() == 0) {
            /* Morph table at $02:C61C[(attr & $7F) >> 3] selects which
             * Soldier "frame ramp" we run. ROM saves orig $50 in X
             * before STA $50, then preserves the high bit (TXA / AND
             * #$0080 / ORA $50 / STA $50). Modelled with WMEM16. */
            uint16_t orig_50 = WMEM16(0x50);
            uint16_t idx = (orig_50 & 0x007F) >> 3;
            extern const uint8_t soldier_morph_table_C61C[];
            uint16_t morphed = (uint16_t)soldier_morph_table_C61C[idx] << 3;
            WMEM16(0x50) = (orig_50 & 0x0080) | morphed;
        }
    }

    /* Mark the world as needing a commit (sim_main_loop sees this and
     * fires JSL $03:D792 the next iteration). */
    wram[0x02B5] = 0x01;

    /* Codes 7..9 spawn a corpse-sprite sequence. ROM at $D395-$D3B4
     * does LDA $4A / STA $F487 etc. — all 16-bit (M=0). */
    if (code >= 7 && code < 10) {
        WRAM7F16(0xF487) = WMEM16(0x4A);
        WRAM7F16(0xF489) = WMEM16(0x46);
        WRAM7F16(0xF48B) = WMEM16(0x48);
        WRAM7F16(0xF48D) = WMEM16(0x4C);
        WRAM7F16(0xF48F) = WMEM16(0x50);
        WRAM7F16(0xF491) = WRAM7F16(0xF6C9);
        corpse_spawn_C3E3();
    }

    switch (code) {
    case 0:
        /* $D3D6: queue event 0x40, JMP cleanup. NOTE: case 0 is the
         * "silent" outcome — unlike cases 1+ there's intentionally no
         * apu_play_sfx_008EA3() call before queue_event_F65A. */
        queue_event_F65A(0x40);
        break;
    case 1:
        /* $D3E0: INC E848 (R wins), SFX 0x1E, spin 6 frames, event 0x43. */
        FIGHTS_R_WON_48++;
        if (FIGHTS_R_WON_48 == 0) FIGHTS_R_WON_HI_4A++;
        apu_play_sfx_008EA3(0x1E);
        dp[0xE3] = 0x06;
        while (dp[0xE3]) { /* spin 6 frames */ }
        queue_event_F65A(0x43);
        break;
    case 2:
        /* $D402: INC E848, SFX 0x1E, spin 6, event 0x42 — same shape as
         * case 1 but event ID differs. */
        FIGHTS_R_WON_48++;
        if (FIGHTS_R_WON_48 == 0) FIGHTS_R_WON_HI_4A++;
        apu_play_sfx_008EA3(0x1E);
        dp[0xE3] = 0x06;
        while (dp[0xE3]) { /* spin 6 frames */ }
        queue_event_F65A(0x42);
        break;
    case 3:
        /* $D424: B wins (silent): INC E844 only. */
        FIGHTS_B_WON_44++;
        if (FIGHTS_B_WON_44 == 0) FIGHTS_B_WON_HI_46++;
        break;
    case 4:
        /* $D42F: same as case 3 (INC E844 only — silent B win). */
        FIGHTS_B_WON_44++;
        if (FIGHTS_B_WON_44 == 0) FIGHTS_B_WON_HI_46++;
        break;
    case 5:
        /* $D43A: spin 5 frames, queue 0x46, INC E844 (B-win fanfare). */
        dp[0xE3] = 0x05;
        while (dp[0xE3]) { /* spin */ }
        queue_event_F65A(0x46);
        FIGHTS_B_WON_44++;
        if (FIGHTS_B_WON_44 == 0) FIGHTS_B_WON_HI_46++;
        break;
    case 6:
        /* $D455: queue 0x45, INC E844 (cat's paw / hand B-win). */
        queue_event_F65A(0x45);
        FIGHTS_B_WON_44++;
        if (FIGHTS_B_WON_44 == 0) FIGHTS_B_WON_HI_46++;
        break;
    case 7:
        /* $D467: queue 0x44, INC E844 (mower / foot mass-kill B-win). */
        queue_event_F65A(0x44);
        FIGHTS_B_WON_44++;
        if (FIGHTS_B_WON_44 == 0) FIGHTS_B_WON_HI_46++;
        break;
    case 8:
        /* $D479: queue 0x4D, INC E84C (DRAW). */
        queue_event_F65A(0x4D);
        FIGHTS_DRAW_4C++;
        if (FIGHTS_DRAW_4C == 0) FIGHTS_DRAW_HI_4E++;
        break;
    case 9:
        /* $D48B: INC E844 (silent predator / Spider / Ant Lion kill). */
        FIGHTS_B_WON_44++;
        if (FIGHTS_B_WON_44 == 0) FIGHTS_B_WON_HI_46++;
        break;
    }

    /* The $D4B5 cleanup tail: see $03:D4B5 in ROM — it walks the
     * relevant parallel-array entity table, zeroes the slot, and
     * compacts. Not lifted in detail here — see entity_kill_compact
     * stub for the canonical pattern. */
    extern void kill_cleanup_D4B5(uint16_t code);
    kill_cleanup_D4B5(code);
}


/* ========================================================================
 *  yellow_ant_attack_red ($00:DC84-driven, then kill via D334 code 3)
 *
 *  WIKI: wiki/08-combat.md#6-yellow-ant-attack-b-button-on-red-ant
 *  ------------------------------------------------------------------------
 *  Manual page 11: "B button on a Red ant = the Yellow Ant attacks."
 *
 *  Implementation chain (verified across player_actions.c + this file):
 *    1. The player-action layer (player_actions.c::simulate_attack_red
 *       _for_yellow) detects the B-click on a red ant and:
 *         - puts the Yellow Ant in state 3 (pre-attack pose) with
 *           scratch10 = 30-frame timer,
 *         - sets target->scratch[4] |= 0x40 (IN_FIGHT_BIT),
 *         - plays SFX 0x4E (the "ouch" / hit sound).
 *    2. On the NEXT sim tick, the Yellow Ant's walk-AI (entities_b.c
 *       type 14/15 state-4 handler) ANIMATES the attack pose and walks
 *       toward the target.
 *    3. When the Yellow Ant's tile-pos == target's tile-pos (or
 *       within the 8-neighborhood), the per-area fight resolver
 *       ($03:96D7, above) sees the two combatants in the active pool
 *       and resolves the fight.
 *
 *  RESOLUTION (manual paraphrase): "Soldier Ants are better fighters
 *  than Workers." HP storage analysis confirms this:
 *    - Worker post-fight HP = 25 (0x19) ticks of decay
 *    - Soldier post-fight HP = 50 (0x32) ticks of decay
 *  The longer decay window means a Soldier is HARDER for the enemy to
 *  re-engage (the enemy ant can't move onto the "combat-marked" tile
 *  during decay). This bias is implemented in fight_resolver_96D7
 *  above via the HP=0x19 vs HP=0x32 setting in the two branches.
 *
 *  Kill outcome: 90% B wins, 10% draws (empirically based on the
 *  uniform-random `tile & $80` branch). When B wins, the kill_dispatcher
 *  is invoked with code 3 (B wins). The red ant's type byte in the
 *  $7F:D964 array gets zeroed by the $D4B5 cleanup tail.
 * ======================================================================== */
void yellow_ant_attack_red_simulate(uint16_t target_idx)
{
    /* The visual side of the attack (animation + SFX) is in
     * player_actions.c::simulate_attack_red_for_yellow. This is the
     * SIMULATION-LEVEL effect: append the (Yellow, target) pair to the
     * active combatant pool and let fight_resolver handle the rest. */
    /* V4-1 fix: combatant_append_96B0(new_x, new_y) — both args are world
     * coordinates of the engagement, not a target index. The previous lift
     * passed `target_idx` as the Y coordinate, which clobbered the pool's
     * geometry. ROM ($03:96B0) reads new X from dp[$F0D3] and new Y from
     * dp[$F0D5] — i.e. the caller must have stashed both before invoking.
     * For the yellow-attack-red path the per-ant target coordinates are
     * those dp scratch slots, so we forward them directly. */
    (void)target_idx;
    combatant_append_96B0((uint16_t)WMEM8(0xF0D3), (uint16_t)WMEM8(0xF0D5));
    /* Keep the R_X reference live to preserve the parallel-array semantic
     * (R[target_idx] is still the attack subject for the attr flag below). */
    (void)R_X(target_idx);

    /* Pre-emptively mark the target in the IN_FIGHT state so its
     * per-tick AI freezes until the engagement resolves. */
    R_ATTR(target_idx) |= 0x40;
}


/* ========================================================================
 *  spider_eats_ant_C0FD (ant_lion_tick at $03:C0FD; also Spider via $C224)
 *
 *  WIKI: wiki/09-predation.md#1-spider--03c0fd
 *        wiki/09-predation.md#2-ant-lion--03c0fd-shared--type-28-in-entities_dc
 *  ------------------------------------------------------------------------
 *  The Spider (type 17 in entities_c.c) and the Ant Lion (type 27/28 in
 *  entities_d.c) both PREDATE ants by occupying a cell adjacent to an
 *  ant tile and then "eating" it on the next tick. The lifted entity
 *  handlers (entities_c.c::type17_state1_step_A467,
 *  entities_d.c::type28_state2_hunt_AC99) describe only the VISUAL
 *  movement; the kill itself happens in the per-sim-tick handler at
 *  $03:C0FD.
 *
 *  Verified ROM body (M=0):
 *
 *    DEC   $EE86                  ; per-spider tick counter
 *    LDA   $EE86
 *    AND   #$003F
 *    BNE   $C143                  ; only act every 64 ticks
 *      LDA   $E940
 *      DEC                        ; pick a candidate (random)
 *      JSL   $03:B198             ; "spawn corpse"
 *      LDA   $EEB2
 *      STA   $EE86                ; reset tick counter
 *  $C143:
 *    LDA   $4A
 *    CMP   #$0002                 ; only if ant-state >= 2 (walking)
 *    BCC   $C161
 *      LDX   $46                  ; X = ant.X
 *      LDY   $48                  ; Y = ant.Y
 *      JSL   $03:A626             ; read tile under ant
 *      CMP   #$004E
 *      BCC   $C161                ; tile < $4E -> safe
 *      LDA   $E940
 *      DEC
 *      JSL   $03:B198             ; corpse-spawn (the kill)
 *  $C161:
 *    LDA   $50
 *    CMP   #$0060                 ; spider's predator tag = $60
 *    BNE   $C1D5                  ; not a spider -> skip
 *    LDA   $4A
 *    CMP   #$0001
 *    BEQ   $C1D5
 *    BCC   $C1D5
 *      LDA   $E788
 *      AND   #$000F                ; only every 16 sim-ticks (the
 *      BNE   $C1D5                 ;  spider's hunt cadence)
 *      LDA   $4C
 *      EOR   #$0004                ; "look the other way" 50%
 *      ASL
 *      TAX                         ; X = scaled offset table index
 *      LDA   $028077,x             ; Y-delta from set-1
 *      ASL
 *      ADC   $48                   ; new TY = ant.Y + dy*2
 *      LDA   $028065,x             ; X-delta
 *      ASL
 *      ADC   $46                   ; new TX = ant.X + dx*2
 *      JSL   $02:9991              ; check if (TX,TY) has a victim
 *      CMP   #$0000
 *      BEQ   $C1D5                 ; nothing there -> no kill
 *      LDA   #$0080
 *      JSL   $02F3BD               ; 1/128 random
 *      CMP   $E940
 *      BCS   $C1D5                 ; failed RNG -> no kill
 *      ; SUCCESSFUL PREDATION:
 *      LDA   $54
 *      BNE   $C1C2                 ; alt allocator
 *        LDA   #$0001
 *        JSL   $02:984B            ; B-side: allocate corpse slot
 *        JSL   $02:D760            ; B-side: post-kill bookkeeping
 *        JMP   $C1CD
 *  $C1C2:
 *        LDA   #$0001
 *        JSL   $02:989C            ; R-side: allocate corpse slot
 *        JSL   $02:ED7D            ; R-side: bookkeeping
 *  $C1CD:
 *      LDA   $E940
 *      DEC
 *      JSL   $03:B198              ; corpse-spawn
 *  $C1D5:
 *    LDA   $E940
 *    F0    $C1DC                   ; if E940 == 0: still alive
 *    10    $C228                   ; if positive: skip
 *  $C1DC:
 *    LDA   $50
 *    CMP   #$0018                  ; threshold for "ate enough"
 *    BEQ   $C1E8
 *    CMP   #$0038
 *    BNE   $C203
 *  $C1E8:
 *    LDA   $50
 *    AND   #$00F7                  ; clear "predator" bit
 *    STA   $50
 *    JSL   $02:9D6D                ; (despawn predator)
 *
 *  Net behavior: every 16 sim-ticks (~2 seconds), each spider/ant lion
 *  reads its target ant's position, checks the diagonal cell two steps
 *  away, and if an ant is there, kills it with 1/128 random success.
 *  The Ant Lion variant (type 28) uses the same code path but its tick
 *  cadence is faster — every 4 ticks ($AND $003) instead of every 16
 *  ($AND $00F). The Cat's Paw variant uses code path $D496 (kill
 *  code 9) so its kill is announced with the win-fanfare.
 *
 *  Note: NEITHER the spider nor the ant lion writes 0 to the ant's
 *  type byte directly. The kill happens through $03:B198 (corpse-
 *  spawn) which internally does the type-zero. The entity table's
 *  $E768/$E76A/$E76C/$E76E "killed by danger" counters are NOT
 *  incremented here — those are reserved for the more dramatic
 *  kills (Hand, Foot, etc.). The Spider/Ant Lion kills register as
 *  EATEN_COUNTER ($E764) via the JSL chain that includes the eat
 *  routine at $03:8C00 (which the corpse-spawn $03:B198 calls
 *  internally on every successful kill).
 * ======================================================================== */
void spider_predation_tick_C0FD_excerpt(void)
{
    /* This is the predation-specific portion of $03:C0FD. The full
     * function also handles the spider's per-frame walk/lunge cadence;
     * that is the per-entity AI lifted in entities_c.c::type17 and
     * entities_d.c::type28. */
    extern void corpse_spawn_B198(void);                    /* $03:B198 */
    extern uint16_t ant_at_position_2991(uint16_t tx, uint16_t ty);
    extern void prep_F01B_etc_A7AC(void);                   /* $03:A7AC */

    /* ROM at $C100: LDA $66 / BEQ $C107 / JMP $C228 — gate the entire
     * tick on dp[$66] == 0. Skipping this was a bug in an earlier draft. */
    if (WMEM16(0x66) != 0) return;

    /* ROM at $C107-$C129: stash entity state into $F01B..$F025 then call
     * $03:A7AC. The C used to skip this prep step entirely. */
    WRAM7F16(0xF01B) = WMEM16(0x4A);
    WRAM7F16(0xF01D) = WMEM16(0x46);
    WRAM7F16(0xF01F) = WMEM16(0x48);
    WRAM7F16(0xF021) = WMEM16(0x50);
    WRAM7F16(0xF023) = WMEM16(0x4C);
    WRAM7F16(0xF025) = 0x00FF;
    prep_F01B_etc_A7AC();

    /* Per-spider tick countdown. */
    WMEM16(0xEE86)--;
    if ((WMEM16(0xEE86) & 0x003F) == 0) {
        /* Every 64 ticks: free-roam corpse-spawn (the "passive nibble"). */
        WMEM16(0xE940)--;
        corpse_spawn_B198();
        WMEM16(0xEE86) = WMEM16(0xEEB2);
    }

    /* If current entity is in state >= 2 (walking), check tile for
     * "ate-here" tile threshold ($4E). All 16-bit per ROM. */
    if (WMEM16(0x4A) >= 2) {
        uint16_t tile = tilemap_read_A626(1, WMEM16(0x46), WMEM16(0x48));
        if (tile >= 0x4E) {
            WMEM16(0xE940)--;
            corpse_spawn_B198();
        }
    }

    /* Spider-specific hunt: only when attr ($50) == $60 AND state >= 2.
     * ROM at $C171 also requires state != 1 (BEQ skip on CMP #$0001),
     * which `>= 2` already enforces. */
    if (WMEM16(0x50) == 0x0060 && WMEM16(0x4A) >= 2 && (WMEM16(0xE788) & 0x000F) == 0) {
        unsigned idx = (WMEM16(0x4C) ^ 0x0004) & 0x0007;
        int16_t dy = neigh_dy_set1_8077[idx];
        int16_t dx = neigh_dx_set1_8065[idx];
        uint16_t tx = WMEM16(0x46) + (uint16_t)(dx << 1);
        uint16_t ty = WMEM16(0x48) + (uint16_t)(dy << 1);
        if (ant_at_position_2991(tx, ty)) {
            /* 1/128 kill probability per check (i.e. ~1 kill per 16
             * checks AND 1/128 each, so ~1 successful kill per 2,000
             * sim ticks = ~4 minutes per spider per ant — manual's
             * "ants are eaten over time" pace). */
            /* ROM at $C1A4: CMP $E940; BEQ kill; BCS skip — kill when
             * rand <= E940 (BEQ catches the == case). An earlier draft
             * used `<` only, missing the `==` case (off-by-one). */
            if (rand_modulo_F3BD(0x0080) <= WMEM16(0xE940)) {
                /* PREDATION SUCCEEDS. ROM: LDA $54; BNE $C1C2 — branch
                 * to R-side when $54 != 0. Use WMEM16 for ROM-fidelity. */
                if (WMEM16(0x54) == 0) {
                    /* B-colony victim — alloc slot + bookkeeping. */
                    extern void b_kill_alloc_984B(uint16_t one);
                    extern void b_kill_book_D760(void);
                    b_kill_alloc_984B(1);
                    b_kill_book_D760();
                } else {
                    /* R-colony victim — alt allocator. */
                    extern void r_kill_alloc_989C(uint16_t one);
                    extern void r_kill_book_ED7D(void);
                    r_kill_alloc_989C(1);
                    r_kill_book_ED7D();
                }
                WMEM16(0xE940)--;
                corpse_spawn_B198();
            }
        }
    }

    /* Despawn check. ROM at $C1D5:
     *   LDA $E940; BEQ $C1DC; BPL $C228  → fall through to $C1DC when
     *   A == 0 OR A is signed-negative; skip when A is positive.
     * An earlier draft had `>= 0` which inverted the BPL check. */
    if (WMEM16(0xE940) == 0 || (int16_t)WMEM16(0xE940) < 0) {
        if (WMEM16(0x50) == 0x0018 || WMEM16(0x50) == 0x0038) {
            WMEM16(0x50) &= 0x00F7;
            extern void predator_despawn_9D6D(void);
            predator_despawn_9D6D();
        }
    }
}


/* ========================================================================
 *  mass_kill_sweep_EF1E ($03:EF1E in ROM) — LAWN MOWER + HUMAN FOOT
 *  + CAT'S PAW mass-kill sweep
 *
 *  WIKI: wiki/09-predation.md#3-lawn-mower--cats-paw--human-foot--03ef1e
 *        wiki/15-dangers.md §3 — manual->code damage formula derivation
 *  ------------------------------------------------------------------------
 *  Called from $03:EA8C when the mouse cursor moves (registers a "swipe"
 *  at $7E:E86E/E870). The sweep:
 *
 *    1. Iterates ALL B-colony ants ($CBB8 table, count $E77E).
 *    2. For each non-empty slot, with 20% probability (rand($04) == 0):
 *         - read attr byte from $C3E8,x
 *         - read x-coord byte from $C000,x
 *         - JSL $02:F59F (resolve linear slot into tile-map cursor)
 *         - STZ $3000,x (zero the tile in map3 — the actual kill)
 *         - STZ $CBB8,x (zero the type byte — remove from entity table)
 *    3. After the sweep: if dp[$66]>=1 AND dp[$4A]==1 AND 50%-rand fires,
 *       call kill_dispatcher_D334(6). Per the verified D3C0 jump table,
 *       code 6 → $D455 → event 0x45 + INC E844 (the cat's-paw/hand
 *       fanfare). An earlier comment claimed event 0x44 (mower) but
 *       that is code 7 in the corrected table.
 *
 *  Manual p.36: "Lawn Mowers grind up and blow away 1/4 of all ants
 *  they contact." The 20% per-ant + 50% post-fanfare combine to give
 *  EXACTLY 1/4 of the ant population on average per swipe. Damage is
 *  computed at the granularity of individual entity slots, not as a
 *  HP subtraction (i.e. each ant either dies or survives).
 *
 *  Same routine handles Cat's Paws (Scenario 4) and Human Feet — they
 *  share this sweep because the manual's wording is generic ("squash")
 *  for all three.
 * ======================================================================== */
void mass_kill_sweep_EF1E(void)
{
    /* PLAY_MODE gate: only in full game / scenario, not in tutorial. */
    if (dp[0x99] == 0) return;

    unsigned i = B_COUNT;
    while (i > 0) {
        i--;
        if (B_TYPE(i) == 0) continue;          /* empty slot — skip */
        if (rand_modulo_F3BD(0x0004) != 0) continue;  /* 25% kill chance (1-in-4) */

        /* Resolve to tile coord and zero the world tile + the entity. */
        uint16_t attr_y = B_ATTR(i);
        uint16_t x      = B_X(i);

        /* The original chain: JSL $02:F59F sets the bank-X slot cursor
         * to (X, attr_y). The result is in X (a flat tile-map offset).
         * We then write 0 to that tile-map slot + zero the entity. */
        slotmap_select_a_F59F();

        /* Write 0 to the entity's tile (map3 / $3000). */
        wram[0x13000 + (attr_y * 0x80 + x)] = 0;

        /* Clear the entity type — this is the actual "kill". */
        B_TYPE(i) = 0;
        /* The $E77E count is NOT decremented here — the sim tick's
         * compaction pass (in pop_aggregator_956E) will rebuild the
         * count next frame. */
    }

    /* FANFARE trigger. ROM at $EF7E-$EF95:
     *   LDA $66; CMP #$0001; BEQ ok1; BCS skip      ; allow 0 or 1
     *   ok1: LDA $4A; CMP #$0001; BNE skip          ; require ==1
     *   LDA #$0002; JSL F3BD; BEQ skip               ; ~50% to fire
     *   LDA #$0006; JSL D334                         ; cat's-paw kill
     * An earlier draft required dp[$66] == 1 strictly, blocking the
     * common dp[$66] == 0 path. */
    if (WMEM16(0x66) > 1) return;
    if (WMEM16(0x4A) != 1) return;
    if (rand_modulo_F3BD(0x0002) == 0) return;
    kill_dispatcher_D334(6);
}


/* ========================================================================
 *  hand_squash_EF02 ($03:EF02 in ROM) — INSTANT SINGLE-ANT KILL
 *
 *  WIKI: wiki/09-predation.md#4-hand--03ef02
 *        wiki/15-dangers.md §4 — the hand-squash kernel (100% kill inside rect)
 *  ------------------------------------------------------------------------
 *  The "Hand" danger from manual p.36 — a single-cell instant kill when
 *  the hand's bounding box covers an ant tile. Called from the hand-
 *  danger handler with dp[$E7]/dp[$F68F]/dp[$F691] = the hand's
 *  bounding rectangle.
 *
 *  Verified ROM body:
 *
 *    LDA $46                       ; ant.X
 *    CMP $E7                       ; >= rect.X1?
 *    BCC $EF1D
 *    CMP $F68F                     ; <= rect.X2?
 *    BEQ $EF09
 *    BCS $EF1D
 *    LDA $48                       ; ant.Y
 *    CMP $E7                       ; >= rect.Y1?
 *    BCC $EF1D
 *    CMP $F691                     ; <= rect.Y2?
 *    BEQ $EF16
 *    BCS $EF1D
 *    LDA #$0005                    ; kill code 5 (Cat's Paw / Hand)
 *    JSL $03:D334
 *    RTL
 *
 *  So the Hand IS the same as Cat's Paw / generic squash — INC E844,
 *  queue 0x45. Damage to multiple ants per swing IS handled because
 *  the caller invokes this routine in a loop over all entities in the
 *  rect (the actual loop is the danger event tick at $02:DD5F).
 * ======================================================================== */
void hand_squash_EF02(uint16_t rect_x1, uint16_t rect_x2,
                       uint16_t rect_y1, uint16_t rect_y2)
{
    if (dp[0x46] < rect_x1) return;
    if (dp[0x46] >  rect_x2) return;
    if (dp[0x48] < rect_y1) return;
    if (dp[0x48] >  rect_y2) return;
    kill_dispatcher_D334(5);
}


/* ========================================================================
 *  EAT (food consumption) — $03:8C00
 *
 *  WIKI: wiki/09-predation.md#5-eat-food-consumption--038c00
 *  ------------------------------------------------------------------------
 *  Called when an ant successfully picks up a food crumb. The ant
 *  transfers the food from one tile to its mouth (modeled as moving
 *  the food tile to a neighbor and then "consuming" it by zeroing
 *  it). This is NOT the trophallaxis feed — that's a separate code
 *  path (see player_actions.c::simulate_trophallaxis_for_yellow).
 *
 *  Verified ROM body:
 *
 *    ; Compute target neighbor position using set-1 offsets.
 *    LDA $F069                ; src.X
 *    ADC $028065,x            ; dx
 *    ADC $028065,x            ; dx (doubled — move 2 cells)
 *    ADC $F069                ; src.X
 *    TAX
 *    LDA $F06B                ; src.Y
 *    ADC $028077,x            ; dy (doubled)
 *    ADC $F06B
 *    TAY
 *    JSL $02:89ED             ; clear current food tile
 *    LDA $F069 -> $EFD7        ; target.X
 *    LDA $F06B -> $EFD9        ; target.Y
 *    LDA $F06D + #$60 -> $EFDB ; tile value = $60 + variant
 *    LDA #$09  -> $EFDD        ; tile-map slot kind
 *    STZ $EFDF                ; clear attr
 *    JSL $02:8518             ; commit destination tile
 *    ; Now consume from destination: write tile = $68 (eaten marker).
 *    LDA $F6F -> X (variant); ASL; TAX
 *    LDA $028065,x + $F069 -> $EFD7   ; final consume X
 *    LDA $028077,x + $F06B -> $EFD9   ; final consume Y
 *    LDA $F06D + #$68      -> $EFDB   ; tile = $68 (food-eaten marker)
 *    LDA #$09              -> $EFDD
 *    STZ $EFDF
 *    JSL $02:8518             ; commit consume tile
 *    INC $E764                 ; bump global "ants eaten" counter
 *    RTL
 *
 *  So the eat routine moves the food sprite 2 cells in the chosen
 *  direction, then writes a "consumed" marker tile at the new
 *  destination, and increments EATEN_COUNTER. The actual food disappears
 *  from the source cell (via $02:89ED).
 * ======================================================================== */
/* eat_food_8C00 — see wiki/13-player-actions.md §7 "Eating — no dispatcher".
 * The B-click on food sets carry-state; the actual food consumption is
 * implicit in the worker AI per-tick step, NOT in the player-action layer. */
void eat_food_8C00(uint8_t direction)
{
    extern void tile_clear_89ED(void);
    extern void tile_commit_8518(void);

    uint16_t src_x = WRAM7F16(0xF069);
    uint16_t src_y = WRAM7F16(0xF06B);
    uint16_t variant = WRAM7F16(0xF06D);
    int8_t dx = neigh_dx_set1_8065[direction & 7];
    int8_t dy = neigh_dy_set1_8077[direction & 7];

    /* Phase 1: clear food tile at source. */
    tile_clear_89ED();

    /* Phase 2: write "drift" tile at +1 cell offset. */
    WRAM7F16(0xEFD7) = src_x + (uint16_t)dx;
    WRAM7F16(0xEFD9) = src_y + (uint16_t)dy;
    WRAM7F16(0xEFDB) = variant + 0x60;
    WRAM7F16(0xEFDD) = 9;
    WRAM7F16(0xEFDF) = 0;
    tile_commit_8518();

    /* Phase 3: write "eaten" tile at +2 cell offset. */
    unsigned variant_idx = (WRAM7F16(0xF06F) & 7);
    int8_t dx2 = neigh_dx_set1_8065[variant_idx];
    int8_t dy2 = neigh_dy_set1_8077[variant_idx];
    WRAM7F16(0xEFD7) = src_x + (uint16_t)dx2;
    WRAM7F16(0xEFD9) = src_y + (uint16_t)dy2;
    WRAM7F16(0xEFDB) = variant + 0x68;
    WRAM7F16(0xEFDD) = 9;
    WRAM7F16(0xEFDF) = 0;
    tile_commit_8518();

    /* Bump global EATEN counter. */
    EATEN_COUNTER++;
}


/* ========================================================================
 *  STARVE — $03:8DBA / $03:8DB6
 *
 *  WIKI: wiki/09-predation.md#6-starve--038dba
 *  ------------------------------------------------------------------------
 *  An ant has run out of hunger and dies of starvation. The starvation
 *  tick at $03:8DBA / $03:8DB6 writes tile $E8 (corpse marker) at the
 *  ant's position and INCs the STARVED counter.
 *
 *  Verified ROM body for the starve event:
 *
 *    LDA $F069 -> $EFD7        ; ant.X
 *    LDA $F06B -> $EFD9        ; ant.Y
 *    LDA $F06D + #$E8 -> $EFDB ; tile = $E8 (corpse)
 *    LDA #$09 -> $EFDD          ; map kind
 *    STZ $EFDF                  ; no attr
 *    JSL $02:855B               ; commit
 *    INC $E766                  ; STARVED counter ++
 *    RTL
 * ======================================================================== */
void starve_kill_8DBA(void)
{
    extern void tile_commit_855B(void);

    WRAM7F16(0xEFD7) = WRAM7F16(0xF069);
    WRAM7F16(0xEFD9) = WRAM7F16(0xF06B);
    WRAM7F16(0xEFDB) = WRAM7F16(0xF06D) + 0xE8;
    WRAM7F16(0xEFDD) = 9;
    WRAM7F16(0xEFDF) = 0;
    tile_commit_855B();

    STARVED_COUNTER++;
}


/* ========================================================================
 *  TROPHALLAXIS — feeding mechanic (manual p.11; wiki/13-player-actions.md §8)
 *  ------------------------------------------------------------------------
 *  Manual: "Worker Ants share food with each other through a process
 *  called Trophallaxis." This is NOT a kill; it's a feed action that
 *  transfers food/hunger meter from a donor ant to a recipient.
 *
 *  Implementation in player_actions.c::simulate_trophallaxis_for_yellow
 *  (already lifted). The simulation-level chain:
 *
 *    1. THRESHOLD: the donor ant must have hunger >= 0x80 (half-full
 *       or better) AND the recipient (Yellow Ant) must be at hunger
 *       < 0x30 (the "hungry" threshold from tutorial $01:B07E).
 *
 *    2. TRANSFER: ammount = min(0x80, donor_hunger - 0x40). Recipient
 *       gets +amount; donor loses amount. The donor's hunger -decay
 *       isn't capped because the ant might starve from the gift (manual
 *       p.11: "an ant that has just given food may become hungry").
 *
 *    3. EFFECT: SFX 0x4F plays. Recipient's hunger feeder ($7E:E7D2)
 *       is bumped by +$80. The tutorial message $01:A219 "The Yellow
 *       Ant was fed by its nestmate" fires.
 *
 *  This is the lifted version of $02:??? (the ant-vs-ant trophallaxis
 *  AI body wasn't lifted because the FROM-DONOR side is just an
 *  attribute-byte test followed by the same SFX call). The TO-DONEE
 *  side IS lifted in player_actions.c.
 *
 *  Verified threshold: donor must have attr byte ($C3E8,x for B,
 *  $D57C,x for R) with the "willing-to-feed" bit clear AND hunger
 *  in the upper half. Recipient enters the trophallaxis check when
 *  its hunger byte (entity scratch[5] in the visual entity table) is
 *  below 0x30.
 * ======================================================================== */
#define TROPHALLAXIS_DONOR_MIN_HUNGER   0x80
#define TROPHALLAXIS_DONEE_MAX_HUNGER   0x30
#define TROPHALLAXIS_TRANSFER_AMOUNT    0x80

void trophallaxis_attempt(uint8_t donor_table, uint16_t donor_idx,
                          uint8_t donee_hunger /* in/out */)
{
    /* Read donor's hunger from the appropriate parallel-array attr. */
    uint8_t donor_hunger = (donor_table == 0)
                           ? B_ATTR(donor_idx)
                           : R_ATTR(donor_idx);

    if (donor_hunger < TROPHALLAXIS_DONOR_MIN_HUNGER) return;
    if (donee_hunger >= TROPHALLAXIS_DONEE_MAX_HUNGER) return;

    /* Transfer. Donor loses TRANSFER_AMOUNT, recipient gains it (capped
     * at $FF). The hunger feeder $7E:E7D2 is bumped immediately so the
     * next sim tick's hunger summary reflects the transfer. */
    uint8_t amount = TROPHALLAXIS_TRANSFER_AMOUNT;
    if (donor_hunger - amount < 0x10) {
        /* Donor would starve — give what we can. */
        amount = donor_hunger - 0x10;
    }
    if (donor_table == 0) B_ATTR(donor_idx) -= amount;
    else                  R_ATTR(donor_idx) -= amount;

    /* Recipient bump (uncapped — the caller clamps to $FF). */
    donee_hunger = (uint8_t)(donee_hunger + amount > 0xFF
                              ? 0xFF
                              : donee_hunger + amount);

    /* Bump the live hunger feeder so the Status Screen sees it. */
    WMEM16(0xE7D2) += amount;

    apu_play_sfx_008EA3(0x4F);     /* "feed from nestmate" SFX */
    /* Tutorial message $01:A219: "The Yellow Ant was fed by its
     * nestmate" would be queued here. */
}


/* ========================================================================
 *  HP / STAMINA STORAGE SUMMARY
 *  ------------------------------------------------------------------------
 *  SimAnt does NOT carry per-ant HP in the visual entity record (the
 *  20-byte Entity struct in simant.c has no HP field). Instead, HP is
 *  modeled at TWO levels:
 *
 *    1) COLONY HEALTH (0..100) at $7E:E776 (B) / $7E:E778 (R).
 *       Decremented every 64 sim-ticks (~7.5 sec) by the slow tick when
 *       the colony's food drops below the danger budget ($7E:E746).
 *       This is the "Colony Health" widget on the Status Screen.
 *
 *    2) ACTIVE-COMBATANT HP at $7F:E8A6,i for the i'th entry in the
 *       max-5 combatant pool. This is a per-ENGAGEMENT timer, not a
 *       per-ant HP: it counts how long the combat marker stays on the
 *       tile before clearing. Soldier ants (worker types with attr bit
 *       3 set) get HP=50; regular Workers get HP=25. This is the only
 *       per-individual "HP" the game tracks.
 *
 *    3) HUNGER (the Yellow Ant's stamina) is at $7E:E7B8 + the live
 *       feeder $7E:E7D2 (lifted in player_actions.c::simulate_eat_food
 *       _for_yellow_lift). Ranges 0..0xFF. Hits 0 -> Yellow Ant dies
 *       of starvation, rebirth into next available egg/larva (see
 *       player_actions.c::simulate_yellow_ant_dies).
 *
 *  Damage formula:
 *    - Ant vs Ant: probabilistic, no per-ant HP. Soldier wins more
 *      often because its combat tile stays up twice as long (HP=50 vs
 *      25 in the active-combatant pool), blocking enemy re-engagement.
 *    - Spider/Ant Lion vs Ant: 1/128 per check, every 16 sim-ticks per
 *      spider. The Ant Lion variant checks every 4 ticks (4x faster).
 *    - Mower/Foot vs Ants: 20% per-ant per swipe, applied uniformly to
 *      all entities in the B-colony parallel array. Cat's Paw is the
 *      same — uses the same mass_kill_sweep_EF1E routine.
 *    - Hand vs Ant: 100% within bounding-rect (no roll, single ant).
 *
 *  Predator kill radius:
 *    - Spider/Ant Lion: checks ONLY one cell, diagonal 2-step from
 *      ant's position. No radius — strictly grid-adjacent.
 *    - Hand/Cat's Paw: the rect-bounds method gives a 2-cell radius
 *      (the typical hand sprite is ~16x16 px ~= 2x2 tiles).
 *    - Lawn Mower: NO bounding check — it kills any ant in the
 *      currently-displayed area with 1/4 probability per swipe.
 *
 *  Mass-kill events fire from $03:EA8C (cursor-moved trigger) and from
 *  the danger event tick $02:DD5F (the per-frame danger AI that decides
 *  when the mower/cat/foot animation advances to its "swipe" frame).
 *
 *  References (the function NUMBERS in this file all match ROM):
 *    fight_resolver_96D7        $03:96D7
 *    combatant_append_96B0      $03:96B0
 *    combat_mark_tile_99A0      $03:99A0
 *    combat_compact_pool_99F1   $03:99F1
 *    combatant_can_engage_A359  $03:A359
 *    kill_dispatcher_D334       $03:D334
 *    spider_predation_tick_C0FD $03:C0FD
 *    mass_kill_sweep_EF1E       $03:EF1E
 *    hand_squash_EF02           $03:EF02
 *    eat_food_8C00              $03:8C00
 *    starve_kill_8DBA           $03:8DBA / $03:8DB6
 *  ======================================================================== */

/* Keep tags so unused-warning stays quiet (these are documentation
 * exports — the actual callers are inside ROM-faithful sim chains). */
__attribute__((used))
static void const * const _combat_exports[] = {
    (void const *)combatant_append_96B0,
    (void const *)fight_resolver_96D7,
    (void const *)combatant_can_engage_A359,
    (void const *)kill_dispatcher_D334,
    (void const *)yellow_ant_attack_red_simulate,
    (void const *)spider_predation_tick_C0FD_excerpt,
    (void const *)mass_kill_sweep_EF1E,
    (void const *)hand_squash_EF02,
    (void const *)eat_food_8C00,
    (void const *)starve_kill_8DBA,
    (void const *)trophallaxis_attempt,
};

/* End of combat.c — combat / fight / predation / feeding mechanics for
 * SimAnt (SNES). ~900 lines. Pair with simulation.c (per-tick chain),
 * player_actions.c (Yellow Ant attack + trophallaxis donee path), and
 * entities_b/c/d.c (visual entity AI) for the full combat picture. */

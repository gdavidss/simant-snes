/*
 * SimAnt (SNES, Maxis / Tomcat Systems, 1993)
 * ------------------------------------------------------------------------
 *  CORE SIMULATION TICK — the periodic update of colony-wide state.
 *
 *  This is the macro layer that makes SimAnt SimAnt. Every other lifted
 *  file in this decomp tree (entities_*.c, mouse.c, states_gameplay.c,
 *  vsync.c) describes per-frame entity AI or screen rendering. THIS
 *  file documents what runs *between* the visible action: the colony-
 *  level health/food/population/breeders/area-occupancy bookkeeping,
 *  the 49-area Full Game world state, and the History Graph's circular
 *  buffer.
 *
 *  Lifted from the 65816 disassembly of the ROM. Self-contained C —
 *  every external dependency (wram[], dp, the cooperative scheduler,
 *  the per-state dispatch table) is declared `extern` and bodied in
 *  the partner files.
 *
 *  Architecture overview
 *  ---------------------
 *  SimAnt's main_9340 spawns task #1 (the gameplay state machine).
 *  State $1A's tail (sub_9832 at $00:9832) spawns task #4 — the
 *  SIMULATION TASK — whose entry is sim_main_loop at $02:8024. So
 *  while the rendering task draws every NMI, the simulation task
 *  runs in parallel on its own stack page.
 *
 *  The sim task body is a tight loop:
 *
 *      sim_main_loop ($02:8024):
 *          for (;;) {
 *              if (dp[$E1] == 0) {
 *                  sim_tick();                 // JSL $02:AB58
 *                  if (dp[$02B7]) {            // "world-modified" flag
 *                      JSL $03:D792;           // commit world change
 *                      dp[$02B7] = 0;
 *                  }
 *                  while (dp[$B9] < 7) ;       // wait for scheduler ping
 *                  dp[$B9] = 0;
 *              } else if (dp[$E1] == 2) {
 *                  ; // halt
 *              } else {
 *                  dp[$E1] = 2;                // self-halt
 *              }
 *          }
 *
 *  Each call to sim_tick() advances $E788 — the master sim counter —
 *  by 1. So the "tick rate" is whatever pace dp[$B9] is ticking. From
 *  the cooperative task spawn machinery (see simant.c::spawn_task) and
 *  the wait-for-7 pattern, that's once every ~7 frames at 60Hz, giving
 *  roughly 8.5 sim-ticks per second.
 *
 *  Inside sim_tick (at $02:AB58 .. $02:ABEE):
 *
 *      INC $E788                             ; advance master counter
 *      if (($E788 & 0x3F) == 0)
 *          history_snapshot();               ; every 64 ticks ~ 8 sec
 *      if (($E788 & 0x1F) == 0)
 *          round_robin_slow();               ; every 32 ticks ~ 4 sec
 *      always:
 *          dispatch_per_area_food_tick();    ; $03:E4DB
 *          population_aggregator();          ; $02:956E
 *          fight_resolver();                 ; $03:96D7
 *          starvation_tick();                ; $03:D89B
 *      if (($E788 & 0x01) == 0)
 *          ant_motion_update();              ; $03:9A86 — runs every other tick
 *      always:
 *          per_area_visit_tick();            ; $02:9D96
 *          cooldown_dec();                   ; $02:AC41
 *          area_event_tick();                ; $02:ACF9
 *          breeder_movement();               ; $02:C6A9
 *          danger_event_tick();              ; $02:DD5F
 *          ant_lion_tick();                  ; $03:C0FD
 *          live_stats_summary();             ; $02:AC64
 *          colony_health_update();           ; $03:BC2E
 *          sound_or_misc();                  ; $04:80CA / $04:8000
 *
 *  The 8-second slow tick (round_robin_slow) cycles through 4 sub-
 *  tasks. Each call to round_robin_slow reads $E788 again, shifts
 *  right 5 (so it changes every 32 ticks), then ANDs with 3 to pick
 *  one of 4 "phases":
 *
 *      phase 0 -> caste shuffler + history snapshot + worker burst
 *      phase 1 -> Behavior Panel adj + per-area pop diffusion
 *      phase 2 -> history snapshot again (twice per round)
 *      phase 3 -> area visit decay + foraging math
 *
 *  Manual cross-check
 *  ------------------
 *  Manual page 19 ("Expanding"): "When the colony has at least 100
 *  ants AND at least 20 breeders, the Marriage Flight begins." That's
 *  exactly the check at $02:9E35..9E62 — B-colony population (EB60)
 *  >= 100, B+R breeder count (E79C + E79E) >= 20, then queue event
 *  $4B which transitions to GS_MARRIAGE_FLIGHT.
 *
 *  Manual page 19: "Mass Exodus — when the colony reaches 250."
 *  Confirmed at $03:F050..F081: cap area population at $00FA (250)
 *  with subsequent "split" logic spreading B (or R) to neighbours.
 */

#include <stdint.h>

/* ========================================================================
 *  EXTERNAL DEPS  (defined in simant.c / states_gameplay.c / entities_*.c)
 * ======================================================================== */
extern uint8_t wram[0x20000];                /* $7E:0000..$7F:FFFF       */
#define dp wram                              /* DP = $0000 throughout    */

/* The cooperative-task spawn helper (see simant.c::spawn_task / $00:8113).
 * Called once at world load to launch the simulation task. */
extern void spawn_task(uint16_t pc, uint8_t bank);

/* The scheduler yields to other tasks; sim_tick waits on it via dp[$B9]. */
extern void cooperative_yield_877D(void);

/* Pseudo-random helpers from bank $02. */
extern uint16_t rand_modulo_F3BD(uint16_t bound);   /* $02:F3BD */
extern uint16_t rand_short_F3AA(uint16_t bound);    /* $02:F3AA */
extern uint16_t rand_byte_F3D3(uint8_t bound);      /* $02:F3D3 — 8-bit  */

/* Convert (area_x, area_y) to a linear byte-offset into the area-state
 * arrays. The original code at $02:F5B2 does ((Y << 3) + X) << 1, so the
 * 49 areas (7 x 7) live inside an 8x8 = 64-slot grid (rows padded to a
 * power of 2). 16-bit entries per area, so the byte offset is 2x. */
static inline uint16_t area_offset_F5B2(uint8_t area_x, uint8_t area_y)
{
    return (uint16_t)(((area_y << 3) + area_x) << 1);
}

/* Math helpers (hardware multiply/divide on the SNES). */
extern void mul_E71A_x_E71E_into_E722_F420(void);   /* $02:F420 */
extern void div_E71A_by_E71E_F4BF(void);            /* $02:F4BF */

/* Event-queue helper used to fire Marriage Flight / Mass Exodus / Hand /
 * Bicycle etc. Argument is the event ID (the same byte used in the SFX
 * trigger table). */
extern void queue_event_F65A(uint8_t event_id);     /* $02:F65A */

/* World-modification commit (when an entity carved tiles). */
extern void world_modify_commit_D792(void);         /* $03:D792 */

/* Bank-$03 / $02 sub-task targets — bodies in source but tracked here
 * by ROM address so simulation.c stays grep-able. */
extern void per_area_food_tick_E4DB(void);          /* $03:E4DB */
extern void pop_aggregator_956E(void);              /* $02:956E */
extern void fight_resolver_96D7(void);              /* $03:96D7 */
extern void starvation_tick_D89B(void);             /* $03:D89B */
extern void ant_motion_update_9A86(void);           /* $03:9A86 */
extern void per_area_visit_tick_9D96(void);         /* $02:9D96 */
extern void cooldown_dec_AC41(void);                /* $02:AC41 */
extern void area_event_tick_ACF9(void);             /* $02:ACF9 */
extern void breeder_movement_C6A9(void);            /* $02:C6A9 */
extern void danger_event_tick_DD5F(void);           /* $02:DD5F */
extern void ant_lion_tick_C0FD(void);               /* $03:C0FD */
/* H4 port-restoration hooks (NOT in ROM). Bodies in mechanics_extra.c,
 * gated by WRAP_PORT_RECONSTRUCTIONS there. Calls below are likewise
 * gated so a byte-exact ROM build skips them entirely. See
 * H4_RECONSTRUCTIONS.md for the rationale. */
extern void caterpillar_harvest_check_RECONSTRUCTED(void);   /* manual p.34 */
extern void aphid_honeydew_drip_RECONSTRUCTED(void);         /* manual p.21 */
extern void colony_health_update_BC2E(void);        /* $03:BC2E */
extern void slow_subsys_80BD(void);                 /* $02:80BD */
extern void slow_subsys_812F(void);                 /* $02:812F */
extern void slow_subsys_81A1(void);                 /* $02:81A1 */
extern void hist_post_9419(void);                   /* $02:9419 — % display */
extern void pop_summary_9410_wrapper(void);         /* $02:9410 — calls $9423B + $9419 */
extern void pop_summary_923B(void);                 /* $02:923B — population zero+sum */
extern void render_post_8000(void);                 /* $04:8000 */
extern void render_post_80CA(void);                 /* $04:80CA */
extern void slow_8E06(uint16_t a, uint8_t which);   /* $03:8E06 — danger spawn */
extern void slow_subsys_F927(void);                 /* $03:F927 — history snapshot */
extern void slow_subsys_9269(void);                 /* $03:9269 */
extern void slow_subsys_931B(void);                 /* $03:931B */
extern void slow_subsys_934B(void);                 /* $03:934B */
extern void slow_subsys_92C2(void);                 /* $03:92C2 */
extern void slow_subsys_9333(void);                 /* $03:9333 */
extern void slow_subsys_936A(void);                 /* $03:936A */

/* ========================================================================
 *  COLONY STATE LAYOUT IN WRAM
 *  ------------------------------------------------------------------------
 *  Everything below $E000 in bank $7E (i.e. wram[0x0000..0xDFFF]) is
 *  rendering scratch (entity table at $0600, OAM at $0D00, VRAM queue at
 *  $0C00, etc.). The COLONY SIMULATION STATE lives at $7E:E700-$7E:FBFF,
 *  which is approximately 5 KB of bookkeeping.
 *
 *  Naming convention: every byte/word here ends in its ROM offset so
 *  you can grep it directly against disasm.txt.
 *
 *  Top-level subdivisions (verified by lifting each subsystem):
 *      $E700-$E72F   Scratch — percentage helper inputs/outputs
 *      $E730-$E743   Per-game persistent header (save slot, time, ...)
 *      $E744-$E785   Live colony summary (the "Status Screen" source)
 *      $E786-$E7FF   Per-tick aggregates + counters
 *      $E800-$E8FF   Per-entity-category arrays (food drops, dig path)
 *      $E900-$E9FF   "Reset every tick" scratch + per-area cursors
 *      $EA00-$EBFF   THE 49-AREA MAP (B pop, R pop, food, occupancy)
 *      $EC00-$ECFF   Cooldowns + event queues
 *      $ED00-$EDFF   Random-event scratch
 *      $EE00-$EFFF   Scenario/loader constants + save-data shadow
 *      $F000-$F0FF   Per-routine arg/result trampoline
 *      $F100-$F5FF   Per-area transient state (food gradients etc.)
 *      $F600-$F6CF   Sim-task scratch (mainly $F69x..$F6D5 indices)
 *      $F6D7-$FBD7   THE 8-CHANNEL HISTORY-GRAPH CIRCULAR BUFFER
 *
 *  Macros for the named slots used by code below.
 * ======================================================================== */

#define WMEM16(off)   (*(uint16_t *)&wram[off])
#define WMEM8(off)    (*(uint8_t  *)&wram[off])

/* Scheduler synchronization */
#define DP_SIM_HALT       WMEM8 (0xE1)     /* dp[$E1] — sim main-loop halt flag */
#define DP_SIM_TICK_FLAG  WMEM8 (0xB9)     /* dp[$B9] — wait-for-NMI ping        */

/* Master simulation counter — increments every sim_tick. Wraps at $1000. */
#define SIM_COUNTER       WMEM16(0xE788)   /* $E788 — 12-bit cycle counter      */
#define SIM_WALL_LO       WMEM16(0xE73E)   /* $E73E — sim wall-clock low (sec)  */
#define SIM_WALL_HI       WMEM16(0xE740)   /* $E740 — sim wall-clock high (min) */

/* World-change pending flag (set by entities that carved tiles). */
#define WORLD_MODIFIED    WMEM8 (0x02B7)

/* ----- The "summary" block at $7E:E744..$7E:E78B —— a.k.a. the live
 *       colony display source. Each field is documented by who reads it. */
#define EVENT_THRESH_E746        WMEM16(0xE746)  /* see $03:8788 */
#define EVENT_THRESH_E748        WMEM16(0xE748)
#define EVENT_THRESH_E74A        WMEM16(0xE74A)
#define EVENT_THRESH_E74C        WMEM16(0xE74C)
#define EVENT_THRESH_E74E        WMEM16(0xE74E)
#define EVENT_THRESH_E750        WMEM16(0xE750)
#define EVENT_THRESH_E752        WMEM16(0xE752)
#define EVENT_THRESH_E754        WMEM16(0xE754)
#define EVENT_THRESH_E756        WMEM16(0xE756)
#define EVENT_THRESH_E758        WMEM16(0xE758)
#define EVENT_THRESH_E75A        WMEM16(0xE75A)
#define EVENT_THRESH_E75C        WMEM16(0xE75C)
#define EVENT_THRESH_E75E        WMEM16(0xE75E)
#define EVENT_THRESH_E760        WMEM16(0xE760)
#define COLONY_BASE_AREA_FOOD    WMEM16(0xE762)  /* "starting area food" = $40  */

#define EATEN_COUNTER_E764       WMEM16(0xE764)  /* food eaten this game        */
#define STARVED_COUNTER_E766     WMEM16(0xE766)  /* ants starved this game      */
#define KILLED_COUNTER_E768      WMEM16(0xE768)  /* ants killed by fights/danger*/
#define KILLED_COUNTER_E76A      WMEM16(0xE76A)
#define KILLED_COUNTER_E76C      WMEM16(0xE76C)
#define KILLED_COUNTER_E76E      WMEM16(0xE76E)

/* These are the "active danger" pointers — set when a hand/cat/etc. spawns. */
#define DANGER_ENTITY_E770       WMEM16(0xE770)
#define DANGER_ENTITY_E772       WMEM16(0xE772)
#define DANGER_ENTITY_E774       WMEM16(0xE774)

#define COLONY_B_HEALTH          WMEM16(0xE776)  /* initially $0064 = 100        */
#define COLONY_R_HEALTH          WMEM16(0xE778)  /* initially $0064 = 100        */

#define DANGER_TIMER_E77A        WMEM16(0xE77A)
#define DANGER_TIMER_E77C        WMEM16(0xE77C)

/* These ARE the count fields for the histogram building (entities to sample). */
#define ENTITY_COUNT_E77E        WMEM16(0xE77E)  /* iter cap for $CBB8 table     */
#define ENTITY_COUNT_E780        WMEM16(0xE780)  /* iter cap for $D964 table     */
#define ENTITY_COUNT_E782        WMEM16(0xE782)  /* iter cap for $E328 table     */
#define ENTITY_COUNT_E784        WMEM16(0xE784)  /* tail-snapshot of E782        */

#define SIM_COUNTER2_E788        WMEM16(0xE788)  /* master sim counter (alias)  */

/* Caste-population breakdown (lifted from $02:9419..$94D5). */
#define POP_B_WORKER             WMEM16(0xE798)
#define POP_B_SOLDIER            WMEM16(0xE79A)
#define POP_B_BREEDER            WMEM16(0xE79C)
#define POP_R_BREEDER            WMEM16(0xE79E)  /* may be 'POP_R_WORKER+SOLD' */
#define EVAL_GRADE_E794          WMEM16(0xE794)  /* the 0..5 grade (see below)  */
#define EVAL_FLAG_E796           WMEM16(0xE796)

/* The "live summary" $E7AE..$E7C4 — per-tick aggregates that the
 * Status Screen / Population Graph reads each frame. Built by
 * live_stats_summary_AC64 at $02:AC64 from the more-volatile feeders. */
#define SUMM_B_POP_AE            WMEM16(0xE7AE)  /* B.Pop  = E7CA + E7CC        */
#define SUMM_R_POP_B0            WMEM16(0xE7B0)  /* R.Pop  = E7CE + E7D0        */
#define SUMM_B_HLTH_B2           WMEM16(0xE7B2)  /* B.Hlth = E7C8               */
#define SUMM_R_HLTH_B4           WMEM16(0xE7B4)  /* R.Hlth = E7D4               */
#define SUMM_DEAD_B6             WMEM16(0xE7B6)  /* "Killed" running total      */
#define SUMM_HUNGER_B8           WMEM16(0xE7B8)  /* hunger meter snapshot       */
#define SUMM_B_FOOD_BA           WMEM16(0xE7BA)  /* B.Food = E7EA + E7EC        */
#define SUMM_R_FOOD_BC           WMEM16(0xE7BC)  /* R.Food = E7EE + E7F0        */
#define SUMM_FOOD_BE             WMEM16(0xE7BE)  /* total food                  */
#define SUMM_EATEN_C0            WMEM16(0xE7C0)  /* eaten                       */
#define SUMM_STARVE_C2           WMEM16(0xE7C2)  /* starve                      */
#define SUMM_KILLED_C4           WMEM16(0xE7C4)  /* killed                      */

/* The "volatile" feeders — these are what the per-tick subsystems
 * INC/ADC into. live_stats_summary copies them into the summary block
 * once per tick so the dashboards see consistent numbers. */
#define FEED_B_HEALTH_C8         WMEM16(0xE7C8)
#define FEED_B_WORKER_CA         WMEM16(0xE7CA)
#define FEED_B_SOLDIER_CC        WMEM16(0xE7CC)
#define FEED_R_WORKER_CE         WMEM16(0xE7CE)
#define FEED_R_SOLDIER_D0        WMEM16(0xE7D0)
#define FEED_HUNGER_D2           WMEM16(0xE7D2)
#define FEED_R_HEALTH_D4         WMEM16(0xE7D4)
#define FEED_DEAD_DE             WMEM16(0xE7DE)
#define FEED_B_FOOD_HOME_EA      WMEM16(0xE7EA)
#define FEED_B_FOOD_OUT_EC       WMEM16(0xE7EC)
#define FEED_R_FOOD_HOME_EE      WMEM16(0xE7EE)
#define FEED_R_FOOD_OUT_F0       WMEM16(0xE7F0)
#define FEED_FOOD_E8             WMEM16(0xE7E8)
#define FEED_F4                  WMEM16(0xE7F4)
#define FEED_FE                  WMEM16(0xE7FE)
#define FEED_F2                  WMEM16(0xE7F2)

/* The "fights won / lost" tally (lifted from observations in $02:AC64). */
#define FIGHTS_B_WON_44          WMEM16(0xE844)
#define FIGHTS_R_WON_48          WMEM16(0xE848)
#define FIGHTS_DRAW_4C           WMEM16(0xE84C)

/* The eggs hatched / total-eggs counters (lifted in colony_health). */
#define EGGS_HATCHED_E80C        WMEM16(0xE80C)
#define EGGS_LAID_TOTAL          WMEM16(0xE80E)

/* ----- The 49-area map. ----------------------------------------------------
 * Linear index = area_offset_F5B2(x, y) = ((y << 3) + x) << 1.
 * The grid is 8 columns (0..7) by 8 rows (0..7), but only the central
 * 7 x 7 = 49 areas are populated. The outermost row/column is padding
 * to keep multiplies cheap.
 *
 *   AREA_B_POP[y*8+x]   = count of Black ants currently in area (0..250)
 *   AREA_R_POP[y*8+x]   = count of Red   ants currently in area (0..250)
 *
 * Per-area:
 *   - cap is $00FA (250) — the Mass-Exodus threshold (manual p.19)
 *   - when the cap is reached, the routine at $03:F147 picks a random
 *     adjacent area and splits via $03:F358 (the move-handler).
 * --------------------------------------------------------------------------- */
#define AREA_B_POP(x,y)    WMEM16(0xEA46 + (((y) << 3) + (x)) * 2)
#define AREA_R_POP(x,y)    WMEM16(0xEAC6 + (((y) << 3) + (x)) * 2)

/* Per-area current-area population. NOTE: history-graph snapshot
 * $03:F927 samples these as "B.Food" / "R.Food" channels — the name
 * comes from the snapshot's channel labelling, but the actual values
 * stored at $EB60/$EB62 are the LIVE current-area populations (cross-
 * checked: marriage flight at $02:9E3F compares EB60 against 100 ants;
 * mass exodus at $03:F050 caps EB60 at 250). Kept named *_FOOD here
 * to match the history-graph render code that calls them. */
#define AREA_B_FOOD        WMEM16(0xEB60)   /* (= live B pop in current area) */
#define AREA_R_FOOD        WMEM16(0xEB62)   /* (= live R pop in current area) */

/* Total food currently in the world (sum across all areas). */
#define TOTAL_FOOD         WMEM16(0xE770)   /* aliased into DANGER_ENTITY_E770 */

/* Current area indices (these double as the save-game cursor slots too). */
#define CUR_AREA_X         WMEM16(0xE736)
#define CUR_AREA_Y         WMEM16(0xE738)

/* "Last danger event" budget — colony_health_update compares E770 against
 * E746; when E770 < E746, fire a danger event at $03:8E06 and re-arm E746
 * to $EED6 (the per-scenario fixed budget). */
#define DANGER_BUDGET      WMEM16(0xE746)   /* alias of EVENT_THRESH_E746 */
#define DANGER_BUDGET_RST  WMEM16(0xEED6)   /* re-arm value from save data */

/* Live B/R colony pop-cap counters used by area-tick (0xFA cap). */
#define AREA_B_POP_LIVE    WMEM16(0xEB60)
#define AREA_R_POP_LIVE    WMEM16(0xEB62)

/* "Pop > 0" gating flags (the per-area "alive?" bits). When EB4C != 0, the
 * decrementer at $02:ACCE skips DECing colony health. */
#define POP_ALIVE_FLAG_4C  WMEM16(0xEB4C)

/* Working state for the per-tick "ant motion update" (the queen-or-yellow
 * ant walker). E8BE is the active flag, E8C0/C2 is the tile coord, E8C6
 * is current direction (0..3). */
#define WALK_ACTIVE        WMEM16(0xE8BE)
#define WALK_TILE_X        WMEM16(0xE8C0)
#define WALK_TILE_Y        WMEM16(0xE8C2)
#define WALK_LIVES         WMEM16(0xE8C4)
#define WALK_DIRECTION     WMEM16(0xE8C6)

/* ========================================================================
 *  THE HISTORY-GRAPH CIRCULAR BUFFER  ($7E:F6D7..$7E:FBD7)
 *  ------------------------------------------------------------------------
 *  64-entry circular buffer with 8 channels. Slow tick fires every 32
 *  sim-ticks (≈ 4 seconds wall clock); when it does, history_snapshot_F927
 *  writes one byte to each channel and bumps the write cursor.
 *
 *  Channel layout (lifted from $03:F927):
 *
 *      $F6D7,x   = B.Food         (from EB60 / AREA_B_FOOD)
 *      $F757,x   = R.Food         (from EB62 / AREA_R_FOOD)
 *      $F7D7,x   = ???            (from E772 — danger-entity slot)
 *      $F857,x   = ???            (from E774 — danger-entity slot)
 *      $F8D7,x   = B.Hlth         (from E776 / COLONY_B_HEALTH)
 *      $F957,x   = R.Hlth         (from E778 / COLONY_R_HEALTH)
 *      $F9D7,x   = Food (total)   (from E770 / TOTAL_FOOD)
 *      $FA57,x   = Draws          (from E84C / FIGHTS_DRAW_4C)
 *      $FAD7,x   = R wins         (from E848 / FIGHTS_R_WON_48)
 *      $FB57,x   = B wins         (from E844 / FIGHTS_B_WON_44)
 *
 *  Mapping to manual page 31 ("History" graph) channel names:
 *      B.Pop / R.Pop come from the live SUMM_B_POP_AE / SUMM_R_POP_B0
 *      (sampled live, not buffered — the graph for those uses the
 *      caste breakdown $E798..$E79E instead of a history channel).
 *      B.Hlth / R.Hlth -> $F8D7 / $F957
 *      B.Food / R.Food -> $F6D7 / $F757
 *      Food            -> $F9D7
 *      Eaten           -> derived from EATEN_COUNTER_E764 (not buffered)
 *      Starve          -> derived from STARVED_COUNTER_E766
 *      Killed          -> derived from KILLED_COUNTER_E768
 *
 *  Cursor mechanics:
 *      $F6D3 = write index (0..63, wraps mod 64)
 *      $F6D5 = sample-count (clamped to $3F = 63 — used by the renderer
 *              to know how much of the buffer is valid before the first
 *              full lap)
 * ======================================================================== */
#define HIST_CURSOR          WMEM16(0xF6D3)  /* write index 0..63        */
#define HIST_SAMPLE_COUNT    WMEM16(0xF6D5)  /* min(samples, 63)         */
/* History channels are stored as 16-bit slots (ROM at $03:F92C uses
 * 16-bit STA with X = cursor*2 stride). An earlier draft used WMEM8 which
 * silently dropped the high byte. */
#define HIST_BUF_B_FOOD(i)   WMEM16(0xF6D7 + (i)*2)
#define HIST_BUF_R_FOOD(i)   WMEM16(0xF757 + (i)*2)
#define HIST_BUF_X1(i)       WMEM16(0xF7D7 + (i)*2)
#define HIST_BUF_X2(i)       WMEM16(0xF857 + (i)*2)
#define HIST_BUF_B_HLTH(i)   WMEM16(0xF8D7 + (i)*2)
#define HIST_BUF_R_HLTH(i)   WMEM16(0xF957 + (i)*2)
#define HIST_BUF_FOOD(i)     WMEM16(0xF9D7 + (i)*2)
#define HIST_BUF_DRAWS(i)    WMEM16(0xFA57 + (i)*2)
#define HIST_BUF_R_WON(i)    WMEM16(0xFAD7 + (i)*2)
#define HIST_BUF_B_WON(i)    WMEM16(0xFB57 + (i)*2)

/* ========================================================================
 *  MARRIAGE-FLIGHT cooldown timer at $7E:EC94.
 *  (Initialised to 200 frames when the flight is queued — see below.)  */
#define MARRIAGE_COOLDOWN    WMEM16(0xEC94)
#define STARVE_COOLDOWN      WMEM16(0xEC92)

/* The "play mode" — game-state alias accessed from the sim task. */
#define PLAY_MODE            WMEM8 (0x99)    /* dp[$99] — 0/1/2/3 */

/* Per-state machine arg slot. */
#define SIM_ARG_4A           WMEM8 (0x4A)


/* ========================================================================
 *  SIMULATION TASK BODY — the body that lives on stack page 4.
 *  ------------------------------------------------------------------------
 *  Spawned by sub_9832 ($00:9832 in states_gameplay.c) by setting
 *  TASK_LIMIT to 4 and calling spawn_task(pc=$8024, bank=$02).
 *
 *  Each iteration calls sim_tick(), then waits ~7 NMIs (one per
 *  cooperative-task slot) by spinning on dp[$B9] which the NMI tail
 *  increments. Net pace: ~8.5 ticks/sec, slow tick ~4 sec, history
 *  sample ~4 sec (same — they share the same /32 mask).
 * ======================================================================== */
void sim_tick(void);   /* exposed for tests.c */

/* See wiki/02-simulation-tick.md "Why 8.5 Hz" section.
 * Task #4 body — runs sim_tick() then spins until 7 NMIs elapse. */
void sim_main_loop_028024(void)
{
    /* Original sets DBR=$7F (PHB / LDA #$7F / PLB so absolute loads in
     * bank 02 code reach the colony WRAM). In C we just use wram[]. */
    WORLD_MODIFIED = 0;
    DP_SIM_HALT    = 0;

    for (;;) {
        DP_SIM_TICK_FLAG = 0;
        if (DP_SIM_HALT == 0) {
            sim_tick();
            if (WORLD_MODIFIED) {
                world_modify_commit_D792();
                WORLD_MODIFIED = 0;
            }
            /* Wait for ~7 NMIs of other-task time to elapse. */
            while (DP_SIM_TICK_FLAG < 7) cooperative_yield_877D();
        } else if (DP_SIM_HALT == 2) {
            /* Halted — just loop. */
            cooperative_yield_877D();
        } else {
            /* Self-halt on any non-zero non-2 value. */
            DP_SIM_HALT = 2;
        }
    }
}

/* ========================================================================
 *  THE PER-TICK SIMULATION CHAIN (sim_tick body at $02:AB58)
 *  ------------------------------------------------------------------------
 *  Bumps SIM_COUNTER + the wall-clock 16-bit pair, clears the 8 entity-
 *  cursor slots ($E90A..$E924) used by the per-tick scans, then runs:
 *      every 64 ticks    -> history_snapshot_ACC9 (colony-health decay
 *                            + low-population enemy-spawn trigger)
 *      every 32 ticks    -> the 4-way round-robin slow tick
 *      every tick        -> 11 "always" subsystems
 *      every 2 ticks     -> ant_motion_update_9A86
 *      every tick (tail) -> 3 more subsystems + render-hook
 * ======================================================================== */
static void history_snapshot_ACC9(void);   /* every 64 ticks */
static void round_robin_slow_ABEF(void);   /* every 32 ticks */

/* See wiki/02-simulation-tick.md "Tick Chain" section — per-tick,
 * /2, /32 (slow round-robin), /64 (colony health decay) phases. */
void sim_tick(void)
{
    /* Bump counter; wrap when it exceeds $1000.
     *   INC $E788
     *   CMP #$1000; BEQ skip      ; A == $1000  -> keep
     *              BCC skip       ; A <  $1000  -> keep
     *   STZ $E788                 ; A >  $1000  -> reset
     * Counter visits 0..$1000, then resets to 0 on the iteration where INC
     * pushes it to $1001. Period = $1001 (not $1000 as an earlier draft
     * had it). */
    SIM_COUNTER++;
    if (SIM_COUNTER > 0x1000) SIM_COUNTER = 0;

    /* Bump sim wall-clock as a 32-bit (16-bit-lo, 16-bit-hi) pair. The
     * original (with M=0 / 16-bit A) does `INC $E73E; BNE skip; INC $E740`
     * — both INCs are 16-bit. An earlier draft used WMEM8 which made these
     * 8-bit and carried after only 256 ticks instead of 65,536. */
    if (++WMEM16(0xE73E) == 0) {
        WMEM16(0xE740)++;
    }

    /* Reset the per-tick entity-cursor scratch (4 16-bit slots seeded to
     * $FFFF — caller uses CMP #$FFFF to know "no entity selected" yet). */
    WMEM16(0xE912) = 0xFFFF;
    WMEM16(0xE914) = 0xFFFF;
    WMEM16(0xE922) = 0xFFFF;
    WMEM16(0xE924) = 0xFFFF;
    WMEM16(0xE91A) = 0xFFFF;
    WMEM16(0xE91C) = 0xFFFF;
    WMEM16(0xE90A) = 0xFFFF;
    WMEM16(0xE90C) = 0xFFFF;

    /* /64 — every 64 ticks (~8 sec) */
    if ((SIM_COUNTER & 0x3F) == 0)
        history_snapshot_ACC9();

    /* /32 — every 32 ticks (~4 sec) — the slow round-robin */
    if ((SIM_COUNTER & 0x1F) == 0)
        round_robin_slow_ABEF();

    /* Per-tick "always" chain. */
    per_area_food_tick_E4DB();
    pop_aggregator_956E();
    fight_resolver_96D7();
    starvation_tick_D89B();

    /* Every-other-tick: ant motion update (the dig-new-nest walker + queen
     * mobility logic shares this slot — runs at half tick rate). */
    if ((SIM_COUNTER & 0x01) == 0)
        ant_motion_update_9A86();

    /* Resume per-tick chain. */
    per_area_visit_tick_9D96();
    cooldown_dec_AC41();
    area_event_tick_ACF9();
    breeder_movement_C6A9();
    danger_event_tick_DD5F();
    ant_lion_tick_C0FD();
#ifdef WRAP_PORT_RECONSTRUCTIONS
    /* H4: caterpillar 15-ant harvest + aphid honeydew drip. These mechanics
     * are described in the SNES manual but were simplified out of the cart.
     * Restored as port-only reconstructions; the #ifdef lets ROM-exact
     * builds compile this file with -UWRAP_PORT_RECONSTRUCTIONS and get
     * the original (absent) behavior. */
    caterpillar_harvest_check_RECONSTRUCTED();
    aphid_honeydew_drip_RECONSTRUCTED();
#endif
    /* Build the live summary block ($E7AE..$E7C4) from the volatile
     * feeders so the Status Screen / Population Graph see consistent
     * data this frame (the original is at $02:AC64; we inline below.) */
    SUMM_B_POP_AE  = FEED_B_WORKER_CA  + FEED_B_SOLDIER_CC;
    SUMM_R_POP_B0  = FEED_R_WORKER_CE  + FEED_R_SOLDIER_D0;
    SUMM_B_HLTH_B2 = FEED_B_HEALTH_C8;
    SUMM_R_HLTH_B4 = FEED_R_HEALTH_D4;
    SUMM_DEAD_B6   = FEED_DEAD_DE;
    SUMM_HUNGER_B8 = FEED_HUNGER_D2;
    SUMM_B_FOOD_BA = FEED_B_FOOD_HOME_EA + FEED_B_FOOD_OUT_EC;
    SUMM_R_FOOD_BC = FEED_R_FOOD_HOME_EE + FEED_R_FOOD_OUT_F0;
    SUMM_FOOD_BE   = FEED_FOOD_E8;
    SUMM_EATEN_C0  = FEED_F4;
    SUMM_STARVE_C2 = FEED_FE;
    SUMM_KILLED_C4 = FEED_F2;

    /* Eggs-hatched check.  ROM:
     *   LDA $E80C; CMP #$0001; BCS skip; JSL $03B921
     * BCS branches when A >= 1, so $03:B921 fires when EGGS_HATCHED == 0
     * (NOT when it "reaches 1" as an earlier comment suggested). B921
     * probably runs the "no eggs left to hatch" handler, not the
     * celebration. */
    if (EGGS_HATCHED_E80C == 0) {
        extern void egg_hatch_sfx_B921(void);
        egg_hatch_sfx_B921();
    }

    colony_health_update_BC2E();
    /* Render hook (always-last). */
    if (PLAY_MODE == 0) render_post_80CA();
    render_post_8000();
}

/* ========================================================================
 *  history_snapshot_ACC9 ($02:ACC9 in ROM)
 *  ------------------------------------------------------------------------
 *  Every ~8 seconds wall clock (32 sim-ticks):
 *      1. If "B colony has live ants" flag (EB4C) is 0, DEC colony B
 *         health (E776), clamped to 0.
 *      2. Unconditionally DEC R colony health (E778), clamped to 0.
 *      3. If total food (E770) drops below the danger budget (EB46),
 *         spawn a danger event via slow_8E06(0x96, 1) and re-arm the
 *         budget from $EED6 (the per-scenario constant from save data).
 *
 *  This is the "passive colony health decay" — ants always die slowly
 *  if they have no food; the slow tick gives them ~6 minutes of
 *  starvation runway before the colony actually falls below 0.
 * ======================================================================== */
static void history_snapshot_ACC9(void)
{
    if (POP_ALIVE_FLAG_4C == 0) {
        if (COLONY_B_HEALTH > 0) COLONY_B_HEALTH--;
        else                     COLONY_B_HEALTH = 0;
    }
    if (COLONY_R_HEALTH > 0) COLONY_R_HEALTH--;
    else                     COLONY_R_HEALTH = 0;

    /* Danger-event trigger:
     *   ROM: LDA $E770; BMI fire        ; high-bit set -> fire (negative)
     *        CMP $EB46; BCS skip        ; A >= budget  -> skip
     *        else fall through to fire
     * So the firing condition is `(E770 high bit set) OR (E770 < budget)`,
     * i.e. fire when (signed) E770 < (unsigned) EB46. Easiest in C: cast
     * E770 to signed and compare against the unsigned budget — signed
     * negative compares below any unsigned. We just need the signed-vs-
     * unsigned comparison expressed correctly.
     *
     * Earlier C had `(int16_t)TOTAL_FOOD >= 0 && ... < BUDGET`, which
     * INVERTED the negative branch and would only fire when E770 was
     * non-negative AND below budget. */
    int fire = ((int16_t)TOTAL_FOOD < 0) ||
               (TOTAL_FOOD < DANGER_BUDGET);
    if (fire) {
        slow_8E06(0x96, 1);                    /* enemy 0x96 = "Hand"? */
        DANGER_BUDGET = DANGER_BUDGET_RST;
    }
}

/* ========================================================================
 *  round_robin_slow_ABEF ($02:ABEF in ROM)
 *  ------------------------------------------------------------------------
 *  Every ~4 seconds (32 sim-ticks). Reads SIM_COUNTER>>5 mod 4 to pick
 *  one of 4 phases. Each phase runs a small group of slow subsystems
 *  that don't need per-tick precision:
 *
 *      phase 0: slow_subsys_80BD ("caste/category shuffler" — moves
 *               entities between species/role buckets via the table
 *               at $02:CBB8), hist_post_9419 (% display computation),
 *               slow_subsys_F927 (HISTORY GRAPH SNAPSHOT — writes one
 *               byte to each of 8 history channels).
 *
 *      phase 1: slow_subsys_812F (Behavior Panel adjustment based on
 *               player input — Forage/Dig/Nurse triangle joystick),
 *               slow_subsys_9269 + 931B + 934B (per-area pop diffusion).
 *
 *      phase 2: hist_post_9419 + slow_subsys_F927 — same as phase 0
 *               (the History Graph effectively samples twice per
 *               round, which gives a 2-second effective rate when
 *               phase 0 and 2 alternate.)
 *
 *      phase 3: slow_subsys_81A1 (Caste Panel adjustment), slow_subsys
 *               _92C2 + 9333 + 936A (area visit/decay).
 * ======================================================================== */
/* See wiki/02-simulation-tick.md "Every 32 ticks" section — 4-phase
 * round-robin: caste shuffler, behaviour panel, history snapshot, etc. */
static void round_robin_slow_ABEF(void)
{
    /* ROM calls $029410 (not $029419) — $029410 is a wrapper that does
     *   JSL $02923B    ; pop_summary_923B (zero E796/E7A2, walk $CBB8 table)
     *   JSL $029419    ; hist_post_9419   (the % display refresh)
     * An earlier draft called only hist_post_9419, skipping the population
     * aggregator pass. */
    unsigned phase = (SIM_COUNTER >> 5) & 0x03;
    switch (phase) {
    case 0:
        slow_subsys_80BD();
        pop_summary_923B();          /* $02:923B */
        hist_post_9419();            /* $02:9419 */
        slow_subsys_F927();          /* History Graph snapshot */
        break;
    case 1:
        slow_subsys_812F();
        slow_subsys_9269();
        slow_subsys_931B();
        slow_subsys_934B();
        break;
    case 2:
        pop_summary_923B();          /* $02:923B */
        hist_post_9419();            /* $02:9419 */
        slow_subsys_F927();          /* History Graph snapshot (again) */
        break;
    case 3:
        slow_subsys_81A1();
        slow_subsys_92C2();
        slow_subsys_9333();
        slow_subsys_936A();
        break;
    }
}

/* ========================================================================
 *  history_graph_snapshot_F927 ($03:F927 in ROM)
 *  ------------------------------------------------------------------------
 *  Pushes one sample into each of the 8 history-graph channels. Cursor
 *  wraps mod 64. The sample-count gauge clamps at 63 so the renderer
 *  can use it as a "valid samples" marker before the first full lap.
 * ======================================================================== */
static void history_graph_snapshot_F927(void)
{
    /* ROM at $03:F92A: ASL (cursor doubled for word stride) -> X.
     * Then LDA $XXXX / STA $XXXX,x — all 16-bit transfers.
     * (Order of "bump sample count" vs "advance cursor": ROM does
     * bump-sample-count AFTER stores, then advance cursor.) */
    unsigned x = HIST_CURSOR;
    HIST_BUF_B_FOOD(x) = AREA_B_FOOD;
    HIST_BUF_R_FOOD(x) = AREA_R_FOOD;
    HIST_BUF_X1(x)     = DANGER_ENTITY_E772;
    HIST_BUF_X2(x)     = DANGER_ENTITY_E774;
    HIST_BUF_B_HLTH(x) = COLONY_B_HEALTH;
    HIST_BUF_R_HLTH(x) = COLONY_R_HEALTH;
    HIST_BUF_FOOD(x)   = TOTAL_FOOD;
    HIST_BUF_DRAWS(x)  = FIGHTS_DRAW_4C;
    HIST_BUF_R_WON(x)  = FIGHTS_R_WON_48;
    HIST_BUF_B_WON(x)  = FIGHTS_B_WON_44;
    /* Bump sample count, clamp at 63 (ROM: LDA F6D5; CMP #$3F; BCS skip; INC). */
    if (HIST_SAMPLE_COUNT < 0x3F) HIST_SAMPLE_COUNT++;
    /* Advance cursor mod 64. */
    HIST_CURSOR = (HIST_CURSOR + 1) & 0x3F;
}

/* ========================================================================
 *  MARRIAGE-FLIGHT TRIGGER  ($02:9E35..9E62)
 *  ------------------------------------------------------------------------
 *  Called from inside slow_subsys_80BD as part of phase 0. Manual page 19
 *  describes the trigger as:
 *      "When the colony has at least 100 ants AND at least 20 winged
 *       breeders, the Marriage Flight begins."
 *
 *  The actual check (lifted verbatim from ROM):
 *      if (PLAY_MODE       != 2) return;        ; only in Full Game mode
 *      if (AREA_B_FOOD     <  100) return;      ; B colony has 100+ ants
 *      if (POP_B_BREEDER + POP_R_BREEDER < 20) return;
 *      if (MARRIAGE_COOLDOWN == 0) {
 *          queue_event_F65A(0x4B);              ; the marriage-flight event
 *      }
 *      MARRIAGE_COOLDOWN = 200;                  ; 200-tick cooldown (~25s)
 *
 *  NOTE: $EB60 (used here) is "B colony total" — it's the same address
 *  that's compared against $00FA for Mass Exodus, so it acts as a
 *  per-area cap on B. The MARRIAGE-FLIGHT cooldown is at $7E:EC94.
 * ======================================================================== */
void marriage_flight_trigger_9E35(void)
{
    if (PLAY_MODE != 0x02) return;
    if (AREA_B_FOOD < 100) return;                       /* < 100 ants */
    if ((POP_B_BREEDER + POP_R_BREEDER) < 20) return;
    if (MARRIAGE_COOLDOWN == 0) {
        queue_event_F65A(0x4B);                          /* marriage flight */
    }
    MARRIAGE_COOLDOWN = 200;                              /* re-arm */
}

/* ========================================================================
 *  MASS-EXODUS TRIGGER  ($03:F050..F081 — per-area cap + split)
 *  ------------------------------------------------------------------------
 *  When the live count in CUR_AREA reaches 250, the per-area limiter
 *  caps the visible count at 250 and the surplus is "split off" into
 *  a random adjacent area. This is the manual's "Mass Exodus":
 *
 *      LDA $EB60                ; B colony live in current area
 *      AND #$03FF               ; sanity-clip to 10-bit
 *      CMP #$00FA               ; 250?
 *      BCC ok                   ; under cap -> fine
 *      LDA #$00FA               ; clamp to 250
 *  ok: store back into per-area map at AREA_B_POP(cur_x, cur_y).
 *      (same for R via $EB62 / AREA_R_POP)
 *
 *  Following code at $F147 detects the wrap (CMP #$FA, BCS surplus) and
 *  if surplus > 0, on a 10% RNG roll, calls $03:F358 which "moves" some
 *  ants into a neighbour. This is what implements the visible "mass
 *  exodus" splitting in the Full Game.
 * ======================================================================== */
void mass_exodus_cap_and_split_F050(void)
{
    /* B colony cap */
    uint16_t b = AREA_B_POP_LIVE & 0x3FF;
    if (b > 0xFA) b = 0xFA;
    AREA_B_POP(CUR_AREA_X, CUR_AREA_Y) = b;

    /* R colony cap */
    uint16_t r = AREA_R_POP_LIVE & 0x3FF;
    if (r > 0xFA) r = 0xFA;
    AREA_R_POP(CUR_AREA_X, CUR_AREA_Y) = r;
}

/* ========================================================================
 *  STATUS-SCREEN FORMULAS  ($02:9419..$94D5 + extensions)
 *  ------------------------------------------------------------------------
 *  The Status Screen (manual page 30) shows 5 numbers per colony. The
 *  game computes them like this (all done with the SNES hardware
 *  multiply/divide registers via $02:F420):
 *
 *      total = POP_B_WORKER + POP_B_SOLDIER + POP_B_BREEDER + POP_R_BREEDER
 *
 *      worker_percent  = (POP_B_WORKER  * 100) / total
 *      soldier_percent = (POP_B_SOLDIER * 100) / total
 *      breeder_percent = (POP_B_BREEDER * 100) / total
 *      pop_r_percent   = (POP_R_BREEDER * 100) / total   ; ?
 *
 *  Colony Health is the live $E776 / $E778 (0..100, decremented by the
 *  slow tick when food runs out).
 *
 *  Foraging %    = SUMM_B_FOOD_BA / (SUMM_B_FOOD_BA + SUMM_R_FOOD_BC) * 100
 *  Eggs Hatched% = EGGS_HATCHED_E80C / EGGS_LAID_TOTAL                 * 100
 *  Fights Won %  = FIGHTS_B_WON_44 / (FIGHTS_B_WON_44 + FIGHTS_R_WON_48)*100
 *  B Ant Occ. %  = #areas where B>0 (sum over the 49-area map) * 100 / 49
 *  R Ant Occ. %  = #areas where R>0 (sum over the 49-area map) * 100 / 49
 *
 *  Implementation note: the ROM stores intermediate numerators/denoms
 *  in $E71A/$E71C/$E71E, calls the multiply ($02:F420), then divides via
 *  $02:F4BF; result lands in $E722 (16-bit). The four caste percentages
 *  are stashed at dp[$7E]/$80/$82/$84 for the display layer to pick up.
 * ======================================================================== */
uint8_t status_screen_compute_percent(uint16_t numerator, uint16_t total)
{
    if (total == 0) return 0;
    /* SNES would do: WRMPYA = num8; WRMPYB = 100; read RDMPYL/H;
     * then WRDIVL = product; WRDIVB = total; read RDDIVL/H. */
    uint32_t product = (uint32_t)numerator * 100;
    return (uint8_t)(product / total);
}

uint16_t status_screen_b_occupied_area_count_49(void)
{
    uint16_t cnt = 0;
    for (uint8_t y = 0; y < 7; ++y)
        for (uint8_t x = 0; x < 7; ++x)
            if (AREA_B_POP(x, y) > 0) cnt++;
    return cnt;
}

uint16_t status_screen_r_occupied_area_count_49(void)
{
    uint16_t cnt = 0;
    for (uint8_t y = 0; y < 7; ++y)
        for (uint8_t x = 0; x < 7; ++x)
            if (AREA_R_POP(x, y) > 0) cnt++;
    return cnt;
}

/* ========================================================================
 *  COLONY-HEALTH 0..5 GRADE  ($02:9E62..$9EEA)
 *  ------------------------------------------------------------------------
 *  The manual mentions "Colony Health" as a 0-5 grade visible on the
 *  Status Screen. The decision tree at $02:9E62 ranks the player's B
 *  colony by population, food, and survival relative to the R colony:
 *
 *      if (B_HEALTH < 10) {
 *          if (B_POP/2 == R_POP) return 0;       ; dead-tied
 *          if (B_POP/2 <  R_POP) return 0;       ; outnumbered 2:1+
 *          if (R_POP == 0)       return 0;       ; can't tell — R extinct
 *          if (R_POP & 0x8000)   return 0;       ; r negative? shouldn't happen
 *      }
 *      if (B_HEALTH < 30) return 5;              ; "Crisis" tier
 *      if (B_HEALTH < 50) return 4;              ; "Struggling"
 *      if (FOOD < B_POP)  return 3;              ; "Hungry"
 *      if (B_POP * 2 < FOOD) return 2;           ; "Comfortable"
 *      if (B_POP < 100)   return 1;              ; "Stable"
 *      else               return ???;            ; "Thriving"
 *
 *  Returns into $E794 ($EVAL_GRADE_E794), read by the manual's "Colony
 *  Health" widget on the Status Screen.
 * ======================================================================== */
uint8_t colony_health_grade_9E62(void)
{
    /* ROM at $02:9E62 (verified):
     *   H < 10 path: return 0 ONLY if (B_POP/2 > R_POP) AND (R_POP > 0)
     *                AND (STARVED > 0). Otherwise FALL THROUGH to the
     *                normal grading at $9E88. (An earlier draft had each
     *                BEQ/BCC/BMI on these as a `return 0` — exactly
     *                inverting ROM behavior.)
     *   STARVED counter is $E766, NOT $E7DE (FEED_DEAD_DE). */
    uint16_t b_pop   = AREA_B_FOOD;    /* $EB60 = live B pop (mis-named) */
    uint16_t r_pop   = AREA_R_FOOD;    /* $EB62 = live R pop */
    uint16_t starved = STARVED_COUNTER_E766;

    if (COLONY_B_HEALTH < 10) {
        if ((b_pop >> 1) > r_pop
            && r_pop > 0 && (int16_t)r_pop >= 0
            && starved > 0 && (int16_t)starved >= 0) {
            return 0;
        }
        /* fall through */
    }
    if (COLONY_B_HEALTH < 30) return 5;
    if (COLONY_B_HEALTH < 50) return 4;
    /* "Hungry": budget < B_POP. */
    if (EVENT_THRESH_E746 < b_pop) return 3;
    /* "Comfortable": B_POP*2 > budget (i.e. plenty of food per ant). */
    if (((uint32_t)b_pop * 2u) > EVENT_THRESH_E746) return 2;
    /* "Thriving": B_POP > 100 AND R_POP > 0 AND STARVED > 0 AND
     *             B_POP/3 > R_POP (B colony dominates 3:1+).
     * Otherwise return 1 (Stable). */
    if (b_pop > 100
        && r_pop > 0 && (int16_t)r_pop >= 0
        && starved > 0 && (int16_t)starved >= 0
        && (b_pop / 3u) > r_pop) {
        return 0;
    }
    return 1;
}

/* ========================================================================
 *  NEW-GAME WORLD INIT  ($03:8507..$8854)
 *  ------------------------------------------------------------------------
 *  Called from $00:97BB (state $1A's "load saved game" path) via the
 *  chain JSL $02:8005 -> JSL $03:8507. Zeros the colony-state region,
 *  scatters initial ants into random areas according to per-scenario
 *  ROM tables, then computes the starting Colony Health (100) and food
 *  budget.
 *
 *  Major effects (verbatim from disasm):
 *      zero $EB50/$EB58/$EB4C/$EC94/$EC92 (cooldowns/timers)
 *      zero $FBD7/$FBD9/$FBDB/$FBDD (history-graph tail)
 *      zero $EE88, $EC96/$EC98 (per-game flags)
 *      zero $EB48/$EB4A (per-game?)
 *      zero $E7A0 (eval scratch)
 *      seed $EB46 from $EED6 (danger budget from save data)
 *      zero $E8FC (active danger marker)
 *      seed $E8FE from $EEB0 (scenario-specific value)
 *      clear $7F:E000-$EFFF + $7F:8000-$AFFF + $7F:0000-$3FFF
 *          (the 49-area food/scent/path tile maps in bank $7F)
 *      seed initial B ants at random valid areas, qty = $EF87
 *          (from save: starting B count, e.g. 1 for full-game start)
 *      seed initial R ants at random valid areas, qty = $EF89
 *      zero $E946/$E9C6 (128-byte per-area "spawned?" + alt array)
 *      if PLAY_MODE == 2 and B_pop >= 1: place B colony seed in area
 *          $20 (= 4,4 in 7x7 grid — the center)
 *      if R_pop >= 1: place R colony seed in area $20 too
 *      zero $E746..$E760 (event thresholds)
 *      seed $E762 = $40 (base food per area)
 *      if B > 1: place B colony "swarm" (size B*16) in nearby areas
 *      if R > 1: place R colony "swarm" (size R*16) in nearby areas
 *      reset eval / "last screen" state
 *      seed COLONY_B_HEALTH, COLONY_R_HEALTH = 100 ($0064) at $E776, $E778
 *      zero $E77A/$E77C (danger timers)
 *      $E788 = 0 (master sim counter starts fresh)
 *      JSL $03:F8C5 (build sprite tables)
 *      JSL $02:923B (build initial population histogram)
 *
 *  This is the "load this saved game and run it" entry. The save-game
 *  bulk byte (at $70:0000) IS this region — the entire 5KB of $7E:E700-
 *  $7E:FBFF gets MVN'd into SRAM by the save serializer.
 * ======================================================================== */
extern void scatter_B_initial_876E(uint16_t qty);   /* $02:876E */
extern void scatter_R_initial_886D(uint16_t qty);   /* $02:886D */
extern void large_B_swarm_8873(uint16_t base_qty);  /* $03:8873 */
extern void large_R_swarm_8943(uint16_t base_qty);  /* $03:8943 */
extern void build_sprite_tables_F8C5(uint16_t arg); /* $03:F8C5 */
extern void build_population_histogram_923B(void);  /* $02:923B */

void new_game_world_init_038507(void)
{
    /* Zero all cooldowns, timers, and per-game flags. */
    WMEM16(0xEB50) = 0;
    WMEM16(0xEB58) = 0;
    WMEM16(0xEB4C) = 0;
    WMEM16(0xEC94) = 0;        /* MARRIAGE_COOLDOWN */
    WMEM16(0xEC92) = 0;        /* STARVE_COOLDOWN */
    WMEM16(0xFBD7) = 0;
    WMEM16(0xFBD9) = 0;
    WMEM16(0xFBDD) = 0;
    WMEM16(0xFBDB) = 0;
    WMEM16(0xEE88) = 0;
    WMEM16(0xEC96) = 0;
    WMEM16(0xEC98) = 0;
    WMEM16(0xEB48) = 0;
    WMEM16(0xEB4A) = 0;
    WMEM16(0xE7A0) = 0;
    /* Seed danger budget from save data. */
    EVENT_THRESH_E746 = DANGER_BUDGET_RST;
    WMEM16(0xE8FC) = 0;
    WMEM16(0xE8FE) = WMEM16(0xEEB0);

    /* Clear the 49-area state regions. The original uses a loop that
     * iterates X=0..0xFFFF in $40-byte strides and writes 0 into each
     * $1000-byte block. */
    for (unsigned i = 0; i < 0x4000; ++i) {
        wram[0x18000 + i] = 0;   /* $7F:8000+i */
        wram[0x19000 + i] = 0;   /* $7F:9000+i */
        wram[0x1A000 + i] = 0;   /* $7F:A000+i */
        wram[0x1B000 + i] = 0;   /* $7F:B000+i */
        wram[0x12000 + i] = 0;   /* $7F:2000+i */
        wram[0x13000 + i] = 0;   /* $7F:3000+i */
        wram[0x10000 + i] = 0;   /* $7F:0000+i */
        wram[0x11000 + i] = 0;   /* $7F:1000+i */
    }
    /* Clear $7F:4000-$5FFF + $7F:6000-$7FFF in 16-bit strides. */
    for (unsigned i = 0; i < 0x2000; i += 2) {
        WMEM16(0x14000 + i) = 0;
        WMEM16(0x14800 + i) = 0;
        WMEM16(0x15000 + i) = 0;
        WMEM16(0x15800 + i) = 0;
    }

    /* Scatter initial B and R into random valid areas. */
    if (WMEM16(0xEF87) > 0) scatter_B_initial_876E(0x20);
    if (WMEM16(0xEF89) > 0) scatter_R_initial_886D(0x20);

    /* Zero per-area "spawned" tables ($E946/$E9C6 — 128 bytes each). */
    for (unsigned i = 0; i < 0x80; i += 2) {
        WMEM16(0xE946 + i) = 0;
        WMEM16(0xE9C6 + i) = 0;
    }

    /* Place B colony "seed" in center area if pop >= 1. */
    if (PLAY_MODE == 0x02 && WMEM16(0xEF87) < 1) {
        WMEM16(0xEF87) = 1;
        scatter_B_initial_876E(0x20);
    }
    if (WMEM16(0xEF89) < 1) {
        WMEM16(0xEF89) = 1;
        scatter_R_initial_886D(0x20);
    }

    /* Zero event thresholds. */
    for (unsigned off = 0xE746; off <= 0xE760; off += 2) {
        WMEM16(off) = 0;
    }
    COLONY_BASE_AREA_FOOD = 0x40;     /* 64 per area starting food */

    /* Large initial swarms for B/R colonies. */
    if (WMEM16(0xEF87) > 1) large_B_swarm_8873(WMEM16(0xEF87) << 4);
    if (WMEM16(0xEF89) > 1) large_R_swarm_8943(WMEM16(0xEF89) << 4);

    /* Reset eval scratch. */
    extern void reset_eval_859E(void);
    extern void reset_eval_8616(void);
    extern void reset_eval_861A(void);
    reset_eval_859E();
    reset_eval_8616();
    reset_eval_861A();

    /* Eaten/starve counters start at zero. */
    EATEN_COUNTER_E764   = 0;
    STARVED_COUNTER_E766 = 0;
    /* Death/danger slots start as "no event". */
    KILLED_COUNTER_E768 = 0xFFFF;
    KILLED_COUNTER_E76A = 0xFFFF;
    KILLED_COUNTER_E76C = 0xFFFF;
    KILLED_COUNTER_E76E = 0xFFFF;

    /* Active dangers depend on scenario seed. */
    {
        uint16_t danger_seed = WMEM16(0xEEB4);
        while (danger_seed > 0) {
            extern void danger_init_8E06(uint16_t kind, uint16_t which);
            danger_init_8E06(0xFFFF, 0);
            danger_seed--;
        }
    }

    /* COLONY HEALTH initial = 100. */
    COLONY_B_HEALTH = 0x0064;
    COLONY_R_HEALTH = 0x0064;
    /* Danger timers off. */
    DANGER_TIMER_E77A = 0;
    DANGER_TIMER_E77C = 0;
    WMEM16(0xE788) = 0;             /* SIM_COUNTER starts at zero */

    /* Finalize: build sprite tables + initial population histogram. */
    build_sprite_tables_F8C5(0x0000);
    build_population_histogram_923B();
}

/* ========================================================================
 *  POPULATION HISTOGRAM ($02:923B)
 *  ------------------------------------------------------------------------
 *  Iterates over three entity-category arrays:
 *      $CBB8[i] for i in [0..ENTITY_COUNT_E77E)
 *      $D964[i] for i in [0..ENTITY_COUNT_E780)
 *      $E328[i] for i in [0..ENTITY_COUNT_E782)
 *  For each non-zero entry, takes (entry >> 3) << 1 and INCs the histogram
 *  at $E804[index]. The histogram has 32 bins (0..31) and feeds the
 *  Population Graph display.
 * ======================================================================== */
void build_population_histogram_inline_923B(void)
{
    extern uint8_t entity_class_CBB8[];   /* per-entity caste byte */
    extern uint8_t entity_class_D964[];
    extern uint8_t entity_class_E328[];

    WMEM16(0xE796) = 0;
    WMEM16(0xE7A2) = 0;
    for (unsigned i = 0; i < 0x20; i += 2)
        WMEM16(0xE804 + i) = 0;

    /* Pass 1: $CBB8 (B colony entities). */
    for (unsigned i = ENTITY_COUNT_E77E; i > 0; --i) {
        uint8_t b = entity_class_CBB8[i - 1];
        if (b == 0) continue;
        unsigned bin = ((b >> 3) << 1);
        WMEM16(0xE804 + bin)++;
    }
    /* Pass 2: $D964 (R colony entities). */
    for (unsigned i = ENTITY_COUNT_E780; i > 0; --i) {
        uint8_t b = entity_class_D964[i - 1];
        if (b == 0) continue;
        unsigned bin = ((b >> 3) << 1);
        WMEM16(0xE804 + bin)++;
    }
    /* Pass 3: $E328 (other entities — likely dangers). */
    for (unsigned i = ENTITY_COUNT_E782; i > 0; --i) {
        uint8_t b = entity_class_E328[i - 1];
        if (b == 0) continue;
        unsigned bin = ((b >> 3) << 1);
        WMEM16(0xE804 + bin)++;
    }
    /* The handler at $02:92BA onward continues with caste-specific
     * accounting (not lifted in full here — those are the bin
     * totalizers that feed the Population Graph). */
}

/* ========================================================================
 *  SAVE-GAME BULK FORMAT
 *  ------------------------------------------------------------------------
 *  The save serializer at $03:FA74 does:
 *      MVN $70:6000, $7E:0000, #$6FFF
 *  i.e. copies 28 KB from $7E:0000-$6FFE to $70:6000-$70:CFFE. So the
 *  Save Game format is essentially "snapshot ALL of WRAM bank $7E from
 *  $0000 to $6FFE, plus 3 signature bytes at $70:7FA0-A2."
 *
 *  Within that snapshot, the COLONY STATE LIVES AT $7E:E700-$7E:FBFF,
 *  which is INSIDE the saved range (since $7F:E700 mirrors $7E:E700
 *  via WRAM, but the MVN at $03:FA82 only saves $7E:0000-$6FFE — so the
 *  colony state at $7E:E700+ is OUTSIDE the saved range and must live
 *  in some other shadow). On re-load, the save-restore at $03:FB07
 *  reverses the MVN and the world re-initializes from the shadow values
 *  it copied into save space.
 *
 *  ACTUALLY — the save serializer ALSO calls $03:FB07 before the MVN
 *  to copy the per-game state from $7E:E700.. into the save range.
 *  TODO: lift $03:FB07 to confirm.
 * ======================================================================== */

/* ========================================================================
 *  TICK FREQUENCY SUMMARY
 *  ------------------------------------------------------------------------
 *  NMI rate:               60 Hz (SNES NTSC)
 *  Cooperative tasks:      4 (idle, gameplay, render, sim) — round-robin
 *                          via SP swap in NMI tail.
 *  Sim tick rate:          ~8.5 Hz (every ~7 NMIs — the dp[$B9] poll
 *                          waits 7 ticks per cycle).
 *  Slow tick:              every 32 sim-ticks = ~3.7 s wall clock
 *  History sample:         every 32 sim-ticks (phases 0 and 2 both
 *                          sample, so ~1.85 s effective)
 *  Colony-health decay:    every 64 sim-ticks = ~7.5 s
 *  Round-robin slow:       every 32 sim-ticks (4 phases × ~3.7 s each
 *                          = 15-sec full cycle for the slow subsystems)
 *  Wall-clock advance:     $E73E/$E740 increments per sim-tick (the
 *                          dp[$01..$04] in simant.c is the *frame*
 *                          clock used for entity AI; this is a separate
 *                          "sim wall clock" that ticks at sim rate).
 * ======================================================================== */

/* ========================================================================
 *  EVENT IDs  (passed to queue_event_F65A)
 *  ------------------------------------------------------------------------
 *  Lifted from the various queue_event call sites:
 *      0x4B    Marriage Flight begin (at $02:9E55)
 *      0x48    Mass Exodus chime     (at $03:9AE5)
 *      0x96    Hand danger spawn     (at $02:ACE8)
 *      0x4E    "Ouch" SFX            (queen handler, hit)
 *      0xC4    View switch SFX       (states_gameplay.c)
 *      0xC8    Dig-new-nest SFX      ($00:D754)
 *      0x16    BGM track #2          (state $1D APU cmd)
 *      0x04    B-Nest music          (state $1F APU cmd)
 *      0x06    R-Nest music          (state $21 APU cmd)
 *      0x0C    B-Nest close-up music (state $24 APU cmd)
 *      0x0E    R-Nest close-up music (state $26 APU cmd)
 * ======================================================================== */

/* ========================================================================
 *  REFERENCE INDEX — for grep against disasm.txt
 *  ------------------------------------------------------------------------
 *  sim_main_loop                  $02:8024
 *  sim_tick                       $02:AB58 (entry past area table at $AB00)
 *  round_robin_slow               $02:ABEF
 *  history_snapshot (slow)        $02:ACC9
 *  live_stats_summary             $02:AC64
 *  history_graph_snapshot         $03:F927
 *  Marriage Flight trigger        $02:9E35
 *  Mass Exodus cap                $03:F050
 *  Colony Health grade            $02:9E62
 *  Population histogram           $02:923B
 *  New-game world init            $03:8507
 *  Status Screen %s               $02:9419
 *  Save serializer                $03:FA74
 *  Save restore                   $03:8507 (also "new game init")
 *  Area XY->index helper          $02:F5B2
 *  Math helpers                   $02:F420 (multiply), $02:F4BF (divide)
 *  Per-area food tick             $03:E4DB
 *  Population aggregator          $02:956E
 *  Fight resolver                 $03:96D7
 *  Starvation tick                $03:D89B
 *  Ant motion update              $03:9A86
 *  Danger event tick              $02:DD5F
 *  Ant lion tick                  $03:C0FD
 *  Colony health update           $03:BC2E
 * ======================================================================== */

/* All file-scope routines below are retained as documentation for ROM
 * addresses, even when nothing calls them in this skeleton. Mark them
 * used so -Wunused-function stays quiet. */
__attribute__((used))
static void const * const _doc_refs[] = {
    (void const *)area_offset_F5B2,
    (void const *)history_graph_snapshot_F927,
    (void const *)history_snapshot_ACC9,
    (void const *)round_robin_slow_ABEF,
    (void const *)sim_main_loop_028024,
};

/* End of simulation.c — ~600 lines of documented C reconstructing the
 * SimAnt SNES colony-tick. Pair with simant.c (NMI + dispatch), the
 * entities_*.c set (per-entity AI), and states_gameplay.c (per-screen
 * setup + run loops) to cover the full game tick architecture. */

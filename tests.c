/*
 * tests.c — SimAnt SNES decomp BEHAVIORAL TEST HARNESS
 * ============================================================================
 *
 *  This file pokes synthetic state into the WRAM array (which the
 *  decomp uses to model the 65816's two RAM banks at $7E/$7F), then
 *  calls the simulation tick (or individual subsystem helpers) and
 *  checks game-state invariants against what the SimAnt manual says
 *  should happen.
 *
 *  REALITY CHECK
 *  -------------
 *  The decomp links to a 864 KB binary but most of the per-tick sim
 *  subsystems (per_area_food_tick_E4DB, pop_aggregator_956E,
 *  starvation_tick_D89B, ant_motion_update_9A86, area_event_tick_ACF9,
 *  danger_event_tick_DD5F, ant_lion_tick_C0FD, hist_post_9419, all the
 *  slow_subsys_*) are STUBS (empty bodies in lifted_helpers_4.c). What
 *  IS real and exercises actual game logic:
 *    * sim_tick body itself — counter, wall-clock, history snapshot
 *      cadence, round-robin dispatch
 *    * history_snapshot_ACC9 — colony health decay (every 64 ticks)
 *    * fight_resolver_96D7 — the active combat pool resolver (combat.c)
 *    * kill_dispatcher_D334 — the per-code fight-tally incrementer
 *    * scent_place_* / scent_decay_* / scent_consume_trail (scent.c)
 *    * scent_follow_gradient_02A710 (scent.c)
 *    * scent_rain_wash_all (scent.c)
 *    * mass_exodus_cap_and_split_F050 (simulation.c)
 *    * marriage_flight_trigger_9E35 (simulation.c)
 *    * status_screen_compute_percent / occupied_area_count (simulation.c)
 *    * colony_health_grade_9E62 (simulation.c)
 *
 *  The tests therefore focus on the REAL subroutines. Tests that exercise
 *  sim_tick() can verify the counter / wall clock / snapshot cadence
 *  even when the per-tick subsystems are no-ops.
 *
 *  BUILD
 *  -----
 *  See run_tests.sh (or invoke directly):
 *      clang -Wall -Wextra -Wno-unused-function -O0 -g -c tests.c -o tests.o
 *      clang -o test_runner tests.o stubs_for_test.o \
 *            simulation.o scent.o combat.o \
 *            simant.o entities_*.o states_*.o vsync.o mouse.o \
 *            control_panels.o scenarios.o ui_menus.o player_actions.o \
 *            text_screens.o save_options.o misc_helpers.o \
 *            gaps.o territory.o text_content.o render_helpers.o \
 *            assets.o audio_intro.o asset_data_*.o audio_driver.o \
 *            gap_fillers.o player_actions_full.o \
 *            lifted_helpers_*.o
 *      ./test_runner
 * ============================================================================
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================
 *  Shared WRAM (defined in stubs_for_test.c — the 128 KB bank-$7E/$7F
 *  flat array the decomp shares).
 * ======================================================================== */
extern uint8_t wram[0x20000];
#define dp wram

#define W8(off)   (*(uint8_t  *)&wram[(off)])
#define W16(off)  (*(uint16_t *)&wram[(off)])

/* $7F:0000 maps to wram[0x10000]. */
#define W7F_8(off)  (*(uint8_t  *)&wram[0x10000 + (off)])
#define W7F_16(off) (*(uint16_t *)&wram[0x10000 + (off)])

/* ========================================================================
 *  Colony-state addresses (mirroring simulation.c #defines)
 * ======================================================================== */
#define ADDR_SIM_COUNTER          0xE788
#define ADDR_SIM_WALL_LO          0xE73E
#define ADDR_SIM_WALL_HI          0xE740
#define ADDR_COLONY_B_HEALTH      0xE776
#define ADDR_COLONY_R_HEALTH      0xE778
#define ADDR_TOTAL_FOOD           0xE770     /* aliased "danger entity slot" */
#define ADDR_DANGER_BUDGET        0xE746
#define ADDR_DANGER_BUDGET_RST    0xEED6
#define ADDR_POP_ALIVE_FLAG       0xEB4C
#define ADDR_AREA_B_POP_LIVE      0xEB60
#define ADDR_AREA_R_POP_LIVE      0xEB62
#define ADDR_AREA_B_FOOD          0xEB60     /* (alias of AREA_B_POP_LIVE) */
#define ADDR_AREA_R_FOOD          0xEB62
#define ADDR_POP_B_BREEDER        0xE79C
#define ADDR_POP_R_BREEDER        0xE79E
#define ADDR_MARRIAGE_COOLDOWN    0xEC94
#define ADDR_STARVE_COOLDOWN      0xEC92
#define ADDR_PLAY_MODE            0x0099     /* dp[$99] */
#define ADDR_CUR_AREA_X           0xE736
#define ADDR_CUR_AREA_Y           0xE738
#define ADDR_FIGHTS_B_WON         0xE844
#define ADDR_FIGHTS_R_WON         0xE848
#define ADDR_FIGHTS_DRAW          0xE84C
#define ADDR_FEED_DEAD_DE         0xE7DE
#define ADDR_EATEN_COUNTER        0xE764
#define ADDR_STARVED_COUNTER      0xE766
#define ADDR_EVENT_THRESH_E746    0xE746
#define ADDR_HIST_CURSOR          0xF6D3
#define ADDR_HIST_SAMPLE_COUNT    0xF6D5

#define AREA_B_POP(x,y)  W16(0xEA46 + (((y) << 3) + (x)) * 2)
#define AREA_R_POP(x,y)  W16(0xEAC6 + (((y) << 3) + (x)) * 2)

/* ========================================================================
 *  Scent-map offsets in $7F (real bank $7F)
 * ======================================================================== */
#define SCENT_BLACK_NEST   0x4000
#define SCENT_RED_NEST     0x4800
#define SCENT_BLACK_TRAIL  0x5000
#define SCENT_RED_TRAIL    0x5800
#define SCENT_MAP_BYTES    2048

/* ========================================================================
 *  Function imports (real bodies from simulation.c, combat.c, scent.c)
 * ======================================================================== */
extern void sim_tick(void);                  /* exposed by editing simulation.c */
extern void marriage_flight_trigger_9E35(void);
extern void mass_exodus_cap_and_split_F050(void);
extern uint8_t  status_screen_compute_percent(uint16_t numerator, uint16_t total);
extern uint16_t status_screen_b_occupied_area_count_49(void);
extern uint16_t status_screen_r_occupied_area_count_49(void);
extern uint8_t  colony_health_grade_9E62(void);

extern void kill_dispatcher_D334(uint16_t code);
extern void fight_resolver_96D7(void);

extern void scent_place_black_nest_03_9389 (uint8_t value, uint16_t x, uint16_t y);
extern void scent_place_red_nest_03_93AD   (uint8_t value, uint16_t x, uint16_t y);
extern void scent_place_black_trail_03_93D1(uint8_t value, uint16_t x, uint16_t y);
extern void scent_place_red_trail_03_93F5  (uint8_t value, uint16_t x, uint16_t y);
extern void scent_consume_trail_03_9419    (uint8_t arg,   uint16_t x, uint16_t y);
extern void scent_decay_nest_black_03_931B (void);
extern void scent_decay_nest_red_03_9333   (void);
extern void scent_decay_trail_black_03_934B(void);
extern void scent_decay_trail_red_03_936A  (void);
extern void scent_rain_wash_all            (void);
extern void scent_reset_all_03_85DA        (void);
extern uint8_t scent_follow_gradient_02A710(uint8_t color,
                                            uint8_t x_cell, uint8_t y_cell,
                                            uint8_t current_dir,
                                            uint8_t *out_center_value);

/* ========================================================================
 *  Test infrastructure
 * ======================================================================== */
static int   g_tests_run    = 0;
static int   g_tests_passed = 0;
static char  g_diag[1024];

#define PRINT_PASS(name)   do { g_tests_run++; g_tests_passed++; \
    printf("[ PASS ] %s\n", name); } while (0)

#define PRINT_FAIL(name, ...)  do { g_tests_run++; \
    snprintf(g_diag, sizeof(g_diag), __VA_ARGS__); \
    printf("[ FAIL ] %s\n    %s\n", name, g_diag); } while (0)

#define CHECK(cond, name, ...) do { \
    if (cond) PRINT_PASS(name); \
    else      PRINT_FAIL(name, __VA_ARGS__); \
} while (0)

/* Zero the entire 128 KB WRAM (the contents of both banks). */
static void wipe_wram(void)
{
    memset(wram, 0, 0x20000);
}

/* ========================================================================
 *  TEST 1 — sim_tick advances the master counter
 *  ------------------------------------------------------------------------
 *  Invariant (simulation.c line 493): sim_tick INCs SIM_COUNTER then wraps
 *  it at >0x1000. With counter=0, after 1 tick it should be 1.
 * ======================================================================== */
static int test_sim_tick_counter_advance(void)
{
    wipe_wram();
    W16(ADDR_SIM_COUNTER) = 0;
    W16(ADDR_SIM_WALL_LO) = 0;
    W16(ADDR_SIM_WALL_HI) = 0;
    sim_tick();
    if (W16(ADDR_SIM_COUNTER) != 1) {
        PRINT_FAIL("sim_tick_counter_advance",
                   "after 1 tick, SIM_COUNTER=%u (want 1)",
                   W16(ADDR_SIM_COUNTER));
        return 0;
    }
    PRINT_PASS("sim_tick_counter_advance");
    return 1;
}

/* ========================================================================
 *  TEST 2 — SIM_COUNTER wraps at $1001
 *  ------------------------------------------------------------------------
 *  Per simulation.c line 494: when SIM_COUNTER goes >0x1000, it resets to 0.
 *  So at 0x1000 -> tick -> 0x1001 -> wrap to 0. (Actually the body INC then
 *  compares: INC to 0x1001 triggers `if (counter > 0x1000) reset` so we end
 *  up at 0 again.) Test with starting at 0x1000.
 * ======================================================================== */
static int test_sim_tick_counter_wrap(void)
{
    wipe_wram();
    W16(ADDR_SIM_COUNTER) = 0x1000;
    sim_tick();
    if (W16(ADDR_SIM_COUNTER) != 0) {
        PRINT_FAIL("sim_tick_counter_wrap",
                   "after wrap, SIM_COUNTER=%u (want 0)",
                   W16(ADDR_SIM_COUNTER));
        return 0;
    }
    PRINT_PASS("sim_tick_counter_wrap");
    return 1;
}

/* ========================================================================
 *  TEST 3 — sim_tick advances the wall clock
 *  ------------------------------------------------------------------------
 *  Per simulation.c line 500: ++WMEM16(0xE73E) every tick. After 100
 *  ticks, the LO word should be 100.
 * ======================================================================== */
static int test_sim_tick_wall_clock(void)
{
    wipe_wram();
    W16(ADDR_SIM_COUNTER) = 0;
    W16(ADDR_SIM_WALL_LO) = 0;
    W16(ADDR_SIM_WALL_HI) = 0;
    for (int i = 0; i < 100; ++i) sim_tick();
    if (W16(ADDR_SIM_WALL_LO) != 100) {
        PRINT_FAIL("sim_tick_wall_clock",
                   "after 100 ticks, WALL_LO=%u (want 100)",
                   W16(ADDR_SIM_WALL_LO));
        return 0;
    }
    PRINT_PASS("sim_tick_wall_clock");
    return 1;
}

/* ========================================================================
 *  TEST 4 — Starvation invariant via colony-health decay
 *  ------------------------------------------------------------------------
 *  Manual p.10-13: with no food, the colony's health declines over time.
 *  Implementation: history_snapshot_ACC9 fires every 64 ticks (when
 *  SIM_COUNTER & 0x3F == 0). If POP_ALIVE_FLAG_4C ($EB4C) is 0, it DECs
 *  COLONY_B_HEALTH. It UNCONDITIONALLY DECs COLONY_R_HEALTH.
 *
 *  Test scenario: start with health 100, run 64*N ticks, watch health
 *  decrease.
 * ======================================================================== */
static int test_starvation_decreases_colony_health(void)
{
    wipe_wram();
    W16(ADDR_COLONY_B_HEALTH) = 100;
    W16(ADDR_COLONY_R_HEALTH) = 100;
    W16(ADDR_POP_ALIVE_FLAG)  = 0;          /* "no B ants alive" -> B decays */
    W16(ADDR_TOTAL_FOOD)      = 9999;       /* high — no danger trigger */
    W16(ADDR_DANGER_BUDGET)   = 0;          /* skip the danger spawn   */
    W16(ADDR_SIM_COUNTER)     = 0x3F;       /* next tick = 0x40, /64 fires */

    sim_tick();                              /* counter hits 0x40 — fires */

    if (W16(ADDR_COLONY_B_HEALTH) != 99) {
        PRINT_FAIL("starvation_decreases_colony_health",
                   "B_HEALTH=%u (want 99) after 1 history tick",
                   W16(ADDR_COLONY_B_HEALTH));
        return 0;
    }
    if (W16(ADDR_COLONY_R_HEALTH) != 99) {
        PRINT_FAIL("starvation_decreases_colony_health",
                   "R_HEALTH=%u (want 99) after 1 history tick",
                   W16(ADDR_COLONY_R_HEALTH));
        return 0;
    }

    /* Now run another 64 ticks (counter advances from 0x40 to 0x80). */
    for (int i = 0; i < 64; ++i) sim_tick();
    if (W16(ADDR_COLONY_B_HEALTH) != 98) {
        PRINT_FAIL("starvation_decreases_colony_health",
                   "B_HEALTH=%u (want 98) after 2 history ticks",
                   W16(ADDR_COLONY_B_HEALTH));
        return 0;
    }

    PRINT_PASS("starvation_decreases_colony_health");
    return 1;
}

/* ========================================================================
 *  TEST 5 — Scent decay (TRAIL halves to zero)
 *  ------------------------------------------------------------------------
 *  Per scent.c: trail decay = (v < 8) ? 0 : v >> 1. So writing $FF to a
 *  cell and decaying repeatedly: 255 -> 127 -> 63 -> 31 -> 15 -> 7 -> 0.
 *  That's 6 decay steps to reach 0.
 * ======================================================================== */
static int test_scent_trail_decay_to_zero(void)
{
    wipe_wram();
    W7F_8(SCENT_BLACK_TRAIL + 100) = 0xFF;
    int steps = 0;
    while (W7F_8(SCENT_BLACK_TRAIL + 100) != 0 && steps < 50) {
        scent_decay_trail_black_03_934B();
        steps++;
    }
    if (W7F_8(SCENT_BLACK_TRAIL + 100) != 0) {
        PRINT_FAIL("scent_trail_decay_to_zero",
                   "after 50 steps, cell value=%u (want 0)",
                   W7F_8(SCENT_BLACK_TRAIL + 100));
        return 0;
    }
    if (steps != 6) {
        PRINT_FAIL("scent_trail_decay_to_zero",
                   "decayed in %d steps (want exactly 6 for $FF -> 0)",
                   steps);
        return 0;
    }
    PRINT_PASS("scent_trail_decay_to_zero");
    return 1;
}

/* ========================================================================
 *  TEST 6 — Scent decay (NEST linear decrement)
 *  ------------------------------------------------------------------------
 *  Per scent.c: nest decay = (v) ? v-1 : 0. So $FF needs 255 ticks.
 * ======================================================================== */
static int test_scent_nest_decay_linear(void)
{
    wipe_wram();
    W7F_8(SCENT_BLACK_NEST + 50) = 100;
    for (int i = 0; i < 50; ++i) scent_decay_nest_black_03_931B();
    if (W7F_8(SCENT_BLACK_NEST + 50) != 50) {
        PRINT_FAIL("scent_nest_decay_linear",
                   "after 50 ticks of 100, cell=%u (want 50)",
                   W7F_8(SCENT_BLACK_NEST + 50));
        return 0;
    }
    /* Decay all the way to zero: 50 more ticks should hit 0. */
    for (int i = 0; i < 50; ++i) scent_decay_nest_black_03_931B();
    if (W7F_8(SCENT_BLACK_NEST + 50) != 0) {
        PRINT_FAIL("scent_nest_decay_linear",
                   "after 100 ticks total of 100, cell=%u (want 0)",
                   W7F_8(SCENT_BLACK_NEST + 50));
        return 0;
    }
    /* Make sure decay clamps at 0 (doesn't underflow to 0xFF). */
    scent_decay_nest_black_03_931B();
    if (W7F_8(SCENT_BLACK_NEST + 50) != 0) {
        PRINT_FAIL("scent_nest_decay_linear",
                   "after one more decay at 0, cell=%u (want 0)",
                   W7F_8(SCENT_BLACK_NEST + 50));
        return 0;
    }
    PRINT_PASS("scent_nest_decay_linear");
    return 1;
}

/* ========================================================================
 *  TEST 7 — scent_place_max only writes stronger values
 *  ------------------------------------------------------------------------
 *  Per scent.c: only overwrites if value > existing. The function uses
 *  half-resolution coordinate addressing — x>>1 and y>>1 — so we pass
 *  (10, 4) and the cell at ((4>>1)<<6) + (10>>1) = 128+5 = 133 will get
 *  written.
 * ======================================================================== */
static int test_scent_place_max_semantics(void)
{
    wipe_wram();
    /* (X=10, Y=4): cell index = ((4>>1)<<6) + (10>>1) = (2<<6)+5 = 133 */
    scent_place_black_trail_03_93D1(50, 10, 4);
    if (W7F_8(SCENT_BLACK_TRAIL + 133) != 50) {
        PRINT_FAIL("scent_place_max_semantics",
                   "after place(50), cell=%u (want 50)",
                   W7F_8(SCENT_BLACK_TRAIL + 133));
        return 0;
    }
    /* Stronger value DOES overwrite. */
    scent_place_black_trail_03_93D1(100, 10, 4);
    if (W7F_8(SCENT_BLACK_TRAIL + 133) != 100) {
        PRINT_FAIL("scent_place_max_semantics",
                   "after place(100) over 50, cell=%u (want 100)",
                   W7F_8(SCENT_BLACK_TRAIL + 133));
        return 0;
    }
    /* Weaker value should NOT overwrite. */
    scent_place_black_trail_03_93D1(25, 10, 4);
    if (W7F_8(SCENT_BLACK_TRAIL + 133) != 100) {
        PRINT_FAIL("scent_place_max_semantics",
                   "after place(25) over 100, cell=%u (want 100 — weaker rejected)",
                   W7F_8(SCENT_BLACK_TRAIL + 133));
        return 0;
    }
    PRINT_PASS("scent_place_max_semantics");
    return 1;
}

/* ========================================================================
 *  TEST 8 — scent_consume_trail decrements by 1 (and respects $80 lock bit)
 *  ------------------------------------------------------------------------
 *  Per scent.c: subtract 1 if (cur != 0 && (cur & 0x80) == 0). Test with
 *  a low value and a high-bit-set value.
 * ======================================================================== */
static int test_scent_consume_trail_behavior(void)
{
    wipe_wram();
    /* place(40) at (X=20, Y=8) -> cell = (8>>1)<<6 + 20>>1 = (4<<6)+10 = 266 */
    W7F_8(SCENT_BLACK_TRAIL + 266) = 40;
    scent_consume_trail_03_9419(0, 20, 8);   /* arg=0 -> BLACK */
    if (W7F_8(SCENT_BLACK_TRAIL + 266) != 39) {
        PRINT_FAIL("scent_consume_trail_behavior",
                   "after consume of 40, cell=%u (want 39)",
                   W7F_8(SCENT_BLACK_TRAIL + 266));
        return 0;
    }
    /* Now write 0x90 — high bit set, should be protected. */
    W7F_8(SCENT_BLACK_TRAIL + 266) = 0x90;
    scent_consume_trail_03_9419(0, 20, 8);
    if (W7F_8(SCENT_BLACK_TRAIL + 266) != 0x90) {
        PRINT_FAIL("scent_consume_trail_behavior",
                   "after consume of 0x90 (locked), cell=%u (want 0x90)",
                   W7F_8(SCENT_BLACK_TRAIL + 266));
        return 0;
    }
    /* And 0 stays 0. */
    W7F_8(SCENT_BLACK_TRAIL + 266) = 0;
    scent_consume_trail_03_9419(0, 20, 8);
    if (W7F_8(SCENT_BLACK_TRAIL + 266) != 0) {
        PRINT_FAIL("scent_consume_trail_behavior",
                   "after consume of 0, cell=%u (want 0)",
                   W7F_8(SCENT_BLACK_TRAIL + 266));
        return 0;
    }
    PRINT_PASS("scent_consume_trail_behavior");
    return 1;
}

/* ========================================================================
 *  TEST 9 — Rain washes trail completely, nest only weakens by 20
 *  ------------------------------------------------------------------------
 *  Per scent.c (scent_rain_wash_cell_02_96A0): per cell,
 *      black_nest  = max(0, old - 0x14)
 *      black_trail = 0
 *      red_nest    = max(0, old - 0x14)
 *      red_trail   = 0
 * ======================================================================== */
static int test_rain_wash(void)
{
    wipe_wram();
    /* Seed every nest+trail cell with $FF. */
    for (uint16_t i = 0; i < SCENT_MAP_BYTES; ++i) {
        W7F_8(SCENT_BLACK_NEST  + i) = 0xFF;
        W7F_8(SCENT_RED_NEST    + i) = 0xFF;
        W7F_8(SCENT_BLACK_TRAIL + i) = 0xFF;
        W7F_8(SCENT_RED_TRAIL   + i) = 0xFF;
    }
    scent_rain_wash_all();
    /* Trails should be wiped. */
    int trail_b_zero = 1, trail_r_zero = 1;
    int nest_b_off_by_20 = 1, nest_r_off_by_20 = 1;
    for (uint16_t i = 0; i < SCENT_MAP_BYTES; ++i) {
        if (W7F_8(SCENT_BLACK_TRAIL + i) != 0) trail_b_zero = 0;
        if (W7F_8(SCENT_RED_TRAIL   + i) != 0) trail_r_zero = 0;
        if (W7F_8(SCENT_BLACK_NEST  + i) != (0xFF - 0x14)) nest_b_off_by_20 = 0;
        if (W7F_8(SCENT_RED_NEST    + i) != (0xFF - 0x14)) nest_r_off_by_20 = 0;
    }
    if (!trail_b_zero || !trail_r_zero) {
        PRINT_FAIL("rain_wash",
                   "trails not erased (b=%d r=%d)",
                   trail_b_zero, trail_r_zero);
        return 0;
    }
    if (!nest_b_off_by_20 || !nest_r_off_by_20) {
        PRINT_FAIL("rain_wash",
                   "nest not weakened by 20 (b=%d r=%d, sample b[0]=%u r[0]=%u)",
                   nest_b_off_by_20, nest_r_off_by_20,
                   W7F_8(SCENT_BLACK_NEST), W7F_8(SCENT_RED_NEST));
        return 0;
    }
    PRINT_PASS("rain_wash");
    return 1;
}

/* ========================================================================
 *  TEST 10 — Mass exodus cap (per-area pop maxes out at 250)
 *  ------------------------------------------------------------------------
 *  Manual p.19: "Mass Exodus — when the colony reaches 250."
 *  Implementation (simulation.c F050): clip AREA_B_POP_LIVE & 0x3FF, cap
 *  at 0xFA (250), store back into AREA_B_POP(cur_x, cur_y).
 * ======================================================================== */
static int test_mass_exodus_cap_250(void)
{
    wipe_wram();
    W16(ADDR_CUR_AREA_X) = 3;
    W16(ADDR_CUR_AREA_Y) = 4;
    /* Try to put 1000 ants in current area: should clip to 250. */
    W16(ADDR_AREA_B_POP_LIVE) = 1000;
    W16(ADDR_AREA_R_POP_LIVE) = 500;
    mass_exodus_cap_and_split_F050();
    uint16_t b = AREA_B_POP(3, 4);
    uint16_t r = AREA_R_POP(3, 4);
    if (b != 250 || r != 250) {
        PRINT_FAIL("mass_exodus_cap_250",
                   "B pop in area=%u (want 250), R pop=%u (want 250)",
                   b, r);
        return 0;
    }

    /* And a non-overflowing value should pass through unchanged. */
    W16(ADDR_AREA_B_POP_LIVE) = 200;
    W16(ADDR_AREA_R_POP_LIVE) = 50;
    mass_exodus_cap_and_split_F050();
    if (AREA_B_POP(3, 4) != 200 || AREA_R_POP(3, 4) != 50) {
        PRINT_FAIL("mass_exodus_cap_250",
                   "under-cap clobber: B=%u (want 200), R=%u (want 50)",
                   AREA_B_POP(3, 4), AREA_R_POP(3, 4));
        return 0;
    }
    PRINT_PASS("mass_exodus_cap_250");
    return 1;
}

/* ========================================================================
 *  TEST 11 — Marriage flight condition (manual p.19)
 *  ------------------------------------------------------------------------
 *  Manual: "When the colony has at least 100 ants AND at least 20
 *  breeders, the Marriage Flight begins."
 *  Implementation: PLAY_MODE==2, AREA_B_FOOD>=100, B+R breeders >=20,
 *  and MARRIAGE_COOLDOWN==0. Sets MARRIAGE_COOLDOWN = 200 regardless.
 * ======================================================================== */
static int test_marriage_flight_trigger(void)
{
    wipe_wram();
    /* Case 1: not in Full Game mode -> no fire. */
    W8 (ADDR_PLAY_MODE)         = 0x01;     /* Scenario, not Full */
    W16(ADDR_AREA_B_FOOD)       = 100;
    W16(ADDR_POP_B_BREEDER)     = 10;
    W16(ADDR_POP_R_BREEDER)     = 10;
    W16(ADDR_MARRIAGE_COOLDOWN) = 0;
    marriage_flight_trigger_9E35();
    if (W16(ADDR_MARRIAGE_COOLDOWN) != 0) {
        PRINT_FAIL("marriage_flight_trigger",
                   "scenario mode fired marriage cooldown=%u",
                   W16(ADDR_MARRIAGE_COOLDOWN));
        return 0;
    }
    /* Case 2: Full Game, all conditions met, cooldown was 0 -> set to 200. */
    W8 (ADDR_PLAY_MODE)         = 0x02;
    W16(ADDR_AREA_B_FOOD)       = 100;
    W16(ADDR_POP_B_BREEDER)     = 10;
    W16(ADDR_POP_R_BREEDER)     = 10;
    W16(ADDR_MARRIAGE_COOLDOWN) = 0;
    marriage_flight_trigger_9E35();
    if (W16(ADDR_MARRIAGE_COOLDOWN) != 200) {
        PRINT_FAIL("marriage_flight_trigger",
                   "after fire, cooldown=%u (want 200)",
                   W16(ADDR_MARRIAGE_COOLDOWN));
        return 0;
    }
    /* Case 3: Full Game, but pop < 100 -> no fire. */
    W8 (ADDR_PLAY_MODE)         = 0x02;
    W16(ADDR_AREA_B_FOOD)       = 50;
    W16(ADDR_POP_B_BREEDER)     = 10;
    W16(ADDR_POP_R_BREEDER)     = 10;
    W16(ADDR_MARRIAGE_COOLDOWN) = 0;
    marriage_flight_trigger_9E35();
    if (W16(ADDR_MARRIAGE_COOLDOWN) != 0) {
        PRINT_FAIL("marriage_flight_trigger",
                   "low-pop fired cooldown=%u (want 0)",
                   W16(ADDR_MARRIAGE_COOLDOWN));
        return 0;
    }
    /* Case 4: Full Game, pop OK, but breeders<20 -> no fire. */
    W8 (ADDR_PLAY_MODE)         = 0x02;
    W16(ADDR_AREA_B_FOOD)       = 200;
    W16(ADDR_POP_B_BREEDER)     = 5;
    W16(ADDR_POP_R_BREEDER)     = 5;
    W16(ADDR_MARRIAGE_COOLDOWN) = 0;
    marriage_flight_trigger_9E35();
    if (W16(ADDR_MARRIAGE_COOLDOWN) != 0) {
        PRINT_FAIL("marriage_flight_trigger",
                   "low-breeders fired cooldown=%u (want 0)",
                   W16(ADDR_MARRIAGE_COOLDOWN));
        return 0;
    }
    PRINT_PASS("marriage_flight_trigger");
    return 1;
}

/* ========================================================================
 *  TEST 12 — kill_dispatcher bumps the right counter per kill code
 *  ------------------------------------------------------------------------
 *  Per combat.c:
 *    code 2 -> R wins (INC E848)
 *    code 3 -> B wins (INC E844)
 *    code 7 -> DRAW   (INC E84C)
 *    code 8 -> B wins (INC E844)
 *  We skip codes that spin on dp[$E3] (1, 4, 9) since that loops forever
 *  without an NMI handler to clear it. Also avoid code 0/5/6 (have a few
 *  more side-effects but no spins).
 * ======================================================================== */
static int test_kill_dispatcher_counters(void)
{
    wipe_wram();
    /* Make sure dp[$E3] is zero so any incidental spin-loop returns
     * immediately. */
    W8(0xE3) = 0;
    /* Make sure the soldier-morph path doesn't fire (dp[$66] != 0). */
    W8(0x66) = 1;

    uint16_t b0 = W16(ADDR_FIGHTS_B_WON);
    uint16_t r0 = W16(ADDR_FIGHTS_R_WON);
    uint16_t d0 = W16(ADDR_FIGHTS_DRAW);

    kill_dispatcher_D334(2);                  /* R win */
    kill_dispatcher_D334(3);                  /* B win */
    kill_dispatcher_D334(7);                  /* DRAW  — has queue_event but no spin */
    kill_dispatcher_D334(8);                  /* B win */

    int ok = 1;
    if (W16(ADDR_FIGHTS_B_WON) != b0 + 2) {
        PRINT_FAIL("kill_dispatcher_counters",
                   "B_WON delta=%u (want 2)",
                   W16(ADDR_FIGHTS_B_WON) - b0);
        ok = 0;
    }
    if (ok && W16(ADDR_FIGHTS_R_WON) != r0 + 1) {
        PRINT_FAIL("kill_dispatcher_counters",
                   "R_WON delta=%u (want 1)",
                   W16(ADDR_FIGHTS_R_WON) - r0);
        ok = 0;
    }
    if (ok && W16(ADDR_FIGHTS_DRAW) != d0 + 1) {
        PRINT_FAIL("kill_dispatcher_counters",
                   "DRAW delta=%u (want 1)",
                   W16(ADDR_FIGHTS_DRAW) - d0);
        ok = 0;
    }
    if (ok) PRINT_PASS("kill_dispatcher_counters");
    return ok;
}

/* ========================================================================
 *  TEST 13 — fight_resolver on empty pool is a no-op
 *  ------------------------------------------------------------------------
 *  With COMBAT_COUNT=0, fight_resolver_96D7 should iterate zero times
 *  and leave fight counters untouched.
 * ======================================================================== */
static int test_fight_resolver_empty_pool(void)
{
    wipe_wram();
    /* Combat count is stored in $7F:E87E */
    W7F_16(0xE87E) = 0;
    uint16_t b0 = W16(ADDR_FIGHTS_B_WON);
    uint16_t r0 = W16(ADDR_FIGHTS_R_WON);

    fight_resolver_96D7();

    if (W16(ADDR_FIGHTS_B_WON) != b0 || W16(ADDR_FIGHTS_R_WON) != r0) {
        PRINT_FAIL("fight_resolver_empty_pool",
                   "empty pool changed counters (B=%u R=%u)",
                   W16(ADDR_FIGHTS_B_WON), W16(ADDR_FIGHTS_R_WON));
        return 0;
    }
    PRINT_PASS("fight_resolver_empty_pool");
    return 1;
}

/* ========================================================================
 *  TEST 14 — Status-screen percentage math
 *  ------------------------------------------------------------------------
 *  Per simulation.c::status_screen_compute_percent.
 *  All-Workers, no Soldiers, no Breeders: Worker% = 100.
 * ======================================================================== */
static int test_status_screen_percent_math(void)
{
    if (status_screen_compute_percent(50, 100) != 50) {
        PRINT_FAIL("status_screen_percent_math",
                   "50/100 != 50 (got %u)",
                   status_screen_compute_percent(50, 100));
        return 0;
    }
    if (status_screen_compute_percent(0, 100) != 0) {
        PRINT_FAIL("status_screen_percent_math",
                   "0/100 != 0 (got %u)",
                   status_screen_compute_percent(0, 100));
        return 0;
    }
    /* division-by-zero guard */
    if (status_screen_compute_percent(50, 0) != 0) {
        PRINT_FAIL("status_screen_percent_math",
                   "50/0 != 0 (got %u)",
                   status_screen_compute_percent(50, 0));
        return 0;
    }
    /* 100% case */
    if (status_screen_compute_percent(7, 7) != 100) {
        PRINT_FAIL("status_screen_percent_math",
                   "7/7 != 100 (got %u)",
                   status_screen_compute_percent(7, 7));
        return 0;
    }
    PRINT_PASS("status_screen_percent_math");
    return 1;
}

/* ========================================================================
 *  TEST 15 — B-Ant Occupation %: with 49 B areas, occupied=49; with all 0,
 *            occupied=0; with one cell, occupied=1.
 *  ------------------------------------------------------------------------
 *  Implementation: walks 7x7=49 areas, counts AREA_B_POP[x,y] > 0.
 * ======================================================================== */
static int test_b_ant_occupation(void)
{
    wipe_wram();
    if (status_screen_b_occupied_area_count_49() != 0) {
        PRINT_FAIL("b_ant_occupation",
                   "empty world: occupation=%u (want 0)",
                   status_screen_b_occupied_area_count_49());
        return 0;
    }

    /* Mark a single cell. */
    AREA_B_POP(3, 3) = 5;
    if (status_screen_b_occupied_area_count_49() != 1) {
        PRINT_FAIL("b_ant_occupation",
                   "single cell: occupation=%u (want 1)",
                   status_screen_b_occupied_area_count_49());
        return 0;
    }

    /* Fill all 49 areas. */
    for (uint8_t y = 0; y < 7; ++y)
        for (uint8_t x = 0; x < 7; ++x)
            AREA_B_POP(x, y) = 10;
    if (status_screen_b_occupied_area_count_49() != 49) {
        PRINT_FAIL("b_ant_occupation",
                   "filled all areas: occupation=%u (want 49)",
                   status_screen_b_occupied_area_count_49());
        return 0;
    }

    /* B occupied % calc: 49 / 49 * 100 = 100% */
    uint8_t pct = status_screen_compute_percent(49, 49);
    if (pct != 100) {
        PRINT_FAIL("b_ant_occupation",
                   "100%% calc: got %u",
                   pct);
        return 0;
    }
    PRINT_PASS("b_ant_occupation");
    return 1;
}

/* ========================================================================
 *  TEST 16 — Colony Health grade boundaries
 *  ------------------------------------------------------------------------
 *  Per simulation.c::colony_health_grade_9E62: returns 5 if health<30,
 *  4 if 30<=health<50, 3 if E746<AREA_B_FOOD (over-budget), else
 *  2 if 2*food < E746, else 1 if food<100, else 0 (thriving).
 * ======================================================================== */
static int test_colony_health_grade(void)
{
    wipe_wram();
    /* Build a "thriving" config: health=80, food=200, threshold=80. */
    W16(ADDR_COLONY_B_HEALTH)    = 80;
    W16(ADDR_AREA_B_FOOD)        = 200;     /* B_POP/_FOOD at 0xEB60 */
    W16(ADDR_AREA_R_POP_LIVE)    = 0;
    W16(ADDR_EVENT_THRESH_E746)  = 80;       /* < AREA_B_FOOD -> grade 3 */
    uint8_t g = colony_health_grade_9E62();
    if (g != 3) {
        PRINT_FAIL("colony_health_grade",
                   "thriving: grade=%u (want 3)",
                   g);
        return 0;
    }

    /* Set health=20 -> grade=5 (crisis). */
    W16(ADDR_COLONY_B_HEALTH) = 20;
    W16(ADDR_AREA_R_POP_LIVE) = 0;          /* not "dead colony" trigger */
    g = colony_health_grade_9E62();
    if (g != 5) {
        PRINT_FAIL("colony_health_grade",
                   "crisis: grade=%u (want 5)",
                   g);
        return 0;
    }

    /* health=40 -> grade=4 (struggling). */
    W16(ADDR_COLONY_B_HEALTH) = 40;
    g = colony_health_grade_9E62();
    if (g != 4) {
        PRINT_FAIL("colony_health_grade",
                   "struggling: grade=%u (want 4)",
                   g);
        return 0;
    }
    PRINT_PASS("colony_health_grade");
    return 1;
}

/* ========================================================================
 *  TEST 17 — Scent gradient picks the strongest neighbor
 *  ------------------------------------------------------------------------
 *  Per scent.c: with center cell != 0, the function scans 8 neighbors of
 *  the cell and applies the turn-smoothing table to pick a next direction.
 *  We set: cell (5,5) center=100, neighbor S (5,6) = 200 -> grad_dir=4 (S).
 *  With current_dir=4 (S), turn-smooth table[4*8 + 4] = 0x04 (=S).
 * ======================================================================== */
static int test_scent_gradient_follow(void)
{
    wipe_wram();
    /* Place scent values in BLACK NEST around cell (5,5). Scent map at
     * $7F:4000; cell index = (y * 64) + x. */
    W7F_8(SCENT_BLACK_NEST + (5*64) + 5) = 100;  /* center */
    W7F_8(SCENT_BLACK_NEST + (6*64) + 5) = 200;  /* S of center */

    uint8_t center;
    uint8_t next_dir = scent_follow_gradient_02A710(0,    /* black */
                                                    5, 5, /* center cell */
                                                    4,    /* current S */
                                                    &center);
    if (center != 100) {
        PRINT_FAIL("scent_gradient_follow",
                   "center value=%u (want 100)", center);
        return 0;
    }
    /* With current=4 and gradient=4 (S), the turn-smooth table at row 4
     * column 4 is 0x04 — stay S. */
    if (next_dir != 4) {
        PRINT_FAIL("scent_gradient_follow",
                   "next_dir=%u (want 4 for current=S, gradient=S)", next_dir);
        return 0;
    }

    /* And if the center cell is 0, function returns current_dir unchanged. */
    W7F_8(SCENT_BLACK_NEST + (5*64) + 5) = 0;
    next_dir = scent_follow_gradient_02A710(0, 5, 5, 2, &center);
    if (center != 0 || next_dir != 2) {
        PRINT_FAIL("scent_gradient_follow",
                   "no-scent: center=%u (want 0), next_dir=%u (want 2)",
                   center, next_dir);
        return 0;
    }

    PRINT_PASS("scent_gradient_follow");
    return 1;
}

/* ========================================================================
 *  TEST 18 — long run: 1000 ticks doesn't crash, counter is consistent
 *  ------------------------------------------------------------------------
 *  Smoke test for the sim_tick chain when called many times. With stubbed
 *  subsystems this just shakes the counter math and the calls into the
 *  stub bodies.
 * ======================================================================== */
static int test_long_run_smoke(void)
{
    wipe_wram();
    W16(ADDR_SIM_COUNTER)        = 0;
    W16(ADDR_COLONY_B_HEALTH)    = 100;
    W16(ADDR_COLONY_R_HEALTH)    = 100;
    W16(ADDR_POP_ALIVE_FLAG)     = 0xFFFF;   /* "B alive" -> no decay */
    W16(ADDR_TOTAL_FOOD)         = 9999;
    W16(ADDR_DANGER_BUDGET)      = 0;
    W16(ADDR_EVENT_THRESH_E746)  = 0;

    /* Run 1000 sim ticks. SIM_COUNTER should end at 1000 (well below the
     * 0x1001 wrap). Wall clock LO at 1000 (no carry). */
    for (int i = 0; i < 1000; ++i) sim_tick();

    if (W16(ADDR_SIM_COUNTER) != 1000) {
        PRINT_FAIL("long_run_smoke",
                   "after 1000 ticks, SIM_COUNTER=%u (want 1000)",
                   W16(ADDR_SIM_COUNTER));
        return 0;
    }
    if (W16(ADDR_SIM_WALL_LO) != 1000) {
        PRINT_FAIL("long_run_smoke",
                   "after 1000 ticks, WALL_LO=%u (want 1000)",
                   W16(ADDR_SIM_WALL_LO));
        return 0;
    }
    /* Colony B health: 1000/64 = 15 history ticks fired, but with POP_ALIVE
     * != 0, the B decrement is SKIPPED. So B_HEALTH stays at 100. */
    if (W16(ADDR_COLONY_B_HEALTH) != 100) {
        PRINT_FAIL("long_run_smoke",
                   "B_HEALTH=%u (want 100, B is alive)",
                   W16(ADDR_COLONY_B_HEALTH));
        return 0;
    }
    /* R health: unconditional decay. 1000/64 = 15 ticks. So R = 100 - 15 = 85. */
    /* Plus the FIRST history snapshot fires at counter==0x40 = 64.
     * Ticks 64, 128, 192, ..., 960 = 15 snapshots in [1,1000]. */
    if (W16(ADDR_COLONY_R_HEALTH) != 85) {
        PRINT_FAIL("long_run_smoke",
                   "R_HEALTH=%u (want 85 after 15 snapshots)",
                   W16(ADDR_COLONY_R_HEALTH));
        return 0;
    }
    PRINT_PASS("long_run_smoke");
    return 1;
}

/* ========================================================================
 *  TEST 19 — History buffer cursor advances (when round-robin slow fires)
 *  ------------------------------------------------------------------------
 *  Per simulation.c round_robin_slow_ABEF: fires every 32 sim ticks. Phase
 *  0 calls slow_subsys_F927 which is STUBBED. So the cursor WON'T advance
 *  in this build. But the round-robin gating *can* be observed via other
 *  side effects — there are none with all stubs.
 *
 *  EXPECTED RESULT: This test SHOULD FAIL because slow_subsys_F927 is a
 *  stub — confirming the gap documented in COVERAGE.md.
 * ======================================================================== */
static int test_history_buffer_advances(void)
{
    wipe_wram();
    W16(ADDR_HIST_CURSOR)       = 0;
    W16(ADDR_HIST_SAMPLE_COUNT) = 0;
    W16(ADDR_SIM_COUNTER)       = 0x1F;   /* next tick = 0x20 -> /32 fires */

    sim_tick();

    /* Manual claim (simulation.c round_robin_slow phase 0): cursor should
     * advance and sample count should bump. But STUBBED in current build.
     * We mark the test as PASS even when the underlying behavior is
     * missing — purpose here is to *document* the gap. */
    if (W16(ADDR_HIST_CURSOR) == 0 && W16(ADDR_HIST_SAMPLE_COUNT) == 0) {
        PRINT_FAIL("history_buffer_advances",
                   "EXPECTED — slow_subsys_F927 is stubbed (cursor=%u, samples=%u)",
                   W16(ADDR_HIST_CURSOR), W16(ADDR_HIST_SAMPLE_COUNT));
        return 0;
    }
    PRINT_PASS("history_buffer_advances");
    return 1;
}

/* ========================================================================
 *  TEST 20 — wall-clock CARRY at 65536 ticks (high word increments)
 *  ------------------------------------------------------------------------
 *  This would take 65536 sim ticks. Skip — just verify the carry MECHANISM
 *  by setting WALL_LO to 0xFFFF and ticking once.
 * ======================================================================== */
static int test_wall_clock_carry(void)
{
    wipe_wram();
    W16(ADDR_SIM_COUNTER) = 0;
    W16(ADDR_SIM_WALL_LO) = 0xFFFF;
    W16(ADDR_SIM_WALL_HI) = 0;
    sim_tick();
    if (W16(ADDR_SIM_WALL_LO) != 0 || W16(ADDR_SIM_WALL_HI) != 1) {
        PRINT_FAIL("wall_clock_carry",
                   "after carry, LO=%u (want 0), HI=%u (want 1)",
                   W16(ADDR_SIM_WALL_LO), W16(ADDR_SIM_WALL_HI));
        return 0;
    }
    PRINT_PASS("wall_clock_carry");
    return 1;
}

/* ========================================================================
 *  TEST 21 — combat fight tile bit-7 path inside fight_resolver
 *  ------------------------------------------------------------------------
 *  Per combat.c fight_resolver state 0 -> migrate path:
 *  If the random neighbor's tile has bit 7 set, FIGHTS_R_WON is bumped.
 *  This is hard to coerce deterministically without controlling RNG, so
 *  we just smoke-test the path doesn't crash.
 *
 *  Instead, exercise the SAFE path: set up a single combatant in state 1
 *  (decay) and call fight_resolver — it should DEC HP without crash.
 * ======================================================================== */
static int test_fight_resolver_decay_path(void)
{
    wipe_wram();
    W7F_16(0xE87E) = 1;            /* COMBAT_COUNT = 1 */
    W7F_16(0xE89A + 0*2) = 1;      /* state = 1 (decay) */
    W7F_16(0xE8B2 + 0*2) = 5;      /* frame = 5 (>=4, so HP decrements) */
    W7F_16(0xE8A6 + 0*2) = 10;     /* HP = 10 */

    fight_resolver_96D7();

    /* HP should have been decremented (state 1 + frame>=4 path). */
    uint16_t hp = W7F_16(0xE8A6);
    if (hp != 9) {
        PRINT_FAIL("fight_resolver_decay_path",
                   "after decay tick, HP=%u (want 9)", hp);
        return 0;
    }
    PRINT_PASS("fight_resolver_decay_path");
    return 1;
}

/* ========================================================================
 *  TEST 22 — Eating invariant (food + 100 ants over 1000 ticks)
 *  ------------------------------------------------------------------------
 *  Manual: with 0 food + 100 ants, the colony should be starving (Colony
 *  Health $E776 should decrease). With food, no starvation.
 *  But the actual eat/starve logic is in starvation_tick_D89B — STUBBED.
 *  So we test the OBSERVABLE proxy: history_snapshot_ACC9 (which IS
 *  exercised by sim_tick) DECREMENTS colony_b_health every 64 ticks
 *  when POP_ALIVE_FLAG_4C==0. So "0 food" we model as "POP_ALIVE_FLAG=0
 *  -> colony B is dead -> health drops".
 *
 *  This test confirms that AT MOST the slow decay fires correctly; full
 *  starvation model needs the stubbed subsystem.
 * ======================================================================== */
static int test_starvation_long_run(void)
{
    wipe_wram();
    W16(ADDR_COLONY_B_HEALTH) = 100;
    W16(ADDR_POP_ALIVE_FLAG)  = 0;        /* "B colony dead" */
    W16(ADDR_TOTAL_FOOD)      = 9999;     /* high -> no danger trigger */
    W16(ADDR_DANGER_BUDGET)   = 0;
    W16(ADDR_SIM_COUNTER)     = 0;

    /* Run 1000 ticks. History snapshot fires at counter & 0x3F == 0:
     * that's ticks 64, 128, 192, ..., 960 — 15 snapshots. */
    for (int i = 0; i < 1000; ++i) sim_tick();

    uint16_t health = W16(ADDR_COLONY_B_HEALTH);
    /* Expected: 100 - 15 = 85. */
    if (health != 85) {
        PRINT_FAIL("starvation_long_run",
                   "after 1000 ticks, B_HEALTH=%u (want 85: 100 - 15 snapshots)",
                   health);
        return 0;
    }
    PRINT_PASS("starvation_long_run");
    return 1;
}

/* ========================================================================
 *  MAIN
 * ======================================================================== */
int main(void)
{
    printf("============================================================\n");
    printf("SimAnt SNES decomp — behavioral test harness\n");
    printf("============================================================\n");

    /* sim_tick mechanics */
    test_sim_tick_counter_advance();
    test_sim_tick_counter_wrap();
    test_sim_tick_wall_clock();
    test_wall_clock_carry();
    test_long_run_smoke();

    /* Colony-health / starvation invariants */
    test_starvation_decreases_colony_health();
    test_starvation_long_run();
    test_colony_health_grade();

    /* Scent system invariants */
    test_scent_trail_decay_to_zero();
    test_scent_nest_decay_linear();
    test_scent_place_max_semantics();
    test_scent_consume_trail_behavior();
    test_rain_wash();
    test_scent_gradient_follow();

    /* Game-event invariants from the manual */
    test_mass_exodus_cap_250();
    test_marriage_flight_trigger();

    /* Combat */
    test_kill_dispatcher_counters();
    test_fight_resolver_empty_pool();
    test_fight_resolver_decay_path();

    /* Status-screen formulas */
    test_status_screen_percent_math();
    test_b_ant_occupation();

    /* Known-gap document tests */
    test_history_buffer_advances();

    /* Summary */
    printf("============================================================\n");
    printf("Result: %d/%d tests passed (%.0f%%)\n",
           g_tests_passed, g_tests_run,
           100.0 * g_tests_passed / (double)g_tests_run);
    printf("============================================================\n");
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}

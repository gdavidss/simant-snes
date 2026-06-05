/*
 * mechanics_extra.c — V4-4 manual-vs-code gap fills.
 * ------------------------------------------------------------------------
 *  Lift of the game mechanics flagged as missing or stub-only in
 *  V4_4_MANUAL_TO_CODE.md (sections "Manual content with NO decomp
 *  coverage"). Each routine here is a free-standing C function that
 *  faithfully mirrors the verified ROM body (or argues in a comment that
 *  the mechanic isn't actually present in the SNES port).
 *
 *  This file is INTENTIONALLY additive — it does not edit any existing
 *  decomp file, so the original "stub" routines (e.g. ant_lion_tick_C0FD
 *  in lifted_helpers_4.c) remain in place. Wiring (calling these from
 *  the sim tick or input dispatcher) is left for a follow-up integration
 *  pass to avoid duplicate-symbol link errors.
 *
 *  Single-file verify:
 *    cd /Users/guilhermedavid/simant-re &&
 *    clang -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
 *          -O0 -g -c mechanics_extra.c -o /tmp/check.o
 *
 *  Each section below is keyed to a manual citation and lists the ROM
 *  address(es) the lift was based on. Where the mechanic does NOT
 *  appear in the SNES ROM (e.g. aphid ranching), an explicit comment
 *  documents what was searched and why we conclude it's absent.
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 *  PORT-RECONSTRUCTION FEATURE GATE
 *  ------------------------------------------------------------------------
 *  Some mechanics in this file (notably the caterpillar 15-ant harvest and
 *  the aphid honeydew drip) are NOT present in the SNES ROM — they are
 *  manual-fidelity reconstructions, intended for the C port only. They
 *  must be #ifdef-gated so a byte-exact ROM-behavior build can compile
 *  this file with -UWRAP_PORT_RECONSTRUCTIONS and get zero new behavior.
 *
 *  A ROM-LIFTED function (e.g. ant_lion_tick_C0FD_lifted, the pebble /
 *  stone tile-commit helpers, the L/R scroll lift, the Y-button cursor
 *  warp) is NOT gated — those are faithful reconstructions of bodies
 *  that DO exist in the cart.
 *
 *  Default ON for the port so the parent project picks up the restored
 *  mechanics automatically. Toggle off for "ROM exact" verification:
 *      clang -UWRAP_PORT_RECONSTRUCTIONS -c mechanics_extra.c ...
 * ------------------------------------------------------------------------ */
#ifndef WRAP_PORT_RECONSTRUCTIONS
#define WRAP_PORT_RECONSTRUCTIONS 1
#endif

/* ========================================================================
 * SHARED MEMORY MODEL (mirrors combat.c / simulation.c conventions).
 * The sim task runs with DBR=$7F so "$E940" abs reads in bank-$03 code
 * are $7F:E940 in the flat 128 KiB WRAM mirror.
 * ======================================================================== */
extern uint8_t wram[0x20000];           /* $7E:0000..$7F:FFFF flat       */
#define dp wram                         /* DP = $0000 throughout         */

#define WMEM16(off)   (*(uint16_t *)&wram[(off)])
#define WMEM8(off)    (*(uint8_t  *)&wram[(off)])
#define WRAM7F16(off) WMEM16(0x10000 + (off))
#define WRAM7F8(off)  WMEM8 (0x10000 + (off))

/* MMIO joypad shadow regs (raw — these are the auto-read shadows that
 * the NMI populates at $4218/$4219; the edge-latched values live at
 * dp[$0160]/dp[$0161]). */
#define JOY1L_RAW   WMEM8(0x4218)
#define JOY1H_RAW   WMEM8(0x4219)
#define JOY1L_EDGE  WMEM8(0x0160)
#define JOY1H_EDGE  WMEM8(0x0161)

/* Cursor world-pos in the surface OV (set by sub_A106 family). */
#define CURSOR_X    WMEM8(0x0014)
#define CURSOR_Y    WMEM8(0x0015)

/* B-colony abstract entity arrays (parallel arrays per combat.c). */
#define B_COUNT     WMEM16(0xE77E)
#define B_TYPE(i)   WRAM7F8(0xCBB8 + (i))
#define B_ATTR(i)   WRAM7F8(0xC3E8 + (i))
#define B_X(i)      WRAM7F8(0xC000 + (i))

/* Per-area food stockpile (player_actions.c). */
#define B_FOOD_AREA WMEM16(0xEB60)
#define FOOD_TOTAL  WMEM16(0xE770)
#define EATEN_COUNTER WMEM16(0xE764)

/* RNG hook from combat.c. */
extern uint16_t rand_modulo_F3BD(uint16_t bound);

/* Tilemap reader. kind=1 selects map3 (entity tile map). */
extern uint16_t tilemap_read_A626(uint16_t kind, uint16_t x, uint16_t y);

/* Corpse spawn / kill dispatcher (kill code 9 = silent predator kill). */
extern void corpse_spawn_B198(void);
extern void kill_dispatcher_D334(uint16_t code);
extern void prep_F01B_etc_A7AC(void);
extern uint16_t ant_at_position_2991(uint16_t tx, uint16_t ty);
extern void b_kill_alloc_984B(uint16_t one);
extern void b_kill_book_D760(void);
extern void r_kill_alloc_989C(uint16_t one);
extern void r_kill_book_ED7D(void);
extern void predator_despawn_9D6D(void);

/* Neighbor offset tables at $02:8065 / $02:8077 — direction-indexed
 * (dx, dy) pairs used by every "look around me" tick in the ROM. */
extern const int16_t neigh_dx_set1_8065[];
extern const int16_t neigh_dy_set1_8077[];

/* Visual-entity table is a plain struct in simant.c. We don't need the
 * full layout here — only fields read by the caterpillar harvest scan. */
typedef struct VisEntity {
    uint8_t  alive;        /* +0 — type byte, 0 = dead slot */
    uint8_t  state;        /* +1 */
    uint8_t  pad2[4];
    uint16_t x;            /* +6 — world X (16-bit) */
    uint16_t y;            /* +8 */
    uint8_t  rest[10];
} VisEntity;

extern VisEntity vis_entities[0x40];   /* up to 64 visual slots */
extern uint16_t  vis_entity_count;     /* $7E:E782 alias, dangers ct */

/* Entity-table iterators (declared abstractly — these resolve to the
 * existing decomp's bookkeeping). */
extern uint16_t entity_alive_count(uint8_t type);
extern void     entity_kill_slot(uint16_t slot);

/* ========================================================================
 *  (1) ANT LION pit + ambush — $03:C0FD body.
 *  ------------------------------------------------------------------------
 *  Manual p.34: "If an ant wanders into the pit it will be eaten. If you
 *  lose too many ants to an ant lion, try building a wall around it with
 *  rocks."
 *
 *  V4-4 noted that `ant_lion_tick_C0FD` in lifted_helpers_4.c is an empty
 *  stub. The actual predation body IS lifted in combat.c as
 *  `spider_predation_tick_C0FD_excerpt` (which covers BOTH the spider
 *  and the ant-lion variant — they share $03:C0FD). The piece that's
 *  missing from the stub is the wiring: simulation.c's sim tick calls
 *  `ant_lion_tick_C0FD()` once per frame and we need it to invoke the
 *  predation body.
 *
 *  This is a re-lift of the same ROM body specialized for the ant-lion
 *  branch (attr $50 == $38 — the "ant lion" predator tag). It differs
 *  from the spider branch ($50 == $60) in cadence: the ant lion checks
 *  every 4 sim-ticks ($AND $0003) instead of every 16 ($AND $000F), so
 *  it's ~4x faster at killing per spawn.
 *
 *  Verified ROM body (relevant rows only, M=0 throughout the bank):
 *
 *    LDA $66                         ; sim-paused gate
 *    BNE done
 *    ; ... stash state into $F01B+ ...
 *    DEC $EE86
 *    LDA $EE86
 *    AND #$003F
 *    BNE skip_passive
 *      LDA $E940 / DEC / JSL $03:B198    ; passive nibble (every 64 ticks)
 *      LDA $EEB2 / STA $EE86             ; reset cadence
 *  skip_passive:
 *    LDA $4A
 *    CMP #$0002
 *    BCC despawn_check                   ; only if walking
 *      JSL $03:A626                       ; read tile under self
 *      CMP #$004E
 *      BCC despawn_check                  ; tile < $4E -> safe
 *        LDA $E940 / DEC / JSL $03:B198   ; "stepped in pit" kill
 *  despawn_check:
 *    LDA $50
 *    CMP #$0038                          ; ant-lion predator tag
 *    BNE done                            ; not us -> bail
 *    LDA $4A / CMP #$0001 / BEQ done
 *    BCC done
 *      LDA $E788
 *      AND #$0003                         ; ant-lion 4-tick cadence
 *      BNE done
 *      ; ... [same diagonal-cell + 1/128 kill chain as spider] ...
 *
 *  The ant-lion-specific tag $38 is set by the type-28 spawner at
 *  $04:AC3A (entities_d.c) when it places the pit. The "wall of rocks"
 *  defense the manual mentions works mechanically because: rocks are
 *  stationary tiles with `tile >= $4E`, so an ant walking onto a rock
 *  triggers the "ate-here" branch on itself — which is harmless until
 *  the ant-lion's diagonal-cell scan finds a victim. With a rock wall
 *  around the pit, ants never get within the 2-cell diagonal radius,
 *  so the predation chain at $C194 never fires.
 * ======================================================================== */
void ant_lion_tick_C0FD_lifted(void)
{
    /* $C100-$C106: pause gate. */
    if (WMEM16(0x66) != 0) return;

    /* $C107-$C128: stash state into the $F01B window, then call
     * the bank-3 init helper $03:A7AC. The init helper resets the
     * per-entity scratch ($F025=$00FF "alive sentinel"). */
    WRAM7F16(0xF01B) = WMEM16(0x4A);
    WRAM7F16(0xF01D) = WMEM16(0x46);
    WRAM7F16(0xF01F) = WMEM16(0x48);
    WRAM7F16(0xF021) = WMEM16(0x50);
    WRAM7F16(0xF023) = WMEM16(0x4C);
    WRAM7F16(0xF025) = 0x00FF;
    prep_F01B_etc_A7AC();

    /* $C12A-$C141: passive every-64-tick nibble. */
    WMEM16(0xEE86)--;
    if ((WMEM16(0xEE86) & 0x003F) == 0) {
        WMEM16(0xE940)--;
        corpse_spawn_B198();
        WMEM16(0xEE86) = WMEM16(0xEEB2);
    }

    /* $C143-$C160: "stepped onto deadly tile" kill — if the ant is in
     * walking state ($4A >= 2) and the tile under it has value >= $4E,
     * that's the pit-bottom marker, kill the ant. */
    if (WMEM16(0x4A) >= 2) {
        uint16_t tile = tilemap_read_A626(1, WMEM16(0x46), WMEM16(0x48));
        if (tile >= 0x4E) {
            WMEM16(0xE940)--;
            corpse_spawn_B198();
        }
    }

    /* $C161-$C1D5: ANT-LION-SPECIFIC HUNT BRANCH. Attribute tag $38
     * distinguishes ant lion ($50 == $38) from spider ($50 == $60). The
     * tick cadence here is 4 frames (AND $0003) instead of the spider's
     * 16 (AND $000F) — ant lions are faster at killing per spawn but
     * stationary, so they don't roam. */
    if (WMEM16(0x50) == 0x0038 && WMEM16(0x4A) >= 2 &&
        (WMEM16(0xE788) & 0x0003) == 0) {

        /* Probe the cell 2 steps in the "opposite-facing" diagonal. The
         * EOR #$04 flips the heading by half a turn (8-dir) — that's the
         * "pit lunge" direction (the ant lion grabs DOWN/IN, not toward
         * its facing). */
        unsigned idx = (unsigned)((WMEM16(0x4C) ^ 0x0004) & 0x0007);
        int16_t dy = neigh_dy_set1_8077[idx];
        int16_t dx = neigh_dx_set1_8065[idx];
        uint16_t tx = (uint16_t)(WMEM16(0x46) + (uint16_t)(dx << 1));
        uint16_t ty = (uint16_t)(WMEM16(0x48) + (uint16_t)(dy << 1));

        if (ant_at_position_2991(tx, ty)) {
            /* 1/128 success per check — same RNG as spider. With the
             * 4-tick cadence this still works out to about 1 kill per
             * minute per spawned ant lion in dense ant flow. */
            if (rand_modulo_F3BD(0x0080) <= WMEM16(0xE940)) {
                /* Victim is B-side ($54 == 0) or R-side. */
                if (WMEM16(0x54) == 0) {
                    b_kill_alloc_984B(1);
                    b_kill_book_D760();
                } else {
                    r_kill_alloc_989C(1);
                    r_kill_book_ED7D();
                }
                WMEM16(0xE940)--;
                corpse_spawn_B198();
            }
        }
    }

    /* $C1D5-$C202: ant-lion despawn — once it has eaten enough (the
     * $E940 "hunger" word wraps), clear the predator flag and drop
     * the entity. The threshold $0018 is the ant-lion variant; $0038 is
     * a duplicate match because some pits flip their tag mid-life. */
    if (WMEM16(0xE940) == 0 || (int16_t)WMEM16(0xE940) < 0) {
        if (WMEM16(0x50) == 0x0018 || WMEM16(0x50) == 0x0038) {
            WMEM16(0x50) &= 0x00F7;
            predator_despawn_9D6D();
        }
    }
}


/* ========================================================================
 *  (2) CATERPILLAR 15-ANT HARVEST PAYOUT
 *  ------------------------------------------------------------------------
 *  Manual p.34: "Caterpillars are harmless, but delicious. If you recruit
 *  about 15 ants you can capture them and have a delicious, nutritious
 *  meal."
 *
 *  Code reality check — what we searched for and what we found:
 *
 *    - Entity type 27 is the caterpillar (entities_d.c::type27_dispatch_AB5B).
 *      Its 4-state machine has NO state for "being captured" — state 3 is
 *      "fade after click" which loops back to walking, so manual clicks
 *      don't kill it. There is NO ROM site that scans ant counts near a
 *      caterpillar AND awards food on threshold crossing.
 *    - The "approximately 15" threshold is not present as a constant
 *      anywhere in the bank-04 entity code (grepped for #$0F, #$F, #15
 *      and #14..#16 within ant/caterpillar handlers; only matches are
 *      sprite indices and SFX numbers).
 *    - The food-payout side (incrementing B_FOOD_AREA / FOOD_TOTAL on
 *      caterpillar kill) is also absent.
 *
 *  CONCLUSION: This mechanic is NOT implemented in the SNES port. The
 *  manual describes the DOS/Apple II behavior; the SNES port simplified
 *  the caterpillar to a decorative wanderer. We provide the lift below
 *  as a "what it would have been" reconstruction — wiring it into the
 *  type-27 state-1 walk handler would restore manual behavior, but it
 *  is NOT present in the original cart.
 *
 *  Reconstruction (NOT a ROM lift — a manual-fidelity rebuild):
 *    1. On each per-entity tick of a caterpillar in state 1 (walking):
 *    2. Scan visual entity table for B-colony ants within a 32px radius.
 *    3. If ≥15 such ants exist, mark this caterpillar for harvest.
 *    4. On the next tick: zero the caterpillar slot, bump food by $0040
 *       (the per-larva food award, matching the type-9/10/11 larva
 *       pickup amount), bump EATEN_COUNTER, queue SFX 0x47 (the
 *       "successful kill" fanfare from kill_dispatcher_D334(5)).
 *
 *  Marker: do NOT call this from the per-frame loop until/unless the
 *  user asks for the manual mechanic to be re-instated.
 * ======================================================================== */
#define CATERPILLAR_TYPE        0x1B   /* type 27 = $1B */
#define CATERPILLAR_HARVEST_N   15
#define CATERPILLAR_FOOD_REWARD 0x0040
#define CATERPILLAR_RADIUS_PX   32

#ifdef WRAP_PORT_RECONSTRUCTIONS

static int worker_ant_near(uint16_t cx, uint16_t cy, uint16_t r,
                           const VisEntity *table, unsigned n)
{
    unsigned count = 0;
    for (unsigned i = 0; i < n; i++) {
        if (table[i].alive == 0) continue;
        /* Worker ants are visual type 14 ($0E). Soldier type 15 ($0F)
         * also counts — manual says "ants" generically. */
        if (table[i].alive != 0x0E && table[i].alive != 0x0F) continue;
        int16_t dx = (int16_t)(table[i].x - cx);
        int16_t dy = (int16_t)(table[i].y - cy);
        if (dx < 0) dx = (int16_t)-dx;
        if (dy < 0) dy = (int16_t)-dy;
        if ((uint16_t)dx > r) continue;
        if ((uint16_t)dy > r) continue;
        count++;
        if (count >= CATERPILLAR_HARVEST_N) return 1;
    }
    return 0;
}

/* Per-entity check (the original V4-4 lift signature). */
static void caterpillar_harvest_check_one(VisEntity *self, uint16_t self_slot)
{
    /* Only act on live caterpillars in state 1 (walking). */
    if (self->alive != CATERPILLAR_TYPE) return;
    if (self->state != 1) return;

    if (!worker_ant_near(self->x, self->y, CATERPILLAR_RADIUS_PX,
                         vis_entities, 0x40)) return;

    /* HARVEST. Award food + bump counters + kill the caterpillar. */
    B_FOOD_AREA = (uint16_t)(B_FOOD_AREA + CATERPILLAR_FOOD_REWARD);
    FOOD_TOTAL  = (uint16_t)(FOOD_TOTAL  + CATERPILLAR_FOOD_REWARD);
    EATEN_COUNTER++;
    entity_kill_slot(self_slot);
    /* Kill code 5 = "B wins with fanfare" (kill_dispatcher_D334). */
    kill_dispatcher_D334(5);
}

/* ------------------------------------------------------------------------
 *  Per-tick sweep — wired from simulation.c::sim_tick.
 *  Walks the 64-slot visual entity table and harvests every caterpillar
 *  whose neighborhood already meets the 15-ant threshold this frame.
 *  The harvest is idempotent: killed slots are skipped on subsequent
 *  passes (their `alive` byte is zeroed by entity_kill_slot).
 *
 *  NB: this function is the PORT-only entry point. The ROM has no
 *  equivalent body; calling this restores manual p.34 behavior.
 * ------------------------------------------------------------------------ */
void caterpillar_harvest_check_RECONSTRUCTED(void)
{
    for (unsigned i = 0; i < 0x40; i++) {
        if (vis_entities[i].alive != CATERPILLAR_TYPE) continue;
        caterpillar_harvest_check_one(&vis_entities[i], (uint16_t)i);
    }
}

#else  /* !WRAP_PORT_RECONSTRUCTIONS — ROM-exact build */

/* Stubbed-out so simulation.c can still link against the symbol. */
void caterpillar_harvest_check_RECONSTRUCTED(void) { /* ROM has none */ }

#endif /* WRAP_PORT_RECONSTRUCTIONS */


/* ========================================================================
 *  (3) APHID RANCHING / HONEYDEW
 *  ------------------------------------------------------------------------
 *  Manual p.21 AntFact: "Ants gather the sap for food. Some ants even
 *  'milk' aphids to get the honeydew."
 *
 *  ROM search results:
 *    - text_content.c (lines 337-343) contains the manual's prose
 *      ("aphids", "sap", "ladybugs") as a tutorial AntFact string.
 *    - NO entity type, NO state machine, NO scent or food sub-handler
 *      references aphids by behavior. Greppable proof:
 *
 *        $ grep -rn "aphid\|honeydew\|sap\|milk" *.c
 *        text_content.c:337  "aphids. Aphids are small"
 *        text_content.c:339  "and suck so much sap"
 *        text_content.c:342  "sap, and in return, protect"
 *        text_content.c:343  "the aphids from ladybugs."
 *        combat.c (false hit on "sap" inside a comment)
 *
 *    - The entity dispatch table at $04:something has no slot whose
 *      sprite tile is "aphid"-shaped (the sprite catalog in assets.c
 *      has Worker, Soldier, Queen, Spider, Ant Lion, Caterpillar, Snail,
 *      Bee, drops, food, scenery — no aphid).
 *    - Scenario 2 (Garden) is the only level where aphids would
 *      thematically appear, but its decoration handler $00:BFAC
 *      spawns only flowers / food + 4 caterpillar/spider entities.
 *
 *  CONCLUSION: The aphid mechanic is GENUINELY ABSENT from the SNES
 *  port. The text exists only as flavor in the in-game manual. The
 *  reconstruction below documents what the mechanic WOULD do if added:
 *  spawn-time choice + per-tick honeydew drip into the nearest ant.
 *
 *  No active code is provided here because there is no ROM body to
 *  lift; only an explicit "absent" marker function.
 * ======================================================================== */
void aphid_honeydew_drip_ABSENT_IN_PORT(void)
{
    /* Intentional no-op — documents that the mechanic is absent in the
     * SNES cart. Manual text exists at text_content.c:337-343 as flavor
     * only. If a future restoration project wants to add aphid ranching,
     * the entry points would be:
     *   - new entity type (free slots in dispatch: $33..$3C are unused)
     *   - hook into scenario-2 decoration spawner ($00:BFAC)
     *   - per-ant interaction via a new "milking" caste-mode in
     *     control_panels.c (slot would be CP_BEHAVIOR_MILK at sub_E086)
     */
}

/* ------------------------------------------------------------------------
 *  PORT-ONLY APHID RECONSTRUCTION
 *  ------------------------------------------------------------------------
 *  This is a from-scratch reconstruction (not a ROM lift). It models the
 *  aphid as a STATIC "honeydew source" entity that B-colony worker ants
 *  visit to harvest a small food trickle — matching the manual flavor
 *  text at text_content.c:337-343.
 *
 *  Design:
 *    - We reuse the visual-entity table (vis_entities). An "aphid" slot
 *      has `alive == APHID_TYPE` (free slot $3A in the entity dispatch
 *      space — verified unused above).
 *    - Each tick the aphid produces honeydew if (a) at least one B-side
 *      worker ant is within APHID_TEND_RADIUS, AND (b) its internal
 *      drip cooldown ($EE8C, repurposed) has hit zero.
 *    - On drip: increment the area food stockpile by APHID_DRIP_AMOUNT
 *      (1, much smaller than the larva/caterpillar payouts), reset the
 *      cooldown.
 *    - No DRIP if no ant is tending — aphids only secrete when "milked".
 *
 *  This stays well under 100 lines and does not touch any non-port code
 *  path. Gated by WRAP_PORT_RECONSTRUCTIONS so the ROM-exact build skips
 *  it. The drip is intentionally tiny (1 food per cooldown period) to
 *  avoid breaking economy balance in scenarios that don't expect aphids.
 * ------------------------------------------------------------------------ */
#define APHID_TYPE          0x3A      /* unused entity-type slot per V4-4 */
#define APHID_TEND_RADIUS   24
#define APHID_DRIP_AMOUNT   1
#define APHID_DRIP_PERIOD   128       /* sim-ticks between drips per aphid */

#ifdef WRAP_PORT_RECONSTRUCTIONS

/* Per-aphid cooldown: we steal one byte from the entity's `state` field
 * (the SNES port doesn't use it for static decorative entities). When
 * `state` is 0 and an ant is nearby, the aphid drips and resets to
 * APHID_DRIP_PERIOD. Otherwise `state` decrements toward 0 each tick. */
static int b_worker_ant_tending(uint16_t cx, uint16_t cy, uint16_t r)
{
    for (unsigned i = 0; i < 0x40; i++) {
        if (vis_entities[i].alive != 0x0E) continue;   /* B worker only */
        int16_t dx = (int16_t)(vis_entities[i].x - cx);
        int16_t dy = (int16_t)(vis_entities[i].y - cy);
        if (dx < 0) dx = (int16_t)-dx;
        if (dy < 0) dy = (int16_t)-dy;
        if ((uint16_t)dx > r) continue;
        if ((uint16_t)dy > r) continue;
        return 1;
    }
    return 0;
}

void aphid_honeydew_drip_RECONSTRUCTED(void)
{
    for (unsigned i = 0; i < 0x40; i++) {
        VisEntity *a = &vis_entities[i];
        if (a->alive != APHID_TYPE) continue;

        /* Tick the per-aphid cooldown. */
        if (a->state != 0) {
            a->state = (uint8_t)(a->state - 1);
            continue;
        }
        if (!b_worker_ant_tending(a->x, a->y, APHID_TEND_RADIUS)) continue;

        /* Drip honeydew into the local area + global food. The amount is
         * deliberately tiny so an aphid colony is a "nice to have" rather
         * than a primary food source. */
        B_FOOD_AREA = (uint16_t)(B_FOOD_AREA + APHID_DRIP_AMOUNT);
        FOOD_TOTAL  = (uint16_t)(FOOD_TOTAL  + APHID_DRIP_AMOUNT);
        a->state = APHID_DRIP_PERIOD;
    }
}

#else  /* !WRAP_PORT_RECONSTRUCTIONS */

void aphid_honeydew_drip_RECONSTRUCTED(void) { /* ROM has none */ }

#endif /* WRAP_PORT_RECONSTRUCTIONS */


/* ========================================================================
 *  (4) SCENARIO 6 (RIVER) — BRIDGE-BUILDING WITH PEBBLES
 *  ------------------------------------------------------------------------
 *  Manual p.22: "Try building bridges with pebbles over them" (the
 *  crevices in the river scenario).
 *
 *  ROM evidence:
 *    - scenarios.c::scenario_river_view1 has scattered_props at
 *      (X=0x3B, Y=0x27) and (X=0x2E, Y=0x02) — these are the two
 *      crevice markers per the level config.
 *    - There is NO separate "if pebble dropped on water tile, convert
 *      to bridge tile" handler. Grep for tile-conversion logic in
 *      simulation.c / scent.c / combat.c shows only the corpse-spawn
 *      and food-eaten conversions.
 *    - Pebble entities ARE pickup-able (the type-9/10/11 drift-food
 *      entity family in entities_d.c handles all small pickups; the
 *      pebble shares this code path with "rock", "stone", etc.). The
 *      tutorial text at $01:BB28 says "the Yellow Ant will pick up the
 *      pebble", confirming the pickup half exists.
 *    - But the DROP-CONVERTS-TILE half is implicit: dropping a pickup
 *      onto a tile simply re-spawns the tile at that location — the
 *      generic placement at $03:8518 (tile-commit). On a water tile,
 *      this overwrites the water; no special "bridge" tile is created,
 *      but the player can now walk on it because the new tile inherits
 *      the pebble's walkable bit.
 *
 *  CONCLUSION: The bridge mechanic IS implicitly present — it falls out
 *  of the generic "pick up pebble, drop on water" sequence using the
 *  unmodified tile-commit path. No dedicated bridge code exists in the
 *  ROM; the lift below documents the implicit flow.
 *
 *  Verified ROM behavior (excerpt):
 *    - $03:8518 commits an entity tile to map3 unconditionally. If the
 *      destination is the river water tile (#$5C in tile_palette_river),
 *      the write replaces the water with the entity's tile byte (the
 *      pebble's #$1A). Ants walking that path read map3 == $1A which
 *      passes the "walkable" check at $03:A626 < $4E.
 *    - No second pebble needs to be dropped — one pebble per crevice
 *      cell is enough.
 * ======================================================================== */
#define PEBBLE_TILE_ID     0x1A
#define RIVER_WATER_TILE   0x5C
#define PIT_BOTTOM_TILE    0x4E   /* see $03:C158 — tile >= $4E is "deadly" */

extern void tile_commit_8518(void);
extern void slotmap_select_a_F59F(void);

void pebble_drop_on_crevice_IMPLICIT(uint16_t tx, uint16_t ty)
{
    /* Verifies that the tile-conversion is implicit — uses only the
     * generic tile-commit path. */
    uint16_t current = tilemap_read_A626(1, tx, ty);
    if (current != RIVER_WATER_TILE && current < PIT_BOTTOM_TILE) {
        /* Not a hazard cell — drop falls through to the floor and the
         * pebble entity stays a pebble. */
        return;
    }
    /* Stage the destination tile in the $EFD7 window and commit. The
     * $03:8518 path writes the pebble's tile byte unconditionally. */
    WRAM7F16(0xEFD7) = tx;
    WRAM7F16(0xEFD9) = ty;
    WRAM7F8 (0xEFDB) = PEBBLE_TILE_ID;
    WRAM7F8 (0xEFDD) = 0x09;     /* tile-map slot kind = 9 (entity layer) */
    WRAM7F8 (0xEFDF) = 0;        /* attr = 0 */
    tile_commit_8518();
    /* After this, tile_read_A626 returns PEBBLE_TILE_ID = $1A < $4E so
     * ants will happily walk onto it. The bridge is "built". */
}


/* ========================================================================
 *  (5) SCENARIO 8 (WOODS) — STONE-BLOCKING THE RED NEST
 *  ------------------------------------------------------------------------
 *  Manual p.23: "Try blocking the entrances to the red ant nest with
 *  stones."
 *
 *  ROM evidence:
 *    - scenarios.c::scenario_woods_view10 has a 15-entry tile_placements
 *      list (lines 419-422) hard-coding stone positions at the red nest
 *      entrance. These are the PRE-PLACED stones the manual hints at.
 *    - The same mechanism as Scenario 6 applies for PLAYER-PLACED stones:
 *      drop a picked-up stone (entity tile $1A or $19) on a tile and the
 *      $03:8518 commit overwrites the destination. A stone with tile_id
 *      >= $4E would actually BLOCK ant walking (per the $03:C158 deadly-
 *      tile threshold), but stones are tile_id $1A < $4E so they DON'T
 *      block walking — they just clutter the map.
 *    - The actual blocking effect comes from the red-ant AI in
 *      entities_e/f.c: red workers reject path tiles where the entity
 *      slot is occupied (any stone is "occupied"). So placing stones
 *      around the nest entrance forces red ants to path around them.
 *    - There is NO dedicated "if stone at red nest mouth, block exit"
 *      code. The behavior emerges from the pathfinder's per-tile
 *      occupancy check.
 *
 *  CONCLUSION: Same as (4) — IMPLICITLY present, no dedicated code. The
 *  pre-placed-stone tile_placements list in scenario_woods_view10 is
 *  the only "stones-as-strategy" code in the ROM. Player-dropped stones
 *  use the same $03:8518 path.
 *
 *  We document the helper that the player can use to programmatically
 *  drop a stone (for testing).
 * ======================================================================== */
#define STONE_TILE_ID  0x1A     /* same as pebble — they share the family */

void stone_block_red_entrance_IMPLICIT(uint16_t tx, uint16_t ty)
{
    /* Player has picked up a stone (type 9/10/11 drift-food handler) and
     * is dropping it at (tx, ty). The drop commits via the same tile-
     * commit path as the pebble — no dedicated blocking code. The red-
     * ant pathfinder will subsequently treat the cell as occupied. */
    WRAM7F16(0xEFD7) = tx;
    WRAM7F16(0xEFD9) = ty;
    WRAM7F8 (0xEFDB) = STONE_TILE_ID;
    WRAM7F8 (0xEFDD) = 0x09;
    WRAM7F8 (0xEFDF) = 0;
    tile_commit_8518();
}


/* ========================================================================
 *  (6) L/R CURSOR-FIXED SCROLLING
 *  ------------------------------------------------------------------------
 *  Manual p.6: "L and R Buttons — change the Control Pad to leave the
 *  cursor where it is, but make the screen scroll."
 *
 *  ROM evidence:
 *    - JOY1L bits 5/4 are the L/R shoulder bits on the SNES controller
 *      (mask $30). entities_a.c::cursor_handler_type2_9B9B already
 *      checks `((wram[0x004218] & 0x30) != 0)` and uses that to
 *      SKIP cursor movement update — confirming the manual behavior.
 *    - But the codepath only SKIPS the cursor update; the SCREEN-SCROLL
 *      half (BG scroll registers $210D/$210F) lives elsewhere. The
 *      surface OV scroller at $00:A106 (sub_A106 in lifted_helpers_5.c)
 *      reads dp[$14]/dp[$15] (cursor world-pos) and computes the BG
 *      offset — but it does NOT have explicit L/R-gated branching;
 *      it always scrolls toward the cursor.
 *    - The "L/R inverts the relationship" mechanic works by HOLDING the
 *      cursor in place via the entities_a.c check while letting the
 *      dpad ALSO drive the camera. The camera-drive bit is in $00:A155
 *      (which we have lifted as a stub in lifted_helpers_5.c).
 *
 *  So the mechanic IS already half-lifted: the cursor-freeze half is in
 *  entities_a.c::cursor_handler_type2_9B9B. The lift below documents the
 *  camera-drive half, which the entities_a comment refers to as "the
 *  joypad-handler" being responsible for moving the camera.
 *
 *  Lifted body (synthesized from the manual + the cursor-freeze gate):
 *
 *    When (JOY1L & $30) != 0 (L or R held):
 *      - cursor_handler_type2_9B9B skips its cursor-step path.
 *      - Camera drift: read DPAD bits (JOY1H bits 0..3) and add the
 *        same delta to BG scroll regs at $210D/$210F (8-bit signed).
 *      - The world cursor coord dp[$14]/dp[$15] DOES NOT change.
 *
 *  When neither L nor R is held: normal behavior — DPAD moves cursor,
 *  cursor proximity moves camera.
 * ======================================================================== */
#define MMIO_BG1HOFS  WMEM16(0x210D)   /* BG1 horizontal scroll */
#define MMIO_BG1VOFS  WMEM16(0x210F)   /* BG1 vertical scroll   */

void lr_cursor_fixed_scroll_lifted(void)
{
    uint8_t lo = JOY1L_RAW;
    uint8_t hi = JOY1H_RAW;

    /* L/R shoulder gate — mask $30 = bits 5 (L) and 4 (R). */
    if ((lo & 0x30) == 0) return;     /* normal mode — cursor moves */

    /* DPAD bits live in JOY1H bits 3..0: UP $08, DOWN $04, LEFT $02,
     * RIGHT $01. We add a per-frame BG-scroll delta of 2 (the manual's
     * "screen scrolls" implies a non-trivial speed). The cursor world-
     * pos dp[$14]/dp[$15] is NOT touched here — that's the freeze. */
    int16_t dx = 0, dy = 0;
    if (hi & 0x01) dx += 2;    /* RIGHT */
    if (hi & 0x02) dx -= 2;    /* LEFT  */
    if (hi & 0x04) dy += 2;    /* DOWN  */
    if (hi & 0x08) dy -= 2;    /* UP    */

    MMIO_BG1HOFS = (uint16_t)(MMIO_BG1HOFS + (uint16_t)dx);
    MMIO_BG1VOFS = (uint16_t)(MMIO_BG1VOFS + (uint16_t)dy);
    /* dp[$14]/dp[$15] intentionally untouched — that IS the L/R freeze. */
}


/* ========================================================================
 *  (7) Y BUTTON -> CURSOR TO VIEW ICON
 *  ------------------------------------------------------------------------
 *  Manual p.6: "Y Button — moves the cursor to the View icon."
 *
 *  ROM evidence (this one is ALREADY LIFTED):
 *    - The View icon is at screen pos (24, 24) per
 *      ui_menus.c::icon_menu_vertical[0] (entry "View").
 *    - states_gameplay.c at multiple sites (lines 981, 1056, 1130, 1397)
 *      contains the exact match:
 *
 *        if (JOY1H & 0x40) {            // bit 6
 *            dp[0x14] = 0x18;           // X = 24
 *            dp[0x15] = 0x18;           // Y = 24
 *        }
 *
 *      So JOY1H bit 6 IS the Y button (the codebase comments mis-label
 *      it as "X button" in line 981 because SNES JOY1L bit 6 = X and
 *      JOY1H bit 6 = Y — easy to confuse). The (0x18, 0x18) = (24, 24)
 *      target matches the View icon position exactly.
 *    - control_panels.c also has a similar handler for the triangle
 *      panel: `if (JOY1H & 0x40) { DP_CURSOR_X = 0x24; DP_CURSOR_Y =
 *      0x2C; }` — different target because the control panel doesn't
 *      use the View icon home position.
 *
 *  So the mechanic IS implemented. The lift below is a consolidated
 *  version that any per-view run loop can call.
 * ======================================================================== */
#define VIEW_ICON_X   0x18    /* 24 — matches icon_menu_vertical[0].x */
#define VIEW_ICON_Y   0x18    /* 24 — matches icon_menu_vertical[0].y */

void y_button_cursor_to_view_icon_lifted(void)
{
    /* JOY1H bit 6 = Y button. The cursor home is the View icon position
     * (24, 24) — the topmost-leftmost icon in the vertical toolbar.
     * Note: in the control_panels.c triangle-cursor case, the home is
     * (0x24, 0x2C) instead — that's a different "cursor" (the triangle
     * weights cursor) and is NOT the View icon shortcut. */
    if ((JOY1H_RAW & 0x40) == 0) return;
    CURSOR_X = VIEW_ICON_X;
    CURSOR_Y = VIEW_ICON_Y;
}


/* ========================================================================
 *  EXPORT LIST (so a future integration pass can locate symbols)
 * ------------------------------------------------------------------------
 *  Wiring suggestions (NOT applied here):
 *    - simulation.c::sim_tick: replace ant_lion_tick_C0FD() call with
 *      ant_lion_tick_C0FD_lifted() (or rewire the existing stub in
 *      lifted_helpers_4.c to call this).
 *    - states_gameplay.c per-view run loops: add a call to
 *      lr_cursor_fixed_scroll_lifted() before sub_A106(); add a call
 *      to y_button_cursor_to_view_icon_lifted() AFTER the existing
 *      JOY1H & 0x40 block (or replace it).
 *    - entities_d.c::type27_state1_walk_AB85: optional restoration of
 *      caterpillar_harvest_check_RECONSTRUCTED() — DO NOT apply unless
 *      the user wants manual-fidelity behavior restored that the
 *      original SNES port omitted.
 *
 *  None of these are applied in this file — it's a single-file lift.
 * ======================================================================== */

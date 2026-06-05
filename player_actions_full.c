/*
 * player_actions_full.c — SimAnt (SNES) PLAYER-ACTION layer, ROM-verified
 *                         replacements for the pseudo-stages in
 *                         player_actions.c (Stage 3..9 + scent gradient).
 *
 * See wiki/13-player-actions.md for the high-level view of the 14-slot
 * dispatcher at $03:D7A3, the dp[$02B7] one-tick-delayed action ID
 * mechanism, and the colour-blind rect-sweep attack kernel.
 *
 * Agent L's player_actions.c had skeletons inferred from manual + tutorial
 * strings. This file replaces those with bodies disassembled directly from
 * the ROM. Each function header lists the ROM source $bank:addr and the
 * disassembly evidence that backed the lift.
 *
 * KEY DISCOVERY: the player-action layer doesn't dispatch from cursor B-click
 * directly to an "eat / attack / recruit" handler. Instead, it has a single
 * dispatch table at $03:D7A3 (14 slots), and ALL player-issued actions
 * commit through dp[$02B7]:
 *
 *      Recruit menu  ($00:9D38)  ->  $02B7 = slot + 1     (slots 1..5)
 *      Queen menu B ($00:9D38)   ->  $02B7 = slot + 8     (slots 8/9)
 *      Queen menu A ($00:9D02)   ->  $02B7 = 9            (Lay Eggs only)
 *
 * The dispatcher at $03:D792 runs once per sim tick from $02:8047 (the
 * colony tick router). It reads dp[$02B7], INDIRECTs through the 14-slot
 * jump table at $03:D7A3, then ZEROES dp[$02B7] ($02:8054-8059) so the
 * action fires exactly once.
 *
 * Dispatcher slot map (ROM-verified):
 *   slot 0 : RTL                                  (cancel / no-op)
 *   slot 1 : recruit_apply(5)         JSL $02:A1F4 LDA #$05
 *   slot 2 : recruit_apply(10)        JSL $02:A1F4 LDA #$0A
 *   slot 3 : recruit_apply(1000)      JSL $02:A1F4 LDA #$03E8    (Recruit All)
 *   slot 4 : release_apply(0)         JSL $02:A2CB LDA #$00      (Release 1/2)
 *   slot 5 : release_apply(1)         JSL $02:A2CB LDA #$01      (Release All)
 *   slot 6 : RTL
 *   slot 7 : RTL
 *   slot 8 : DIG_action               JSL $03:B7A7 (gated by dp[$4A]==1)
 *   slot 9 : LAY_or_NEIGHBOUR action  JSL $03:D10D (gated by dp[$4A]==2 && dp[$48]>=3)
 *   slot 10-12 : RTL
 *   slot 13: RECT_SWEEP               JSL $03:EE66 (scan ants in rect, dispatch kill)
 *
 * The "Eat" action does NOT have a separate dispatcher slot — eating is
 * implicit in the worker AI: when a worker walks onto a food tile while
 * hungry, the food is consumed and the hunger feeder ($7E:E7D2) is bumped.
 * The B-click on food triggers carry-state, not eat. The actual food
 * decrement happens inside the per-tile per-tick sweep at $03:87C0+.
 *
 * Build:
 *   cd /Users/guilhermedavid/simant-re && \
 *   clang -Wall -Wextra -c player_actions_full.c -o /tmp/pf.o
 */

#include <stdint.h>

/* ========================================================================
 * Shared aliases (extern from simant.c).
 * ======================================================================== */
extern uint8_t wram[0x20000];
#define dp wram

static inline uint16_t W16(unsigned a)             { return *(uint16_t *)&wram[a]; }
static inline void     SW16(unsigned a, uint16_t v){ *(uint16_t *)&wram[a] = v; }

/* WRAM $7F mirror accessors (matches the convention in scent.c / combat.c). */
#define WRAM_7F(off)   (*(uint8_t *)&wram[0x10000 + ((off) & 0xFFFF)])
#define WRAM_7F_W(off) (*(uint16_t *)&wram[0x10000 + ((off) & 0xFFFF)])

/* ========================================================================
 * EXTERNAL HELPERS (extern from elsewhere in the decomp).
 * ======================================================================== */
extern void apu_play_sfx_008EA3(uint8_t sfx);
extern uint16_t random_mask_02F3BD(uint16_t mask);     /* RNG, A &= mask */

/* ROM data at $02:8065 / $02:8077 — 8-direction (dx, dy) offsets, compass
 * clockwise from north. The ROM stores them as 16-bit values (read with
 * M=0 LDA $028065,x) but the values fit in int8_t (each is 0, 1, or -1).
 *
 * NOTE: scent.c also has these as `static const int8_t[8]`; we can't
 * `extern` against a `static` symbol — the linker would silently bind to
 * an unrelated symbol of the same name in lifted_helpers_6.c (a void
 * function stub) which would corrupt reads. So we re-declare locally. */
static const int8_t scent_dir_dx_028065[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
static const int8_t scent_dir_dy_028077[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };


/* ========================================================================
 *  STAGE 4 RECRUIT/RELEASE — REPLACES the inferred recruit_menu_apply_pseudo
 *  and the existing release-logic in player_actions.c.
 *  ------------------------------------------------------------------------
 *  Both functions operate over the SIMULATION's parallel-array entity
 *  tables (in WRAM bank $7F), NOT the 20-byte entity slots at $0600.
 *
 *  Parallel arrays (per combat.c::B_TYPE etc.):
 *      B-colony:  type=$7F:CBB8+i  state=$7F:C7D0+i  timer=$7F:CFA0+i
 *      R-colony:  type=$7F:D964+i  state=$7F:D770+i  timer=$7F:DB58+i
 *      counts:    B=$7E:E77E       R=$7E:E780
 *
 *  STATE 6 is the "escort the Yellow Ant" state. Setting an ant's state
 *  byte to 6 (and zeroing its timer) puts it in the player's escort.
 *
 *  Type-byte format: bit 7 = "in fight" lock; low 7 bits = caste byte.
 *  Caste-byte high-nibble (after AND #$7F, LSR x3) encodes the role:
 *     2 = Worker (forager)
 *     6 = Soldier
 *     4 = Breeder    (only acceptable if dp[$50]==$20, i.e. mating flight)
 *     8 = Queen      (only acceptable if dp[$50]==$20, same)
 *
 *  The Recruit handler iterates colony entities from index (count-1) down
 *  to 0, accepting any non-fighting ant of the right caste. When state is
 *  not already 6, set it to 6, zero its timer, and decrement the requested
 *  count. The same loop runs for both colonies (the Yellow Ant can be in
 *  either colony in Scenario 8 "The Other Side"). The Recruit All variant
 *  uses count = $03E8 = 1000, which is larger than the entity cap, so it
 *  drains every recruitable ant.
 * ======================================================================== */

/* recruit_apply_02A1F4 — ROM body at $02:A1F4. Called by dispatcher slot
 * 1/2/3 with A = desired recruit count (5, 10, or 1000).
 *
 * KEY POINT (see wiki/13-player-actions.md §4): "recruit" is just a flip
 * of the per-ant state byte to 6 (= "follow Yellow Ant"). There is no
 * proximity check, no escort list, no in-frame visual change. */
void recruit_apply_02A1F4(uint16_t desired)
{
    if (desired == 0 || (int16_t)desired < 0) return;

    /* --- BLACK colony pass --- */
    uint16_t b_remaining = W16(0xE77E);                /* B_COUNT */
    while ((int16_t)desired > 0 && (int16_t)b_remaining > 0) {
        b_remaining--;
        unsigned i = b_remaining;
        uint8_t type = WRAM_7F(0xCBB8 + i);            /* B_TYPE(i) */
        if (type == 0) continue;                        /* dead slot */
        if (type & 0x80) continue;                      /* fighting — locked */

        unsigned caste = (type & 0x7F) >> 3;
        int ok = 0;
        if (caste == 2 || caste == 6) {
            ok = 1;                                    /* Worker / Soldier */
        } else if (dp[0x50] == 0x20 && (caste == 4 || caste == 8)) {
            ok = 1;                                    /* Breeder / Queen
                                                        * (mating-flight mode) */
        }
        if (!ok) continue;

        if (WRAM_7F(0xC7D0 + i) != 6) {                /* B_STATE(i) */
            WRAM_7F(0xC7D0 + i) = 6;                   /* "follow Yellow Ant" */
            WRAM_7F(0xCFA0 + i) = 0;                   /* zero per-ant timer */
            desired--;
        }
    }

    /* --- RED colony pass (only fires if Yellow controls Red side) --- */
    uint16_t r_remaining = W16(0xE780);                /* R_COUNT */
    while ((int16_t)desired > 0 && (int16_t)r_remaining > 0) {
        r_remaining--;
        unsigned i = r_remaining;
        uint8_t type = WRAM_7F(0xD964 + i);            /* R_TYPE(i) */
        if (type == 0) continue;
        if (type & 0x80) continue;

        unsigned caste = (type & 0x7F) >> 3;
        int ok = 0;
        if (caste == 2 || caste == 6) ok = 1;
        else if (dp[0x50] == 0x20 && (caste == 4 || caste == 8)) ok = 1;
        if (!ok) continue;

        if (WRAM_7F(0xD770 + i) != 6) {                /* R_STATE(i) */
            WRAM_7F(0xD770 + i) = 6;
            WRAM_7F(0xDB58 + i) = 0;                   /* timer */
            desired--;
        }
    }
}

/* release_apply_02A2CB — ROM body at $02:A2CB. Called by dispatcher slot 4/5.
 *
 * mode == 0  ->  release_count = (current_recruits / 2)        (Release 1/2)
 * mode == 1  ->  release_count = (current_recruits + $64)      (Release All)
 *                The +$64 (100) ensures we exceed any in-loop add-back, so
 *                every state-6 ant gets cleared.
 *
 * "Current recruits" is the running hunger-feeder value at $7E:E7D2 (the
 * ROM reuses it as a counter for active escort pressure). Then the loop
 * scans both colonies and zeroes the state of any ant currently in state 6
 * until release_count reaches 0.
 *
 * Also clears the colony-3 (dangers) parallel array entries — that table is
 * the predator/danger entity pool and isn't recruitable, but the same loop
 * shape sweeps it to release any state-6 marker (probably defensive). */
void release_apply_02A2CB(uint16_t mode)
{
    uint16_t feeder = W16(0xE7D2);
    uint16_t release_count;

    if (mode == 0) {
        /* Release 1/2: drop half of the current escort. */
        release_count = feeder >> 1;
    } else {
        /* Release All: drop everything plus +$64 padding. */
        release_count = (uint16_t)(feeder + 0x64);
    }

    /* --- BLACK colony --- */
    uint16_t b_remaining = W16(0xE77E);
    while ((int16_t)release_count > 0 && (int16_t)b_remaining > 0) {
        b_remaining--;
        unsigned i = b_remaining;
        uint8_t type = WRAM_7F(0xCBB8 + i);
        if (type == 0) continue;
        if (type & 0x80) continue;
        if (WRAM_7F(0xC7D0 + i) == 6) {
            WRAM_7F(0xC7D0 + i) = 0;                    /* clear escort state */
            release_count--;
        }
    }

    /* --- RED colony --- */
    uint16_t r_remaining = W16(0xE780);
    while ((int16_t)release_count > 0 && (int16_t)r_remaining > 0) {
        r_remaining--;
        unsigned i = r_remaining;
        uint8_t type = WRAM_7F(0xD964 + i);
        if (type == 0) continue;
        if (type & 0x80) continue;
        if (WRAM_7F(0xD770 + i) == 6) {
            WRAM_7F(0xD770 + i) = 0;
            release_count--;
        }
    }

    /* --- DANGER colony --- */
    uint16_t d_remaining = W16(0xE782);
    while ((int16_t)release_count > 0 && (int16_t)d_remaining > 0) {
        d_remaining--;
        unsigned i = d_remaining;
        uint8_t type = WRAM_7F(0xE328 + i);             /* D_TYPE */
        if (type == 0) continue;
        if (type & 0x80) continue;
        if (WRAM_7F(0xE134 + i) == 6) {                 /* D_STATE */
            WRAM_7F(0xE134 + i) = 0;
            release_count--;
        }
    }
}


/* ========================================================================
 *  STAGE 7 ATTACK / RECT-SWEEP — REPLACES simulate_attack_red_for_yellow
 *  ------------------------------------------------------------------------
 *  ROM body at $03:EE66 (called by dispatcher slot 13).
 *
 *  This is NOT a "find one entity and mark IN_FIGHT_BIT" — it's a
 *  RECTANGULAR SWEEP that fires the kill-dispatcher on every ant that
 *  falls inside the rectangle (dp[$E5]..dp[$E5]+dp[$EB]) by
 *  (dp[$E7]..dp[$E7]+dp[$E9]). The dispatcher target is $02:C379
 *  (the kill resolver) which is colour-blind: it doesn't matter whether
 *  the target is black, red, or yellow — the rect is the gate.
 *
 *  Why a rectangle for "attack"? In SimAnt the player B-clicks on the
 *  WORLD, not on a specific sprite. The cursor's hit-box defines the rect.
 *  Any enemy ant inside the rect gets engaged. This also explains the
 *  Mass-Kill behaviour for Cat's Paw / Foot — same rect-sweep with a
 *  bigger rect.
 *
 *  Fall-through paths:
 *    - After the per-ant loop ends, a second check uses dp[$02]/[$04]
 *      (the cursor world coords) shifted >> 4 to compute the TILE the
 *      cursor sits on, and if that tile is inside the rect, JSL $03:E1DC
 *      (the per-tile event handler — probably handles "B-click on egg"
 *      pickup of an egg-shaped sim object).
 *    - Final dp[$4A]==1 check: when the action came from a Worker-form
 *      click (vs Queen-form), play "kill" SFX $05.
 * ======================================================================== */
/* rect_sweep_action_03EE66 — see wiki/13-player-actions.md §6 "Attack" and
 * wiki/15-dangers.md §3. This is the colour-blind rectangular sweep that the
 * player B-click attack, Cat's Paw, Lawn Mower, and Foot all funnel through. */
void rect_sweep_action_03EE66(void)
{
    /* Compute rect bounds: rect = (dp[$E5]..dp[$E5]+dp[$EB], dp[$E7]..dp[$E7]+dp[$E9]).
     *
     * ROM detail (verified at $03:EE66): all comparisons run with M=0 (16-bit
     * accumulator) AGAINST the FULL 16-bit values at $E5/$E7/$F68F/$F691.
     * The per-ant Y attr at $C3E8,x and X coord at $C000,x are loaded with
     * an 8-bit LDA (after SEP #$20) while A's high byte is staged to 0 by
     * the LDA #$0000 / REP #$20 sandwich, so they're zero-extended 8-bit
     * values compared 16-bit against the full rect words. Earlier C versions
     * masked the rect bounds with & 0xFF which would let an ant pass on a
     * rect at high address even when the ROM would have rejected. */
    uint16_t rect_x0 = W16(0xE5);
    uint16_t rect_y0 = W16(0xE7);
    uint16_t rect_x1 = (uint16_t)(rect_x0 + W16(0xEB));
    uint16_t rect_y1 = (uint16_t)(rect_y0 + W16(0xE9));
    SW16(0xF68F, rect_x1);
    SW16(0xF691, rect_y1);

    /* Per-ant pass: B-colony only (the ROM only sweeps E77E, not E780). The
     * ROM iterates via INC $F693 / CPX $E77E — so this is a forward scan
     * from i=0 to i=count-1 inclusive. */
    SW16(0xF693, 0);
    uint16_t b_count = W16(0xE77E);
    for (uint16_t i = 0; i < b_count; i++) {
        if (WRAM_7F(0xCBB8 + i) == 0) {                /* dead slot */
            SW16(0xF693, (uint16_t)(i + 1));
            continue;
        }
        uint16_t y_val = (uint16_t)WRAM_7F(0xC3E8 + i);/* zero-extended */
        if (y_val < rect_y0) { SW16(0xF693, (uint16_t)(i + 1)); continue; }
        /* ROM: CMP $F691 / BEQ keep / BCS reject  -> accept when y_val <= rect_y1 */
        if (y_val > rect_y1) { SW16(0xF693, (uint16_t)(i + 1)); continue; }
        SW16(0xF68B, y_val);

        uint16_t x_val = (uint16_t)WRAM_7F(0xC000 + i);/* zero-extended */
        if (x_val < rect_x0) { SW16(0xF693, (uint16_t)(i + 1)); continue; }
        if (x_val > rect_x1) { SW16(0xF693, (uint16_t)(i + 1)); continue; }
        SW16(0xF689, x_val);

        /* Inside the rect — read type, zero type slot, then call kill
         * resolver. Note ROM passes A=(type & $80), X=x_val, Y=y_val to the
         * JSL $02:C379. */
        uint8_t type = WRAM_7F(0xCBB8 + i);
        WRAM_7F(0xCBB8 + i) = 0;                       /* despawn */
        uint8_t flag = type & 0x80;
        extern void kill_resolver_02C379(uint8_t flag, uint16_t x, uint16_t y);
        kill_resolver_02C379(flag, x_val, y_val);
        SW16(0xF693, (uint16_t)(i + 1));
    }

    /* Per-tile cursor pass: if the cursor's WORLD tile (dp[$02..$05] >> 4)
     * sits in the rect, fire the per-tile handler at $03:E1DC. ROM uses
     * 4 LSRs (divide by 16) to convert pixel→tile. ROM at $EEDD-$EEE0
     * has NO BEQ-accept before the BCS-reject, so the upper bound is
     * EXCLUSIVE here (unlike the per-ant pass which has a BEQ-accept
     * making it inclusive). An earlier V2 fix used `<=` on both
     * cursor_tile_x and cursor_tile_y which over-included the boundary. */
    uint16_t cursor_tile_x = (uint16_t)(W16(0x02) >> 4);
    uint16_t cursor_tile_y = (uint16_t)(W16(0x04) >> 4);
    if (cursor_tile_x >= rect_x0 && cursor_tile_x < rect_x1 &&
        cursor_tile_y >= rect_y0 && cursor_tile_y < rect_y1) {
        extern void tile_event_handler_03E1DC(void);
        tile_event_handler_03E1DC();
    }

    /* Entity-state-1 SFX gate: if dp[$4A]==1 AND cursor (dp[$46],[$48]) is
     * in rect, play kill SFX $05. dp[$4A] is the current entity's state
     * byte during the colony tick (see combat.c usages). */
    if (W16(0x4A) == 1 &&
        W16(0x46) >= rect_x0 && W16(0x46) <= rect_x1 &&
        W16(0x48) >= rect_y0 && W16(0x48) <= rect_y1) {
        extern void kill_dispatcher_03D334(uint16_t code);
        kill_dispatcher_03D334(0x0005);
    }
}


/* ========================================================================
 *  STAGE 8 DIG ACTION — REPLACES the inferred "dig new nest" stub.
 *  ------------------------------------------------------------------------
 *  ROM body at $03:D7EA (dispatcher slot 8). Gates on dp[$4A]==1 (the
 *  current entity's state byte — see combat.c usages of dp[$4A] for the
 *  per-tick "entity in state N" pattern; state 1 is typically "active"
 *  for the ant types). Then calls $03:B7A7 with (A=dp[$4A], X=dp[$46],
 *  Y=dp[$48]).
 *
 *  Return value in A:
 *    A != $FFFF  ->  success (dug a chamber) -> RTL silently
 *    A == $FFFF  ->  failed (no dirt here)    -> play SFX $003C via $02:F65A
 *  The "not in state 1" branch falls into the failure SFX path too.
 *
 *  ROM:
 *      LDA $4A / CMP #$0001 / BNE $D800
 *      LDA $4A / LDX $46 / LDY $48
 *      JSL $03B7A7
 *      CMP #$FFFF / BNE $D807     ; success: skip the SFX
 *    $D800: LDA #$003C / JSL $02F65A
 *    $D807: RTL
 * ======================================================================== */
void dig_action_03D7EA(void)
{
    extern void apu_sfx_play_02F65A(uint16_t code);
    extern uint16_t dig_kernel_03B7A7(uint16_t a, uint16_t x, uint16_t y);

    if (W16(0x4A) != 1) {
        /* Not in Worker form — fall through to the failure SFX. */
        apu_sfx_play_02F65A(0x003C);
        return;
    }

    uint16_t result = dig_kernel_03B7A7(W16(0x4A), W16(0x46), W16(0x48));
    if (result != 0xFFFF) return;                      /* dug successfully */

    /* result == $FFFF — "can't dig" feedback. */
    apu_sfx_play_02F65A(0x003C);
}


/* ========================================================================
 *  STAGE 9 NEIGHBOUR / LAY ACTION — REPLACES queen_menu_apply_pseudo "Lay
 *                                   Eggs" stub for the Queen menu.
 *  ------------------------------------------------------------------------
 *  ROM body at $03:D808 (dispatcher slot 9). Reads dp[$4A] (must be 2 —
 *  this is the entity state byte; for the Queen entity (type 18), state 2
 *  is the "lay/dig action ready" state) and dp[$48] (the Y position must
 *  be >=3 = away from the top edge so the doubled-offset target stays in
 *  bounds).
 *
 *  ROM:
 *      LDA $4A / CMP #$0002 / BNE $D816   ; require $4A == 2
 *      LDA $48 / CMP #$0003 / BCS $D817   ; require $48 >= 3
 *      RTL   ($D816)
 *    $D817:
 *      LDA $4A / STA $F6B9                ; stash caste/state byte
 *      LDA $4C / EOR #$0004               ; flip direction 180°
 *      ASL / TAX                          ; *2 for 16-bit table indexing
 *      LDA $028065,x / ADC $028065,x      ; dx + dx = 2*dx (with CLC implicit)
 *      ADC $46 / STA $F6BB                ; + cursor X -> target X
 *      LDA $028077,x / ADC $028077,x      ; 2*dy
 *      ADC $48 / STA $F6BD                ; + cursor Y -> target Y
 *      LDA #$0010 / STA $F6BF             ; kind = $10
 *      LDA #$0001 / STA $F6C1             ; persistence = 1
 *      JSL $03D10D                        ; schedule the action
 *      RTL
 * ======================================================================== */
void neighbour_action_03D808(void)
{
    if (W16(0x4A) != 2) return;                        /* not the right state */
    if (W16(0x48) < 3)  return;                        /* too close to top edge */

    /* Stage action parameters at $F6B9..$F6C1. */
    SW16(0xF6B9, W16(0x4A));                           /* caste byte */

    /* Direction: ^=4 flips it (opposite direction). Doubled neighbour
     * offset (2x dx, 2x dy) puts the lay target 2 cells away. */
    uint16_t dir = (uint16_t)(W16(0x4C) ^ 0x0004) & 7;
    int16_t dx = (int16_t)scent_dir_dx_028065[dir] * 2;
    int16_t dy = (int16_t)scent_dir_dy_028077[dir] * 2;

    SW16(0xF6BB, (uint16_t)((int16_t)W16(0x46) + dx));
    SW16(0xF6BD, (uint16_t)((int16_t)W16(0x48) + dy));
    SW16(0xF6BF, 0x0010);                              /* kind = $10 */
    SW16(0xF6C1, 0x0001);                              /* persistence = 1 */

    /* JSL $03:D10D — schedule the action for the next sim tick. */
    extern void action_schedule_03D10D(void);
    action_schedule_03D10D();
}


/* ========================================================================
 *  STAGE 9 TOP-LEVEL DISPATCH — REPLACES the inferred dispatcher in
 *                               player_a_button_action / b_button_action.
 *  ------------------------------------------------------------------------
 *  ROM body at $03:D792. Called once per sim tick from $02:8047 (the colony
 *  tick router). It reads dp[$02B7] (set by the menu commit at $00:9D38),
 *  gates on dp[$66] (must be == 1, game mode = "active"), then INDIRECT
 *  jumps through the 14-slot table at $03:D7A3.
 *
 *  After the slot returns, $02:8054-8059 explicitly ZEROES dp[$02B7] so
 *  the action fires exactly once.
 * ======================================================================== */
/* player_action_dispatch_03D792 — the 14-slot dispatcher at $03:D7A3.
 * See wiki/13-player-actions.md §4 "Recruit menu — the dispatcher" for the
 * full slot map. The menu commit at $00:9D38 writes dp[$02B7]; this
 * dispatcher reads it one sim-tick later and zeroes it after run. */
void player_action_dispatch_03D792(void)
{
    uint16_t slot = W16(0x02B7);
    /* Gate: only run during gameplay (dp[$66] is the game-mode word; 1 =
     * "active simulation tick").  */
    if (W16(0x66) > 1) return;
    /* The BCC at $D79D allows == 1 and < 1 (so 0 also runs, but slot 0
     * = RTL so no effect). */

    switch (slot) {
    case 0:                              /* no-op */
        return;
    case 1: recruit_apply_02A1F4(5);     return;
    case 2: recruit_apply_02A1F4(10);    return;
    case 3: recruit_apply_02A1F4(0x03E8); return;
    case 4: release_apply_02A2CB(0);     return;
    case 5: release_apply_02A2CB(1);     return;
    case 6:                              /* RTL */
    case 7:                              /* RTL */
        return;
    case 8:  dig_action_03D7EA();         return;
    case 9:  neighbour_action_03D808();   return;
    case 10:                             /* RTL */
    case 11:                             /* RTL */
    case 12:                             /* RTL */
        return;
    case 13: rect_sweep_action_03EE66();  return;
    default:
        return;
    }
}


/* ========================================================================
 *  STAGE 9 MENU COMMIT — REPLACES the inferred menu-confirm path.
 *  ------------------------------------------------------------------------
 *  ROM body at $00:9CF0 (Queen menu opener) / $00:9D1A (Recruit menu).
 *  Both paths funnel through $00:9D38 which writes dp[$02B7] = action_id.
 *
 *  The menu_dispatcher_009187 helper (lifted in ui_menus.c probably) shows
 *  a popup with the table at Y, X spawn position, A entries. On confirm,
 *  it sets dp[$1A] = selected slot 0..N-1 and returns C=1.
 *
 *  $00:9CF0 reads dp[$02B1] (a "popup variant" flag):
 *      != 0 -> Queen menu A (table $01:874A, 1 entry, "Lay Eggs"):
 *              On confirm: writes A=9 to $02B7.
 *      == 0 -> Queen menu B (table $01:872F, 2 entries):
 *              On confirm: writes (slot+8) to $02B7 (slot 0 -> 8 = Dig,
 *              slot 1 -> 9 = Lay Eggs).
 *
 *  $00:9D1A is the Recruit menu (table $01:86E8, 5 entries):
 *      On confirm: plays SFX $0186E3+slot (a per-option confirm sound)
 *      then writes (slot+1) to $02B7 (slot 0 -> 1 = Recruit 5, ...,
 *      slot 4 -> 5 = Release All).
 * ======================================================================== */
extern int  menu_dispatcher_009187(uint16_t pos_xy, uint16_t table_ptr,
                                   uint8_t timeout);

/* The ROM at $9CF0 / $9D1A both run a fixed "tail" at $9D3B regardless of
 * whether the popup confirmed or cancelled. Only the $02B7 write is gated
 * on confirm. Earlier lifts that did `if (!cancel) return;` skipped the
 * tail — that breaks the per-frame speed shadow propagation, which is
 * what makes the game feel paused after a menu cancel. */
static void menu_open_common_tail_9D3B(void)
{
    extern void view_restore_00A0F4(void);
    view_restore_00A0F4();
    dp[0x001E] = dp[0x0016];                          /* propagate speed     */
    dp[0x0026]++;                                     /* bump frame counter  */
}

void queen_menu_open_009CF0(void)
{
    int confirmed;
    if (dp[0x02B1] != 0) {
        /* Queen menu variant A: only "Lay Eggs" (table at $01:874A). */
        confirmed = menu_dispatcher_009187(0x0A0A, 0x874A, 0x0C);
        if (confirmed) {
            SW16(0x02B7, 0x0009);                     /* slot 9 = Lay Eggs   */
        }
    } else {
        /* Queen menu variant B: "Dig New Nest" / "Lay Eggs" (table $01:872F). */
        confirmed = menu_dispatcher_009187(0x0A0A, 0x872F, 0x0C);
        if (confirmed) {
            uint8_t selected = dp[0x1A];              /* 0 or 1              */
            SW16(0x02B7, (uint16_t)(selected + 8));   /* 8 = Dig, 9 = Lay    */
        }
    }
    menu_open_common_tail_9D3B();
}

void recruit_menu_open_009D1A(void)
{
    /* Recruit menu (table at $01:86E8, 5 entries; $01:86E3 has the 5-byte
     * per-option SFX prefix table). */
    int confirmed = menu_dispatcher_009187(0x0A08, 0x86E8, 0x11);
    if (confirmed) {
        /* SFX prefix table at $01:86E3..86E7 = {$32, $34, $36, $38, $3A}.
         * ROM: `LDA $0186E3,x` long-mode where X = dp[$1A]. */
        uint8_t selected = dp[0x1A];
        static const uint8_t recruit_sfx[5] = { 0x32, 0x34, 0x36, 0x38, 0x3A };
        apu_play_sfx_008EA3(recruit_sfx[selected & 0x07]);
        SW16(0x02B7, (uint16_t)(selected + 1));        /* 1..5             */
    }
    menu_open_common_tail_9D3B();
}


/* ========================================================================
 *  scent_follow_gradient_full_02A710 — REPLACES the simplification in
 *                                       scent.c::scent_follow_gradient_02A710.
 *
 *  The existing scent.c lift covered only the 8-neighbour max-pick path.
 *  The ROM body at $02:A712 is THREE concentric paths:
 *
 *      A. SCENT GRADIENT (center cell != 0) — scan 8 neighbours, pick max,
 *         RNG-randomize 25% of the time, smooth through $02:AAC7.
 *
 *      B. TARGET-FOLLOWING (center cell == 0, target known)— call $02:98ED
 *         to get the direction to a fixed target (the home colony at
 *         $EE38/$EE3A for B, $EE3C/$EE3E for R), RNG-randomize 75% of the
 *         time by overriding with a random direction, smooth through AAC7.
 *
 *      C. WANDER (center cell == 0, no target) — pick a random direction
 *         via $02:AA51 (edge-aware random), smooth through AAC7.
 *
 *  Inputs (read from WRAM bank $7F's $F6xx scratch frame):
 *      $F61B   x_cell (0..63)
 *      $F61D   y_cell (0..31)
 *      $F619   color  (0 = black -> read $4000, !=0 = red -> read $4800)
 *      $F607   current_direction (0..7)
 *      $F603   self_x (raw, used by target-following)
 *      $F605   self_y (raw)
 *      $F609   high byte of "color/state" — passed via >>7 to F619 by A6CF
 *
 *      $EE38/EE3A  Black home X/Y
 *      $EE3C/EE3E  Red   home X/Y
 *      $000100     volatile per-frame RNG byte (read as 16-bit long-mode)
 *
 *  Outputs:
 *      $F611   center scent value (caller uses to early-out)
 *      $F60F   max neighbour scent value
 *      $F615   direction (0..7) of max
 *      RETURN  smoothed direction (0..7) via $02:AAC7
 *
 *  The function ENDS with RTL — the smoothed direction is left in A as the
 *  return value of $02:AAC7.
 * ======================================================================== */

/* External — the turn-smoothing lookup at $02:AAC7. Indexes as
 * smooth(current, gradient) -> next direction. (16-bit table at $02:AAD8,
 * 64 entries; high byte always 0.) */
extern uint8_t scent_turn_smooth_02AAC7(uint8_t current, uint8_t gradient);

/* External — `dir_from_to(self_xy, target_xy)` direction picker at $02:98ED.
 * Returns 0 when self==target, else 1..8 (the ROM uses dir-1 as the gradient
 * direction in the bias path, see PATH B below). */
extern uint8_t scent_dir_from_to_0298ED(uint16_t self_x, uint16_t self_y,
                                        uint16_t target_x, uint16_t target_y);

/* External — distance helper at $02:9953. */
extern uint16_t scent_distance_029953(uint16_t self_x, uint16_t self_y,
                                      uint16_t target_x, uint16_t target_y);

/* External — frame-volatile RNG noise at $0001:0000 (long-mode read). The
 * ROM uses `LDA $000100` long-mode to get a free per-frame randomness
 * source. */
static inline uint16_t scent_frame_rng(void)
{
    return W16(0x0100);
}

/* The replacement body. Signature kept identical to the scent.c version so
 * any existing caller compiles without change. */
uint8_t scent_follow_gradient_full_02A710(uint8_t color,
                                          uint8_t x_cell, uint8_t y_cell,
                                          uint8_t current_dir,
                                          uint8_t *out_center_value)
{
    /* Set up the per-call scratch frame. */
    SW16(0xF61B, x_cell);                             /* x_cell */
    SW16(0xF61D, y_cell);                             /* y_cell */
    SW16(0xF619, color);                              /* color flag */
    SW16(0xF607, current_dir);                        /* current direction */

    /* idx = (y_cell << 6) | x_cell  (matches $02:F5A8). */
    uint16_t idx = (uint16_t)((y_cell << 6) | (x_cell & 0x3F));
    uint16_t map_base = (color != 0) ? 0x4800 : 0x4000;

    uint8_t center = WRAM_7F(map_base + idx);
    SW16(0xF611, center);
    if (out_center_value) *out_center_value = center;

    if (center != 0) {
        /* === PATH A: scent gradient present, scan 8 neighbours. === */
        SW16(0xF60F, 0);                              /* max scent value */
        SW16(0xF615, 0);                              /* dir of max */
        for (uint8_t d = 0; d < 8; d++) {
            int ny = ((int)y_cell + (int)scent_dir_dy_028077[d]) & 0x1F;
            int nx = ((int)x_cell + (int)scent_dir_dx_028065[d]) & 0x3F;
            uint16_t nidx = (uint16_t)((ny << 6) | nx);
            uint8_t v = WRAM_7F(map_base + nidx);
            if (v > (uint8_t)W16(0xF60F)) {
                SW16(0xF60F, v);
                SW16(0xF615, d);
            }
        }

        /* RNG path. The ROM has TWO branches at $A7A0..$A7BD but both
         * end up at JSL $02:AAC7(current_dir, max_dir) — the LSR; AND #1
         * branch is dead-code equivalent. So we just smooth directly. */
        uint8_t gradient_dir = (uint8_t)W16(0xF615);
        return scent_turn_smooth_02AAC7(current_dir, gradient_dir);
    }

    /* === PATH B: no scent here, follow a fixed target. === */
    uint16_t target_x, target_y;
    if (color != 0) {
        /* Red colony -> home at $EE3C/$EE3E */
        target_x = W16(0xEE3C);
        target_y = W16(0xEE3E);
    } else {
        /* Black colony -> home at $EE38/$EE3A */
        target_x = W16(0xEE38);
        target_y = W16(0xEE3A);
    }

    uint16_t self_x = W16(0xF603);
    uint16_t self_y = W16(0xF605);
    uint8_t  dir = scent_dir_from_to_0298ED(self_x, self_y,
                                            target_x, target_y);
    SW16(0xF611, dir);

    if (dir != 0) {
        /* Target known — RNG decides between (dir-1) bias vs random. The
         * ROM check is `LDA $000100; LSR; AND #$03; BEQ random_path`. So
         * the bias path runs 75% (3/4) of the time, NOT 25%. The earlier
         * lift had this inverted: BEQ branches when AND result is 0, which
         * only happens when (rng>>1) & 3 == 0 i.e. 25% of the time. So
         * RANDOM is the BEQ branch (25%), BIAS is the fall-through (75%). */
        if ((scent_frame_rng() >> 1) & 0x0003) {
            /* 75% — bias toward target with gradient_dir = dir - 1. */
            uint8_t grad = (uint8_t)((dir - 1) & 0x07);
            return scent_turn_smooth_02AAC7(current_dir, grad);
        }
        /* fall through to PATH C (the BEQ at $A7F5 / $A83E joins here) */
    }

    /* === PATH C: target = self (or RNG forced random). Wander. ===
     * ROM body at $02:A800-$A80C:
     *     LDX $F607                  ; current direction
     *     LDA $000100 / LSR / AND #$0007 / TAY    ; random 0..7 from frame RNG
     *     JSL $02:AAC7                ; smooth current toward random
     * No call to $02:AA51 here — earlier lift was wrong about that. */
    uint8_t grad = (uint8_t)((scent_frame_rng() >> 1) & 0x07);
    return scent_turn_smooth_02AAC7(current_dir, grad);
}


/* ========================================================================
 * Compile-anchor.
 * ======================================================================== */
__attribute__((used))
static void *const _player_actions_full_refs[] = {
    (void *)recruit_apply_02A1F4,
    (void *)release_apply_02A2CB,
    (void *)rect_sweep_action_03EE66,
    (void *)dig_action_03D7EA,
    (void *)neighbour_action_03D808,
    (void *)player_action_dispatch_03D792,
    (void *)queen_menu_open_009CF0,
    (void *)recruit_menu_open_009D1A,
    (void *)scent_follow_gradient_full_02A710,
};

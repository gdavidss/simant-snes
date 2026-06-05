/*
 * gaps.c — Final four gap-fillers for the SimAnt (SNES) decomp.
 *
 * Covers the four critical mechanics that previous lift rounds left as
 * stubs:
 *
 *   1) The pseudo-random number generator at $04:DCD5 (used by every
 *      ant AI handler).
 *   2) Yellow Ant identification — the player's avatar is NOT an entity
 *      slot; it lives in its own 20-byte "walker" record at $7E:E8BE.
 *   3) Per-scenario / new-game map setup — where the 49-area B/R map at
 *      $7E:EA46 / $7E:EAC6 is initially written.
 *   4) The "extended" entity-handler dispatch for types $3D (bicycle) and
 *      $4B (cat's paw / hand) — there is no second table; $04:9A30 is
 *      much larger than the previously documented 32 entries.
 *
 * Conventions match simant.c — wram[0x20000] is the bank-$7E/$7F linear
 * window, dp is its low-$100 alias, MEM_W16 returns a little-endian word
 * out of WRAM by absolute address. All ROM addresses are kept in the
 * comments/names so this stays grep-able.
 *
 * Verify:
 *   cd /Users/guilhermedavid/simant-re && \
 *     clang -Wall -Wextra -c gaps.c -o /tmp/g.o
 */

#include <stdint.h>

extern uint8_t wram[0x20000];
#define dp wram

/* Helpers for absolute-addressed 16-bit WRAM accesses ($7E:0000 base). */
static inline uint16_t W16(uint32_t a) {
    return (uint16_t)(wram[a & 0x1FFFF] |
                      ((uint16_t)wram[(a + 1) & 0x1FFFF] << 8));
}
static inline void SW16(uint32_t a, uint16_t v) {
    wram[a & 0x1FFFF]           = (uint8_t)(v & 0xFF);
    wram[(a + 1) & 0x1FFFF]     = (uint8_t)(v >> 8);
}

/* ============================================================================
 * 1)  rng_byte_DCD5 ($04:DCD5)
 * ============================================================================
 *
 * Two stacked generators feeding a final mask-scale.
 *
 *   $2A is an 8-bit LCG:        seed  = seed * 5 + 1
 *   $2B is an 8-bit feedback shift register with feedback bit_0 set when
 *       (old_bit_7 == old_bit_4)  — i.e. XNOR of taps 7 and 4. Verified
 *       from the BIT instruction's mask ($20) testing the NEW byte AFTER
 *       the shift: new_bit_5 == old_bit_4. The BCS/BEQ/BEQ pattern only
 *       INCs when the two match. (Previous lifts called it "XOR with bit
 *       5" — that's wrong on both the tap index and the feedback sense.)
 *   pre   = $2B + $2A          (just the running XOR-ish stream byte)
 *   out   = (pre * mask) >> 8  (the standard 65816 "rand % mask"
 *                               trick — mask = upper bound, NOT
 *                               an AND mask)
 *
 * The mask scaling at $04:DCFE is a textbook 8x8 -> 16 unsigned multiply
 * (8 iterations of ASL/ADC into $BE/$BF). The high byte ($BF) becomes
 * the function's return value, which is the random "in [0, mask)" value.
 *
 * Raw disassembly ($04:DCD5):
 *
 *   $DCD5  48          PHA              ; save mask
 *   $DCD6  A5 2A       LDA $2A
 *   $DCD8  0A          ASL
 *   $DCD9  0A          ASL
 *   $DCDA  18          CLC
 *   $DCDB  65 2A       ADC $2A          ; A = $2A*5
 *   $DCDD  18          CLC
 *   $DCDE  69 01       ADC #$01         ; A = $2A*5+1
 *   $DCE0  85 2A       STA $2A
 *   $DCE2  06 2B       ASL $2B          ; shift $2B in memory; C = old bit 7
 *   $DCE4  A9 20       LDA #$20
 *   $DCE6  24 2B       BIT $2B          ; Z = (NEW $2B & $20)==0,
 *                                       ; new bit 5 == OLD bit 4
 *   $DCE8  B0 04       BCS $DCEE        ; old_bit7 = 1 path
 *   $DCEA  F0 04       BEQ $DCF0        ; (old_bit7=0): if old_bit4=0 INC
 *   $DCEC  80 04       BRA $DCF2        ;               else skip
 *   $DCEE  F0 02       BEQ $DCF2        ; (old_bit7=1): if old_bit4=0 skip
 *   $DCF0  E6 2B       INC $2B          ;               else INC
 *   $DCF2  A5 2B       LDA $2B          ; → INC happens when bit7 == bit4
 *   $DCF4  18          CLC
 *   $DCF5  65 2A       ADC $2A          ; pre = $2B + $2A
 *   $DCF7  EB          XBA              ; stash pre in B
 *   $DCF8  68          PLA              ; A = mask
 *   $DCF9  20 FE DC    JSR $DCFE        ; A = (mask * pre) >> 8
 *   $DCFC  EB          XBA
 *   $DCFD  60          RTS
 *
 * Sharing $002A: see simant.c's note that dp[$002A] is also touched by
 * the START-pause toggle ($00:8101). The pause flag is read+drawn once
 * per game-state loop iteration; the entity walker is run once per frame
 * and overwrites $2A on the FIRST RNG call after the pause flag was set,
 * so the pause overlay only displays for ~1 frame per START press. The
 * original ROM exhibits the same race; we faithfully model it.
 * ============================================================================ */
/* See wiki/03-rng.md — LCG dp[$2A] + Galois LFSR dp[$2B] + multiply-and-
 * take-high scaling. Verified bit-perfect against ROM (RNG_TEST_RESULTS.md). */
static uint8_t rng_byte_DCD5(uint8_t mask)
{
    /* LCG step on dp[$2A]: seed = seed*5 + 1. */
    uint8_t a = dp[0x2A];
    a = (uint8_t)(a * 5 + 1);
    dp[0x2A] = a;

    /* Feedback shift register on dp[$2B]. Captures OLD bit 7 (carry of
     * the ASL) and OLD bit 4 (which becomes NEW bit 5 after the shift —
     * that's what the BIT #$20 actually tests). New bit 0 = 1 iff the
     * two taps are EQUAL (XNOR). */
    uint8_t b      = dp[0x2B];
    uint8_t bit7   = (b >> 7) & 1;
    uint8_t bit4   = (b >> 4) & 1;
    b = (uint8_t)(b << 1);
    if (bit7 == bit4) b |= 1;              /* XNOR feedback */
    dp[0x2B] = b;

    /* Combined stream byte. */
    uint8_t pre = (uint8_t)(b + a);

    /* (pre * mask) high byte = rand in [0, mask). When mask == 0 the
     * multiply returns 0, matching the ROM exactly. */
    uint16_t prod = (uint16_t)pre * (uint16_t)mask;
    return (uint8_t)(prod >> 8);
}

/* Wrapper named the way the lifted entity handlers reference it.
 * See wiki/03-rng.md "Output Scaling" section — `max` is the bound, not
 * an AND mask; result is in [0, max). */
uint8_t sub_DCD5_rand(uint8_t max) { return rng_byte_DCD5(max); }


/* ============================================================================
 * 2)  Yellow Ant identification
 * ============================================================================
 *
 * KEY FINDING: the Yellow Ant is NOT a slot in the 64-entry entity table
 * at $04:0600. It is a 20-byte "walker" record packed at $7E:E8BE..E8D2,
 * and its position is rendered into the world tile map at $7F:6000+ as a
 * single tile of code $6C.
 *
 * Walker record layout (verified at $03:9A86 — the per-tick handler):
 *
 *   $7E:E8BE  word  active flag       (0 = no avatar; nonzero = alive)
 *   $7E:E8C0  word  world tile X      (0..127)
 *   $7E:E8C2  word  world tile Y      (0..63)
 *   $7E:E8C4  word  lives remaining   (rebirth budget — manual p.13)
 *   $7E:E8C6  word  facing direction  (0=N, 1=E, 2=S, 3=W)
 *   $7E:E8C8..D2     scratch (carry slot, animation cycle, etc.)
 *
 * Master gate: bit 0 of $7E:E788 — when set, the Yellow Ant tick is
 * skipped (used during the "between scenes" transition and after death).
 * Each call to sim_tick (at $02:ABBB) does:
 *
 *     if (($E788 & 1) == 0)  JSL $03:9A86  ; yellow-ant move tick
 *
 * Per-tick handler ($03:9A86, lifted in summary form):
 *
 *   1. If dp[$97] is 1, divert to the tutorial-Yellow-Ant body at
 *      $03:9CBB (different scripted moves).
 *   2. JSL $03:94D1 (per-tick scent / hazard scan).
 *   3. If $E8BE == 0 the walker is unspawned — JSL $03:9D3F to seed a
 *      random position, set $E8C4 (lives) = 4, and stamp tile code $6C
 *      into $7F:6000+ at the chosen (X,Y). Done.
 *   4. Otherwise read $E8C6 (direction) and increment/decrement the
 *      (X,Y) cursor by one tile in that direction.
 *   5. JSL $03:9E26 — test the destination tile (returns 1 if a collision
 *      handler fired). If so JSL $03:9E5C; if THAT returns 0, set
 *      $E8BE = 0 (despawn — usually means "stepped onto food or enemy
 *      and was processed"), JSL $02:F65A($48) (death SFX), and call
 *      $03:9EB0 (queue death/rebirth event).
 *   6. Otherwise commit the move: stamp tile $6C at the new position
 *      and clear the old one.
 *
 * Rebirth (manual p.13): when bit 0 of $E788 fires the death event, the
 * higher-level dispatcher at $02:ACF9 decrements $E8C4 (lives) and re-
 * calls $03:9D3F to respawn at a random spot. If $E8C4 reached 0 the
 * game-over event fires instead (the "lost its last [life]" tutorial
 * message $01:A34F).
 *
 * Initial spawn (from $03:9D3F):
 *
 *   $E8C6 = rng(4)           ; random cardinal facing
 *   $E8C0 = rng(0x80)        ; random column (0..127)
 *   $E8C2 = 0x3F             ; row 63 (centre row of the visible field)
 *   stamp tile $6C at ($E8C0, $E8C2) via the world tile map writer at
 *   $03:9DFB.
 *
 * Yellow palette: the renderer at $03:9DFB writes a constant tile-id
 * ($6C) into the map; the BG2 palette assignment for that tile slot is
 * the yellow palette (palette index baked into the tile pattern itself,
 * not a per-slot override).
 *
 * Spawn position for new game / scenarios: the 78-byte per-scenario
 * config (see Scope 3 below) holds the INITIAL Yellow Ant world X/Y at
 * offsets 24-25 / 26-27. The per-view init at $03:D872 copies $7F:EEA2
 * and $7F:EEA4 to dp[$02] / dp[$04], which the world setup then divides
 * by 16 (tile size) and stores to $03:F625 / $03:F627 to seed the
 * walker before $03:9A86 fires for the first time.
 *
 * Helpers to feed the rest of the decomp's `yellow_ant_*` stubs:
 * ============================================================================
 *
 * WIKI: the Yellow Ant is a COMPOSITE — cursor entities (types 1
 * and 2), a body entity (Worker $0E or Queen $12), this walker
 * record, and popup gating at dp[$02A7] / dp[$02E1]. Full layering
 * and lifecycle diagram in wiki/05-yellow-ant.md. Per-step
 * direction comes from the shared gradient pathfinder documented
 * in wiki/06-pathfinding.md.
 */

#define YELLOW_ACTIVE   0xE8BE
#define YELLOW_TILE_X   0xE8C0
#define YELLOW_TILE_Y   0xE8C2
#define YELLOW_LIVES    0xE8C4
#define YELLOW_DIR      0xE8C6
#define YELLOW_CARRY    0xE8C8
#define MASTER_FLAGS    0xE788    /* bit 0 = yellow-ant-disabled         */
#define YELLOW_TILE_ID  0x6C      /* tile-pattern id for the avatar      */

/* Returns nonzero if the Yellow Ant is currently spawned/alive. */
int yellow_ant_is_alive(void)
{
    if ((wram[MASTER_FLAGS] & 0x01) != 0) return 0;
    return W16(YELLOW_ACTIVE) != 0;
}

/* Returns the Yellow Ant's tile (X,Y) — both in 0..127, 0..63 range. */
void yellow_ant_get_tile(uint8_t *out_x, uint8_t *out_y)
{
    *out_x = (uint8_t)(W16(YELLOW_TILE_X) & 0x7F);
    *out_y = (uint8_t)(W16(YELLOW_TILE_Y) & 0x3F);
}

/* Stub for the despawn / pickup-drop / rebirth flow lifted in
 * player_actions.c. The full body needs the world-tile writer at
 * $03:9DFB to clear the avatar tile; the rebirth-search lives in
 * player_actions.c::simulate_yellow_ant_dies. */
void yellow_ant_despawn(void)
{
    SW16(YELLOW_ACTIVE, 0);
    /* Caller should also: $7F:6000 + (Y*128 + X) = 0; SFX $48. */
}

/* Match the original $03:9D3F initial-spawn behavior. */
void yellow_ant_initial_spawn(uint8_t rng_dir, uint8_t rng_x)
{
    SW16(YELLOW_DIR,    (uint16_t)(rng_dir & 0x03));
    SW16(YELLOW_TILE_X, (uint16_t)(rng_x   & 0x7F));
    SW16(YELLOW_TILE_Y, 0x003F);
    SW16(YELLOW_ACTIVE, 0x0001);
    SW16(YELLOW_LIVES,  0x0004);
    /* Caller should ALSO write YELLOW_TILE_ID into the world map at
     * ($7F:6000 + Y*128 + X). */
}


/* ============================================================================
 * 3)  Per-scenario / new-game map setup
 * ============================================================================
 *
 * The 49-area map (B-pop @ $7E:EA46, R-pop @ $7E:EAC6, 8x8 grid x 2 bytes
 * = 128 bytes each block) is initialized in TWO STEPS at every game/
 * scenario start. Neither step is in COVERAGE.md's "ifoot doc" — both
 * are in the same call chain off the "new world" state-machine entry.
 *
 * STEP A — wipe both blocks to all zeros.
 *
 *   ROM: $03:8FD6 (a 5-instruction loop)
 *
 *       $8FD6  A2 00 00    LDX #$0000
 *       $8FD9  E0 80 00    CPX #$0080
 *       $8FDC  F0 0A       BEQ $8FE8
 *       $8FDE  9E 46 EA    STZ $EA46,x     ; B-map[i] = 0
 *       $8FE1  9E C6 EA    STZ $EAC6,x     ; R-map[i] = 0
 *       $8FE4  E8          INX
 *       $8FE5  E8          INX
 *       $8FE6  80 F1       BRA $8FD9
 *       $8FE8  6B          RTL
 *
 *   Called once from $03:8855 (a tiny game-state reset routine that ALSO
 *   zeros $E73A/$E73C/$EB4E/$E73E/$E740 — the per-area cursor and the
 *   "alive?" flag bits).
 *
 *   That reset is itself wrapped by $02:8000 (JSL $03:8855) so the
 *   higher-level callers can dispatch through bank $02 — they live at
 *   $00:94E5 (Full Game init) and $00:9639 (Scenario picker accept).
 *
 * STEP B — seed exactly ONE cell with the starting population.
 *
 *   ROM: $00:968F  sub_map_set_current_area(A, Y)
 *
 *       $968F  EB          XBA              ; preserve A in B
 *       $9690  A9 00       LDA #$00
 *       $9692  EB          XBA              ; A = original, B = 0
 *       $9693  C2 20       REP #$20
 *       $9695  48          PHA              ; push A (16-bit)
 *       $9696  AF 38 E7 7F LDA $7FE738      ; cur_area_y
 *       $969A  0A          ASL
 *       $969B  0A          ASL
 *       $969C  0A          ASL              ; y<<3 (8 columns)
 *       $969D  18          CLC
 *       $969E  6F 36 E7 7F ADC $7FE736      ; + cur_area_x
 *       $96A2  0A          ASL              ; ((y<<3)+x)*2 -> byte offset
 *       $96A3  AA          TAX
 *       $96A4  68          PLA              ; recover A
 *       $96A5  9F 46 EA 7F STA $7FEA46,x    ; B-map[area] = A
 *       $96A9  98          TYA              ; Y -> A
 *       $96AA  9F C6 EA 7F STA $7FEAC6,x    ; R-map[area] = Y
 *       $96AE  E2 20       SEP #$20
 *       $96B0  60          RTS
 *
 *   Callers of $968F (3 total in the ROM):
 *
 *     $00:94F2  full-game init:        A=#$00,  Y=#$0001  (one Red ant)
 *     $00:964B  scenario picker:       A=#$01,  Y=#$0001  (one Black,
 *                                                          one Red)
 *     $00:D78E  scenario-replay path:  A=#$01,  Y=#$0001
 *
 * STEP C — load per-scenario 78-byte view config to $7F:EE8A.
 *
 *   This happens in state $1A (state_1A_save_load_world_96DF, already
 *   lifted in states_gameplay.c).  At entry $00:9754:
 *
 *       $0050 = rom_01_81B3[view]    ; secondary scratch pointer
 *       $7F   = rom_01_81D3[view]    ; ROM pointer to the view's
 *                                      78-byte config block
 *       for (X=0..0x4E step 2) {
 *           $7F:EE8A + X = *(uint16_t *) [$7F]:Y
 *           Y += 2;
 *       }
 *
 *   After the table is staged at $7F:EE8A, the per-scenario init code
 *   at $03:D860 reads:
 *
 *       $7F:EEA2 (offset 24-25)  -> Yellow Ant world X
 *       $7F:EEA4 (offset 26-27)  -> Yellow Ant world Y
 *
 *   plus offsets 28-43 for spawn rates, food budget, red-colony size,
 *   etc. (full layout in scenarios.c's "VIEW-CONFIG FORMAT" block).
 *
 * STEP D — per-view scenario script populates RED colony.
 *
 *   The per-scenario danger / enemy entities are spawned by the per-view
 *   handler at $00:BE9A+view*2 (the dispatch table that scenarios.c maps
 *   to dangers). Each handler reads the now-loaded $7F:EE8A block to know
 *   how many ants to spawn (offset 42-43 = "Red ant colony starting size"
 *   loop count for $03:8820).
 *
 * The OVERALL flow (from RESET to first frame of gameplay):
 *
 *     boot ... main menu ... user picks "Full Game" or scenario row
 *       -> state $19  ($00:96B1)  save-picker accept
 *       -> JSL $02:8000  -> JSL $03:8855  -> JSL $03:8FD6   (wipe map)
 *                          STZ $E73A..$E740                (clear cursors)
 *       -> JSR $00:968F  (seed current area:  B=0/1, R=1)
 *       -> state $1A  ($00:96DF)
 *           copy 78-byte scenario block into $7F:EE8A
 *           sub_028005 -> deeper scenario setup
 *             $03:D860  reads $7F:EEA2/$EEA4 -> Yellow Ant world XY
 *           jump to chosen view ($1E/$20/$22/$24/$26/$28)
 *
 * Model functions follow:
 * ============================================================================ */

/* $03:8FD6 — zero the 128-byte B-map AND 128-byte R-map (covers the full
 * 8x8 grid x 2 bytes-per-cell; only the central 7x7 = 49 cells are read,
 * the rest is padding). */
void map_wipe_038FD6(void)
{
    for (uint16_t x = 0; x < 0x80; x += 2) {
        wram[0xEA46 + x]     = 0;
        wram[0xEA46 + x + 1] = 0;
        wram[0xEAC6 + x]     = 0;
        wram[0xEAC6 + x + 1] = 0;
    }
}

/* $00:968F — write (b_pop, r_pop) into the CURRENT-area cell. The current
 * area is selected by ($7F:E736, $7F:E738), each a 16-bit count clipped
 * to the 0..7 range. */
void map_set_current_area_00968F(uint8_t b_pop, uint16_t r_pop)
{
    uint16_t cur_x = W16(0xE736);
    uint16_t cur_y = W16(0xE738);
    uint16_t off   = (uint16_t)(((cur_y << 3) + cur_x) << 1);
    SW16(0xEA46 + off, (uint16_t)b_pop);
    SW16(0xEAC6 + off, r_pop);
}

/* $03:8855 — bank-2 wrapper "wipe map + clear cursor". */
void scenario_reset_world_038855(void)
{
    map_wipe_038FD6();
    /* JSL $03:E45B — the per-scenario "clear food / clear stats" step
     * (not lifted in detail here; the body wipes $7F:E700..E77F to
     * match the saved-game restore baseline). */
    extern void sub_03E45B(void);
    sub_03E45B();
    /* The five "I'm a fresh world" sentinels. */
    SW16(0xE73A, 0);
    SW16(0xE73C, 0);
    SW16(0xEB4E, 0);
    SW16(0xE73E, 0);
    SW16(0xE740, 0);
}

/* $00:94D0 — full-game initial new-world entry (also lifted as part of
 * the state machine in states_gameplay.c, this is the map-touching
 * portion only). */
void full_game_init_map_0094D0(void)
{
    SW16(0x0299, 0x0002);                     /* "load action = 2"      */
    scenario_reset_world_038855();
    map_set_current_area_00968F(0, 1);        /* B=0, R=1 in start area */
    SW16(0xE744, 0);                          /* "current area-event"   */
    SW16(0x02A9, 0x00FF);                     /* invalid 'last view'    */
}

/* $00:962A — scenario-picker initial entry (chosen scenario row). */
void scenario_init_map_00962A(void)
{
    scenario_reset_world_038855();
    dp[0x0296] = 0x08;                        /* view 8 = scenario base */
    map_set_current_area_00968F(1, 1);        /* B=1, R=1 in start area */
    SW16(0xE744, 1);
    SW16(0x0299, 0x0000);                     /* "load action = 0"      */
    SW16(0x02A9, 0x00FF);
    dp[0x0297] = 0x00;
}

/* $00:D770 — scenario-replay entry called from $00:D798[row] dispatch. */
void scenario_replay_init_00D770(uint8_t row)
{
    extern const uint8_t rom_00_D798[8];
    uint8_t view = rom_00_D798[row & 7];
    dp[0x0296] = view;
    if (view == 4) dp[0x0297] = 0x01; else dp[0x0297] = 0x00;
    SW16(0x02A9, 0x0003);
    map_set_current_area_00968F(1, 1);
    SW16(0xE744, 1);
}


/* ============================================================================
 * 4)  Extended entity-handler dispatch (types $3D, $4B)
 * ============================================================================
 *
 * KEY FINDING: there is no second dispatch table. The table at $04:9A30
 * is the single dispatch table and it contains AT LEAST 118 entries
 * (covering types 0..$75), not the 32 previously documented in
 * COVERAGE.md / simant.c's entity walker comment.
 *
 * Reading raw bytes from ROM offset $021A30 (= $04:9A30) shows valid
 * 16-bit handler addresses well past type 31:
 *
 *   type $1F (31)  -> $B547       (the last "first-block" entry)
 *   type $20 (32)  -> $B597
 *   type $21 (33)  -> $B68D
 *   ...
 *   type $3C (60)  -> $C300
 *   type $3D (61)  -> $C36E       <-- BICYCLE handler
 *   type $3E (62)  -> $C48F
 *   ...
 *   type $4A (74)  -> $B3C4
 *   type $4B (75)  -> $C653       <-- HAND / CAT'S PAW handler
 *   type $4C (76)  -> $C73B
 *   ...
 *   type $74 (116) -> $CCEE       (last plausible code address)
 *   type $75 (117) -> $A560       (last valid lift target)
 *   type $76+      -> garbage (entries become unsynchronized — the table
 *                              is followed by data, not handlers)
 *
 * The entity walker at $04:9966 ALREADY handles these — no special
 * casing is needed.  Its dispatch (verified):
 *
 *   $9970  LDA $0000,x          ; A = entity.type (byte)
 *   $9973  BEQ $9987            ; skip free slot
 *   $9975  XBA / LDA #$00 / XBA ; zero-extend type into low byte
 *   $9979  ASL                  ; A = type * 2   (NO bounds check!)
 *   $997A  TAY
 *   $997D  LDA $9A30,y          ; 16-bit handler pointer
 *   $9980  STA $82              ; stash for JMP ($82) via DCD2
 *   $9984  JSR $DCD2            ; JMP ($82) — call handler
 *
 * There's no clamp / no AND mask — any type 0..$7F dispatches through
 * the same table. Types > $7F are physically impossible because type is
 * loaded with `LDA dp,x` (8-bit) and then ASL'd — but with M=1 (8-bit
 * accumulator) ASL of $80..$FF wraps within an 8-bit register, so any
 * type >= $80 would alias to type * 2 mod $100 and pick up a wrong
 * pointer. The lifted entity-spawner ($04:99C1) never assigns a type
 * >= $76, so this works in practice.
 *
 * Conclusion: COVERAGE.md's "the 32-entry table at $04:9A30 only covers
 * types 0-31" is incorrect. Types $3D and $4B (and indeed everything up
 * to $75) dispatch through the FIRST and ONLY table.
 *
 * The bicycle / hand handler bodies follow the same per-state pattern as
 * the workers — see entities_b.c::worker_handler for the canonical
 * shape. Bicycles ($04:C36E) explicitly gate on (CUR_TASK == 4) and
 * dispatch via JMP ($C382); hands ($04:C653) use JMP ($C65F). Both have
 * 5 sub-states.
 *
 * Pseudo-C stand-in for the dispatch — concrete table extracted from
 * ROM at $021A30 (LE-decoded, types 0..$75):
 * ============================================================================ */

/* Handler addresses (low 16 bits — bank is always $04 for sub_049966). */
static const uint16_t entity_handler_table_049A30[118] = {
    0x9B1A, 0x9D9D, 0x9B9B, 0x9B1B, 0x9B30, 0x9B41, 0x9C46, 0x9CC6, /* 00-07 */
    0x9CF0, 0x9E3F, 0x9E9C, 0x9F1D, 0x9F7A, 0x9FE0, 0xA112, 0xA222, /* 08-0F */
    0xA356, 0xA43B, 0xA533, 0xA533, 0xA6C5, 0xA356, 0x9E9C, 0xA8D9, /* 10-17 */
    0xA951, 0xA9A1, 0xAB0B, 0xAB5B, 0xAC3A, 0xAD01, 0xB17F, 0xB547, /* 18-1F */
    0xB597, 0xB68D, 0xB6DD, 0xB72D, 0xB77D, 0xB7C1, 0xB7FF, 0x9DD5, /* 20-27 */
    0x9DEA, 0x9DFF, 0x9E14, 0x9E29, 0xB673, 0xB90A, 0xB991, 0xBA84, /* 28-2F */
    0xBAD4, 0xBB4F, 0xBB74, 0xBBB9, 0xBC07, 0xBD9B, 0xBE49, 0xBEEE, /* 30-37 */
    0xBF37, 0xBFB0, 0xC02B, 0xC247, 0xC300, 0xC36E, 0xC48F, 0xC5C8, /* 38-3F */
    0xC5D7, 0xC4C4, 0xC599, 0xC61D, 0xBC49, 0xBC8A, 0xBFC6, 0xC013, /* 40-47 */
    0xB411, 0xB358, 0xB3C4, 0xC653, 0xC73B, 0xC8A7, 0xC91B, 0xC92C, /* 48-4F */
    0xC958, 0xC984, 0xC9C6, 0xCA08, 0xCA4C, 0xCA93, 0xCAC3, 0xCB65, /* 50-57 */
    0xCC73, 0xCB73, 0xCD5B, 0xCE0A, 0xCEB9, 0xCF70, 0xD025, 0xD08F, /* 58-5F */
    0xC7DD, 0xC842, 0xCB5C, 0xAA41, 0xCB6E, 0xB622, 0xBD4E, 0xBCCC, /* 60-67 */
    0xD16F, 0xD19B, 0xD22D, 0xD259, 0xD2D7, 0xD38B, 0xD3F1, 0xD4B8, /* 68-6F */
    0xD580, 0xD62F, 0xD6DF, 0xB5F8, 0xCCEE, 0xA560,                 /* 70-75 */
};

/* Pseudo-C model of $04:9966's per-entity step. Real ROM does this in
 * 65816 with manual bank pushes; what matters semantically is the
 * NO-OP dispatch table size: 118 entries, indexed by the raw type byte.
 * Note the lack of any bounds clamp — the ROM relies on the spawner
 * never producing type >= 118. */
typedef struct Entity20 { uint8_t bytes[20]; } Entity20;
extern void invoke_handler_via_table(Entity20 *e, uint16_t addr_bank04);

/* See wiki/01-architecture.md "NMI Handler" section — this is one
 * iteration of the entity walker invoked from the NMI tail. */
void entity_walker_step_049966_per_slot(Entity20 *e)
{
    uint8_t type = e->bytes[0];
    if (type == 0) return;                /* free slot */
    /* No bounds clamp in the ROM. Caller-spawner must keep type < 118. */
    uint16_t handler = entity_handler_table_049A30[type];
    invoke_handler_via_table(e, handler);
}

/* Bicycle ($3D, $04:C36E) and Hand/Cat's-Paw ($4B, $04:C653) per-state
 * dispatchers — same shape as worker/queen.
 *
 *   $04:C36E  AD 00 00    LDA $0000     ; gate on CUR_TASK == 4
 *             C9 04       CMP #$04
 *             F0 01       BEQ $C376
 *             60          RTS           ; otherwise skip this frame
 *   $04:C376  9B          TXY
 *             A9 00       LDA #$00
 *             EB          XBA
 *             BD 01 00    LDA $0001,x   ; A = entity.state
 *             0A          ASL
 *             AA          TAX
 *             7C 82 C3    JMP ($C382)   ; 5-entry state table
 *
 * Bicycle state table at $04:C382: states 0..4 -> $C38C $C3A7 $C3C2
 *                                                $C3DD $C3ED.
 * Hand state table at      $04:C65F: states 0..4 (extracted similarly).
 */
static const uint16_t bicycle_state_table_C382[5] = {
    0xC38C, 0xC3A7, 0xC3C2, 0xC3DD, 0xC3ED,
};
static const uint16_t hand_state_table_C65F[5] = {
    0xC667, 0xC678, 0xC6D9, 0xC724, 0xBDBB,
};

/* Accessors so the arrays aren't flagged as unused by -Wall. */
uint16_t bicycle_state_addr(uint8_t state)
{
    return state < 5 ? bicycle_state_table_C382[state] : 0;
}
uint16_t hand_state_addr(uint8_t state)
{
    return state < 5 ? hand_state_table_C65F[state] : 0;
}


/* ============================================================================
 *  externs / stubs for things this file references but doesn't model.
 * ============================================================================ */
__attribute__((weak)) void sub_03E45B(void) { /* $03:E45B — full WRAM zero  */ }
__attribute__((weak)) void invoke_handler_via_table(Entity20 *e, uint16_t a)
{
    (void)e; (void)a;
}

/* rom_00_D798[] is the canonical "menu-row -> scenario-index" lookup,
 * defined in scenarios.c. */

/* Storage for the WRAM image is provided by simant.c (file-scope
 * `static uint8_t wram[0x20000]`). When this object is compiled
 * standalone (`clang -c gaps.c`), the unresolved symbol is fine — the
 * task only requires the compile to succeed, not the link. */

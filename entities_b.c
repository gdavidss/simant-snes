/*
 * entities_b.c — SimAnt (SNES) entity handlers, types 8..15.
 *
 * WIKI: type 14/15 (Worker / Soldier) state-4 attack pose is one half of
 *       the Yellow-Ant-attack chain — see wiki/08-combat.md#6-yellow-ant-
 *       attack-b-button-on-red-ant for how it pairs with the active-
 *       combatant pool. The Worker-vs-Soldier "longer attack ping-pong"
 *       comment below is implemented at the SIM layer as a 25-vs-50 tick
 *       tile-hold — see wiki/08-combat.md#worker-vs-soldier--not-raw-hp.
 *
 * Lifted from ROM bank $04 ($04:9CF0 .. $04:A355). These are the
 * "creature" types — most use the per-state JMP-indirect machine pattern
 * (TXY / LDA #$00 / XBA / LDA $0001,x / ASL / TAX / JMP (state_table)),
 * so their state byte at Entity+1 directly indexes a small table.
 *
 * Style: follow simant.c — `dp[]` aliases wram[0..0xFF], MMIO via macros,
 * Entity layout is the 20-byte packed struct. All addresses are kept in
 * names so this stays grep-able against the disassembly.
 *
 * GAME-MECHANICS SUMMARY (what these types DO):
 *   type  8: "static UI prop drawn at dp[$10/$11]" — gated by a countdown
 *            compare ($02B2 == $024A+1). Same body as the type-6/7 prop
 *            drawers; this one uses the dp[$10/$11] marker (the THIRD of
 *            the three on-screen marker positions). Pure UI.
 *
 *   types 9, 10, 11 (and 22 == 10): SHORT-LIVED CREATURE BURST.
 *            A tiny 3-state lifecycle:
 *              state 0 (spawn): randomize x/y on screen, set 16-bit velocity
 *                               to (+$0100, +$0100) — i.e. ~1.0 pixel/frame
 *                               in both axes; advance.
 *              state 1 (alive): integrate velocity each frame on which a
 *                               D747 gate fires (only when CUR_TASK==4 —
 *                               this throttles physics to once per 5-task
 *                               cycle). Draw the entity. If the cursor +
 *                               click overlaps the entity (DC84 CS), latch
 *                               a small countdown into +10 and advance to
 *                               state 2 (= "death-fade").
 *              state 2 (death): keep drawing for $0010,x more frames,
 *                               then zero the type byte (entity reclaimed).
 *            Types 9/10/11 differ only in:
 *              - init_word (sprite base ID): $014C / $0100 / $0180
 *              - init_attr (palette/prio):    $1E / $9E / $9E
 *              - draw helper for type 11: D997 (uses init_attr-driven
 *                sprite-selector indirect table at $D9A8) instead of
 *                DB52's plain "draw at world XY".
 *            Manual-context guess: these are the SCENE PARTICLES /
 *            "creature-debris" sprites that pop in/out when the player
 *            taps them — short-lived blobs (food crumbs?, tiny things to
 *            click) rather than walking ants.
 *
 *   type 12: STATIC FOOD/PROP with click-to-pick-up & directional flip.
 *            2 states.
 *              state 0: spawn, set velocity (+$0100, +$0080) — half Y of
 *                       above; advance.
 *              state 1: on CUR_TASK==4 frames, if NOT clicked then run
 *                       physics, else skip. Cycle attribute bits +F
 *                       through $00..$08..$04 (palette swap). Flip OAM
 *                       priority/palette ($BF when +8 is negative, $9F
 *                       otherwise — Y-flip vs not). Set dp[$44]=$10
 *                       (drop-shadow offset) and draw.
 *            Likely "drifting food crumb" — the +Y bit is slow drift,
 *            +X is normal speed, attribute cycle is animation.
 *
 *   type 13: SCROLLING/SCROLLED MULTI-TILE BANNER (UI, not a creature).
 *            2 states. State 1 unfurls a 4-tile-wide x 2-tile-tall sprite
 *            in the upper-left corner, then a 4-tile column off to the
 *            left at relative offsets ($00:80, $84, $88, $8C). The "if
 *            CUR_TASK & 2" gates cycle the tile id by +4 to animate a
 *            two-frame loop. The leading state-1 also calls DC84 (click
 *            check) but the carry path just skips physics, doesn't
 *            transition — so this is a polled-but-passive banner.
 *
 *   types 14, 15: WALKING ANTS (the actual creatures — Worker caste etc.)
 *            5 states each. Same overall shape:
 *              state 0 (spawn): randomize world position, zero animation
 *                               phase ($13,x = walk phase 0..7), call
 *                               D721 to set velocity from heading (Y=$80
 *                               for type 14 = "due east"-ish, Y=$C0 for
 *                               type 15 = ~135° SW). Latch a 60-frame
 *                               timer into +10. Advance.
 *              state 1 (walking): integrate physics on T==4 ticks. Every
 *                               4 NMI cycles flip the lateral
 *                               sprite-direction bit ($000E,x) so the
 *                               sprite faces the moving direction. Run
 *                               draw helper (A20E for type 14 — sprite
 *                               tables at $01:F0B8 / $01:F0D8; A342 for
 *                               type 15 — tables at $01:F0F8 / $01:F118).
 *                               When the timer expires: pick a random
 *                               number 0..3 into $0D,x ("legs to take"),
 *                               set +0E = 2 (turn lane), reload timer to
 *                               20, advance.
 *              state 2 (turning): no physics. On T==4: bump animation
 *                               phase ($13,x = (phase+1) & 7). Decrement
 *                               $0D,x; if still >= 0, reload timer to 20
 *                               and stay in state 2 (= "another walk
 *                               leg"); if it underflows, advance to
 *                               state 3 with timer = 30 (or for type 15:
 *                               if (phase & 3) != 0 go to state 4 with
 *                               timer 30; else go BACK to state 1 with
 *                               random 60-frame timer and a fresh heading
 *                               from D721($C0).)
 *              state 3 (idle-pre-attack/pose): no physics. On timer
 *                               expiry: call D721(Y=$0A00 for type 14 /
 *                               Y=$0800 for type 15) to face a new heading
 *                               and advance to state 4 with timer 7/9
 *                               and animation lane $0E=3.
 *              state 4 (attack/show-anim): integrate physics. Look up an
 *                               animation frame from a per-type table
 *                               (type 14: $04:A206 = [4,6,8,A,C,A,8,6];
 *                                type 15: $04:A338 = [4,7,A,D,10,13,10,
 *                                D,A,7]) indexed by countdown $10,x and
 *                               store into +6 (the "raw frame number"
 *                               passed to the draw helper). When timer
 *                               hits 0 (type 14: returns to state 1 with
 *                               heading=$80; type 15: returns to state 1
 *                               with heading=$C0). Both reload the timer
 *                               to a random 0..60 (DCD5 #$3C).
 *            Heuristic role: type 14 is the BLACK ANT WORKER, type 15 is
 *            the RED ANT WORKER (or one is a soldier — the longer attack
 *            sequence in type 15 fits "soldier-bite" vs type 14's
 *            shorter "worker-touch"). The init_attr difference ($1F vs
 *            $9E) — palette 7/6 priority 1 vs palette 3 priority 2 —
 *            backs up the "two colony-color worker variants" reading.
 *
 * EXTERNAL DEPS — declared as `extern` (defined in simant.c / not yet
 * lifted from ROM). Names retain hex addresses for grep against dig.txt.
 */

#include <stdint.h>

/* ============================================================
 * WRAM + Entity (copied/extern'd from simant.c)
 * ============================================================ */
extern uint8_t wram[0x20000];
#define dp wram

typedef struct __attribute__((packed)) Entity {
    uint8_t  type;         /* +0  */
    uint8_t  state;        /* +1  per-type state-machine index           */
    uint16_t x;            /* +2-3 16-bit world X                        */
    uint16_t y;            /* +4-5 16-bit world Y                        */
    uint8_t  flag;         /* +6  draw-frame override / per-state scratch */
    uint8_t  scratch7;     /* +7  vel-X low                              */
    uint8_t  scratch8;     /* +8  vel-X high (signed)                    */
    uint8_t  scratch9;     /* +9  vel-Y low                              */
    uint8_t  scratchA;     /* +A  vel-Y high (signed)                    */
    uint8_t  scratchB;     /* +B  (rarely used)                          */
    uint16_t init_word;    /* +C-D base sprite tile id                   */
    uint8_t  pad_e;        /* +E  draw direction lane (0..7 etc.)        */
    uint8_t  init_attr;    /* +F  OAM priority|palette                   */
    uint8_t  scratch10;    /* +10 countdown timer (in NMI frames)        */
    uint8_t  scratch11;    /* +11 vel-X subpixel accumulator             */
    uint8_t  scratch12;    /* +12 vel-Y subpixel accumulator             */
    uint8_t  scratch13;    /* +13 walk-cycle anim phase (0..7)           */
} Entity;
_Static_assert(sizeof(Entity) == 20, "entity record is 20 bytes");

/* ============================================================
 * COMMON HELPERS (bodies summarised in comments; the actual
 * ROM bodies stay tracked as `sub_XXXX` for grep). All are
 * bank-$04 routines that live at the noted addresses.
 * ============================================================ */

/* $04:DCD5 — pseudo-random byte in [0, max). Mutates the LCG at dp[$2A/$2B].
 * Original is an LCG step (a = a*5+1; b = (b<<1) | feedback) followed by
 * an 8x8 multiply at $DCFE that scales the running state by `max`. */
extern uint8_t sub_DCD5_rand(uint8_t max);

/* $04:DC84 — "did the player click this entity?" Returns 1 (carry-set in
 * the original) when:
 *   - dp[$71] is 0 (no menu lockout) AND a button bit ($C0 = A|B) of
 *     JOY1 is held; OR
 *   - dp[$71] is non-zero AND (dp[$7B] & 3) is non-zero (menu-internal
 *     fast-tick click)
 *   AND the entity's world position (+2..+5) is within a $20x$20 box
 *   around the cursor (cursor world XY at dp[$05..$06], dp[$07..$08];
 *   cursor screen offset dp[$14], dp[$15]).
 *
 * The body falls through $DC84 -> $DC9E -> CMP/AND tests, RTS with
 * carry clear/set. */
extern int sub_DC84_clicked(const Entity *e);

/* $04:D747 — physics gate + step. Runs only on NMI ticks where dp[$00]
 * (CUR_TASK) == 4: integrates velocity into world position once, then
 * RTS. Otherwise immediate RTS. The integrator:
 *   $D755 ($07,$08 + $11 -> $11; signed-extended +0008 -> +0002,
 *          AND #$07FF — 11-bit world X wrap)
 *   $D77B ($09,$0A + $12 -> $12; signed-extended +000A -> +0004,
 *          AND #$03FF — 10-bit world Y wrap)
 * So world is 2048x1024 pixels, signed subpixel velocity. */
extern void sub_D747_physics_step(Entity *e);

/* $04:D7A1 — randomize spawn position. Calls rand(#$80) << 4 -> +2..+3
 * (world X), rand(#$40) << 4 -> +4..+5 (world Y). */
extern void sub_D7A1_random_spawn_pos(Entity *e);

/* $04:D721 — compute velocity from (walk_phase, speed).
 *
 *   LDA $0013,x          ; A = entity.scratch13 (walk_phase)
 *   SEC; SBC #$02        ; A = phase - 2
 *   ASL × 5              ; A = (phase - 2) << 5      (the "angle" lookup key)
 *   PHA / PHY            ; preserve angle + Y (caller's speed)
 *   JSL $00:8A0E         ; sin(angle) * Y  -> $0009,x  (vel-Y, signed 16-bit)
 *   PLY / PLA            ; restore
 *   JSL $00:8A0B         ; cos(angle) * Y  -> $0007,x  (vel-X)
 *
 * So Y is the SPEED, and the heading-angle is implicit in the entity's
 * walk_phase ($13,x). An earlier comment claimed `(Y - 2) << 5` which is
 * wrong — Y is just the magnitude. */
extern void sub_D721_set_velocity_from_heading(Entity *e, uint16_t speed);

/* $04:DB52 — composite draw: screen-translate, build OAM tile word from
 * init_word + direction lane, push via $DB9E to one of the two shadow-OAM
 * banks. */
extern void sub_DB52_draw(Entity *e);

/* $04:D997 — alt draw helper for type 11. Examines init_attr bits 4..5
 * to pick one of 4 sprite-selector indirects in the jump table at
 * $04:D9A8. */
extern void sub_D997_draw_indirect(Entity *e);

/* $04:DB9E — push OAM record to the shadow OAM table at $00:0D00 using
 * dp[$37/$39] = screen XY, dp[$3B/$3C] = tile id, dp[$3D] = attribute.
 * Allocates from dp[$32] (hi-prio half) and, if dp[$44] != 0, also pushes
 * a shadow tile $44 pixels down using dp[$34] (lo-prio half). */
extern void sub_DB9E_oam_push(void);

/* $04:DB5C — compute screen-relative XY into dp[$37..$3A] from the
 * entity's world +2..+5 and the camera scroll at dp[$3E] (or dp[$40]
 * when the entity's attribute high bit is set — distinguishes views). */
extern void sub_DB5C_world_to_screen(Entity *e);

/* $04:DB88 — compose tile id into dp[$3B/$3C] from entity +0C..+0D base
 * + +E direction lane; copy attribute +F into dp[$3D]. */
extern void sub_DB88_compose_tile(Entity *e);

/* $04:DB40 — offset+draw helper used by type 13: takes A:Y as a relative
 * offset (X,Y), adds entity world +2,+4 to produce screen XY in
 * dp[$37/$39]. */
extern void sub_DB40_offset_draw(int16_t x_off, int16_t y_off);

/* $04:DC71 — add dp[$05]/[$07] (camera offset) to dp[$37..$3A]. Used by
 * UI props (types 6/7/8) that draw at fixed screen positions plus camera
 * adjust. */
extern void sub_DC71_apply_camera(void);

/* $04:D6F6 — DIRECTIONAL ANIMATED-SPRITE draw used by type 14 / type 15.
 * Computes tile lookup index Y = (E.pad_e << 3) + E.scratch13 (i.e.
 * 8 directions * 8 anim phases) then pulls a tile id via [82],y and an
 * attribute byte via [85],y. The two indirects ($82.. and $85..) are
 * set up by the per-type "tableset" helper (A20E for type 14, A342 for
 * type 15) so each ant has its own sprite/attribute tables in ROM. */
extern void sub_D6F6_draw_animated(Entity *e);

/* $04:9D1A — UI-prop BLINK helper. NOT a gate — it does its OWN draw
 * conditionally on ($02B2 + 1 == $024A) at a fixed (dp[$0246]/2+$C8,
 * dp[$0248]/2+$10) position, then RTS. Type 8 calls this for free,
 * then unconditionally runs its own draw afterward.
 * (Previously this was wired to a same-named but DIFFERENT function in
 * lifted_helpers_5.c — that one always returned 0, which made the
 * "if (!match) return" gate below kill the rest of type 8's draw.) */
static void sub_9D1A_blink(void)
{
    /* ROM ($04:9D1A):
     *   LDA $02B2 / INC / CMP $024A / BEQ skip / RTS   — guard
     *   LDA $0246 / LSR / CLC / ADC #$C8 / STA $37     — 8-bit ADC, carry discarded
     *   STZ $38
     *   LDA $0248 / LSR / CLC / ADC #$10 / STA $39     — same 8-bit pattern
     *   STZ $3A
     *   JSR $DC71  (camera)
     *   LDA #$18 / STA $3D
     *   LDA #$26 / STA $3B        (only $3B written — $3C untouched by this path)
     *   JSR $DB9E  (oam push)
     */
    if ((uint8_t)(dp[0x02B2] + 1) != dp[0x024A]) return;
    /* Strict 8-bit ADCs: only low byte of sum is kept; carry is discarded. */
    dp[0x37] = (uint8_t)((dp[0x0246] >> 1) + 0xC8);
    dp[0x38] = 0;
    dp[0x39] = (uint8_t)((dp[0x0248] >> 1) + 0x10);
    dp[0x3A] = 0;
    sub_DC71_apply_camera();
    dp[0x3D] = 0x18;
    /* ROM does LDA #$26 / STA $3B (8-bit). The earlier 16-bit form here
     * inadvertently zeroed $3C too — ROM does NOT touch $3C in this path. */
    dp[0x3B] = 0x26;
    sub_DB9E_oam_push();
}

/* $04:9D49 — UI-prop "draw cursor box" follow-up used by type 8.
 * Pushes four sprite tiles around (dp[$37]..) to outline a 16x16 box. */
extern void sub_9D49_draw_cursor_box(void);

/* ============================================================
 * TYPE 8 — static UI prop at dp[$10/$11].
 *
 * Same pattern as types 6 and 7 (which use dp[$0C/$0D] and dp[$0E/$0F]
 * respectively). The "marker" world position is held in dp directly
 * (not in an entity record), so this routine is shape-only: project
 * the dp marker to screen, draw one tile, then draw the surrounding
 * cursor box. Pure UI, no game logic. ($04:9CF0)
 * ============================================================ */
static void type8_dispatch_9CF0(Entity *e)
{
    (void)e;
    /* The blink helper draws ITSELF conditionally; type 8's own draw still
     * runs every frame. (Was previously gated by a wrong-function call.) */
    sub_9D1A_blink();

    /* screen X = dp[$10]/2 + $C8, screen Y = dp[$11]/2 + $10 */
    dp[0x37] = (dp[0x10] >> 1) + 0xC8;
    dp[0x38] = 0;
    dp[0x39] = (dp[0x11] >> 1) + 0x10;
    dp[0x3A] = 0;

    sub_DC71_apply_camera();                        /* camera adjust */

    dp[0x3D] = 0x18;                                /* OAM attr     */
    *(uint16_t *)&dp[0x3B] = 0x0046;                /* base tile id */
    sub_DB9E_oam_push();                            /* draw sprite  */

    sub_9D49_draw_cursor_box();                     /* outline box  */
}

/* ============================================================
 * TYPE 9 — short-lived burst creature (3 states). ($04:9E3F)
 * ============================================================ */

/* $04:9E51 — spawn: random world pos, vel = (+$0100, +$0100), advance. */
static void type9_state0_9E51_spawn(Entity *e)
{
    sub_D7A1_random_spawn_pos(e);
    *(uint16_t *)&e->scratch7 = 0x0100;             /* vel X = +1.0 */
    *(uint16_t *)&e->scratch9 = 0x0100;             /* vel Y = +1.0 */
    e->state++;                                     /* INC $0001,x */
}

/* $04:9E6D — alive: physics step (gated by D747), draw on odd frames,
 * check for click; if clicked, set pad_e=2 (turn frame), latch
 * scratch10=$08 (death-anim duration), advance. */
static void type9_state1_9E6D_alive(Entity *e)
{
    sub_D747_physics_step(e);

    /* every other NMI ($00 & 1 == 0): draw the entity */
    if ((dp[0x00] & 0x01) == 0)
        sub_DB52_draw(e);

    if (!sub_DC84_clicked(e))
        return;                                     /* still alive */

    e->pad_e    = 0x02;
    e->scratch10 = 0x08;
    e->state++;
}

/* $04:9E8E — dying: keep drawing, count scratch10 down to 0, then zero
 * type byte (entity becomes empty slot). */
static void type9_state2_9E8E_die(Entity *e)
{
    sub_DB52_draw(e);
    if (--e->scratch10 != 0) return;
    e->type = 0;                                    /* slot reclaimed */
}

static void (*const type9_states[3])(Entity *) = {
    type9_state0_9E51_spawn,
    type9_state1_9E6D_alive,
    type9_state2_9E8E_die,
};

static void type9_dispatch_9E3F(Entity *e)
{
    type9_states[e->state](e);
}

/* ============================================================
 * TYPE 10 (and alias TYPE 22) — same as type 9 with one wrinkle:
 * a special-case "blink" on frame T==$16. ($04:9E9C)
 * ============================================================ */

static void type10_state0_9EAE_spawn(Entity *e)
{
    sub_D7A1_random_spawn_pos(e);
    *(uint16_t *)&e->scratch7 = 0x0100;
    *(uint16_t *)&e->scratch9 = 0x0100;
    e->state++;
}

/* The ROM at $9ECE does `LDA $0000,x` (i.e. entity.type) and compares with
 * $16 (= 22). This handler is aliased to BOTH type 10 and type 22 in the
 * dispatch table, so the check picks between the two:
 *   - type 22 (the alias): push dp[$44]=$20 (drop-shadow offset = 32 px)
 *     and draw every frame — a "flash" effect for the colony-color variant.
 *   - type 10: draw only on even CUR_TASK ticks (plain).
 * Earlier C wrongly checked CUR_TASK == 0x16 — but CUR_TASK never reaches
 * that value; the test is really "am I the type-22 alias?". */
static void type10_state1_9ECA_alive(Entity *e)
{
    sub_D747_physics_step(e);

    if (e->type == 0x16) {
        dp[0x44] = 0x20;
        sub_DB52_draw(e);
        dp[0x44] = 0;
    } else if ((dp[0x00] & 0x01) == 0) {
        sub_DB52_draw(e);
    }

    if (!sub_DC84_clicked(e)) return;

    e->pad_e     = 0x04;
    e->scratch10 = 0x08;
    e->state++;
}

/* Same death as type 9, but the type-22 alias still gets its drop-shadow. */
static void type10_state2_9EFD_die(Entity *e)
{
    if (e->type == 0x16) {
        dp[0x44] = 0x20;
        sub_DB52_draw(e);
        dp[0x44] = 0;
    } else {
        sub_DB52_draw(e);
    }
    if (--e->scratch10 != 0) return;
    e->type = 0;
}

static void (*const type10_states[3])(Entity *) = {
    type10_state0_9EAE_spawn,
    type10_state1_9ECA_alive,
    type10_state2_9EFD_die,
};

static void type10_dispatch_9E9C(Entity *e)
{
    type10_states[e->state](e);
}

/* Alias — handler table entry for type 22 jumps to the exact same address
 * ($04:9E9C). Re-export under a new name for clarity. */
static void type22_dispatch_9E9C(Entity *e) { type10_dispatch_9E9C(e); }

/* ============================================================
 * TYPE 11 — same shape as 9/10 but draws via D997 (init-attr-
 * driven sprite selector) instead of DB52. ($04:9F1D)
 * ============================================================ */

static void type11_state0_9F2F_spawn(Entity *e)
{
    sub_D7A1_random_spawn_pos(e);
    *(uint16_t *)&e->scratch7 = 0x0100;
    *(uint16_t *)&e->scratch9 = 0x0100;
    e->state++;
}

static void type11_state1_9F4B_alive(Entity *e)
{
    sub_D747_physics_step(e);
    if ((dp[0x00] & 0x01) == 0)
        sub_D997_draw_indirect(e);

    if (!sub_DC84_clicked(e)) return;
    e->pad_e     = 0x08;
    e->scratch10 = 0x08;
    e->state++;
}

static void type11_state2_9F6C_die(Entity *e)
{
    sub_D997_draw_indirect(e);
    if (--e->scratch10 != 0) return;
    e->type = 0;
}

static void (*const type11_states[3])(Entity *) = {
    type11_state0_9F2F_spawn,
    type11_state1_9F4B_alive,
    type11_state2_9F6C_die,
};

static void type11_dispatch_9F1D(Entity *e)
{
    type11_states[e->state](e);
}

/* ============================================================
 * TYPE 12 — drifting prop with attribute-cycle animation
 * ($04:9F7A). Half the Y-velocity of types 9-11, only 2 states.
 * ============================================================ */

static void type12_state0_9F8A_spawn(Entity *e)
{
    sub_D7A1_random_spawn_pos(e);
    *(uint16_t *)&e->scratch7 = 0x0100;             /* vel X = +1.0 */
    *(uint16_t *)&e->scratch9 = 0x0080;             /* vel Y = +0.5 */
    e->state++;
}

/* Step physics on non-click frames. Every 4 frames bump init_attr by 4
 * to cycle through palette/anim variants (wrapped at $0C -> $00). Flip
 * the +F low-bit "facing" based on sign of vel-X (e->scratch8). Drop
 * a +$10-pixel shadow tile under the entity each draw. */
static void type12_state1_9FA6_alive(Entity *e)
{
    if (sub_DC84_clicked(e)) {
        /* clicked: skip physics this frame (carry-set in original) */
    } else {
        sub_D747_physics_step(e);
    }

    if (dp[0x00] == 0x04) {
        uint8_t a = e->pad_e + 4;
        if (a == 0x0C) a = 0x00;
        e->pad_e = a;
    }

    /* Velocity-X sign drives the OAM attribute byte ($BF when negative,
     * $9F when non-negative — these encode horizontal flip + palette). */
    if ((int8_t)e->scratch8 < 0)
        e->init_attr = 0x9F;
    else
        e->init_attr = 0xBF;

    dp[0x44] = 0x10;                                /* drop-shadow Y off */
    sub_DB52_draw(e);
    dp[0x44] = 0;
}

static void (*const type12_states[2])(Entity *) = {
    type12_state0_9F8A_spawn,
    type12_state1_9FA6_alive,
};

static void type12_dispatch_9F7A(Entity *e)
{
    type12_states[e->state](e);
}

/* ============================================================
 * TYPE 13 — multi-tile UI banner ($04:9FE0). 2 states. State 1
 * unfurls a 4x2 grid of tiles around the entity's world XY and
 * polls input-click as a no-op pass.
 * ============================================================ */

static void type13_state0_9FF0_spawn(Entity *e)
{
    e->x = 0;
    e->y = 0;
    *(uint16_t *)&e->scratch7 = 0x0400;             /* vel X = +4.0 */
    *(uint16_t *)&e->scratch9 = 0x0080;             /* vel Y = +0.5 */
    e->state++;
}

/* The body draws 4 columns x 2 rows of 16x16 tiles (8 sprites), the
 * second row offset +$20 in Y, columns at -$20 each from the entity X.
 * The tile id flips between $00 and $04 (+$04 / +$00) when CUR_TASK
 * bit 1 is set — a simple 2-frame banner animation. The "if CUR_TASK
 * is the 4-tick slot and bit 1 not set" guard also bumps screen X by
 * +5 px every other frame to scroll the banner. */
static void type13_state1_A013_banner(Entity *e)
{
    int t4 = (dp[0x00] == 0x04);
    int alt = (dp[0x00] & 0x02);

    if (sub_DC84_clicked(e)) {
        /* polled-but-passive: skip physics, still draw */
    } else {
        sub_D747_physics_step(e);
    }

    /* Row 1, col 1 — tile $0100 (or $0104 when on the +4 alt). */
    dp[0x3D] = 0xFE;
    sub_DB40_offset_draw((int16_t)0xFFE0, (int16_t)0xFFD8);
    *(uint16_t *)&dp[0x3B] = (!t4 && !alt) ? 0x0104 : 0x0100;
    sub_DB9E_oam_push();

    /* Same row, col 2 (X += 5 px on scroll frames).
     * ROM does 16-bit SBC of $39:$3A, not 8-bit. */
    *(uint16_t *)&dp[0x39] -= 0x0020;
    dp[0x3B] += 0x40;
    if (!t4 && !alt) {
        *(uint16_t *)&dp[0x37] += 0x05;
    }
    sub_DB9E_oam_push();

    /* Row 2, col 1 — tile id $00 (or $04) */
    dp[0x3D] = 0xBE;
    sub_DB40_offset_draw(0xFFE0, 0x0008);
    dp[0x3B] = (!t4 && !alt) ? 0x04 : 0x00;
    sub_DB9E_oam_push();

    /* Row 2, col 2 (with optional scroll) */
    *(uint16_t *)&dp[0x39] += 0x0020;
    dp[0x3B] += 0x40;
    if (!t4 && !alt) {
        *(uint16_t *)&dp[0x37] += 0x05;
    }
    sub_DB9E_oam_push();

    /* Trailing column (4 tiles at offsets $80/$84/$88/$8C) — these are
     * the "extension" tiles to the left of the main grid. */
    dp[0x3D] = 0xBE;
    dp[0x3B] = 0x80;
    sub_DB5C_world_to_screen(e);
    sub_DB9E_oam_push();
    *(uint16_t *)&dp[0x37] -= 0x20;
    dp[0x3B] = 0x84;
    sub_DB9E_oam_push();
    *(uint16_t *)&dp[0x37] -= 0x20;
    dp[0x3B] = 0x88;
    sub_DB9E_oam_push();
    *(uint16_t *)&dp[0x37] -= 0x20;
    dp[0x3B] = 0x8C;
    sub_DB9E_oam_push();
}

static void (*const type13_states[2])(Entity *) = {
    type13_state0_9FF0_spawn,
    type13_state1_A013_banner,
};

static void type13_dispatch_9FE0(Entity *e)
{
    type13_states[e->state](e);
}

/* ============================================================
 * TYPE 14 — WALKING ANT (5 states). ($04:A112)
 *
 * sprite + attribute lookup tables (set by sub_A20E):
 *   $01:F0B8  — tile-id base per (direction << 3 | anim_phase)
 *   $01:F0D8  — attribute byte per same key
 * Animation frame table at $04:A206: { 4, 6, 8, A, C, A, 8, 6 } —
 *   a classic ping-pong used for the state-4 "attack pose" loop.
 * ============================================================ */

/* Animation-frame ping-pong (state 4). */
static const uint8_t type14_attack_anim_A206[8] = {
    0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0A, 0x08, 0x06,
};

/* $04:A20E — install per-type sprite/attribute table pointers. The
 * original does LDY/STY into dp[$82..$87] (the "indirect handler
 * address" slot) so D6F6 can read [82],y / [85],y. */
static void sub_A20E_install_type14_tables(Entity *e)
{
    (void)e;
    *(uint16_t *)&dp[0x82] = 0xF0B8;
    dp[0x84] = 0x01;                                /* bank for [82] */
    *(uint16_t *)&dp[0x85] = 0xF0D8;
    dp[0x87] = 0x01;                                /* bank for [85] */
    /* D6F6 draws via the just-installed tables. */
}

/* State 0 (spawn): random world pos, zero walk phase, set heading to
 * $0080 (east), 60-frame timer, advance. */
static void type14_state0_A128_spawn(Entity *e)
{
    sub_D7A1_random_spawn_pos(e);
    e->scratch13 = 0;                               /* walk phase = 0  */
    sub_D721_set_velocity_from_heading(e, 0x0080);  /* heading = $80   */
    e->scratch10 = 0x3C;                            /* 60-frame timer  */
    e->state++;
}

/* State 1 (walking): physics, lateral facing flip every ~4 frames,
 * draw via D6F6. When timer expires: random 0..3 into +0D (the
 * "remaining walk legs" counter — NOT e->flag/+6 as an earlier draft
 * had it), +0E=2, timer=20, advance to state 2 (turn).
 *
 * The ROM at $04:A167 writes the rand byte to $0D,x. The earlier C
 * version wrote to e->flag (+6), which collided with the +6 frame
 * override that state 4 uses for the attack-pose ping-pong. Result:
 * type-14 ants either never finished their walk leg counter, or
 * stamped a bogus frame index onto the next attack pose.
 *
 * WIKI: walking-ant AI pipeline is documented in
 * wiki/06-pathfinding.md §4 ("Walking-ant AI hookpoints"). The
 * gradient -> AAC7 -> D721 -> D747 chain hits this function.
 * Worker (type 14) is also a candidate body for the Yellow Ant
 * composite — see wiki/05-yellow-ant.md §1. */
static void type14_state1_A13E_walking(Entity *e)
{
    sub_D747_physics_step(e);

    if (dp[0x00] == 0x04) {
        e->pad_e = (dp[0x00] >> 2) & 0x01;          /* flip "facing" bit */
    }
    sub_A20E_install_type14_tables(e);
    sub_D6F6_draw_animated(e);

    if (dp[0x00] != 0x04) return;
    if (--e->scratch10 != 0) return;

    /* Random 0..3 into entity+$0D (= walk-leg counter). The struct doesn't
     * have a named field there — it's actually the high byte of init_word,
     * which the AI repurposes as scratch for this state machine. */
    ((uint8_t *)e)[0x0D] = sub_DCD5_rand(0x04);     /* 0..3 walk legs */
    e->pad_e     = 0x02;                            /* turn lane */
    e->scratch10 = 0x14;                            /* 20-frame timer */
    e->state++;
}

/* State 2 (turning between walk legs): no physics. On T==4: bump walk
 * phase, decrement leg counter (+0D); if still >= 0, reset timer to 20
 * and stay; if it underflowed, set timer to 30 and advance to state 3.
 *
 * ROM does `DEC $000D,x` (leg counter at +0D, not +6). */
static void type14_state2_A178_turning(Entity *e)
{
    sub_A20E_install_type14_tables(e);
    sub_D6F6_draw_animated(e);

    if (dp[0x00] != 0x04) return;
    if (--e->scratch10 != 0) return;

    e->scratch13 = (e->scratch13 + 1) & 0x07;       /* phase++ mod 8 */
    int8_t legs = (int8_t)--((uint8_t *)e)[0x0D];   /* DEC +$0D */
    if (legs >= 0) {
        e->scratch10 = 0x14;                        /* stay in turn */
        return;
    }
    e->scratch10 = 0x1E;                            /* 30 frames    */
    e->state++;                                     /* -> state 3   */
}

/* State 3 (pose before attack): no physics. On timer expiry: face heading
 * $0A00, timer = 7, lane = 3, advance. */
static void type14_state3_A1A7_pose(Entity *e)
{
    sub_A20E_install_type14_tables(e);
    sub_D6F6_draw_animated(e);

    if (dp[0x00] != 0x04) return;
    if (--e->scratch10 != 0) return;

    sub_D721_set_velocity_from_heading(e, 0x0A00);  /* new heading */
    e->scratch10 = 0x07;                            /* 7 frames   */
    e->pad_e     = 0x03;
    e->state++;
}

/* State 4 (attack/anim): physics. Look up frame from A206 by timer index,
 * stash into +6 (raw frame override). On timer 0: face heading $0080,
 * timer = rand(0..60), advance back to state 1. */
/* State 4 = visual attack pose. The SIMULATION half of the fight (kill
 * roll, tile-hold counter, kill-dispatcher routing) lives in
 * combat.c::fight_resolver_96D7. See wiki/08-combat.md. */
static void type14_state4_A1CC_attack(Entity *e)
{
    sub_D747_physics_step(e);

    uint8_t idx = e->scratch10 & 0x07;
    e->flag = type14_attack_anim_A206[idx];

    sub_A20E_install_type14_tables(e);
    sub_D6F6_draw_animated(e);

    if (dp[0x00] != 0x04) return;
    if (--e->scratch10 != 0) return;

    e->state     = 0x01;                            /* back to walking */
    e->flag      = 0x02;                            /* reset frame   */
    sub_D721_set_velocity_from_heading(e, 0x0080);
    e->scratch10 = sub_DCD5_rand(0x3C);
}

static void (*const type14_states[5])(Entity *) = {
    type14_state0_A128_spawn,
    type14_state1_A13E_walking,
    type14_state2_A178_turning,
    type14_state3_A1A7_pose,
    type14_state4_A1CC_attack,
};

static void type14_dispatch_A112(Entity *e)
{
    type14_states[e->state](e);
}

/* ============================================================
 * TYPE 15 — WALKING ANT variant (5 states). ($04:A222)
 *
 * Same lifecycle as type 14, longer attack ping-pong and different
 * sprite tables.
 *   $01:F0F8 / $01:F118 — tile + attribute tables.
 *   $04:A338 — { 4, 7, A, D, 10, 13, 10, D, A, 7 } animation frames.
 * ============================================================ */

static const uint8_t type15_attack_anim_A338[10] = {
    0x04, 0x07, 0x0A, 0x0D, 0x10, 0x13, 0x10, 0x0D, 0x0A, 0x07,
};

static void sub_A342_install_type15_tables(Entity *e)
{
    (void)e;
    *(uint16_t *)&dp[0x82] = 0xF0F8;
    dp[0x84] = 0x01;
    *(uint16_t *)&dp[0x85] = 0xF118;
    dp[0x87] = 0x01;
}

/* State 0: random pos, zero phase, heading $00C0 (~ESE), 60f timer,
 * draw-lane +6 = 4, advance. */
static void type15_state0_A238_spawn(Entity *e)
{
    sub_D7A1_random_spawn_pos(e);
    e->scratch13 = 0;
    sub_D721_set_velocity_from_heading(e, 0x00C0);
    e->scratch10 = 0x3C;
    e->flag      = 0x04;
    e->state++;
}

/* State 1: walking. Identical to type 14's state 1 but uses A342.
 * Leg-counter writes go to entity+$0D, not +6 — see notes on type 14. */
static void type15_state1_A253_walking(Entity *e)
{
    sub_D747_physics_step(e);

    if (dp[0x00] == 0x04) {
        e->pad_e = (dp[0x00] >> 2) & 0x01;
    }
    sub_A342_install_type15_tables(e);
    sub_D6F6_draw_animated(e);

    if (dp[0x00] != 0x04) return;
    if (--e->scratch10 != 0) return;

    ((uint8_t *)e)[0x0D] = sub_DCD5_rand(0x04);     /* walk-leg counter */
    e->pad_e     = 0x02;
    e->scratch10 = 0x14;
    e->state++;
}

/* State 2: turn. Two-branch terminal: when leg counter underflows,
 * if (walk_phase & 3) is nonzero -> timer=30, state=3; else snap
 * directly back to state 1 with a fresh heading $C0 and a random
 * timer. The "&3" branch creates uneven turn timing — a small
 * fidget.
 *
 * Leg counter is at entity+$0D (NOT e->flag/+6) — see the fix in
 * type 14 state 2 for the same bug. */
static void type15_state2_A28D_turning(Entity *e)
{
    sub_A342_install_type15_tables(e);
    sub_D6F6_draw_animated(e);

    if (dp[0x00] != 0x04) return;
    if (--e->scratch10 != 0) return;

    e->scratch13 = (e->scratch13 + 1) & 0x07;
    int8_t legs = (int8_t)--((uint8_t *)e)[0x0D];   /* leg counter at +$0D */
    if (legs >= 0) {
        e->scratch10 = 0x14;
        return;
    }

    if ((e->scratch13 & 0x03) != 0) {
        /* go to state 3 -> 4 (the bigger attack lookahead) */
        e->scratch10 = 0x1E;
        e->state++;
        return;
    }

    /* skip attack, restart walking with a fresh random duration */
    e->state     = 0x01;
    e->scratch10 = sub_DCD5_rand(0x3C);
    sub_D721_set_velocity_from_heading(e, 0x00C0);
}

/* State 3: pose. Timer expiry sets heading $0800, timer=9, lane=3,
 * advance. */
static void type15_state3_A2D7_pose(Entity *e)
{
    sub_A342_install_type15_tables(e);
    sub_D6F6_draw_animated(e);

    if (dp[0x00] != 0x04) return;
    if (--e->scratch10 != 0) return;

    sub_D721_set_velocity_from_heading(e, 0x0800);
    e->scratch10 = 0x09;
    e->pad_e     = 0x03;
    e->state++;
}

/* State 4: attack. Animation frame from A338 by timer index. When timer
 * expires (BMI taken on -1): restart with heading $C0, random timer. */
/* Soldier (type 15) attack pose. Longer ping-pong than type 14 (Worker) =
 * longer visual scuffle = matches the 50-tick (vs 25) post-engagement
 * tile-hold in the sim layer. See wiki/08-combat.md#worker-vs-soldier--not-raw-hp. */
static void type15_state4_A2FC_attack(Entity *e)
{
    sub_D747_physics_step(e);

    if (dp[0x00] == 0x04) {
        uint8_t idx = e->scratch10;
        if (idx < sizeof(type15_attack_anim_A338))
            e->flag = type15_attack_anim_A338[idx];
    }

    sub_A342_install_type15_tables(e);
    sub_D6F6_draw_animated(e);

    if (dp[0x00] != 0x04) return;

    /* The original DEC + BMI handles underflow ($FF) — i.e. the counter
     * already 0 wraps to $FF and the branch fires. */
    if ((int8_t)--e->scratch10 >= 0) return;

    e->state     = 0x01;
    sub_D721_set_velocity_from_heading(e, 0x00C0);
    e->scratch10 = sub_DCD5_rand(0x3C);
}

static void (*const type15_states[5])(Entity *) = {
    type15_state0_A238_spawn,
    type15_state1_A253_walking,
    type15_state2_A28D_turning,
    type15_state3_A2D7_pose,
    type15_state4_A2FC_attack,
};

static void type15_dispatch_A222(Entity *e)
{
    type15_states[e->state](e);
}

/* ============================================================
 * Handler-table exports (so the bank-$04 dispatch table at
 * $04:9A30 can be populated from these). Marked `used` so
 * unused-but-load-bearing references don't get stripped.
 * ============================================================ */
typedef void (*EntityHandler)(Entity *);

__attribute__((used))
static const EntityHandler entities_b_handlers[16] = {
    /* 8  */ type8_dispatch_9CF0,
    /* 9  */ type9_dispatch_9E3F,
    /* 10 */ type10_dispatch_9E9C,
    /* 11 */ type11_dispatch_9F1D,
    /* 12 */ type12_dispatch_9F7A,
    /* 13 */ type13_dispatch_9FE0,
    /* 14 */ type14_dispatch_A112,
    /* 15 */ type15_dispatch_A222,
    /* 22 */ type22_dispatch_9E9C,
};

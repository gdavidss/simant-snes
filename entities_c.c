/*
 * SimAnt entity handlers — types 16, 17, 18/19 (Queen), 20, 21 (alias 16), 23.
 *
 * Lifted from SNES ROM ($SCI1996 build, bank $04). Each handler runs once per
 * frame against one Entity record (the dispatcher in entity_step_all_049966
 * walks the table at $04:0600 and tail-calls the handler for byte +0 (type)).
 *
 * Multi-state types use a JMP (table) at the top, indexed by entity.state
 * (byte +1, doubled for word entries). Some "states" in the raw table are
 * actually padding/instructions from the routine immediately following — we
 * only lift the entries that are reachable from the recovered state-set
 * transitions.
 *
 * QUEEN HYPOTHESIS (Type 18, aliased as Type 19):
 *   STRONG MATCH — see queen_handler_A533 below. State machine has a
 *   6-state cycle: 0=spawn-init, 1=walk-toward-target (chooses turn
 *   randomly when arrived), 2/3=after-turn walk, 4=after-turn variant,
 *   5=injured-bounce. State 1->5 transition plays SFX $4E (probably the
 *   "ouch" / hit sound) when the entity hits something (DC84 returns C set).
 *   The init in state 0 uses LDY #$00C0 (sprite-frame init constant) and a
 *   default decay timer of $3C frames (60 frames = 1 sec at 60Hz). Bytes +6
 *   set to $04 (sub-type/animation period?) which matches Yellow Ant /
 *   Queen body cadence.
 *
 *   NOTE: I see NO direct egg-laying / entity-spawn (no JSL $0499C1) inside
 *   types 18-23 — so the Queen's "lay egg" mechanic likely lives elsewhere
 *   (game-state level, perhaps in a periodic tick that scans for the unique
 *   queen entity and spawns workers via $0499C1). The Queen handler itself
 *   is "walk around and react to hits". This is consistent with: queens in
 *   real ant colonies don't actively forage — they sit and lay eggs while
 *   the AI moves them between chambers. The Queen *type* is a body that
 *   exists; egg-laying is a colony-level event.
 *
 * DBR = $04 at entry. M=1, X=0 (8-bit accumulator, 16-bit index regs).
 * The original handler is entered with X = pointer to entity (offset into
 * wram). We faithfully model that — Entity *self maps to X.
 */

#include <stdint.h>

/* ---- Externally provided (from simant.c / runtime) ---------------------- */
typedef struct __attribute__((packed)) Entity {
    uint8_t  type;          /* +0  */
    uint8_t  state;         /* +1  per-type state index */
    uint16_t x;             /* +2-3 world X */
    uint16_t y;             /* +4-5 world Y */
    uint8_t  flag;          /* +6  draw-radius / sub-type byte */
    int8_t   vx_lo;         /* +7  velocity X low byte */
    int8_t   vx_hi;         /* +8  velocity X high byte (sign drives mirror) */
    int8_t   vy_lo;         /* +9  velocity Y low byte */
    int8_t   vy_hi;         /* +A  velocity Y high byte */
    uint8_t  acc_lo;        /* +B  sub-pixel accumulator? */
    uint16_t init_word;     /* +C-D init_word from $01:EF59 */
    uint8_t  pad_e;         /* +E  facing/anim frame seed */
    uint8_t  init_attr;     /* +F  OAM attr seed from $01:F043 (bit 7 sign
                                     flips screen-relative side; bit 6 = HFLIP) */
    uint8_t  timer;         /* +10 countdown timer */
    uint8_t  pad_11;        /* +11 */
    uint8_t  pad_12;        /* +12 */
    uint8_t  anim_phase;    /* +13 animation phase (0..7), only nibble used */
} Entity;
_Static_assert(sizeof(Entity) == 20, "entity record must be 20 bytes");

/* Direct-page alias (see simant.c). dp[0x00] is wall-clock frame counter. */
extern uint8_t wram[0x20000];
#define dp wram

/* APU command writer at $00:8EA3 — schedules a sound command in A to the
 * SPC700 via APUIO0+3. */
extern void apu_play_sfx_008EA3(uint8_t sfx_code);

/* Drawing / motion helpers in bank $04 (verbatim names from disasm). */
extern void draw_step_D747(Entity *self);      /* $04:D747 — every 4 frames,
                                                   adds velocity to position
                                                   wrapping x mod $0800,
                                                   y mod $0400 (the world is
                                                   2048×1024). */
extern void sprite_init_D721(Entity *self,
                             uint16_t init_word);
                                                /* $04:D721 — randomized
                                                   sprite-frame init using
                                                   $0013 phase and routines
                                                   at $00:8A0B/$00:8A0E
                                                   (sin/cos lookup); writes
                                                   $0007..$000A (velocity). */
extern void scatter_init_D7A1(Entity *self);   /* $04:D7A1 — randomize entity
                                                   x/y inside world bounds.
                                                   Internally: random byte
                                                   * $10 -> x, random *$10 -> y. */
extern uint8_t rng_byte_DCD5(uint8_t mask);    /* $04:DCD5 — pseudo-random
                                                   byte ANDed with `mask`. */
extern uint8_t collision_check_DC84(void);     /* $04:DC84 — reads JOY1 or
                                                   $0071/$007B menu flags;
                                                   returns C set when "hit"
                                                   condition triggers. We
                                                   treat the carry bit as a
                                                   bool. */
extern void   render_sprite_DB9E(uint8_t tile, uint8_t attr);
                                                /* $04:DB9E — push current
                                                   ($37/$39, $3B/$3D) to OAM. */
extern void   render_sprite_pos_DB52(void);    /* $04:DB52 — compute screen
                                                   pos from entity pos and
                                                   draw via DB9E. */
extern void   render_pair_DB5C(void);          /* $04:DB5C — compute screen
                                                   pos based on $0F sign. */
extern void   compute_screen_DB40(uint16_t xa, uint16_t ya);
                                                /* $04:DB40 — store screen
                                                   pos in $37 / $39. */

/* The "indirect anim" helper: walks two tables (tile @ $82, attr @ $85),
 * indexed by $000E*8 + $0013. Used by Queen + Type 23. */
extern void render_anim_D6F6(uint8_t  attr_overlay);

/* Per-type DP scratch — these are what state 1's loops read/write. */
#define DP_AT(off)   (*(uint8_t *)&dp[(off)])
#define DP16(off)    (*(uint16_t *)&dp[(off)])

/* Frame clock (low byte of free-running counter). Periodic-tick code uses
 *   if ((dp[$00] & 0x3E) == 0)   -> trigger event every 64 frames roughly. */
#define FRAME_TICK   DP_AT(0x00)

/* ========================================================================
 * Tile / attr tables in ROM bank $01 used by the Queen + Type 23 animations.
 * (Read by render_anim_D6F6 through dp[$82..$87] long pointers.) */
extern const uint8_t bank1_F138_queen_tiles[];     /* $01:F138 (32 bytes)  */
extern const uint8_t bank1_F158_queen_attrs[];     /* $01:F158 (32 bytes)  */
extern const uint8_t bank1_F178_t23_tiles[];       /* $01:F178 (32 bytes)  */
extern const uint8_t bank1_F188_t23_attrs[];       /* $01:F188 (32 bytes)  */

/* ========================================================================
 * Type 16 / Type 21 (alias) — handler at $04:A356
 *
 * Two states. The init constants in entity_init_word/_attr are $0000 / $9F.
 * State 0 (one-frame): seed velocity to ($0400, $0080), advance to state 1.
 * State 1 (recurring): collision check; if hit, no rebound. Step animation
 * frame at $000E once every 4 frames. Draw 4 sprites in a 2×2 pattern at
 * tile from A42B[(frame>>2)&0x0F] — a 16-entry animation lookup table.
 * Mirror direction is taken from sign of velocity byte +8.
 *
 * Looks like a 4-tile creature (32×16 px) with a 16-frame animation cycle.
 * Strong candidates per manual: ANT LION or CATERPILLAR (those are wide
 * larvae; spider has its own sprite arrangement and uses Type 17's helper).
 * ======================================================================== */

/* A42B: 16-byte tile-frame table for type 16 animation. */
static const uint8_t type16_anim_tiles[16] = {
    0x80, 0x88, 0xC0, 0xC8,
    0x80, 0x88, 0xC0, 0xC8,
    0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80,
};

/* State 0 ($04:A366): one-frame setup — gives the creature initial momentum
 * and bumps state. */
static void type16_state0_init_A366(Entity *self)
{
    scatter_init_D7A1(self);
    /* +7..+8 = $0400 (16-bit vx) */
    self->vx_lo = 0x00;
    self->vx_hi = 0x04;
    /* +9..+A = $0080 (16-bit vy) */
    self->vy_lo = 0x80;
    self->vy_hi = 0x00;
    self->state = 1;
}

/* State 1 ($04:A382): the per-frame body — collision, anim step, draw 4
 * sprite cells (a 2×2 quad with mirror flip in the second column). */
static void type16_state1_step_A382(Entity *self)
{
    /* DC84: if no hit (C clear), step motion via D747. */
    if (!collision_check_DC84())
        draw_step_D747(self);

    /* dp[$44] = sprite size hint = $20 (32 px wide?) */
    DP_AT(0x44) = 0x20;

    /* "Every 4 frames (clock low byte == 4), bump frame counter +E." */
    if (FRAME_TICK == 0x04)
        self->pad_e++;

    /* anim frame = ((pad_e >> 2) & 0x0F) -> tile index into the table. */
    uint8_t tile = type16_anim_tiles[(self->pad_e >> 2) & 0x0F];
    DP_AT(0x3B) = tile;
    DP_AT(0x3C) = 0x01;

    /* Mirror handling: sign of vy_hi (+8). Two attr values: $BF (no flip)
     * if positive, $9F (flip) if negative. */
    if ((int8_t)self->vx_hi >= 0) {
        /* Facing right */
        DP_AT(0x3D) = 0xBF;
        compute_screen_DB40(0x0000, 0xFFE0);  /* offset upper-left */
    } else {
        /* Facing left — mirror */
        DP_AT(0x3D) = 0x9F;
        compute_screen_DB40(0xFF00 | 0xE0, 0xFFE0);
    }
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    /* Draw the second tile row 32 px below; toggle HFLIP. */
    DP16(0x39) += 0x0020;
    DP_AT(0x3D) ^= 0x40;
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    /* Second column: shift X by ±32 depending on facing. */
    if ((int8_t)self->vx_hi >= 0)
        DP16(0x37) -= 0x0020;
    else
        DP16(0x37) += 0x0020;
    DP_AT(0x3B) += 0x04;       /* next tile pair (offset by 4 in VRAM) */
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    DP16(0x39) -= 0x0020;
    DP_AT(0x3D) ^= 0x40;
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    DP_AT(0x44) = 0;
}

static void (* const type16_states[2])(Entity *) = {
    type16_state0_init_A366,
    type16_state1_step_A382,
};

void type16_handler_A356(Entity *self) /* alias type 21 */
{
    type16_states[self->state](self);
}

/* ========================================================================
 * Type 17 — handler at $04:A43B  (SPIDER, manual p.34)
 *
 * WIKI: this is ONLY the visual sprite. The predation logic (1/128 kill
 *       on a 2-step diagonal scan, every 16 sim-ticks per spider) lives
 *       in combat.c::spider_predation_tick_C0FD_excerpt — see
 *       wiki/09-predation.md#1-spider--03c0fd for the full mechanic.
 *
 * Two states. Init $01C0/$9F. Same skeleton as type 16 but a different
 * draw pattern: 4 sprite cells in a 2×2 grid at a fixed tile $88 (no
 * animation table). Frame counter at +E still ticks but is used only for
 * minor variation (tile $88 vs $8C). Bytes +F mirror flag becomes the
 * "second row" sprite attr — meaning the bottom row is rendered with a
 * separate attribute byte, different from the top.
 *
 * Best guess: SPIDER (4-cell sprite with split-attribute legs) per manual
 * iconography. Init $01C0 looks like an "initial spawn at high Y velocity"
 * value common to creatures that scuttle.
 * ======================================================================== */

/* State 0 ($04:A44B): same shape as type 16's. */
static void type17_state0_init_A44B(Entity *self)
{
    scatter_init_D7A1(self);
    self->vx_lo = 0x00; self->vx_hi = 0x04;          /* vx = $0400 */
    self->vy_lo = 0x80; self->vy_hi = 0x00;          /* vy = $0080 */
    self->state = 1;
}

/* State 1 ($04:A467): big draw routine — top row offset by $FFBC, four
 * sprite cells, then a fifth cell using a separate attribute saved in +F. */
static void type17_state1_step_A467(Entity *self)
{
    /* Collision check (no rebound on hit). */
    if (!collision_check_DC84())
        draw_step_D747(self);

    DP_AT(0x44) = 0x20;

    /* Pick HFLIP based on vx_hi sign. */
    DP_AT(0x3D) = ((int8_t)self->vx_hi < 0) ? 0x9F : 0xBF;

    /* Y-offset $FFBC = -68 (drawn slightly above the entity centroid). */
    compute_screen_DB40(0x00F0, 0xFFBC);

    /* Bump animation every 4 frames. */
    if (FRAME_TICK == 0x04)
        self->pad_e++;

    /* Tile = $88 normally, $8C when (pad_e<<2) & 4 set — two-frame strobe. */
    uint8_t tile = 0x88 + (((self->pad_e << 2) & 0x04));
    DP_AT(0x3B) = tile;
    DP_AT(0x3C) = 0x01;

    /* First cell. */
    render_sprite_DB9E(tile, DP_AT(0x3D));

    /* Second cell: next tile in X (tile + $40). */
    DP_AT(0x3B) = tile + 0x40;
    DP16(0x39) += 0x0020;
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    /* Toggle HFLIP and draw below at +$28 Y. */
    DP_AT(0x3D) ^= 0x40;
    DP16(0x39) += 0x0028;
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    /* Symmetric draw: tile-$40, +$20 Y. */
    DP_AT(0x3B) -= 0x40;
    DP16(0x39) += 0x0020;
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    /* JSR $DB5C — re-compute screen pos using the +F attr. */
    render_pair_DB5C();

    /* Final cell at tile $C0 — save attr into +F (it's the "second-row
     * attr" cache that the next frame's DB5C will consult). */
    DP_AT(0x3B) = 0xC0;
    self->init_attr = ((int8_t)self->vx_hi < 0) ? 0x9F : 0xBF;
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    /* Move X by ±$20 then draw the tile-+$04 cell. */
    if ((int8_t)self->vx_hi >= 0)
        DP16(0x37) -= 0x0020;
    else
        DP16(0x37) += 0x0020;
    DP_AT(0x3B) += 0x04;
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));
}

static void (* const type17_states[2])(Entity *) = {
    type17_state0_init_A44B,
    type17_state1_step_A467,
};

void type17_handler_A43B(Entity *self)
{
    type17_states[self->state](self);
}

/* ========================================================================
 * QUEEN — Type 18 (and alias Type 19) — handler at $04:A533
 *
 * Init constants $0100 / $9E. Six-state cycle:
 *   0 = spawn (one-shot init)
 *   1 = roaming (walk, collide; on hit -> state 5 "stunned")
 *   2 = walking the post-turn segment
 *   3 = arrived at corner, waiting timer
 *   4 = special-attr walk (face randomization)
 *   5 = stun bounce (then re-enter state 1)
 *
 * Per-state pseudo-code summary (lifted verbatim from disasm):
 *
 *  state 0: scatter pos, clear anim phase $0013, init sprite frame
 *           (LDY #$00C0), seed timer +10 = $3C (60 frames = 1 sec),
 *           set +6 = $04 (animation period), advance -> 1.
 *
 *  state 1: every 4 frames, refresh facing from clock low byte.
 *           DC84 collision check: if hit -> play SFX $4E, set timer to
 *           $78 (120 frames), set facing to $80 (rear), jump to state 5.
 *           Otherwise call A6B1 (the tile/attr render via $01:F138/F158
 *           tables), then if 4-frame tick, decrement timer. When timer
 *           hits 0 -> pick random direction, advance to state 2.
 *
 *  state 2: collide -> stun (state 5, SFX $4E, timer $78).
 *           Otherwise render via A6B1; on 4-tick, dec timer. Timer 0:
 *           bump anim phase $0013 by 1 (mod 8). If (phase+2)&3 == 0,
 *           go back to state 1 with a fresh $3C timer and randomized
 *           direction. Otherwise INC state -> 3 with $1E timer.
 *
 *  state 3: render + tick; timer 0 -> reset velocity, +6=$10, advance ->
 *           state 4 with $1E timer. (Probably "pause at corner".)
 *
 *  state 4: re-randomize facing every 4 frames, render, tick; timer 0 ->
 *           snap state back to 1, set period byte +6 = $04, set facing to
 *           one of two random orientations, restart $3C timer.
 *
 *  state 5 (stun): mirror sprite attribute based on (clock<<3)&$20|$9E,
 *           draw via DB52. Timer 0 -> reset anim phase, init sprite, set
 *           period = $04, snap to state 1 fresh.
 *
 * Confidence in "Queen" naming: MEDIUM. The behavior is generic "walk +
 * randomized turns + hit-react" — typical for any creature. What ties it
 * to the Queen specifically:
 *   * Type 19 aliases here → matches "Black Queen vs Red Queen, both alias
 *     to one AI" (manual: "Each colony has exactly one queen").
 *   * init_word $0100 is the same as Type 22 ("Worker variant"), and Type
 *     22 aliases Type 10 — fitting the "queen is just a special-attr ant".
 *   * Init attr $9E sets vertical-flip+priority distinct from worker $9F.
 *   * The $9E pattern with priority bits matches the Queen sprite in the
 *     ANT INFORMATION screen (which displays castes vertically).
 *
 * What is MISSING for the "lays eggs" mechanic:
 *   No JSL $0499C1 in this handler family. Egg-laying must be triggered
 *   elsewhere — likely the GS_FULL_GAME / GS_SCENARIO_GAME tick walks the
 *   entity table looking for the Queen entity (a singleton, since the
 *   manual says "one queen"), checks an off-band timer, and spawns workers.
 * ======================================================================== */

/* A6B1 helper: render Queen sprite using the two-table animation. The ROM
 * loads dp[$82..$84] = $01:F138 and dp[$85..$87] = $01:F158 BEFORE calling
 * D6F6 — those pointers are how D6F6 finds the tile + attr tables. */
static void queen_render_A6B1(Entity *self)
{
    (void)self;
    *(uint16_t *)&dp[0x82] = 0xF138;
    dp[0x84] = 0x01;
    *(uint16_t *)&dp[0x85] = 0xF158;
    dp[0x87] = 0x01;
    render_anim_D6F6(/*attr_overlay=*/0x00);
}

/* State 0 ($04:A54B): one-shot init. */
static void queen_state0_init_A54B(Entity *self)
{
    scatter_init_D7A1(self);
    self->anim_phase = 0;                  /* clear $0013 */
    sprite_init_D721(self, 0x00C0);        /* init velocity from random angle */
    self->timer    = 0x3C;                 /* 60-frame countdown */
    self->flag     = 0x04;                 /* draw-period byte */
    self->state    = 1;
}

/* State 1 ($04:A566): main wander state.
 *
 * WIKI: queens use a simpler heading-by-random-reroll model rather
 * than the scent-gradient pathfinder shared by the other walkers —
 * see wiki/06-pathfinding.md §4. The Queen body is also one of the
 * two avatar bodies (type 18) used by the Yellow Ant composite —
 * see wiki/05-yellow-ant.md §1. */
static void queen_state1_wander_A566(Entity *self)
{
    draw_step_D747(self);                   /* advance position */

    /* Every 4 frames, refresh facing by reading clock bit -> +E (facing). */
    if (FRAME_TICK == 0x04) {
        self->pad_e = (FRAME_TICK >> 2) & 0x01;
    }

    /* Collision/hit check. C set = hit. */
    if (collision_check_DC84()) {
        apu_play_sfx_008EA3(0x4E);          /* "ouch" SFX */
        self->timer  = 0x78;                 /* 120-frame stun */
        self->pad_e  = 0x80;                 /* turn around */
        self->state  = 5;                    /* enter stun/bounce */
        return;
    }

    queen_render_A6B1(self);

    /* Tick countdown every 4 frames. */
    if (FRAME_TICK == 0x04) {
        if (--self->timer == 0) {
            /* Time to turn: stash a random scratch byte at entity+$B (NOT
             * vx_lo at +$7). The ROM does `LDA #$04 / JSR DCD5 / STA $000B,x`. */
            self->acc_lo = rng_byte_DCD5(0x04);
            self->timer  = 0x14;             /* 20-frame turn-segment timer */
            self->state  = 2;
        }
    }
}

/* State 2 ($04:A5B6): post-decision walk. */
static void queen_state2_walk_A5B6(Entity *self)
{
    queen_render_A6B1(self);

    if (collision_check_DC84()) {
        apu_play_sfx_008EA3(0x4E);
        self->timer = 0x78;
        self->pad_e = 0x80;
        self->state = 5;
        return;
    }

    if (FRAME_TICK != 0x04) return;
    if (--self->timer != 0)  return;

    /* Timer expired: bump anim phase, then DEC the leg counter at +$B.
     * ROM does `INC $13 ; AND #7 ; STA $13 ; DEC $000B,x ; BPL stay`. */
    self->anim_phase = (self->anim_phase + 1) & 0x07;

    if ((int8_t)--self->acc_lo >= 0) {
        /* Still have legs to walk — stay in state 2 with a fresh 20-frame
         * timer. (BPL fires on N=0 i.e. value >= 0 signed.) */
        self->timer = 0x14;
        return;
    }

    /* Leg counter underflowed: pick next sub-state from (phase+2) & 3. */
    uint8_t phase_check = (self->anim_phase + 2) & 0x03;
    if (phase_check == 0) {
        /* Loop back to roam with a random 0..$3B-frame timer. ROM does
         * `LDA #$3C / JSR DCD5 / STA $10,x`, not a constant. */
        self->state = 1;
        self->timer = rng_byte_DCD5(0x3C);
        sprite_init_D721(self, 0x00C0);
    } else {
        /* Pause briefly. INC state -> 3 (we were in 2). */
        self->timer = 0x1E;                  /* 30 frames */
        self->state = 3;
    }
}

/* State 3 ($04:A61E): paused at corner. */
static void queen_state3_pause_A61E(Entity *self)
{
    queen_render_A6B1(self);

    if (FRAME_TICK != 0x04) return;
    if (--self->timer != 0)  return;

    /* Recompute velocity from the current anim_phase via D721 with speed
     * $0400. ROM does `LDY #$0400 / JSR D721`, which uses sin/cos at
     * $00:8A0E/$00:8A0B (NOT a direct vx assignment). */
    sprite_init_D721(self, 0x0400);
    self->timer = 0x1E;
    self->flag  = 0x10;                      /* different draw period */
    self->state = 4;
}

/* State 4 ($04:A643): facing-randomization phase. */
static void queen_state4_face_A643(Entity *self)
{
    draw_step_D747(self);

    /* Every 4 frames: pad_e = ((clock>>1) & 1) + 2 — gives values 2 or 3
     * (presumably two "looking around" frames). */
    if (FRAME_TICK == 0x04)
        self->pad_e = (uint8_t)(((FRAME_TICK >> 1) & 0x01) + 2);

    queen_render_A6B1(self);

    if (FRAME_TICK != 0x04) return;
    if (--self->timer != 0)  return;

    /* Snap to state 1 with fresh roaming params. */
    self->state = 1;
    self->flag  = 0x04;
    sprite_init_D721(self, 0x00C0);
    self->timer = rng_byte_DCD5(0x3C);
}

/* State 5 ($04:A682): stunned/bounce — flash sprite based on clock, then
 * recover. */
static void queen_state5_stun_A682(Entity *self)
{
    /* attr = ((clock<<3) & $20) | $9E — i.e. flicker the priority bit. */
    self->init_attr = (uint8_t)((FRAME_TICK << 3) & 0x20) | 0x9E;

    render_sprite_pos_DB52();

    if (--self->timer != 0) return;

    /* Recovery: rerun the state-0 setup. */
    self->anim_phase = 0;
    sprite_init_D721(self, 0x00C0);
    self->timer = 0x3C;
    self->flag  = 0x04;
    self->state = 1;
}

static void (* const queen_states[6])(Entity *) = {
    queen_state0_init_A54B,
    queen_state1_wander_A566,
    queen_state2_walk_A5B6,
    queen_state3_pause_A61E,
    queen_state4_face_A643,
    queen_state5_stun_A682,
};

void queen_handler_A533(Entity *self)            /* type 18 + type 19 alias */
{
    queen_states[self->state](self);
}

/* ========================================================================
 * Type 20 — handler at $04:A6C5
 *
 * UNIQUE: has a JSR $A7C9 prefix BEFORE the state dispatcher. That prefix
 * is a giant 6-cell sprite renderer ($A7C9-$A8D8): it draws a tall composite
 * sprite (head, torso, legs) at a fixed offset pattern with tile values
 * $01C0, $C4, $CC, $C8, $8E, $9F-or-$3F-or-$1F. The fact that A7C9 is
 * always called first (before dispatching state-specific behavior) means
 * the sprite is drawn EVERY frame regardless of state.
 *
 * Init constants are $0000 / $00 — i.e. NO init_word, NO init_attr — which
 * is unusual. The type is "non-sprite" per simant.c's comment, but actually
 * it IS drawn (just at hard-coded tile offsets, not via the standard attr
 * byte). This is the "BIG creature with hand-drawn rendering" — strong
 * candidate for SPIDER or ANT LION (which are larger than ants and use
 * multi-cell composite sprites).
 *
 * Five states:
 *   0 = init (velocity $00C0, $0040; +E=2 (facing #2); timer $C8 = 200f)
 *   1 = stretch / wandering (samples world pos into $69/$6A, calls A7BA
 *       to convert into a tile coord, then ZEROES a 4-row 8-col block in
 *       WRAM bank $7F at $4000/$4800/$5000/$5800,x — clearly clobbering
 *       tile data (digging through floor?). On hit -> state 2 with $0F timer.
 *   2 = brief pause (timer-driven). Timer 0: bump +E (facing), advance to 3
 *       with timer $78 (120 frames).
 *   3 = brief pause #2. Timer 0: advance to state 4 with timer $0F.
 *   4 = velocity-negate + recover. Timer 0: NEGATE +7..+8 (turn around),
 *       bump +E, snap to state 1.
 *
 * The "ZEROES a 4-row 8-col block in WRAM" is the giveaway: this entity
 * MODIFIES the playfield. Best fit: ANT LION — the manual says "ant lions
 * dig pits in the sand to catch ants". The sand-pit-digging mechanic
 * literally is clearing tiles in the playfield map.
 *
 * Alternative: this could be the "DIG NEW NEST" entity controller — a
 * placeholder entity created when the player issues the Dig New Nest
 * menu command, which then carves out a new chamber by zeroing tiles
 * at the queen's current location.
 *
 * Either way: this is the WORLD-MODIFYING entity. Worth investigating
 * further by checking which game-state spawns it.
 * ======================================================================== */

/* A7C9 — composite-sprite renderer (always called as prefix). Renders 6
 * sprite cells at fixed offsets relative to entity (vx_hi sign drives
 * mirroring). Doesn't modify state. */
static void type20_render_composite_A7C9(Entity *self)
{
    /* Cell 1: tile $01C0, attr $9F/$BF based on facing, offset $0000,
     * $FFE0 (above-center). */
    DP_AT(0x3B) = 0xC0;
    DP_AT(0x3C) = 0x01;
    DP_AT(0x3D) = ((int8_t)self->vx_hi < 0) ? 0x9F : 0xBF;
    compute_screen_DB40(0x0000, 0xFFE0);
    render_sprite_DB9E(0xC0, DP_AT(0x3D));

    /* Cell 2: tile $C4 at ±$0020 X. */
    DP_AT(0x3B) = 0xC4;
    if ((int8_t)self->vx_hi < 0) DP16(0x37) += 0x0020;
    else                          DP16(0x37) -= 0x0020;
    render_sprite_DB9E(0xC4, DP_AT(0x3D));

    /* Cell 3: tile $CC at +$0020 Y. */
    DP_AT(0x3B) = 0xCC;
    DP16(0x39) += 0x0020;
    render_sprite_DB9E(0xCC, DP_AT(0x3D));

    /* Cell 4: tile $C8 at ±$0020 X again. */
    DP_AT(0x3B) = 0xC8;
    if ((int8_t)self->vx_hi < 0) DP16(0x37) -= 0x0020;
    else                          DP16(0x37) += 0x0020;
    render_sprite_DB9E(0xC8, DP_AT(0x3D));

    /* Cell 5: tile $8E, attr toggled — at -$0008 Y. */
    DP_AT(0x3B) = 0x8E;
    DP16(0x39) -= 0x0008;
    DP_AT(0x3D) = ((int8_t)self->vx_hi < 0) ? 0x1F : 0x3F;
    /* Then X shifted ±$0010 (different from the ±$0020 above). */
    if ((int8_t)self->vx_hi < 0) DP16(0x37) -= 0x0010;
    else                          DP16(0x37) += 0x0020;
    render_sprite_DB9E(0x8E, DP_AT(0x3D));

    /* Cell 6: tile $AE; Y +$0010 then add to $39. */
    DP_AT(0x3B) = 0xAE;
    DP16(0x39) += 0x0010;
    render_sprite_DB9E(0xAE, DP_AT(0x3D));

    /* (a few more cells lifted from $A88F onward — eyes / antennae) */
    DP16(0x39) += 0x0010;                            /* shift Y +16 */
    render_sprite_DB9E(0xAE, DP_AT(0x3D));

    if ((int8_t)self->vx_hi < 0) DP16(0x37) -= 0x0010;
    else                          DP16(0x37) += 0x0010;
    DP_AT(0x3B) = 0xA8 + (self->pad_e << 1);
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));

    DP16(0x39) -= 0x0010;
    DP_AT(0x3B) -= 0x20;
    render_sprite_DB9E(DP_AT(0x3B), DP_AT(0x3D));
}

/* A7BA — convert ($69/$6A) tile coords -> bank $7F tile-data byte index in X. */
static uint16_t type20_tile_index_A7BA(uint8_t tx, uint8_t ty)
{
    uint16_t a = ((uint16_t)(ty >> 1) << 8) | (uint16_t)(tx << 1);
    return a >> 2;
}

/* State 0 ($04:A6DE): init. */
static void type20_state0_init_A6DE(Entity *self)
{
    scatter_init_D7A1(self);
    self->vx_lo = 0xC0; self->vx_hi = 0x00;          /* vx = $00C0 */
    self->vy_lo = 0x40; self->vy_hi = 0x00;          /* vy = $0040 */
    self->pad_e = 0x02;
    self->timer = 0xC8;                              /* 200 frames */
    self->state = 1;
}

/* State 1 ($04:A704): wander + carve out playfield tiles in bank $7F. */
static void type20_state1_carve_A704(Entity *self)
{
    draw_step_D747(self);
    type20_render_composite_A7C9(self);

    if (FRAME_TICK == 0x04) {
        /* Compute tile coords (entity pos / 16). */
        uint8_t tx = (uint8_t)(self->x >> 4);
        uint8_t ty = (uint8_t)((self->y + 0x18) >> 4);
        DP_AT(0x69) = tx;
        DP_AT(0x6A) = ty;

        uint16_t idx = type20_tile_index_A7BA(tx, ty);

        /* Zero out four 0x800-aligned tile banks at WRAM $7F:4000+ — i.e.
         * carve a hole in the world map (4 layer rows of tile data). */

        wram[0x14000 + idx] = 0;          /* $7F:4000+ in 128KB WRAM = +$10000 */
        wram[0x14800 + idx] = 0;
        wram[0x15000 + idx] = 0;
        wram[0x15800 + idx] = 0;
    }

    if (collision_check_DC84()) {
        self->pad_e--;                     /* turn (face change) */
        self->timer = 0x0F;
        self->state = 2;
    }
}

/* State 2 ($04:A758): timed pause #1. */
static void type20_state2_pause_A758(Entity *self)
{
    type20_render_composite_A7C9(self);

    if (FRAME_TICK != 0x04) return;
    if (--self->timer != 0)  return;

    self->timer = 0x78;                    /* 120 frames */
    self->pad_e--;
    self->state = 3;
}

/* State 3 ($04:A775): timed pause #2. */
static void type20_state3_pause_A775(Entity *self)
{
    type20_render_composite_A7C9(self);

    if (FRAME_TICK != 0x04) return;
    if (--self->timer != 0)  return;

    self->timer = 0x0F;
    self->pad_e++;
    self->state = 4;
}

/* State 4 ($04:A792): velocity negate + recover. */
static void type20_state4_recover_A792(Entity *self)
{
    type20_render_composite_A7C9(self);

    if (FRAME_TICK != 0x04) return;
    if (--self->timer != 0)  return;

    /* Negate 16-bit vx (turn around). */
    uint16_t vx16 = (uint16_t)self->vx_lo | ((uint16_t)self->vx_hi << 8);
    vx16 = (uint16_t)(-(int16_t)vx16);
    self->vx_lo = vx16 & 0xFF;
    self->vx_hi = (vx16 >> 8) & 0xFF;

    self->pad_e++;
    self->state = 1;
}

static void (* const type20_states[5])(Entity *) = {
    type20_state0_init_A6DE,
    type20_state1_carve_A704,
    type20_state2_pause_A758,
    type20_state3_pause_A775,
    type20_state4_recover_A792,
};

void type20_handler_A6C5(Entity *self)
{
    /* Prefix A7C9 is INSIDE state 1's body in our lift (each state already
     * calls it). Originally the JSR was on the dispatcher path; the effect
     * is the same — sprite drawn every frame. */
    type20_states[self->state](self);
}

/* ========================================================================
 * Type 23 — handler at $04:A8D9
 *
 * Init $0000 / $9F. Two states.
 *   0 = init (scatter, clear anim phase, init sprite frame with LDY #$0200,
 *       advance).
 *   1 = main: collision-aware step, every 64 frames re-pick a random anim
 *       phase delta + re-init sprite frame. Render via the A6B1-style
 *       two-table animation with tables at $01:F178/$01:F188 (different
 *       sprite set than the Queen at $01:F138/$01:F158).
 *
 * Tables show ($01:F178): "80 C8 C0 C8 80 C8 C0 C8 84 CC C4 CC 84 CC C4 CC ..."
 * — looks like 4-frame walking animation with 4-tile sprites.
 *
 * Strong guess: BREEDER ANT (or perhaps a different ant caste). The
 * $0200 sprite-init constant is unique to this type; init_word=$0000 means
 * the entity record doesn't carry a sprite seed (the seed is hard-coded in
 * the handler). And $9F init_attr matches the "WORKER" ant variant.
 * Compared to the Queen, this entity has NO collision rebound state — it
 * just keeps walking, randomly turning every 64 frames. A simpler creature.
 *
 * Could also be the YELLOW ANT (the breeder ant in Full Game that becomes
 * a Queen after mating flight). The init_attr $9F matches Worker color
 * palette, not Queen $9E.
 * ======================================================================== */

/* State 0 ($04:A8E9). */
static void type23_state0_init_A8E9(Entity *self)
{
    scatter_init_D7A1(self);
    self->anim_phase = 0;
    sprite_init_D721(self, 0x0200);
    self->state = 1;
}

/* A6B1-style render but with different tables. Sets dp[$82..$87] to point
 * to $01:F178 (tiles) and $01:F188 (attrs) before calling D6F6, just like
 * the inline setup in type 23 state 1's ROM body (A92D..A93D). */
static void type23_render(Entity *self)
{
    (void)self;
    *(uint16_t *)&dp[0x82] = 0xF178;
    dp[0x84] = 0x01;
    *(uint16_t *)&dp[0x85] = 0xF188;
    dp[0x87] = 0x01;
    render_anim_D6F6(/*attr_overlay=*/0x00);
}

/* State 1 ($04:A8FA). */
static void type23_state1_step_A8FA(Entity *self)
{
    /* If no hit, step motion. */
    if (!collision_check_DC84())
        draw_step_D747(self);

    if (FRAME_TICK == 0x04) {
        /* Every 64 frames roughly (clock & $3E == 0) -> re-pick direction. */
        if ((FRAME_TICK & 0x3E) == 0) {
            /* Random delta -1..+1 mod 8 applied to anim phase. */
            uint8_t delta = rng_byte_DCD5(0x03) - 1;
            self->anim_phase = (uint8_t)((self->anim_phase + delta) & 0x07);
            sprite_init_D721(self, 0x0200);
        }
        /* Facing comes from clock bit 1. */
        self->pad_e = (FRAME_TICK >> 1) & 0x01;
    }

    type23_render(self);
}

static void (* const type23_states[2])(Entity *) = {
    type23_state0_init_A8E9,
    type23_state1_step_A8FA,
};

void type23_handler_A8D9(Entity *self)
{
    type23_states[self->state](self);
}

/* ========================================================================
 * Registration helpers — these match the dispatcher table at $04:9A30.
 * (The actual table is in ROM; the C runtime in simant.c reads it at
 * boot. Provided here for reference.)
 * ======================================================================== */
typedef void (*EntityHandler)(Entity *);
const EntityHandler entities_16_through_23[] = {
    [16] = type16_handler_A356,
    [17] = type17_handler_A43B,
    [18] = queen_handler_A533,
    [19] = queen_handler_A533,         /* alias */
    [20] = type20_handler_A6C5,
    [21] = type16_handler_A356,        /* alias */
    /* 22 aliases type 10 — not in our scope */
    [23] = type23_handler_A8D9,
};

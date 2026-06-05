/*
 * scent.c — SimAnt (SNES) PHEROMONE/SCENT SYSTEM
 *
 * WIKI: see wiki/07-scent-system.md for the full system overview, decay
 *       tables, MAX-update semantics, and the place/decay/follow diagram.
 *
 * Lifted from ROM banks $03 + $02. The "scent" system is the colony's
 * shared chemical communication channel — four 2048-byte 2-D maps that
 * the player can visualise via the on-screen "Scent Display" overlay.
 * The system answers four mechanical questions:
 *
 *   PLACE   — when does scent get dropped, by whom, with what value?
 *   DECAY   — how does scent fade over time?
 *   FOLLOW  — how does an ant pick which way to walk based on a gradient?
 *   WASH    — how do scenario-level events (rain) erase scent?
 *
 * ============================================================================
 * STORAGE LAYOUT (4 maps × 2 KB each = 8 KB at $7F:4000-$7F:5FFF)
 * ============================================================================
 *
 *   $7F:4000-47FF   BLACK NEST  scent (2048 bytes)
 *   $7F:4800-4FFF   RED   NEST  scent (2048 bytes)
 *   $7F:5000-57FF   BLACK TRAIL scent (2048 bytes)
 *   $7F:5800-5FFF   RED   TRAIL scent (2048 bytes)
 *
 * Each map is a 64 × 32 cell grid (X by Y). The world is 2048×1024 pixels
 * (entities use 11-bit X / 10-bit Y world coords; see entities_b.c
 * sub_D747_physics_step), so each scent cell covers 32×32 world pixels.
 *
 * Cell indexing: byte_offset = (Y_cell * 64) + X_cell
 *   - Helper $02:F5A8 computes this index from (X, Y) inputs that have
 *     already been pre-shifted by `LSR` (i.e. half-cell precision); see
 *     sub_index_compute_02F5A8 below.
 *   - The four sub-byte indexing schemes (described at each lift):
 *       PLACE/CONSUME  use ant.cell_x / ant.cell_y (range 0..127 / 0..63),
 *                      shift right once -> 0..63 / 0..31
 *       DECAY          iterate raw byte offset 0..2047
 *       FOLLOW         scan 8 neighbours via (dx,dy) offsets at $02:8065/8077
 *
 * Each cell byte is an INTENSITY 0..255. Visualisation maps to 8 tiles:
 *     tile_index = (cell_byte >> 5) + 8   ; 8 strength levels (8..15)
 *     cell_byte == 0  -> tile 1 ("empty")
 *
 * ============================================================================
 * NEST GRID (ROM-driven map, the scent's "anchor")
 * ============================================================================
 *
 *   $7F:6000-7FFF   BLACK NEST tile grid (4 KB; 16-bit per cell on a 64×32
 *                                          grid — the second byte is unused
 *                                          for the scent seed but holds tile
 *                                          attributes for rendering)
 *   $7F:8000-9FFF   RED   NEST tile grid
 *
 *   $7F:E946-E9C5   BLACK nest column-X table  (64 16-bit entries: per Y row,
 *                                                holds the X coord of the
 *                                                nest's main column at that Y)
 *   $7F:E9C6-EA45   RED   nest column-X table
 *
 * The seed routine $03:9269 walks the column table — for each row Y with a
 * non-zero column X, it samples the nest tile at (X, Y). If the tile == $51
 * (probably "passable tunnel"), the seed is 0 (no scent inside the tunnel
 * itself). Otherwise the seed is $FF (max scent). The seed is then written
 * to the half-resolution scent cell (X/2, Y/2). This creates the "scent
 * ring" around each nest.
 *
 * ============================================================================
 * DECAY MODEL (per colony tick, called from $02:AC17 / AC1B / AC38 / AC3C)
 * ============================================================================
 *
 *   NEST  scent decays LINEARLY: new = max(0, old - 1) per tick.
 *   TRAIL scent decays EXPONENTIALLY:  new = (old < 8) ? 0 : old >> 1.
 *
 * I.e. nest scent is a slow, persistent territory marker (256 ticks to
 * decay from $FF); trail scent is a fast-fading breadcrumb (≈5 halvings
 * to vanish from $FF).
 *
 * ============================================================================
 * PLACE MODEL (called from per-ant AI in bank $02)
 * ============================================================================
 *
 * scent_place_max(map, X, Y, value) - "drop scent if stronger than what's
 *   already there" (MAX). Callers pass the colony-color byte of the ant as
 *   `value`, so darker (higher 16-bit) colors leave stronger trails.
 *
 *   WIKI: wiki/07-scent-system.md#2-place--max-update-semantics
 *
 *   The actual ROM helpers are 4 small wrappers, one per map:
 *     $03:9389 -> $7F:4000  (Black Nest)
 *     $03:93AD -> $7F:4800  (Red   Nest)
 *     $03:93D1 -> $7F:5000  (Black Trail)
 *     $03:93F5 -> $7F:5800  (Red   Trail)
 *
 * scent_consume_trail(X, Y, choose_red) - "I'm walking AGAINST a trail —
 *   weaken it". Decrements the cell by 1 if it's positive and not flag-bit
 *   set ($80 = "locked", maybe nest interior). Routes to Black or Red trail
 *   map by the caller's selector argument. ROM: $03:9419.
 *
 * ============================================================================
 * FOLLOW MODEL (sub_gradient_follow_02A710)
 * ============================================================================
 *
 * WIKI: the gradient is THREE concentric paths (scent / target-bias /
 * wander), all funneled through the AAC7 smoothing table. Full lift
 * with mermaid diagram in wiki/06-pathfinding.md §1. The simplified
 * body below covers Path A only — superseded by
 * player_actions_full.c::scent_follow_gradient_full_02A710.
 *
 *   1. Read center cell scent (color-selected by ant's flag).
 *   2. If center is 0, no scent here -> ant wanders aimlessly (early out).
 *   3. Loop 8 compass directions (N, NE, E, SE, S, SW, W, NW):
 *        - Compute neighbour cell with mod-64 (X) and mod-32 (Y) wrap.
 *        - Read neighbour scent value.
 *        - Track the maximum value found and the direction it came from.
 *   4. Pass (current_direction, gradient_direction) to a lookup table at
 *      $02:AAC7 -> $02:AAD8 — an 8x8 table that returns a smoothed next
 *      direction (so ants don't 180° snap).
 *   5. Update the ant's heading.
 *
 * 8-direction offset tables ($02:8065 dx, $02:8077 dy):
 *   N=(+0,-1) NE=(+1,-1) E=(+1,+0) SE=(+1,+1)
 *   S=(+0,+1) SW=(-1,+1) W=(-1,+0) NW=(-1,-1)
 *
 * ============================================================================
 * RAIN WASH (Scenario 3 "Rainy Yard", $02:96A0)
 * ============================================================================
 *
 *   For each cell (called per-frame while rain is active):
 *     Black Nest:  new = max(0, old - 0x14)   ; weaken by 20
 *     Red   Nest:  new = max(0, old - 0x14)
 *     Black Trail: new = 0                     ; fully erased
 *     Red   Trail: new = 0
 *
 * The trail's full erasure matches the manual: rain washes away food trails
 * but the nest territory only fades.
 *
 * ============================================================================
 * GLOBAL RESET ($03:85DA, $03:FB07)
 * ============================================================================
 *
 * At scenario load / new game, all four scent maps are STZ-cleared.
 * Same loop, two locations (one for full game, one for scenario load).
 */

#include <stdint.h>
#include <string.h>

/* External dependencies (defined in simant.c / runtime). */
extern uint8_t wram[0x20000];           /* $7E:0000-$7F:FFFF */

/* Direct page alias for $7E:0000-$7E:00FF. */
#define dp wram

/* WRAM helpers: $7E/$7F bank addressing into the contiguous 128KB array. */
#define WRAM_7F(off)   (*(uint8_t *)&wram[0x10000 + ((off) & 0xFFFF)])
#define WRAM_7F_W(off) (*(uint16_t *)&wram[0x10000 + ((off) & 0xFFFF)])

/* ============================================================================
 * Map layout constants — keep these as #defines so the port can repoint
 * them on Flipper without touching the algorithms.
 * ============================================================================ */
#define SCENT_MAP_W            64        /* cells wide  (X: 0..63)            */
#define SCENT_MAP_H            32        /* cells tall  (Y: 0..31)            */
#define SCENT_MAP_BYTES        2048      /* 64 * 32                           */
#define SCENT_PIXELS_PER_CELL  32        /* world pixel pitch                 */

/* SNES WRAM base offsets (relative to $7F:0000). */
#define SCENT_BLACK_NEST       0x4000
#define SCENT_RED_NEST         0x4800
#define SCENT_BLACK_TRAIL      0x5000
#define SCENT_RED_TRAIL        0x5800

/* Nest tile grids (ROM-loaded, used for scent SEEDING). */
#define NEST_BLACK_TILES       0x6000    /* 4 KB: 64*32 cells * 2 bytes/cell */
#define NEST_RED_TILES         0x8000

/* Nest column tables (per-row X coordinate of the nest's main shaft). */
#define NEST_BLACK_COL_TABLE   0xE946    /* 64 16-bit entries (one per row) */
#define NEST_RED_COL_TABLE     0xE9C6

/* Magic tile value: cells where the tile == $51 are "tunnel interior" and
 * receive ZERO scent during the seed pass (so ants don't get drawn into the
 * nest centre by the gradient). All other tile values seed $FF. */
#define NEST_TILE_TUNNEL       0x0051

/* Enum constants — make these visible to the rest of the engine. */
enum ScentColor    { SCENT_BLACK = 0, SCENT_RED = 1 };
enum ScentKind     { SCENT_NEST = 0, SCENT_TRAIL = 1 };

/* ============================================================================
 * sub_index_compute_02F5A8 — $02:F5A8
 *
 * The "convert (X_half, Y_half) into byte offset" helper. Used everywhere a
 * scent map is indexed.
 *
 *   ROM:  STX $F1
 *         TYA
 *         XBA          ; A = Y << 8
 *         LSR          ; A = Y << 7
 *         LSR          ; A = Y << 6   (= Y * 64)
 *         ADC $F1      ; + X
 *         TAX
 *
 * Note the inputs (X, Y) are already half-resolution: the callers do `LSR X
 * / LSR Y` before this. So the actual entity-coord -> cell-offset is:
 *
 *     cell_offset = ((entity_pixel_Y / 2) * 64) + (entity_pixel_X / 2)
 *
 * The pixel-X/Y passed by callers is in the "cell" coordinate scheme (not
 * 11-bit world X). For an entity at world (X, Y), the AI converts to cell
 * coordinates separately before calling scent_place.
 * ============================================================================ */
static uint16_t scent_index_F5A8(uint16_t x_half, uint16_t y_half)
{
    /* (Y >> 1) << 6 + (X >> 1).  X is masked low-byte (0..127), Y low-byte
     * (0..63). After the caller's pre-shifts (LSR X, LSR Y), X is 0..63 and
     * Y is 0..31 — yielding 0..2047 byte offsets. */
    return (uint16_t)((((y_half) & 0xFF) << 6) | ((x_half) & 0x3F));
}

/* ============================================================================
 * SCENT PLACE — drop scent if stronger than the current cell value
 * ============================================================================
 *
 * ROM signatures (one wrapper per map):
 *   $03:9389  scent_place_max_4000_BlackNest(value, X, Y)
 *   $03:93AD  scent_place_max_4800_RedNest  (value, X, Y)
 *   $03:93D1  scent_place_max_5000_BlackTrail(value, X, Y)
 *   $03:93F5  scent_place_max_5800_RedTrail (value, X, Y)
 *
 * X and Y arrive in CPU X and Y registers (pre-half-resolution). The
 * `value` arrives on the stack — actually pushed via PHA at the helper's
 * entry, then re-read from the stack offset 1.
 *
 * Body:
 *   LSR X / LSR Y / JSL $02F5A8     ; X = cell byte offset
 *   LDA scent_map[X]
 *   CMP value (16-bit)              ; (only low byte is meaningful since
 *                                    ;  scent bytes are 8-bit, but the
 *                                    ;  comparison is unsigned 16-bit)
 *   BCS skip                         ; old >= new -> keep old
 *   STA scent_map[X] = value         ; otherwise write the new (stronger)
 * ============================================================================ */
static void scent_place_max_internal(uint16_t map_base,
                                     uint8_t value,
                                     uint16_t x, uint16_t y)
{
    uint16_t idx = scent_index_F5A8(x >> 1, y >> 1);
    uint8_t  existing = WRAM_7F(map_base + idx);
    if (value > existing)
        WRAM_7F(map_base + idx) = value;
}

/* Public entry points (one per scent map). The byte at $7F:CFA0+ant is the
 * ant's "color" byte that ROM passes in A; for nest scents it represents
 * the colony, for trail scents it represents the ant's per-frame intensity.
 * In the original SimAnt manual this maps to "thick" vs "faint" trails. */
void scent_place_black_nest_03_9389 (uint8_t value, uint16_t x, uint16_t y)
{ scent_place_max_internal(SCENT_BLACK_NEST,  value, x, y); }

void scent_place_red_nest_03_93AD   (uint8_t value, uint16_t x, uint16_t y)
{ scent_place_max_internal(SCENT_RED_NEST,    value, x, y); }

void scent_place_black_trail_03_93D1(uint8_t value, uint16_t x, uint16_t y)
{ scent_place_max_internal(SCENT_BLACK_TRAIL, value, x, y); }

void scent_place_red_trail_03_93F5  (uint8_t value, uint16_t x, uint16_t y)
{ scent_place_max_internal(SCENT_RED_TRAIL,   value, x, y); }

/* ============================================================================
 * SCENT CONSUME — weaken trail scent under a passing ant
 * ============================================================================
 *
 *   ROM: $03:9419  scent_consume_trail(arg, X, Y)
 *
 * The argument `arg` is the ant's colony color flag (the caller does
 * `AND #$0080` on a per-ant byte): non-zero -> red, zero -> black.
 *
 * Body:
 *   LSR X / LSR Y / JSL $02F5A8
 *   if arg != 0:        ; RED ant -> decrement RED trail
 *     LDA [$5800,X]
 *     if 0 or bit7-set: skip      ; treat $80..$FF as locked (= max-strength
 *                                ;  trail laid by carrying ant; don't
 *                                ;  weaken it further)
 *     else: DEC; STA [$5800,X]
 *   else:               ; BLACK ant
 *     same on $5000
 *
 * The "BMI skip" path on bit 7 is interesting: it means trail values >= $80
 * are PROTECTED from this weaken pass. Probably the strong-trail marker
 * from a food-carrying ant.
 * ============================================================================ */
void scent_consume_trail_03_9419(uint8_t arg, uint16_t x, uint16_t y)
{
    uint16_t idx = scent_index_F5A8(x >> 1, y >> 1);
    uint16_t base = (arg != 0) ? SCENT_RED_TRAIL : SCENT_BLACK_TRAIL;

    uint8_t  cur = WRAM_7F(base + idx);
    if (cur == 0)        return;
    if (cur & 0x80)      return;        /* locked / max-strength trail */
    WRAM_7F(base + idx) = (uint8_t)(cur - 1);
}

/* ============================================================================
 * SCENT DECAY — per-colony tick (linear for nest, exponential for trail)
 * ============================================================================
 *
 * Dispatch (called from $02:AC00 — the colony-tick router):
 *   colony == 1 (Black): JSL $03:931B + JSL $03:934B   (decay black maps)
 *   colony == 3 (Red):   JSL $03:9333 + JSL $03:936A   (decay red maps)
 *
 * Decay laws:
 *   NEST  (linear): for each cell byte, if non-zero, decrement by 1.
 *   TRAIL (halving): for each cell byte, if value >= 8 -> halve; else 0.
 *
 * Both walk the full 2048-byte array (CPX #$0800 loop bound).
 *
 * ROM bodies are essentially the same shape; one DEC vs LSR + cutoff.
 * ============================================================================ */
void scent_decay_nest_black_03_931B(void)
{
    uint8_t *m = &WRAM_7F(SCENT_BLACK_NEST);
    for (uint16_t i = 0; i < SCENT_MAP_BYTES; ++i)
        if (m[i]) m[i]--;
}

void scent_decay_nest_red_03_9333(void)
{
    uint8_t *m = &WRAM_7F(SCENT_RED_NEST);
    for (uint16_t i = 0; i < SCENT_MAP_BYTES; ++i)
        if (m[i]) m[i]--;
}

void scent_decay_trail_black_03_934B(void)
{
    uint8_t *m = &WRAM_7F(SCENT_BLACK_TRAIL);
    for (uint16_t i = 0; i < SCENT_MAP_BYTES; ++i)
        m[i] = (m[i] < 8) ? 0 : (uint8_t)(m[i] >> 1);
}

void scent_decay_trail_red_03_936A(void)
{
    uint8_t *m = &WRAM_7F(SCENT_RED_TRAIL);
    for (uint16_t i = 0; i < SCENT_MAP_BYTES; ++i)
        m[i] = (m[i] < 8) ? 0 : (uint8_t)(m[i] >> 1);
}

/* ============================================================================
 * SCENT FOLLOW — pick a heading from the local 8-neighbour gradient
 *
 * WIKI: wiki/07-scent-system.md#4-follow--8-neighbour-scan-with-turn-smoothing
 * ============================================================================
 *
 *   ROM: $02:A710  follow_nest_scent_gradient(ant)
 *
 * Inputs (read from $7F:F6xx — the AI's per-call scratch frame):
 *   $F61B   X_cell (0..63)
 *   $F61D   Y_cell (0..31)
 *   $F619   color: 0 -> read black nest, !=0 -> read red nest
 *   $F607   current_direction (0..7)
 *
 * Outputs:
 *   $F611   center scent value (for "should I follow at all?")
 *   $F60F   max scent value found
 *   $F615   direction (0..7) of the max
 *   THEN: JSL $02:AAC7(current_dir, gradient_dir) — turn-smoothing lookup
 *         returns the actual next heading. Stored back through $F607.
 *
 * The 8-direction offsets ($02:8065 dx, $02:8077 dy) — compass clockwise
 * from north:
 * ============================================================================ */
static const int8_t scent_dir_dx_028065[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
static const int8_t scent_dir_dy_028077[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

/* The smoothed-turn lookup at $02:AAD8 — an 8x8 table indexed as
 *   next_dir = table[current_dir * 8 + gradient_dir]
 * It blunts 180° flips and gives a more natural-looking ant walk.
 *
 * Recovered from ROM bytes at $02:AAD8 (first 8 entries dumped manually;
 * the full 64-entry shape was reconstructed by reading sequential entries).
 * Each byte is a direction (0..7). */
static const uint8_t scent_turn_smooth_02AAD8[64] = {
    /* current=0 (N): gradient=N..NW -> stay N/NE for 0..3, swing to NW for 4..7
     * (i.e. "if the scent is roughly behind you, drift the side that keeps the
     *  current bias rather than U-turning"). */
    0x00, 0x01, 0x01, 0x01, 0x07, 0x07, 0x07, 0x07,
    /* current=1 (NE) */
    0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00,
    /* current=2 (E) */
    0x01, 0x01, 0x02, 0x03, 0x03, 0x03, 0x03, 0x01,
    /* current=3 (SE) */
    0x02, 0x02, 0x02, 0x03, 0x04, 0x04, 0x04, 0x04,
    /* current=4 (S) */
    0x03, 0x03, 0x03, 0x03, 0x04, 0x05, 0x05, 0x05,
    /* current=5 (SW) */
    0x06, 0x06, 0x04, 0x04, 0x04, 0x05, 0x06, 0x06,
    /* current=6 (W) */
    0x07, 0x07, 0x07, 0x05, 0x05, 0x05, 0x06, 0x07,
    /* current=7 (NW) */
    0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x06, 0x07,
};

/* Helper: pick best gradient direction (max scent in 8 neighbours).
 * Returns 0..7. Out_max is the scent value at the picked neighbour. */
static uint8_t scent_pick_gradient_dir(uint16_t map_base,
                                       int x_cell, int y_cell,
                                       uint8_t *out_max)
{
    uint8_t best_val = 0;
    uint8_t best_dir = 0;
    for (uint8_t d = 0; d < 8; ++d) {
        int nx = (x_cell + scent_dir_dx_028065[d]) & (SCENT_MAP_W - 1);
        int ny = (y_cell + scent_dir_dy_028077[d]) & (SCENT_MAP_H - 1);
        uint16_t idx = scent_index_F5A8(nx, ny);
        uint8_t v = WRAM_7F(map_base + idx);
        if (v > best_val) {
            best_val = v;
            best_dir = d;
        }
    }
    if (out_max) *out_max = best_val;
    return best_dir;
}

/* ============================================================================
 * NOTE — SUPERSEDED BY player_actions_full.c::scent_follow_gradient_full_02A710
 * ----------------------------------------------------------------------------
 * This simplification was a first lift. The ROM body at $02:A712 actually
 * has THREE concentric paths (scent-gradient / target-following /
 * wander) — see the full lift for details. This stub kept only PATH A
 * (the 8-neighbour gradient scan) and silently falls back to
 * `current_dir` when there is no scent (the ROM follows a home target
 * or wanders edge-aware-randomly in that case).
 *
 * The disasm at $02:A710 starts at $02:A712 (the first two bytes
 * `C4 A7` are operand bytes for the `JMP $A7C4` at $02:A70F); the prior
 * lift correctly captured this address but missed the no-scent paths.
 *
 * KEEP this for backward compatibility (existing callers); prefer the
 * full version for new work. */
/* SUPERSEDED — use player_actions_full.c::scent_follow_gradient_full_02A710. */
uint8_t scent_follow_gradient_02A710(uint8_t color,
                                     uint8_t x_cell, uint8_t y_cell,
                                     uint8_t current_dir,
                                     uint8_t *out_center_value)
{
    uint16_t map_base = (color != 0) ? SCENT_RED_NEST : SCENT_BLACK_NEST;
    uint16_t idx = scent_index_F5A8(x_cell, y_cell);
    uint8_t  center = WRAM_7F(map_base + idx);

    if (out_center_value) *out_center_value = center;
    if (center == 0)
        return current_dir;        /* no scent here -> keep walking
                                    * (SIMPLIFIED — real ROM follows home
                                    * target via $02:98ED in this case) */

    uint8_t grad_dir = scent_pick_gradient_dir(map_base,
                                               x_cell, y_cell, NULL);

    /* Smooth the turn. The lookup softens 180° flips: an ant heading
     * North (dir 0) that sees the strongest scent due South (dir 4)
     * doesn't snap — it takes the table-defined intermediate heading
     * (usually a tangential step). */
    uint8_t next_dir = scent_turn_smooth_02AAD8[(current_dir & 7) * 8
                                               + (grad_dir    & 7)];
    return next_dir & 7;
}

/* ============================================================================
 * SCENT WASH — rain (Scenario 3 "Rainy Yard") sweeps the maps
 *
 * WIKI: wiki/07-scent-system.md#5-rain-wash--scenario-3-rainy-yard
 * ============================================================================
 *
 *   ROM: $02:96A0  rain_wash_cell(cell_offset)
 *
 * The original is called per-cell from a 2048-iteration loop (cell_offset
 * walks 0..2047). For each cell:
 *   - Nest scents lose 20 ($14) per pass, clamped at 0
 *   - Trail scents are zeroed outright
 *
 * Behavior matches the manual's "rain washes scent away" — the chemical
 * the ants left on the ground is gone, but the nest-aura territory only
 * weakens (the ants are still inside, still smelling like home).
 * ============================================================================ */
void scent_rain_wash_cell_02_96A0(uint16_t cell_offset)
{
    uint8_t v;

    /* Black nest: weaken by 0x14 (20). */
    v = WRAM_7F(SCENT_BLACK_NEST + cell_offset);
    WRAM_7F(SCENT_BLACK_NEST + cell_offset) = (v < 0x14) ? 0 : (uint8_t)(v - 0x14);

    /* Black trail: erase. */
    WRAM_7F(SCENT_BLACK_TRAIL + cell_offset) = 0;

    /* Red nest: weaken by 0x14. */
    v = WRAM_7F(SCENT_RED_NEST + cell_offset);
    WRAM_7F(SCENT_RED_NEST + cell_offset) = (v < 0x14) ? 0 : (uint8_t)(v - 0x14);

    /* Red trail: erase. */
    WRAM_7F(SCENT_RED_TRAIL + cell_offset) = 0;
}

/* Convenience wrapper: walk all 2048 cells (the caller in bank 2 will
 * have a wrapping loop that paces this across frames, but for a port we
 * can run the full pass in one call). */
void scent_rain_wash_all(void)
{
    for (uint16_t i = 0; i < SCENT_MAP_BYTES; ++i)
        scent_rain_wash_cell_02_96A0(i);
}

/* ============================================================================
 * SCENT SEED — initial nest-aura imprint at scenario start
 * ============================================================================
 *
 *   ROM: $03:9269  seed_black_nest_scent()
 *        $03:92C2  seed_red_nest_scent()
 *
 * The seed routine "burns" the nest's column outline into the scent map.
 * Each colony has a column-X table at $7F:E946 (black) / $7F:E9C6 (red),
 * 64 16-bit entries — one per Y row of the nest tile grid. The entry
 * holds the X column of that row's main shaft (or 0 if no nest here).
 *
 *  Walk Y = 0..63:
 *    col_x = colony_col_table[Y]               ; 16-bit value, ASL Y indexes
 *    if col_x == 0:  skip (no nest in this row)
 *    JSL $02:F5A8(col_x, Y)                    ; idx = (Y<<6) + col_x (BYTES)
 *    SEP #$20; LDA $6000,x                     ; 8-bit read of tile
 *    if tile == $51:  seed = 0                 ; tunnel interior
 *    else:            seed = $FF               ; nest wall / chamber
 *    JSL $02:F5A8(col_x>>1, Y>>1)              ; half-res scent index
 *    scent_map[idx] = seed
 *
 * NOTE: the nest tile grid at $7F:6000 is a 64-row × 64-column array of
 * 1-byte cells (4 KB). The ROM reads it via an 8-bit LDA. An earlier draft
 * of this file treated it as a 64×32 grid of 16-bit cells — that's wrong;
 * the tile value is the LOW byte only.
 *
 * In effect: every nest WALL/CHAMBER cell gets $FF, every interior tunnel
 * cell gets 0. The ants then drop trail scent on top of this baseline as
 * they forage, and other ants follow the gradient back to the nest.
 * ============================================================================ */
static void scent_seed_one_colony(uint16_t col_table_off,
                                  uint16_t nest_tiles_off,
                                  uint16_t scent_map_off)
{
    for (uint16_t y = 0; y < 64; ++y) {
        uint16_t col_x = *(uint16_t *)&WRAM_7F(col_table_off + y*2);
        if (col_x == 0) continue;

        /* Read nest tile at (col_x, y). The nest grid is 64x64 with 1 byte
         * per cell ($6000-$6FFF = 4 KB). The ROM does:
         *     JSL F5A8(col_x, y)   -> X = (y << 6) + col_x
         *     SEP #$20; LDA $6000,X (8-bit read)
         */
        uint16_t tile_idx = (uint16_t)((y << 6) + col_x);
        uint8_t  tile = WRAM_7F(nest_tiles_off + tile_idx);
        uint8_t  seed = (tile == (NEST_TILE_TUNNEL & 0xFF)) ? 0x00 : 0xFF;

        /* Write to half-resolution scent cell (col_x/2, y/2). */
        uint16_t scent_idx = scent_index_F5A8(col_x >> 1, y >> 1);
        WRAM_7F(scent_map_off + scent_idx) = seed;
    }
}

void scent_seed_black_03_9269(void)
{
    scent_seed_one_colony(NEST_BLACK_COL_TABLE,
                          NEST_BLACK_TILES,
                          SCENT_BLACK_NEST);
}

void scent_seed_red_03_92C2(void)
{
    scent_seed_one_colony(NEST_RED_COL_TABLE,
                          NEST_RED_TILES,
                          SCENT_RED_NEST);
}

/* ============================================================================
 * SCENT RESET — full wipe (game start / new scenario)
 * ============================================================================
 *
 *   ROM: $03:85DA  scent_reset_all()
 *   ROM: $03:FB07  (alternate entry — same body)
 *
 * Just a 4-way STZ over the entire 8 KB region.
 * ============================================================================ */
void scent_reset_all_03_85DA(void)
{
    memset(&WRAM_7F(SCENT_BLACK_NEST),  0, SCENT_MAP_BYTES);
    memset(&WRAM_7F(SCENT_RED_NEST),    0, SCENT_MAP_BYTES);
    memset(&WRAM_7F(SCENT_BLACK_TRAIL), 0, SCENT_MAP_BYTES);
    memset(&WRAM_7F(SCENT_RED_TRAIL),   0, SCENT_MAP_BYTES);
}

/* ============================================================================
 * SCENT VISUALISATION — what the player sees during "Scent Display"
 * ============================================================================
 *
 * The overlay is rendered by the surface-overview tile composer at
 * $00:E844 (lifted in states_gameplay.c as state $2C). For each on-screen
 * BG tile, the composer consults:
 *
 *     scent_map_base    = pointer_table_E8FB[dp[$02B4]]  in WRAM $7F
 *     dp[$02B4] = 0 -> $7F:0000 (terrain — "Hide")
 *                 1 -> $7F:4000 (Black Nest)
 *                 2 -> $7F:4800 (Red Nest)
 *                 3 -> $7F:5000 (Black Trail)
 *                 4 -> $7F:5800 (Red Trail)
 *
 * For each tile cell the value is read via long-indirect [dp[$79]],Y, then:
 *
 *     tile_id = ((cell_byte >> 5) << 1) & 0x0E    ; bits 4..6 doubled (0..14)
 *     if tile_id != 0:  add 0x80                  ; -> $80, $82, ..., $8E
 *     else:             use default terrain tile  ; $23E6
 *
 * I.e. 8 visible scent intensities mapped to 8 tile patterns ($80..$8E
 * even); zero scent leaves the underlying terrain visible.
 *
 * The display kind ($02B4) is set by the icon-menu "Scent Display" option
 * — value 0..4 — and is captured at the moment state $2C is entered.
 * ============================================================================ */
uint8_t scent_visualize_tile(uint8_t cell_value)
{
    /* Match the ROM mapping at $00:E8C0-$00:E8DE. */
    uint8_t tile = (uint8_t)((cell_value >> 4) & 0x0E);
    return (tile == 0) ? 0 : (uint8_t)(tile + 0x80);
}

/* ============================================================================
 * Cell coordinate helpers — these convert between the layered coord
 * systems the ROM uses.
 *
 *   WORLD    pixel X 0..2047,  Y 0..1023   (entity physics; 11+10 bits)
 *   HIGH-RES cell  X 0..127,   Y 0..63     (used by nest tile grid; 32-px
 *                                            cells in pixel space)
 *   SCENT    cell  X 0..63,    Y 0..31     (the scent map's native grid;
 *                                            64-px cells in pixel space)
 *
 * The scent map deliberately uses HALF the resolution of the nest tile
 * grid — one scent cell covers 2x2 nest tiles.
 * ============================================================================ */
uint16_t scent_world_to_cell_offset(uint16_t world_x, uint16_t world_y)
{
    uint16_t cell_x = (world_x >> 5) & (SCENT_MAP_W - 1);
    uint16_t cell_y = (world_y >> 5) & (SCENT_MAP_H - 1);
    return (uint16_t)((cell_y * SCENT_MAP_W) + cell_x);
}

/* ============================================================================
 * Compile-check anchor — keeps the toolchain from dropping the lifts.
 * ============================================================================ */
__attribute__((used))
static const void *const _scent_refs[] = {
    (const void *)scent_index_F5A8,
    (const void *)scent_place_black_nest_03_9389,
    (const void *)scent_place_red_nest_03_93AD,
    (const void *)scent_place_black_trail_03_93D1,
    (const void *)scent_place_red_trail_03_93F5,
    (const void *)scent_consume_trail_03_9419,
    (const void *)scent_decay_nest_black_03_931B,
    (const void *)scent_decay_nest_red_03_9333,
    (const void *)scent_decay_trail_black_03_934B,
    (const void *)scent_decay_trail_red_03_936A,
    (const void *)scent_pick_gradient_dir,
    (const void *)scent_follow_gradient_02A710,
    (const void *)scent_rain_wash_cell_02_96A0,
    (const void *)scent_rain_wash_all,
    (const void *)scent_seed_black_03_9269,
    (const void *)scent_seed_red_03_92C2,
    (const void *)scent_reset_all_03_85DA,
    (const void *)scent_visualize_tile,
    (const void *)scent_world_to_cell_offset,
    (const void *)scent_dir_dx_028065,
    (const void *)scent_dir_dy_028077,
    (const void *)scent_turn_smooth_02AAD8,
};

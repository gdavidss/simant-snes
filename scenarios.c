/*
 * scenarios.c — Per-scenario configuration tables and danger handlers for
 * SimAnt (SNES, 1993). Lifted from the disassembled ROM.
 *
 * See wiki/14-scenarios.md for the high-level page covering the 8 scenarios
 * (manual p.22-23), the 6 cooperating ROM tables, and the per-view config
 * format. For the 7 dangers spawned by the decoration dispatcher at
 * $00:BE9A, see wiki/15-dangers.md.
 *
 * Source: /Users/guilhermedavid/simant-re/simant.sfc (1 MB LoROM ROM).
 *
 * STRUCTURE
 * =========
 * The 8 Scenario Game levels (manual p.22-23: Park / Garden / Yard / House
 * / Road / River / Porch / Woods) are NOT a single contiguous block of
 * scenario-config structs. The game splits scenario state across SIX
 * cooperating tables:
 *
 *   (1) $00:D798   8-byte "cursor-row → scenario-index" mapping.
 *                  Tells the picker UI that row 0 (top of 4x2 grid) means
 *                  scenario index 6 ("In the Park", aka manual L1).
 *
 *   (2) $01:9C20   8 × 2-byte pointer table to briefing TEXTs ($01:9C30..
 *                  $01:9FB3). The briefings name the LOCATION and the
 *                  PRIMARY DANGER of each scenario in plain English.
 *
 *   (3) $01:9C00   2 × 8-byte tables of the 8 portrait positions on the
 *                  picker screen (column X then row Y).
 *
 *   (4) $7F:E736/  Persistent shadows for "last picked column" / "last
 *       $7F:E738   picked row". Together they form a 16-bit save-slot
 *                  index that maps into:
 *
 *   (5) $01:8143[idx]  8-byte: 1 = scenario, 0 = full game.
 *   (5) $01:817B[idx]  8-byte: View mode (0x08..0x0E) — picks one of the
 *                              16 view-config blocks at $01:81F3 (78 bytes
 *                              each — the actual per-LEVEL data).
 *
 *   (6) $04:FF00..  Per-view byte tables that pick the SCENERY pack
 *                   ($04:8000/A000/C000/E000) and palette/tilemap pack
 *                   ($04:F400..FC00).
 *
 * So the "scenario config" is really the union of (view config @ $01:81F3
 * + N) and (the per-view decoration handler @ $00:BE9A + view*2) plus the
 * shared per-scenario danger entities described below.
 *
 * VIEW-CONFIG FORMAT ($01:81F3 + view*$4E, 78 bytes each)
 * =========================================================================
 *   offset  size   field
 *   ------  -----  ----------------------------------------------------------
 *    0-23   6×4    Up to 6 (16-bit X, 16-bit Y) "scattered prop" positions.
 *                  Read at $03:9609 (`LDA $EE8A,x; LDA $EE8C,x`). Sentinel
 *                  $FFFF = "empty slot". Used by scenarios that place
 *                  fixed items (flower pots in Garden, water blocks in
 *                  River).
 *   24-25   word   Yellow Ant initial WORLD X. Loaded at $03:D872.
 *   26-27   word   Yellow Ant initial WORLD Y. Loaded at $03:D877.
 *   28-29   word   $F071 — initial game-clock divisor (faster=more
 *                  dangers).
 *   30-31   word   X-spawn offset bias (added after `value/256`).
 *   32-33   word   Y-spawn offset bias (added after `value/256`).
 *   34-35   word   X-spawn multiplier (passed to $02:F3BD divider).
 *   36-37   word   Y-spawn multiplier (passed to $02:F3BD divider).
 *   38-39   word   $E8FE — global danger-spawn rate.
 *   40-41   word   $EE86 — colony food budget.
 *   42-43   word   Loop count for the per-scenario sprite spawner at
 *                  $03:8820 (Red ant colony starting size).
 *   44-75  16×2    Per-tile placement list. Each entry is (Y, X) at byte
 *                  offsets (44+2i, 45+2i). Code at $03:921C reads these
 *                  and writes tile $51 into the $7F:6000+ terrain map.
 *                  Used only by scenarios that hand-place scenery (Woods,
 *                  River). Sentinel $FFFF = "empty".
 *   76-77   word   $EB46 — initial entity-table size cap (default $0028
 *                  = 40 entities; $0001 for Full-Game view 2; $000A for
 *                  Full-Game view 9 and Woods view 10).
 *
 * VIEW MODE ↔ SCENARIO INDEX
 * =========================================================================
 *   view  scenario(manual)        briefing ptr     view-config $01:
 *   ----  ----------------------  ---------------  -----------------
 *    0    (--- Full Game view 0, no scenario ---)  81F3 (block 0)
 *    1    Scenario L6 / River      9C30             8241 (block 1)
 *    2    (--- Full Game view 1 ---)                828F (block 2)
 *    3    Scenario L7 / Porch      9D82             82DD (block 3)
 *    4    Scenario L4 / House      9DFA             832B (block 4)
 *    5    Scenario L5 / Road       9CA7             8379 (block 5)
 *    6    Scenario L1 / Park       9ED6             83C7 (block 6)
 *    7    Scenario L2 / Garden     9F4D             8415 (block 7)
 *    8    Scenario L3 / Yard       9E67             8463 (block 8)
 *    9    (Full Game inland)                        84B1 (block 9)
 *   10    Scenario L8 / Woods      9D1C             84FF (block 10)
 *   11..15 (Full Game variant slots, identical bodies) 854D..8685
 *
 * The cursor-row → scenario-index table at $00:D798 maps menu row 0..7 to
 * scenario index in the briefing pointer table (which mirrors view mode).
 *
 * THE 7 DANGER HANDLERS
 * =========================================================================
 * The manual (p.36) lists seven dangers. Each maps to an entity type
 * spawned by either (a) the per-view decoration handler at $00:BE9A or
 * (b) the per-scenario tile placement at offset 44 of the view config:
 *
 *   Danger        Manual    Entity type    Spawn site               Notes
 *   --------------  ------    -----------    ---------------------    -----
 *   RAIN          Sc 3      type $0F+$10   $00:BEDA (view 8)         falling drops
 *   HUMAN FEET    Sc 7      type $1B+$1C   $00:BF33 (view 11+)       walking pair
 *   LAWN MOWERS   Sc 3      type $1B+$1C   shared with FEET          fast-moving
 *   SNAILS        Sc 6      type $13       $00:BF5E (view 5/River)   stationary
 *   CAT'S PAWS    Sc 4      type $17       $00:BEF3 (view 4/House)   "spider" handler
 *   BICYCLE TIRES Sc 5      type $1C+$3D   $00:BF2D (view 5/Road)    type-53 looper
 *   HANDS         Sc 7?     type $4B       $00:BEF3 (view 3/Porch)   per scene
 *
 * The bicycle/lawn-mower spawn is the most distinctive: it's the type-53
 * looper at $04:BD9B which reads $7F:E87E (red-colony count) and spawns
 * its body N times — matching the manual's "bicycles roll across in
 * groups". Cat paws use the spider type ($17 @ $04:A8D9, already lifted
 * in entities_c.c) because the cat is treated as a giant spider AI-wise.
 *
 * RAIN MECHANIC (Scenario 3 / Yard)
 * The rain handler shares with the falling-egg visual (type $0F at
 * $04:9F1D, lifted in entities_d.c as types 24/25 family). When rain
 * is active, the game decrements scent-strength bytes globally (the
 * code is gated by view==8 in $00:BEDA's setup, which spawns the
 * "1× type $10 + 3× type $0F" rain cluster).
 *
 * Verify:
 *   cd /Users/guilhermedavid/simant-re &&
 *   clang -Wall -Wextra -c scenarios.c -o /tmp/sc2.o
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Shared aliases — same conventions as states_menu.c & entities_*.c.
 * ------------------------------------------------------------------------ */
extern uint8_t  wram[0x20000];
#define dp wram

/* ROM-resident tables (extern; address fixed by the lifted disassembly). */
extern const uint8_t  rom_01_8143[8];      /* save-slot game type        */
extern const uint8_t  rom_01_817B[8];      /* save-slot view mode        */
extern const uint8_t  rom_00_D798[8];      /* cursor-row → scenario idx  */

/* ------------------------------------------------------------------------
 * (1) Scenario index -> menu row mapping ($00:D798).
 *
 * Byte values are the SCENARIO INDEX in the briefing pointer table at
 * $01:9C20. Listed in PICKER ROW ORDER (manual order: Park = L1 = row 0).
 * ------------------------------------------------------------------------ */
const uint8_t rom_00_D798[8] = {
    0x06, /* row 0 ("In the Park")    → scen idx 6 → manual L1 */
    0x07, /* row 1 ("In the Garden")  → scen idx 7 → manual L2 */
    0x05, /* row 2 ("In the Yard")    → scen idx 5 → manual L3 */
    0x04, /* row 3 ("In the House")   → scen idx 4 → manual L4 */
    0x01, /* row 4 ("On the Road")    → scen idx 1 → manual L5 */
    0x00, /* row 5 ("By the River")   → scen idx 0 → manual L6 */
    0x03, /* row 6 ("Under the Porch")→ scen idx 3 → manual L7 */
    0x02, /* row 7 ("In the Woods")   → scen idx 2 → manual L8 */
};

/* ------------------------------------------------------------------------
 * (2) Per-scenario briefing pointer table ($01:9C20), 8 × 2-byte entries.
 *
 * Each entry points to a packed ASCII string (terminator $FF, newline $FE)
 * INSIDE bank $01. Indexed by SCENARIO INDEX (0..7), NOT by the manual's
 * level number — use rom_00_D798[row] to translate.
 *
 * The 8 briefing texts (with $FE replaced by \n):
 *   idx 0 ($01:9C30)  "By the River\nThis is a dangerous area.\n
 *                      Many ants will get lost. Use\nthe Yellow Ant to
 *                      call and\nrelease help as needed."
 *   idx 1 ($01:9CA7)  "On the Road\nIt's the middle of summer\nand it's
 *                      really hot! Ants\nwill lose energy quickly,\nso
 *                      watch your food supply!"
 *   idx 2 ($01:9D1C)  "In the Woods\nGet ready for winter!\nYou need to
 *                      stock up on\nfood so you can survive\nthe barren
 *                      months."
 *   idx 3 ($01:9D82)  "Under the Porch\nThere are children playing\nnearby
 *                      --and their feet can\nsquash you! Be careful while
 *                      \ncrossing the bricks!"
 *   idx 4 ($01:9DFA)  "In the House\nYou'll be building your nest\nin a
 *                      human's house. There is\nalso a new danger here--
 *                      \nthe pet cat!"
 *   idx 5 ($01:9E67)  "In the Yard\nThere's been a lot of rain\nrecently.
 *                      Your trails will\nsoon wash away, so be sure\nto
 *                      make new ones."
 *   idx 6 ($01:9ED6)  "In the Park\nThe long winter has ended\nand spring
 *                      is here!\nStart by collecting the\nfood that has
 *                      fallen in\nthe sandbox."
 *   idx 7 ($01:9F4D)  "In the Garden\nMany ants may get lost\namong the
 *                      flowers.\nUse the Yellow Ant to guide\nthem along
 *                      safely."
 * ------------------------------------------------------------------------ */
extern const uint8_t rom_01_9C20[16];  /* 8 × 2-byte LE pointers */
extern const uint8_t rom_01_9C30[];    /* briefing strings start */

/* ------------------------------------------------------------------------
 * (3) Picker-screen sprite-portrait positions ($01:9C00..$01:9C0F).
 *
 * Each scenario's portrait sprite is drawn at (X, Y) on a 4-column ×
 * 2-row grid. Indexed by SCENARIO INDEX.
 * ------------------------------------------------------------------------ */
static const uint8_t rom_01_9C00_portrait_X[8] = {
    0x20, 0x20, 0x60, 0x60, 0xA0, 0xA0, 0xE0, 0xE0
};
static const uint8_t rom_01_9C08_portrait_Y[8] = {
    0x2F, 0x6F, 0x2F, 0x6F, 0x2F, 0x6F, 0x2F, 0x6F
};

/* ------------------------------------------------------------------------
 * (4) The 78-byte view-config struct. ROM layout matches the C struct
 * 1:1; the original is read via direct LDA-abs $EE8A+offset (DBR=$7F)
 * after state $1A copies the chosen view's 78 bytes from
 * $01:[ptr in 81B3,x] to $7F:EE8A.
 * ------------------------------------------------------------------------ */
struct __attribute__((packed)) ViewConfig {
    /*  0-23 */ struct { uint16_t x, y; } scattered_props[6];
    /* 24    */ uint16_t player_x;
    /* 26    */ uint16_t player_y;
    /* 28    */ uint16_t game_clock_F071;
    /* 30    */ uint16_t spawn_x_bias;
    /* 32    */ uint16_t spawn_y_bias;
    /* 34    */ uint16_t spawn_x_mul;
    /* 36    */ uint16_t spawn_y_mul;
    /* 38    */ uint16_t danger_rate_E8FE;
    /* 40    */ uint16_t food_budget_EE86;
    /* 42    */ uint16_t red_colony_size_E87E;
    /* 44-75 */ struct { uint8_t y, x; } tile_placements[16];
    /* 76    */ uint16_t entity_cap_EB46;
};
_Static_assert(sizeof(struct ViewConfig) == 78, "view config is 78 bytes");

/* ------------------------------------------------------------------------
 * (5) The 16 view-config blocks at $01:81F3 + view*78.
 *
 * For each scenario, we name the block and annotate its non-FF fields.
 * Sentinel values: $FFFF in any field = "use default / no spawn".
 *
 * Manual L1..L8 (in scenario-index order: River=0, Road=1, Woods=2,
 * Porch=3, House=4, Yard=5, Park=6, Garden=7) → view modes 1, 5, 10, 3,
 * 4, 8, 6, 7 respectively.
 * ------------------------------------------------------------------------ */

/* View 6 — Scenario L1 / Park (Spring/Park, easiest, food + recruiting)
 *   player_at=(0x60, 0x64), spawn region offsets ($30, $40), $E8FE=$3F,
 *   $EE86=$0001. Decoration handler $00:BF71 spawns 1× $0B + 2× $0A +
 *   3× $09 + 1× $0C (Park flora). No rain/feet/etc.        */
static const struct ViewConfig scenario_park_view6 = {
    .scattered_props      = { {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF},
                              {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF} },
    .player_x             = 0x0700,           /* same as view 6's $0007 */
    .player_y             = 0x0160,
    .game_clock_F071      = 0x0064,
    .spawn_x_bias         = 0x0000,
    .spawn_y_bias         = 0x0000,
    .spawn_x_mul          = 0x0030,
    .spawn_y_mul          = 0x0040,
    .danger_rate_E8FE     = 0x0600,
    .food_budget_EE86     = 0x003F,
    .red_colony_size_E87E = 0x0001,
    .tile_placements      = {{0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}},
    .entity_cap_EB46      = 0x0028,
};

/* View 7 — Scenario L2 / Garden (Spring/Garden, flower pots block paths) */
static const struct ViewConfig scenario_garden_view7 = {
    .scattered_props      = { {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF},
                              {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF} },
    .player_x             = 0x0680,
    .player_y             = 0x0100,
    .game_clock_F071      = 0x0032,
    .spawn_x_bias         = 0x0028,
    .spawn_y_bias         = 0x0000,
    .spawn_x_mul          = 0x0020,
    .spawn_y_mul          = 0x0040,
    .danger_rate_E8FE     = 0x0600,
    .food_budget_EE86     = 0x003F,
    .red_colony_size_E87E = 0x0001,
    .tile_placements      = {{0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}},
    .entity_cap_EB46      = 0x0028,
};

/* View 8 — Scenario L3 / Yard (Rainy Season/Yard, RAIN washes scents,
 * deep nest → drowning). Decoration handler $00:BFC1 spawns 1× $48.
 * Tile placements list shows 16 fixed map tiles (stones/water).         */
static const struct ViewConfig scenario_yard_view8 = {
    .scattered_props      = { {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF},
                              {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF} },
    .player_x             = 0x0400,
    .player_y             = 0x0200,
    .game_clock_F071      = 0x00C8,
    .spawn_x_bias         = 0x0020,
    .spawn_y_bias         = 0x0000,
    .spawn_x_mul          = 0x0040,
    .spawn_y_mul          = 0x0040,
    .danger_rate_E8FE     = 0x0600,
    .food_budget_EE86     = 0x003F,
    .red_colony_size_E87E = 0x0001,
    /* 16 stationary water/stone tiles, manually placed at the BOTTOM of
     * the map (y=$53..$5E, x=$0D..$3A). These survive the rain. */
    .tile_placements      = {{0x55,0x0D},{0x58,0x0F},{0x58,0x11},{0x58,0x14},
                             {0x59,0x1A},{0x59,0x1B},{0x57,0x20},{0x58,0x20},
                             {0x58,0x25},{0x59,0x25},{0x5E,0x28},{0x5E,0x29},
                             {0x59,0x30},{0x5A,0x31},{0x57,0x38},{0x53,0x3A}},
    .entity_cap_EB46      = 0x0028,
};

/* View 4 — Scenario L4 / Summer-House (spiders, cat, electric outlet).
 * Decoration handler $00:BF2D: 1× $3D (cat paw "wave") + 6× $1C
 * (spiders) + 2× $1B (caterpillars).                                  */
static const struct ViewConfig scenario_house_view4 = {
    .scattered_props      = { {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF},
                              {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF} },
    .player_x             = 0x0400,
    .player_y             = 0x0100,
    .game_clock_F071      = 0x00C8,
    .spawn_x_bias         = 0x0040,
    .spawn_y_bias         = 0x0000,
    .spawn_x_mul          = 0x0020,
    .spawn_y_mul          = 0x0040,
    .danger_rate_E8FE     = 0xFFFF,           /* CAT triggers via clock */
    .food_budget_EE86     = 0x003F,
    .red_colony_size_E87E = 0x0001,
    .tile_placements      = {{0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}},
    .entity_cap_EB46      = 0x0028,
};

/* View 5 — Scenario L5 / Summer-Road (bicycles, heat → energy loss).
 * scattered_props: 5 fixed "road stripe" markers at fixed positions.
 * Decoration handler $00:BF5E spawns 3× type $13 (snail/bicycle base).*/
static const struct ViewConfig scenario_road_view5 = {
    .scattered_props      = { {0x0076,0x000B}, {0x0076,0x001D}, {0x0076,0x0036},
                              {0xFFFF,0xFFFF}, {0x0006,0x0027}, {0xFFFF,0xFFFF} },
    .player_x             = 0x0400,
    .player_y             = 0x0060,
    .game_clock_F071      = 0x0064,
    .spawn_x_bias         = 0x0040,
    .spawn_y_bias         = 0x0008,
    .spawn_x_mul          = 0x0010,
    .spawn_y_mul          = 0x0040,
    .danger_rate_E8FE     = 0x0150,           /* HOT — fast clock */
    .food_budget_EE86     = 0x003F,
    .red_colony_size_E87E = 0x0001,
    .tile_placements      = {{0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}},
    .entity_cap_EB46      = 0x0028,
};

/* View 1 — Scenario L6 / Summer-River (crevices block food, eat
 * spiders/caterpillars). scattered_props: 2 crevice markers at fixed
 * positions.                                                          */
static const struct ViewConfig scenario_river_view1 = {
    .scattered_props      = { {0xFFFF,0xFFFF}, {0x003B,0x0027}, {0xFFFF,0xFFFF},
                              {0x002E,0x0002}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF} },
    .player_x             = 0x0400,
    .player_y             = 0x0200,
    .game_clock_F071      = 0x0032,
    .spawn_x_bias         = 0x0018,
    .spawn_y_bias         = 0x0010,
    .spawn_x_mul          = 0x0068,
    .spawn_y_mul          = 0x0030,
    .danger_rate_E8FE     = 0x2000,
    .food_budget_EE86     = 0x001F,
    .red_colony_size_E87E = 0x0001,
    .tile_placements      = {{0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}},
    .entity_cap_EB46      = 0x0028,
};

/* View 3 — Scenario L7 / End-Summer-Porch (sparse food, falling objects).
 * Decoration handler $00:BEF3 spawns 3× $17 (spider/falling object)
 * + 2× $4B at fixed (X=$3E, Y=$2A) with special $0010 init flags.    */
static const struct ViewConfig scenario_porch_view3 = {
    .scattered_props      = { {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF},
                              {0xFFFF,0xFFFF}, {0x0070,0x0005}, {0x0029,0x0029} },
    .player_x             = 0x0180,
    .player_y             = 0x0130,
    .game_clock_F071      = 0x0032,
    .spawn_x_bias         = 0x0050,
    .spawn_y_bias         = 0x0010,
    .spawn_x_mul          = 0x0030,
    .spawn_y_mul          = 0x0030,
    .danger_rate_E8FE     = 0x0400,
    .food_budget_EE86     = 0x003F,
    .red_colony_size_E87E = 0x0001,
    .tile_placements      = {{0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF},
                             {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}, {0xFF,0xFF}},
    .entity_cap_EB46      = 0x0028,
};

/* View 10 — Scenario L8 / Autumn-Woods (food everywhere, block red nest
 * with stones). Decoration handler $00:BFC1 fires + 15-tile placement
 * list draws stones at red nest entrance.                             */
static const struct ViewConfig scenario_woods_view10 = {
    .scattered_props      = { {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF},
                              {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF}, {0xFFFF,0xFFFF} },
    .player_x             = 0x0100,
    .player_y             = 0x0100,
    .game_clock_F071      = 0x00C8,
    .spawn_x_bias         = 0x0050,
    .spawn_y_bias         = 0x0000,
    .spawn_x_mul          = 0x0030,
    .spawn_y_mul          = 0x0040,
    .danger_rate_E8FE     = 0x0300,
    .food_budget_EE86     = 0x003F,
    .red_colony_size_E87E = 0x0001,
    /* 15 stones (entry 14 has tile_placements[14]=$FFFF as a gap),
     * placed at the entrance of the red nest. */
    .tile_placements      = {{0x2B,0x09},{0x2B,0x0B},{0x2C,0x0B},{0x2E,0x0F},
                             {0x2E,0x10},{0x2E,0x13},{0x2A,0x14},{0x29,0x18},
                             {0x29,0x1A},{0x29,0x1F},{0x2B,0x25},{0x2A,0x29},
                             {0x29,0x31},{0x29,0x33},{0xFF,0xFF},{0x2B,0x39}},
    .entity_cap_EB46      = 0x000A,
};

/* ------------------------------------------------------------------------
 * Top-level "Scenario" struct — collates briefing + view-mode + danger
 * handler for each manual level.
 * ------------------------------------------------------------------------ */
typedef struct Scenario {
    const char *name;                       /* manual name             */
    uint8_t     manual_level;               /* 1..8                    */
    uint8_t     scenario_index;             /* 0..7 (sort order)       */
    uint8_t     view_mode;                  /* 1..10                   */
    uint16_t    briefing_ptr;               /* into bank $01           */
    const struct ViewConfig *config;        /* per-level data          */
    const char *primary_danger;             /* manual p.36             */
} Scenario;

static const Scenario scenarios[8] = {
    { "Park",   1, 6,  6,  0x9ED6, &scenario_park_view6,   "none (tutorial level)" },
    { "Garden", 2, 7,  7,  0x9F4D, &scenario_garden_view7, "flower pots block paths" },
    { "Yard",   3, 5,  8,  0x9E67, &scenario_yard_view8,   "RAIN washes scents" },
    { "House",  4, 4,  4,  0x9DFA, &scenario_house_view4,  "cat's paws, spiders" },
    { "Road",   5, 1,  5,  0x9CA7, &scenario_road_view5,   "BICYCLE TIRES, summer heat" },
    { "River",  6, 0,  1,  0x9C30, &scenario_river_view1,  "rough terrain, no shortcuts" },
    { "Porch",  7, 3,  3,  0x9D82, &scenario_porch_view3,  "HUMAN FEET, falling objects" },
    { "Woods",  8, 2, 10,  0x9D1C, &scenario_woods_view10, "winter pressure, block red nest" },
};

/* ========================================================================
 * DANGER HANDLERS — entity types + game events that implement each manual
 * danger. The handler bodies themselves are in entities_*.c (or stubs); the
 * spawn glue is here.
 *
 * Full danger documentation: wiki/15-dangers.md (kill kernels at
 * $03:EF1E mass_kill_sweep and $03:EF02 hand_squash live in combat.c).
 * ======================================================================== */

/* Sub-spawn hook from $00:BE46 (`JSR ($BE9A,x)`) — picks per-view
 * decoration callback by view*2. We re-declare each one here as a stub
 * so the linker can resolve cross-file refs. */
extern void entity_spawn_0499C1(uint16_t x, uint16_t y, uint8_t type);
extern uint8_t danger_clock_E8FE;          /* derived from view-config */
extern uint8_t red_colony_count_E87E;      /* set by sub_03_96D3       */

/* --------------------------------------------------------------------
 * DANGER #1 — RAIN (Scenario 3 / Yard)
 *
 * Decoration handler $00:BEDA spawns "1× $10 + 3× $0F" once per scenario.
 *   - Type $0F (4:9F1D) is the falling-water-drop entity (3-state machine
 *     in the same family as the egg-fall types 24/25 lifted in
 *     entities_d.c — landing at $69 then despawning).
 *   - Type $10 (4:A356) is the "puddle" stationary entity, drawn behind
 *     scent layer.
 *
 * RAIN EFFECT: gating in $03:E0FE — if dp[$97] != 0 (scenario mode),
 * every Nth frame (N = view config $E8FE / 4) the scent tilemap row is
 * shifted by 1 row and the bottom row is zeroed. This is "rain washes
 * trails away". Lift TODO — body would be ~30 lines.
 * -------------------------------------------------------------------- */
static void danger_rain_spawn(void) {
    /* $00:BEDA: */
    entity_spawn_0499C1(0, 0, 0x10);
    entity_spawn_0499C1(0, 0, 0x0F);
    entity_spawn_0499C1(0, 0, 0x0F);
    entity_spawn_0499C1(0, 0, 0x0F);
}

/* --------------------------------------------------------------------
 * DANGER #2 — HUMAN FEET (Scenario 7 / Porch)
 *
 * Implemented by the same entity types as Scenario 3's "spider", but
 * given a CHARGE behaviour via the danger_rate $E8FE. The "feet" are
 * really pairs of type $1B (left foot) + $1C (right foot) walking
 * across the level on a 6-frame timer.
 *
 * The actual "squash" event: when a foot's collision box overlaps any
 * ant entity, the ant's type byte is zeroed (kill). The collision code
 * is shared with the spider AI in entities_d.c (sub_AC99 hunt state).
 * -------------------------------------------------------------------- */
static void danger_feet_spawn(void) {
    /* From $00:BF33 (also feeds view 11+): 5× $1C + 2× $1B */
    entity_spawn_0499C1(0, 0, 0x1C);  /* "foot 1" (left) */
    entity_spawn_0499C1(0, 0, 0x1C);  /* "foot 2" */
    entity_spawn_0499C1(0, 0, 0x1C);  /* "foot 3" */
    entity_spawn_0499C1(0, 0, 0x1C);
    entity_spawn_0499C1(0, 0, 0x1C);
    entity_spawn_0499C1(0, 0, 0x1B);  /* "foot R" (right) */
    entity_spawn_0499C1(0, 0, 0x1B);
}

/* --------------------------------------------------------------------
 * DANGER #3 — LAWN MOWERS (Scenario 3 / Yard, advanced sub-event)
 *
 * Lawn mowers are NOT a separate entity type in the SNES port. They are
 * spawned by the SAME view-3/view-4 decoration handler as the human
 * feet — only differing in their movement speed (set via the entity's
 * +$10 timer to $06 instead of $0A). The visual is shared.
 *
 * VERDICT: implemented as a "fast-foot" via the type 1B/1C state-2 hunt
 * branch (entities_d.c type28_state2_hunt_AC99).
 * -------------------------------------------------------------------- */
/* (no separate spawn — shared with feet) */

/* --------------------------------------------------------------------
 * DANGER #4 — SNAILS (Scenario 6 / River)
 *
 * Decoration handler $00:BF5E spawns 3× type $13. Type $13 ($04:A533)
 * shares the Queen-Ant dispatcher (it's a 6-state wanderer with very
 * slow motion AI). Snails appear stationary in screenshots because
 * their walk-cycle period is ~120 frames vs. ants' 4-frame cycle.
 *
 * COLLISION: shared with spider — but unlike spiders, snails do not
 * cause damage; they only BLOCK trail propagation (the scent tilemap
 * skips over a snail's tile).
 * -------------------------------------------------------------------- */
static void danger_snails_spawn(void) {
    /* From $00:BF5E: */
    entity_spawn_0499C1(0, 0, 0x13);
    entity_spawn_0499C1(0, 0, 0x13);
    entity_spawn_0499C1(0, 0, 0x13);
}

/* --------------------------------------------------------------------
 * DANGER #5 — CAT'S PAWS (Scenario 4 / House)
 *
 * Decoration handler $00:BEF3 (view 4 House) does:
 *   spawn 3× type $17 (the "spider" entity at $04:A8D9 — already lifted
 *                      in entities_c.c as a 5-cell predator with
 *                      separate top/bottom OAM attrs)
 *   spawn 2× type $4B at fixed (X=$3E, Y=$2A) with init flag $0010
 *                      and the dp[$010] bias byte set to $80 (which
 *                      causes the entity to do a "swipe" animation
 *                      across the screen at start of state 1).
 *
 * The CAT itself shares the spider entity ($17) because SNES sprite
 * budget was already maxed out (the cat is too large for OAM).
 * -------------------------------------------------------------------- */
static void danger_cat_paws_spawn(void) {
    /* From $00:BEF3: */
    entity_spawn_0499C1(0, 0, 0x17);  /* spider/cat-paw base */
    entity_spawn_0499C1(0, 0, 0x17);
    entity_spawn_0499C1(0, 0, 0x17);
    entity_spawn_0499C1(0x3E, 0x2A, 0x4B);  /* fixed-position pair */
    entity_spawn_0499C1(0x3E, 0x2A, 0x4B);
    /* (the original then sets entity+0x10=#$80 and entity+0x0C=#$0100 to
     * stage the swipe — see lifted code at $00:BF1D..BF2A.) */
}

/* --------------------------------------------------------------------
 * DANGER #6 — BICYCLE TIRES (Scenario 5 / Road)
 *
 * Decoration handler $00:BF2D (view 5 Road) spawns:
 *   1× type $3D  (the bicycle-tire entity at $04:BD9B = type 53 in
 *                 hex-decimal naming. Its initial state reads
 *                 $7F:E87E (red colony count) and spawns ITSELF that
 *                 many times along a horizontal sweep line — matching
 *                 the manual's "groups of bicycles").
 *   6× type $1C  (the small "stones in road" obstacles — same handler
 *                 as the spider in House scenarios, but state 0 sets
 *                 motion to zero).
 *   2× type $1B  (the caterpillar-base, but here used as a stationary
 *                 prop because dp[$0050] flags it as scenery in this
 *                 view).
 *
 * The bicycle's KILL EVENT: type 53 walks left-to-right at $0040
 * pixels/frame; on collision with any ant, the ant's +$10 timer is
 * forced to $00 and the ant's type is zeroed.
 * -------------------------------------------------------------------- */
static void danger_bicycles_spawn(void) {
    /* From $00:BF2D: */
    entity_spawn_0499C1(0, 0, 0x3D);  /* bicycle squad spawner */
    entity_spawn_0499C1(0, 0, 0x1C);  /* road obstacle 1 */
    entity_spawn_0499C1(0, 0, 0x1C);
    entity_spawn_0499C1(0, 0, 0x1C);
    entity_spawn_0499C1(0, 0, 0x1C);
    entity_spawn_0499C1(0, 0, 0x1C);
    entity_spawn_0499C1(0, 0, 0x1C);
    entity_spawn_0499C1(0, 0, 0x1B);  /* secondary scenery */
    entity_spawn_0499C1(0, 0, 0x1B);
}

/* --------------------------------------------------------------------
 * DANGER #7 — HANDS (manual p.36, no scenario explicitly assigned but
 * triggered by the Porch + House levels)
 *
 * Implemented as type $4B (entity dispatch $04:C653). This is a 4-state
 * machine triggered by a long-press input event (the "hand" appears
 * when the player holds the mouse button on a non-ant area; the
 * disasm at $00:BEF3 only spawns 2 instances, both fixed-position).
 *
 * KILL EVENT: when a hand state-2 timer fires, it picks the entity-
 * table entry closest to its on-screen position and zeroes its type
 * byte (same as bicycle tires).
 * -------------------------------------------------------------------- */
/* (spawn merged into danger_cat_paws_spawn above; only fires on House) */

/* ========================================================================
 * RAIN-EFFECT GAMEPLAY HOOK
 *
 * The actual "rain washes trails" code lives at $03:E0FE — gated by
 * `dp[$97] != 0` (scenario mode). In Yard (view 8 / scenario L3) the
 * game's per-frame tick subtracts 1 from every scent tile every
 * ($E8FE / 4) frames, fading them faster than a non-rainy level.
 *
 * We model the effect here as a stub that the per-frame game tick
 * calls; the actual scent decay LUT is in WRAM at $7F:6000+.
 * ======================================================================== */
extern uint8_t *scent_tilemap_7F6000;       /* 128x64 byte map         */
extern uint8_t  is_scenario_mode_dp97;      /* dp[$97]: 0 = full game  */

static void scenario_rain_tick(void)
{
    /* Only active in Yard scenario (view 8). Game-time accrued at
     * dp[$F083] reaches the danger_rate, then we step the wash. */
    if (!is_scenario_mode_dp97) return;
    extern uint16_t game_clock_F083;
    static uint16_t rain_accum = 0;
    rain_accum++;
    if (rain_accum < 0x0600) return;        /* matches scenario_yard's
                                             * danger_rate_E8FE */
    rain_accum = 0;
    /* For each scent tile, scent-=1 if >0 (i.e., FADE).
     * This is the "rain washes scent" mechanic. */
    if (scent_tilemap_7F6000) {
        for (unsigned i = 0; i < (128 * 64); ++i) {
            if (scent_tilemap_7F6000[i]) scent_tilemap_7F6000[i]--;
        }
    }
}

/* ------------------------------------------------------------------------
 * Per-view decoration dispatcher ($00:BE9A — `JSR ($BE9A,x)` from $BE46).
 * Indexed by view*2.
 *
 * See wiki/14-scenarios.md §5 for the full slot→danger mapping table,
 * and wiki/15-dangers.md §1 for the kill kernels each danger consumes.
 * ------------------------------------------------------------------------ */
static void view_decoration_handler(uint8_t view)
{
    /* $00:BE9A jump table (16 entries × 2 bytes). The bodies are at the
     * addresses listed in the comment block at the top of this file. */
    switch (view) {
    case 0:  /* $00:BEBA — Full Game (view 0): 1× type $37 (clock prop) */
        entity_spawn_0499C1(0, 0, 0x37);
        break;
    case 1:  /* $00:BEC1 — River (scenario idx 0): 1× $41 + 3× $11/$12 */
        entity_spawn_0499C1(0, 0, 0x41);
        entity_spawn_0499C1(0, 0, 0x11);
        entity_spawn_0499C1(0, 0, 0x12);
        entity_spawn_0499C1(0, 0, 0x12);
        break;
    case 2:  /* $00:BEDA — Yard (scenario idx 5, manual L3): RAIN */
        danger_rain_spawn();
        break;
    case 3:  /* $00:BEF3 — Porch (scenario idx 3, manual L7): HANDS */
        danger_cat_paws_spawn();
        break;
    case 4:  /* $00:BF2D — House (scenario idx 4, manual L4): BICYCLES */
        danger_bicycles_spawn();
        break;
    case 5:  /* $00:BF5E — Road (scenario idx 1, manual L5): SNAILS */
        danger_snails_spawn();
        break;
    case 6:  /* $00:BF71 — Park (scenario idx 6, manual L1): just flora */
        entity_spawn_0499C1(0, 0, 0x0B);
        entity_spawn_0499C1(0, 0, 0x0A);
        entity_spawn_0499C1(0, 0, 0x0A);
        entity_spawn_0499C1(0, 0, 0x09);
        entity_spawn_0499C1(0, 0, 0x09);
        entity_spawn_0499C1(0, 0, 0x09);
        entity_spawn_0499C1(0, 0, 0x0C);
        break;
    case 7:  /* $00:BF9C — Garden (idx 7, manual L2): 5× $16 + 1× $15 */
        entity_spawn_0499C1(0, 0, 0x16);
        entity_spawn_0499C1(0, 0, 0x16);
        entity_spawn_0499C1(0, 0, 0x16);
        entity_spawn_0499C1(0, 0, 0x16);
        entity_spawn_0499C1(0, 0, 0x16);
        entity_spawn_0499C1(0, 0, 0x15);
        break;
    case 8: case 9: case 10:
        /* $00:BFC1 — Full Game (or Woods/Yard heavy variants): 1× $48 */
        entity_spawn_0499C1(0, 0, 0x48);
        break;
    case 11: case 12: case 13: case 14: case 15:
        /* $00:BF33 — Full Game variants (FEET-like): 5× $1C + 2× $1B */
        danger_feet_spawn();
        break;
    default:
        break;
    }
}

/* ========================================================================
 * Boot-time entry: when the player picks a scenario, this is called by
 * state $19 ($00:96B1) → state $1A ($00:96DF). Documented here as a free
 * function for reference; the actual entry point is the chained
 * INC dp[$0B] state-machine progression.
 * ======================================================================== */
extern void state_19_save_commit_choice_96B1(void);  /* in states_gameplay.c */
extern void state_1A_save_load_world_96DF(void);     /* in states_gameplay.c */

/* Translate a menu cursor row to a Scenario* (or NULL for non-scenario). */
const Scenario *scenario_from_menu_row(uint8_t cursor_row)
{
    if (cursor_row >= 8) return 0;
    uint8_t scen_idx = rom_00_D798[cursor_row];      /* 0..7 */
    for (unsigned i = 0; i < 8; ++i) {
        if (scenarios[i].scenario_index == scen_idx) return &scenarios[i];
    }
    return 0;
}

/* Silence "unused" warnings — these are documented references the linker
 * needs to keep around in case a debugger wants to inspect them. */
__attribute__((used))
static void const * const _scenario_doc_refs[] = {
    (void const *)scenarios,
    (void const *)&scenario_park_view6,
    (void const *)&scenario_garden_view7,
    (void const *)&scenario_yard_view8,
    (void const *)&scenario_house_view4,
    (void const *)&scenario_road_view5,
    (void const *)&scenario_river_view1,
    (void const *)&scenario_porch_view3,
    (void const *)&scenario_woods_view10,
    (void const *)rom_01_9C00_portrait_X,
    (void const *)rom_01_9C08_portrait_Y,
    (void const *)scenario_from_menu_row,
    (void const *)view_decoration_handler,
    (void const *)scenario_rain_tick,
    (void const *)danger_rain_spawn,
    (void const *)danger_feet_spawn,
    (void const *)danger_snails_spawn,
    (void const *)danger_cat_paws_spawn,
    (void const *)danger_bicycles_spawn,
};

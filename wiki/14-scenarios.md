# 14 — Scenarios: 8 Hand-Crafted Levels

This page documents the 8 scenario levels (manual p.22-23: Park, Garden,
Yard, House, Road, River, Porch, Woods) and how each one is configured.
The scenarios are NOT a contiguous block of data — they're a union of
SIX cooperating tables in ROM plus per-view decoration handlers.

For the danger entities each scenario spawns, see
[15 — Dangers](15-dangers.md). For the per-view game-state machinery
that picks a scenario at boot, see
[01 — Architecture](01-architecture.md) (state `$1A`).

Manual cross-reference: **pages 22..23** ("Scenario Game — Level List").

---

## 1. Cooperating ROM tables

| Table | Address | Purpose |
|-------|---------|---------|
| (1) Picker row → scen idx | `$00:D798` | 8-byte map from menu row to briefing-pointer index |
| (2) Briefing pointers | `$01:9C20` | 8 × 16-bit pointers to ASCII briefings (terminator `$FF`, newline `$FE`) |
| (3) Portrait positions | `$01:9C00..9C0F` | X column + Y row for each scenario's portrait sprite |
| (4) Per-view config | `$01:81F3` (78 B × 16 views) | The actual gameplay parameters |
| (5) Save-slot game type | `$01:8143`/`$01:817B` | "is this slot a scenario or a full-game", and which view it picks |
| (6) Decoration dispatcher | `$00:BE9A` | Per-view jump table → per-scenario spawn handler |

The most important consequence: there is no single "Scenario" struct in
ROM. Each level's identity is the *union* of its view-config block + its
decoration handler + its briefing.

---

## 2. Picker-row → scenario index map (`$00:D798`)

The picker UI lays the 8 portraits out on a 4-column × 2-row grid. The
table at `$00:D798` maps the menu cursor row (0..7, reading row-major)
to the SCENARIO INDEX in the briefing pointer table:

```
$00:D798: 06 07 05 04 01 00 03 02
```

Decoded (verified in V4-8):

| Row | Picker label | Scen idx | View mode | Manual L# |
|-----|--------------|----------|-----------|-----------|
| 0 | "In the Park" | 6 | 6 | L1 |
| 1 | "In the Garden" | 7 | 7 | L2 |
| 2 | "In the Yard" | 5 | 8 | L3 |
| 3 | "In the House" | 4 | 4 | L4 |
| 4 | "On the Road" | 1 | 5 | L5 |
| 5 | "By the River" | 0 | 1 | L6 |
| 6 | "Under the Porch" | 3 | 3 | L7 |
| 7 | "In the Woods" | 2 | 10 | L8 |

The scenario index is a sort key used internally; the view mode is the
index into the 16-block config table at `$01:81F3`.

---

## 3. View modes 1, 3, 4, 5, 6, 7, 8, 10 = the 8 scenarios

`$01:81F3` holds **16** 78-byte view-config blocks. Of these, 8 are the
scenarios above; the remaining 8 are the Full Game's view modes (full
game uses indices 0, 2, 9, and 11..15).

The 78-byte layout (`scenarios.c:struct ViewConfig`):

| Bytes | Field | Purpose |
|-------|-------|---------|
| 0..23 | 6 × `(x, y)` | Scattered prop positions; `$FFFF` = empty |
| 24..27 | `player_x`, `player_y` | Yellow Ant initial WORLD position |
| 28..29 | `game_clock_F071` | Game-clock divisor; smaller = faster |
| 30..33 | `spawn_x_bias`, `spawn_y_bias` | Danger spawn offset bias |
| 34..37 | `spawn_x_mul`, `spawn_y_mul` | Danger spawn region multiplier (fed to `$02:F3BD` divider) |
| 38..39 | `danger_rate_E8FE` | Global danger-spawn rate; larger = more dangers per minute |
| 40..41 | `food_budget_EE86` | Colony food budget (starts colony with this many crumbs) |
| 42..43 | `red_colony_size_E87E` | Red ant colony starting size (used by `$03:8820` spawner loop) |
| 44..75 | 16 × `(y, x)` | Per-tile placement list — writes tile `$51` to terrain map |
| 76..77 | `entity_cap_EB46` | Initial entity-table size cap (default `$28`=40) |

See `scenarios.c:243..424` for each block's literal contents. The 16-tile
placement list is what hand-decorates Yard (stones+water) and Woods
(stones blocking the red nest entrance).

---

## 4. Briefing pointer table (`$01:9C20`)

Each scenario's briefing is a packed ASCII string in bank `$01`,
terminated by `$FF` and using `$FE` for newline.

| Idx | Pointer | Briefing (newlines preserved) |
|-----|---------|-------------------------------|
| 0 | `$9C30` | "By the River — This is a dangerous area. Many ants will get lost. Use the Yellow Ant to call and release help as needed." |
| 1 | `$9CA7` | "On the Road — It's the middle of summer and it's really hot! Ants will lose energy quickly, so watch your food supply!" |
| 2 | `$9D1C` | "In the Woods — Get ready for winter! You need to stock up on food so you can survive the barren months." |
| 3 | `$9D82` | "Under the Porch — There are children playing nearby — and their feet can squash you! Be careful while crossing the bricks!" |
| 4 | `$9DFA` | "In the House — You'll be building your nest in a human's house. There is also a new danger here — the pet cat!" |
| 5 | `$9E67` | "In the Yard — There's been a lot of rain recently. Your trails will soon wash away, so be sure to make new ones." |
| 6 | `$9ED6` | "In the Park — The long winter has ended and spring is here! Start by collecting the food that has fallen in the sandbox." |
| 7 | `$9F4D` | "In the Garden — Many ants may get lost among the flowers. Use the Yellow Ant to guide them along safely." |

(See `scenarios.c:163..188` for the rendered briefing comments.)

---

## 5. Per-decoration dispatch (`$00:BE9A`)

After the view-config is loaded and the world is built, the per-view
decoration handler fires once. The dispatcher at `$00:BE9A` is 16 × 2
bytes of jump-table pointers, indexed by `view * 2`:

| View | Spawn site | Spawns |
|------|-----------|--------|
| 0 | `$00:BEBA` | 1× type `$37` (clock prop — Full Game) |
| 1 | `$00:BEC1` | 1× `$41` + 3× `$11/$12` (River) |
| 2 | `$00:BEDA` | RAIN — 1× `$10` (puddle) + 3× `$0F` (drop) (Yard L3) |
| 3 | `$00:BEF3` | HANDS / cat-paws — 3× `$17` + 2× `$4B` (Porch L7) |
| 4 | `$00:BF2D` | BICYCLES — 1× `$3D` + 6× `$1C` + 2× `$1B` (House L4) |
| 5 | `$00:BF5E` | SNAILS — 3× `$13` (Road L5; mislabeled in V3 — Road's snails are decorative; the danger is bicycles) |
| 6 | `$00:BF71` | Park flora (7 entities; no dangers) |
| 7 | `$00:BF9C` | Garden flora (5× `$16` + 1× `$15`) |
| 8,9,10 | `$00:BFC1` | 1× `$48` (Full Game / Woods variant) |
| 11..15 | `$00:BF33` | FEET / MOWERS — 5× `$1C` + 2× `$1B` (Full Game variants) |

Note: the spawn-handler boundaries are *not* a clean 1:1 mapping with
the manual's scenario names. For example, "Cat's Paws" in the House
scenario shares the `$3D + 1C + 1B` decoration with the Bicycle danger
because cat paws and bicycles use the same caterpillar/spider visual
budget. See [15 — Dangers](15-dangers.md) §3 for the exact entity types.

---

## 6. The eight scenarios in detail

### L1 — Park (view 6, scen idx 6) — Spring, easiest

- **Briefing**: "Start by collecting the food that has fallen in the
  sandbox." (`$01:9ED6`)
- **Config** (`scenarios.c:scenario_park_view6`): player at
  `(0x0700, 0x0160)`, food budget `$3F`, danger rate `$0600`, no tile
  placements, no scattered props.
- **Decoration** (`$00:BF71`): 1× `$0B` + 2× `$0A` + 3× `$09` + 1× `$0C`
  — Park flora (rocks and weeds). **No danger entities.**
- **Gameplay**: a tutorial level. The recruit/release menus and food
  pickup are introduced; there is no rain, no spider, no foot.

### L2 — Garden (view 7, scen idx 7) — Spring, flower pots block paths

- **Briefing**: "Many ants may get lost among the flowers." (`$01:9F4D`)
- **Config**: player at `(0x0680, 0x0100)`, game clock `$32` (faster
  than Park), spawn bias `$28` in X (off-center spawn region).
- **Decoration** (`$00:BF9C`): 5× `$16` (flower pots) + 1× `$15` (large
  obstacle). The flower pots are scenery, NOT danger entities; their
  effect is to block scent-trail propagation (the scent tilemap skips
  over a flower-pot tile, so ants can't follow trails through them).

### L3 — Yard (view 8, scen idx 5) — Rainy Season, RAIN washes scents

- **Briefing**: "Your trails will soon wash away." (`$01:9E67`)
- **Config**: player at `(0x0400, 0x0200)`, game clock `$C8` (slow),
  tile placements are 16 fixed stones/water tiles at the BOTTOM of the
  map (y=$53..$5E, x=$0D..$3A).
- **Decoration** (`$00:BEDA`): RAIN — 1× type `$10` (puddle, `$04:A356`)
  + 3× type `$0F` (drop, `$04:9F1D`).
- **Rain mechanic** (`scent.c:scent_rain_wash_cell_02_96A0`, line 471):
  every cell's nest scent loses `$14` (20) per pass; every cell's trail
  scent is zeroed outright. The 16 placed tiles survive (they're
  terrain, not scent). The rain wash is paced by view-config
  `danger_rate_E8FE` ($0600 in Yard).

### L4 — House (view 4, scen idx 4) — Summer, spiders + cat

- **Briefing**: "Building your nest in a human's house ... the pet cat!"
  (`$01:9DFA`)
- **Config**: player at `(0x0400, 0x0100)`, **danger rate `$FFFF`** —
  effectively "cat fires via clock, not rate" (the cat is timed by the
  game clock, not the rate).
- **Decoration** (`$00:BEF3`): 3× type `$17` (spider/cat-paw,
  `$04:A8D9`) + 2× type `$4B` (hand, `$04:C653`) at fixed `(X=$3E,
  Y=$2A)` with init flag `$0010` (the `$80` swipe bias). The cat's
  visual reuses the spider entity because SNES OAM is already maxed.

### L5 — Road (view 5, scen idx 1) — Summer, bicycles + heat

- **Briefing**: "It's really hot! Ants will lose energy quickly."
  (`$01:9CA7`)
- **Config**: player at `(0x0400, 0x0060)`, danger rate `$0150` (fast
  clock — the heat), scattered props mark fixed "road stripes" at
  `(0x0076,0x000B)`, `(0x0076,0x001D)`, `(0x0076,0x0036)`,
  `(0x0006,0x0027)`.
- **Decoration** (`$00:BF2D`): 1× type `$3D` (bicycle squad spawner,
  `$04:C36E`) + 6× `$1C` + 2× `$1B` (road obstacles). Type `$3D`
  reads `$7F:E87E` (red colony count) and spawns ITSELF that many
  times along a horizontal sweep line — matching the manual's "groups
  of bicycles".
- **Heat mechanic**: the fast game clock at `$0150` makes hunger
  decrement faster per real-time second. The manual's "lose energy
  quickly" is a clock-rate effect, not a special damage path.

### L6 — River (view 1, scen idx 0) — Summer, crevices + edible danger

- **Briefing**: "Many ants will get lost. Use the Yellow Ant to call and
  release help as needed." (`$01:9C30`)
- **Config**: player at `(0x0400, 0x0200)`, danger rate `$2000`,
  scattered props mark 2 crevice obstacles at `(0x003B,0x0027)` and
  `(0x002E,0x0002)`.
- **Decoration** (`$00:BF5E`): 3× type `$13` (Queen-family wanderer,
  `$04:A533`; rendered as snails because the walk-cycle period is ~120
  frames vs. ants' 4-frame cycle).
- **Eat-the-danger**: spiders and caterpillars in the River scenario
  are *food entities* — they can be picked up + eaten like crumbs. The
  food-tile sweep at `$03:87C0` accepts them as edible because their
  tile-marker IDs are in the `$60..$67` range.

### L7 — Porch (view 3, scen idx 3) — End of summer, sparse food + falling

- **Briefing**: "There are children playing nearby — and their feet can
  squash you!" (`$01:9D82`)
- **Config**: player at `(0x0180, 0x0130)`, food budget `$3F` (sparse —
  the colony starts with little), scattered props at
  `(0x0070,0x0005)` and `(0x0029,0x0029)`.
- **Decoration** (`$00:BEF3`): same as House — 3× `$17` + 2× `$4B`.
  But the Porch's `dp[$0050]` flag differs, so the type `$1C/$1B`
  motion timer is set to `$06` (fast) rather than `$0A` (House
  default), which is what makes the foot "stomp" rapidly.

### L8 — Woods (view 10, scen idx 2) — Autumn, food everywhere

- **Briefing**: "Stock up on food so you can survive the barren months."
  (`$01:9D1C`)
- **Config**: player at `(0x0100, 0x0100)`, entity cap `$0A` (only 10
  entities — sparse population), 15 fixed stones at the red nest
  entrance (y=$29..$2E, x=$09..$39) blocking the enemy.
- **Decoration** (`$00:BFC1`): 1× `$48` (Woods background prop).
- **No active danger spawn handler** — the 15-stone wall is the
  scenario's "feature", and the autumn theme is communicated by the
  palette / tilemap pack at `$04:F400..FC00`.

---

## 7. The Scenario struct (informational)

`scenarios.c:440` collates each manual level into a single struct for
documentation purposes:

```c
typedef struct Scenario {
    const char *name;
    uint8_t     manual_level;       /* 1..8 */
    uint8_t     scenario_index;     /* 0..7 — picker sort key */
    uint8_t     view_mode;          /* 1..10 — config index */
    uint16_t    briefing_ptr;       /* into bank $01 */
    const struct ViewConfig *config;
    const char *primary_danger;
} Scenario;
```

The 8-entry `scenarios[]` table is for reference; the ROM never
materializes it as a struct array.

---

## 8. Boot-time entry

When the player picks a scenario:

1. State `$19` (`$00:96B1`, "save commit choice") writes the selected
   menu row to `$7F:E736/E738`.
2. State `$1A` (`$00:96DF`, "save load world") indexes `rom_01_8143`
   (game-type) and `rom_01_817B` (view-mode) by the save-slot, then
   memcopies 78 bytes from `$01:81F3 + view*78` to `$7F:EE8A` (the
   live config mirror).
3. The simulation task is spawned (`sub_9832` per `01-architecture.md`).
4. The decoration handler at `$00:BE9A + view*2` fires once.
5. The Yellow Ant is placed at `(player_x, player_y)`.

After that, the per-tick simulation reads the live config from
`$7F:EE8A..F0xx` for spawn pacing, food budget, etc.

---

## 9. Surprising findings

- **The 16 view-config blocks are not 1:1 with scenarios.** 8 of them
  are scenarios; the other 8 are Full Game variants. The `$00:D798`
  table is what hides this from the picker UI.
- **Picker row order ≠ manual level order.** Row 0 ("Park") maps to
  scenario index 6 in the briefing table. The manual's L1..L8 ordering
  is the *picker* order; the briefing-pointer table is sorted by some
  internal grouping (probably "season"-first, scenario-difficulty
  second).
- **Rain isn't a damage effect.** Yard's RAIN handler at `$00:BEDA`
  spawns visuals, but the actual gameplay effect is a periodic call to
  `scent_rain_wash_cell_02_96A0` that erases trail scents and weakens
  nest scents by `$14` per pass. Ants don't drown in rain; they just
  can't follow trails.
- **The same decoration handler serves multiple manual dangers.**
  `$00:BEF3` spawns the same `3× $17 + 2× $4B` for both House (cat) and
  Porch (foot/hand) — the difference is the entity's motion timer,
  controlled by `dp[$0050]` after spawn.

---

## 10. Inline source pointers

- `scenarios.c:243..424` — the 8 view-config literal definitions.
- `scenarios.c:654..708` — `view_decoration_handler` (the `$00:BE9A`
  dispatcher).
- `scenarios.c:630..648` — `scenario_rain_tick` (rain wash glue).
- `scent.c:471` — `scent_rain_wash_cell_02_96A0` (the actual wash
  body).
- `scenarios.c:440` — the documentation-only `scenarios[]` array.

---

## 11. Manual references

- **p.22..23**: "Scenario Game — Level List". The 8 levels, in
  picker-row order: Park, Garden, Yard, House, Road, River, Porch,
  Woods. Each manual entry's "Danger" hint matches §3's
  decoration-handler spawn.
- **p.20**: "Marriage Flight" — referenced because Full Game view 9 is
  the marriage-flight view, NOT a scenario (it's view-config block 9
  with `entity_cap_EB46 = $000A`).

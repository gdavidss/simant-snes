# SimAnt entity-type cheatsheet

The game has a **118-entry** entity dispatch table at `$04:9A30` (V4-8
re-decoded it directly from `simant.sfc` at file offset 0x21A30). Each
entity is a 20-byte record; `entity.type` (byte +0) indexes the table.

**Lift status: ~110 / 118 (~93 %) handlers have bodies after the
F/G/H fix rounds.** Types $00-$1F cover the original ant / cursor /
popup / egg / HUD handlers (lifted in V2). Types $20-$2B were lifted
in **G2** as `entities_e.c` (41 state bodies across 13 dispatchers).
Types $2C-$5F dispatchers were lifted in **G3** as `entities_f.c` (24
dispatcher state bodies; verified by **H2**). Types $60-$71
dispatchers were lifted in **G4** as `entities_g.c` (28 per-state
bodies across 14 multi-state handlers). Dialog renderers $2D/$2E/$2F
were lifted in **H1**. The House Screen renderer at type $35 is in
`ui_menus.c`. The ~8 still-stubbed entries include the bicycle danger
($3D) and the hand / cat-paw danger ($4B).

Companion docs: `V4_8_DISPATCH_TABLES.md` (raw table dump),
`scenarios.c` (danger-entity mapping), `AUDIT_SUMMARY.md`,
`G2_ENTITIES_E_BODIES.md`, `G4_ENTITIES_G_BODIES.md`,
`H1_DIALOG_RENDERERS.md`, `H2_VERIFY_G_FIXES.md`.

---

## Types 1-31 — lifted handlers

| Type | Handler | Init word | Init attr | Role (best evidence) |
|------|---------|-----------|-----------|----------------------|
| $00 | `$04:9B1A` (RTS) | — | — | Empty slot |
| $01 | `$04:9D9D` | — | — | **Cursor / input pickup** (reads `dp[$60/$61]` for A/B) |
| $02 | `$04:9B9B` | — | $18 | Cursor sprite (sign-magnitude delta accumulator) |
| $03 | `$04:9B1B` | — | — | Selection rectangle marker at `dp[$0C/$0D]` |
| $04 | `$04:9B30` | — | — | Selection rect at `dp[$0E/$0F]` |
| $05 | `$04:9B41` | — | — | Selection rect at `dp[$10/$11]` |
| $06 | `$04:9C46` | — | — | Timed HUD indicator (population counter?) |
| $07 | `$04:9CC6` | — | — | Static HUD prop |
| $08 | `$04:9CF0` | — | — | Static HUD prop variant |
| $09 | `$04:9E3F` | $014C | $1E | 3-state burst sprite (small short-lived FX) |
| $0A / $16 | `$04:9E9C` (alias) | $0100 / $0100 | $9E / $9E | 3-state burst with flash variant |
| $0B | `$04:9F1D` | $0180 | $9E | 3-state burst — palette variant (raindrop) |
| $0C | `$04:9F7A` | $0140 | $9F | 2-state drifting prop, palette flip |
| $0D | `$04:9FE0` | $0180 | $9E | 2-state banner sprite (4x2 + extension) |
| $0E | `$04:A112` | — | $1F | **Worker Ant** (5-state walking AI; anim table `$A206` = 8 frames) |
| $0F | `$04:A222` | — | $9E | **Soldier Ant** (5-state walking AI; anim table `$A338` = 10 frames) |
| $10 / $15 | `$04:A356` (alias) | — | $9F | Generic 4-cell creature (caterpillar / ant lion larva?) — also reused as the rain "puddle" tile |
| $11 | `$04:A43B` | $01C0 | $9F | 5-cell creature, top/bottom row attrs (**Spider** candidate) |
| $12 / $13 | `$04:A533` (alias) | $0100 / $0100 | $9E / $9E | **Queen Ant** AND **Snail** wanderer (same handler reused — Scenario 6 reuses Queen AI for snail) |
| $14 | `$04:A6C5` | — | — | **Dig-New-Nest excavator** — state 1 ZEROES 4 rows of `$7F:4000` (the tile bitmap), carving new chambers |
| $17 | `$04:A8D9` | — | $9F | 2-state wanderer (**Breeder / winged ant** candidate) — also reused as cat's-paw danger entity |
| $18 | `$04:A951` | — | $98 | **Egg** (spawned by Queen's Lay Eggs action; bumps `EGGS_LAID_E80E`) |
| $19 | `$04:A9A1` | $0008 | $98 | Egg drop-in animation partner of $18 |
| $1A | `$04:AB0B` | — | — | HUD strip drawer (4-cell horizontal at screen 0,0) |
| $1B | `$04:AB5B` | — | $9E | 3-state walker (**Caterpillar** / also **Human Foot** per `scenarios.c`, `danger_feet_spawn`) |
| $1C | `$04:AC3A` | — | $9F | Companion to type 27 (longer timers — also **Lawn Mower** variant) |
| $1D | `$04:AD01` | — | — | **Dialog / Popup machine** — 10 states; gates on `dp[$02A7]` (POPUP_ACTIVE) + `dp[$02E1]` (POPUP_LOCK); jumps via `dp[$02E3]` (POPUP_GOTO_STATE). Implements Recruit menu (Recruit 5/10/All / Release 1/2/All) and Queen menu (Dig New Nest / Lay Eggs) |
| $1E | `$04:B17F` | $0080 | $9C | 6-state cursor-glide sprite (menu cursor with 3-frame click anim) |
| $1F | `$04:B547` | — | — | Stateless tooltip drawer (icon under cursor with 8-frame anim) |

## Types $20-$75 — mostly lifted after F/G/H rounds

The remaining 86 entries in the dispatch table at `$04:9A30` were
mostly lifted across G2/G3/G4/H1. Full address list in
`V4_8_DISPATCH_TABLES.md` §5 (with post-Z1 addendum noting the lifts).
Status highlights:

| Type | Handler | Status | Role |
|------|---------|--------|------|
| $20-$2B | various | **LIFTED (G2)** | HUD widgets, Auto/Manual icons, numeric-digit panel icons in `entities_e.c` |
| $24 | `$04:B77D` | LIFTED (G2) | Numeric-digit / icon prop (Behavior / Caste panel) |
| $25 | `$04:B7C1` | LIFTED (G2) | Numeric-digit / icon prop |
| $26 | `$04:B7FF` | LIFTED (G2) | Numeric-digit / icon prop |
| $27 | `$04:9DD5` | LIFTED (G2) | Auto/Manual icon T1 — Behavior side |
| $28 | `$04:9DEA` | LIFTED (G2) | Auto/Manual icon T2 — Caste side |
| $29 | `$04:9DFF` | LIFTED (G2) | Auto/Manual icon T3 — inverted Behavior |
| $2A | `$04:9E14` | LIFTED (G2) | Auto/Manual icon T4 — inverted Caste |
| $2C-$5F | various | **LIFTED (G3)** | Dispatchers in `entities_f.c` |
| $2D, $2E, $2F | $B90A, $B991, $BA84 | **LIFTED (H1)** | Dialog renderers |
| $35 | `$04:BD9B` | **LIFTED** | **House Screen renderer** (`house_screen_render_04_BD9B`, `ui_menus.c`) |
| $3D | `$04:C36E` | **stub** | **Bicycle danger** (Scenario 5) — body still referenced from `gaps.c`, no implementation |
| $4B | `$04:C653` | **stub** | **Hand / Cat-Paw danger** (Scenarios 4, 7) — referenced from `gaps.c` + `combat.c` (`hand_squash_EF02`) |
| $60-$71 | various | **LIFTED (G4)** | Dispatchers in `entities_g.c` (28 per-state bodies / 14 multi-state handlers) |

These out-of-range types (e.g. $3D, $4B) confirm that the dispatch
table is genuinely 118 entries — they are not a "secondary dispatch
table"; they are the same table, just with later indices.

---

## Yellow Ant — composite player avatar

The Yellow Ant is **NOT** a single entity type. It is a composite:

- **Cursor entities** types 1 and 2 (`cursor_handler_type1_9D9D`,
  `cursor_handler_type2_9B9B`) — the player's pointer
- **Body** — a Worker (type 14) when the player is a worker, a Queen
  (type 18) when the player is a queen
- **Walker record** at `$7E:E8BE..E8C6` (20 bytes, distinct from the
  entity-table slot) — tracks player-specific state (lives, current
  task, food carried)
- **Popup gating** — `dp[$02A7]` (POPUP_ACTIVE) and `dp[$02E1]`
  (POPUP_LOCK) determine when the Recruit / Queen menu is allowed to
  open

The state diagram in `V4_5_DIAGRAMS.md` §9 walks the full lifecycle
(Spawn → Walking → {Menu, Fight, Eat, Starve} → Death → NextEgg).

---

## Cross-references — danger handlers (manual p.36)

From `scenarios.c` and verified against the spawn calls in `$00:BEDA..$00:BF5E`:

| Danger | Type(s) | Handler addr | Status | Notes |
|---|---|---|---|---|
| Rain (Scenario 3 / Rainy Yard) | $0F + $10 | `$04:9F1D`, `$04:A356` | Lifted | Falling drop + puddle. Wash logic at `scent_rain_wash_cell_02_96A0` |
| Snails (Scenario 6 / River) | $13 | `$04:A533` | Lifted (Queen alias) | Same handler as Queen, different spawn ctx |
| Cat's Paws (Scenario 4 / House) | $17, $4B | `$04:A8D9`, `$04:C653` | $17 Lifted; $4B unlifted | $4B handler in dispatch table but no C body |
| Bicycle (Scenario 5 / Road) | $3D | `$04:C36E` | unlifted | Reads `$7F:E87E` for instance loop |
| Hands (Scenario 4 / House) | $4B | `$04:C653` | unlifted | `hand_squash_EF02` in `combat.c` |
| Human Feet / Lawn Mowers (Scenario 7) | $1B, $1C | `$04:AB5B`, `$04:AC3A` | Lifted | Same as Caterpillar/Spider handlers — different timer ($06 vs $0A) |

---

## Entity-spawn helper

`entity_spawn_0499C1` at `$04:99C1` — call with `X=pos_x, Y=pos_y, A=type`.
Walks the table to find the first empty slot (or extends `dp[$30]`),
populates type/state/position, and copies per-type init constants from
the ROM tables at `$01:EF59` (init word) and `$01:F043` (init attr).

## Per-state machine dispatch

Most multi-state entities (9-19, 23-25, 27, 28, 30) use this pattern at
the top of their handler:
```
TXY                   ; save entity ptr in Y
LDA #$00 / XBA        ; clear A high
LDA $0001,x           ; A = entity.state
ASL                   ; *2 for word table
TAX                   ; X = state*2
JMP (table,pc)        ; indirect dispatch to per-state body
```

The state table follows the JMP instruction in ROM. Each table entry is
a 16-bit ROM address in bank $04.

## Aliases observed in the lifted region

Three handler aliases in the first 32 entries of the table:
- type 19 = type 18 (`$04:A533`) — Queen ↔ Snail-on-Scenario-6
- type 21 = type 16 (`$04:A356`) — Worker / Caterpillar role variant
- type 22 = type 10 (`$04:9E9C`) — colony color variant

The aliasing means the SAME handler is reused with different spawn
context to implement different game-world characters.

## Two parallel entity systems

The dispatch table above drives the **visual entity pool** at `$7E:0600`
(20-byte records, walked every NMI). There is a second system: the
**abstract per-colony parallel arrays** at:

- `$7F:C000` (B X coords) / `$7F:CBB8` (B type), count `$7E:E77E`
- `$7F:D388` (R X coords) / `$7F:D964` (R type), count `$7E:E780`
- `$7F:DD4C` (Danger type) / `$7F:E328` (Danger X), count `$7E:E782`

The two systems are independent. They sync only at fight ingest into
the combatant pool at `$7F:E87E` and at kill events through
`kill_dispatcher_D334`. See `V4_5_DIAGRAMS.md` §6 for the diagram.

---

_Last updated post-Z1 (audit round, 2026-05-22). Reflects F/G/H +
A1-A5 results: ~110/118 entity handlers have bodies; ~8 stubs
remaining (notably $3D bicycle, $4B hand/cat-paw)._

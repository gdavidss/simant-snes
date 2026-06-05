# 15 — Dangers: The 7 Lethal Hazards

This page documents the 7 dangers from the manual (page 36): Rain, Lawn
Mowers, Human Feet, Snails, Cat's Paws, Bicycle Tires, Hands. Each maps
to entity types spawned by the per-view decoration dispatcher at
`$00:BE9A` and consumed by danger-AI handlers in `entities_*.c`.
The kill resolution is centralized in `combat.c` through two routines:
`mass_kill_sweep_EF1E` (`$03:EF1E`) and `hand_squash_EF02`
(`$03:EF02`).

For the per-scenario decoration mapping, see
[14 — Scenarios](14-scenarios.md). For the rectangular-sweep kernel that
the dangers share with the player's B-click attack, see
[13 — Player Actions](13-player-actions.md) §6.

Manual cross-reference: **page 36** ("Dangers").

---

## 1. Spawn dispatch table

The 16-entry jump table at `$00:BE9A` is indexed by `view * 2`. Each
slot's body lives at a fixed bank-`$00` address that calls
`entity_spawn_0499C1` one or more times:

| Address | View slots | Danger entities spawned |
|---------|-----------|-------------------------|
| `$00:BEDA` | 2 | RAIN: 1× `$10` (puddle) + 3× `$0F` (drop) |
| `$00:BEF3` | 3 | HANDS + CAT'S PAWS: 3× `$11` (decimal 17, spider/paw) + 2× `$4B` (hand) |
| `$00:BF2D` | 4 | BICYCLES: 1× `$3D` (bike squad) + 6× `$1C` + 2× `$1B` |
| `$00:BF33` | 11..15 | FEET + MOWERS: 5× `$1C` + 2× `$1B` (same handlers, different timer) |
| `$00:BF5E` | 5 | SNAILS: 3× `$13` (Queen-family wanderer) |

(Verified in V4-8 dispatch-tables audit.)

The mapping is not 1-to-1 between manual-listed dangers and entity
types: Hand reuses `$4B`, Cat's Paw reuses `$11` (decimal 17, spider), and Lawn
Mowers share `$1C/$1B` with Human Feet. The distinguishing detail is the
per-entity timer set at spawn-init (`dp[$0050]` in the spawner) — `$06`
= "fast" = mower; `$0A` = "normal" = foot.

---

## 2. The 7 dangers in detail

### Danger 1 — Rain (Scenario 3 / Yard, view 8)

- **Spawn**: `$00:BEDA` — 1× type `$10` (puddle entity at `$04:A356`)
  + 3× type `$0F` (falling-water-drop entity at `$04:9F1D`).
- **Entity handlers**:
  - Type `$0F` `$04:9F1D` — 3-state machine in the egg-fall family;
    falls to landing-Y `$69`, then despawns.
  - Type `$10` `$04:A356` — stationary puddle drawn behind the scent
    layer.
- **Kill mechanic**: rain does NOT kill. The lethal aspect is *scent
  erasure*: `scent.c:scent_rain_wash_cell_02_96A0` (line 471) weakens
  every nest-scent cell by `$14` (20) and zeros every trail-scent cell.
  Ants that depend on scent following will wander, get lost, and may
  starve.
- **Pacing**: gated by `view_config.danger_rate_E8FE` (`$0600` in
  Yard).

### Danger 2 — Lawn Mowers (Scenario 3 / Yard, advanced)

- **Spawn**: shares with **Human Feet** at `$00:BF33` (view slots
  11..15). 5× `$1C` + 2× `$1B`.
- **Entity handler**: type `$1C` and `$1B` share the spider AI body in
  `entities_d.c::type28_state2_hunt_AC99`. What makes them "mower" vs
  "foot" is the entity's +`$10` timer:
  - Timer `$06` = fast = MOWER
  - Timer `$0A` = normal = FOOT
- **Kill mechanic** (`combat.c:mass_kill_sweep_EF1E`, line 1177):
  iterates all B-colony ants (`B_TYPE` at `$7F:CBB8`, count at
  `$7E:E77E`). For each non-empty slot, with **25% probability**
  (`rand_modulo_F3BD(4) == 0` returns 0..3 uniformly; `== 0` is 1-in-4),
  zero the entity tile in map3
  (`$13000+y·$80+x`) and zero the type byte (`$7F:CBB8+i = 0`).
- **Fanfare**: after the sweep, if `dp[$66] <= 1` AND `dp[$4A] == 1` AND
  another 50% roll fires (`rand_modulo_F3BD(2)`), call
  `kill_dispatcher_D334(6)` → event `$45` + `INC $E844` (fights-won
  counter).
- **Manual claim**: "Lawn Mowers grind up and blow away 1/4 of all ants
  they contact." Per-ant kill probability = **25%** (1-in-4); the 50%
  post-sweep gate is a fanfare-announce gate that does NOT affect the
  kill rate. So the per-swipe kill expectation matches the manual's
  "1/4" wording.

### Danger 3 — Human Feet (Scenario 7 / Porch)

- **Spawn**: `$00:BF33` — 5× `$1C` + 2× `$1B` (same as mower).
- **Distinguishing feature**: timer `$0A` (slower stomp animation than
  mower).
- **Kill mechanic**: same `mass_kill_sweep_EF1E` as mower. Each "stomp
  cycle" of the foot is one sweep call. The manual describes "kids
  playing nearby" — code-wise this is just a timer reload.

### Danger 4 — Snails (Scenario 6 / River, view 5)

- **Spawn**: `$00:BF5E` — 3× type `$13` (`$04:A533`).
- **Entity handler**: Type `$13` shares the Queen-Ant dispatcher; it's
  a 6-state wanderer with very slow motion (walk-cycle ~120 frames vs
  ants' 4 frames). Hence "snails" — the same code as a Queen, just
  rendered with snail tiles.
- **Kill mechanic**: snails do **not damage**. Their gameplay effect is
  to BLOCK scent-trail propagation: the scent tilemap skips over a
  snail's tile so ants can't path through them. (Confirmed in
  `scenarios.c:533` and `scent.c` propagation logic.)
- **Manual**: snails are listed as a danger because they obstruct
  pathing, not because they squash.

### Danger 5 — Cat's Paws (Scenario 4 / House, view 4)

- **Spawn**: `$00:BEF3` — 3× type `$11` (decimal 17, Spider; handler
  at `$04:A43B`) + 2× type `$4B` (`$04:C653`) at fixed `(X=$3E, Y=$2A)`
  with init flag `$0010`. (Earlier wiki drafts wrote `$17` here — that
  was a hex/decimal confusion. The decompiled function name
  `type17_handler_A43B` uses **decimal** 17 = `$11`.)
- **Entity handlers**:
  - Type `$11` `$04:A43B` — 5-cell predator with separate top/bottom OAM
    attrs. Originally the spider entity; reused as the cat's paw because
    SNES OAM was already maxed out (the full cat sprite wouldn't fit).
  - Type `$4B` `$04:C653` — 4-state hand entity (see Danger 7).
- **Kill mechanic**: cat's paw uses the same
  `mass_kill_sweep_EF1E` as mower/foot. The cat is treated as a "giant
  spider AI-wise" — same hunt-state pathing as the spider, same
  rectangular kill sweep.
- **Distinct trait**: the `+$10` bias byte is set to `$80` after spawn,
  which causes the entity to perform a "swipe" animation across the
  screen at the start of state 1 (this is the visual "paw cuts
  through"). See lifted code at `$00:BF1D..BF2A`.

### Danger 6 — Bicycle Tires (Scenario 5 / Road, view 5)

- **Spawn**: `$00:BF2D` — 1× type `$3D` (`$04:C36E` bicycle handler)
  + 6× type `$1C` + 2× type `$1B`.
- **Entity handler**: Type `$3D` reads `$7F:E87E` (red colony count)
  and spawns ITSELF that many times along a horizontal sweep line —
  matching the manual's "groups of bicycles".
- **Kill mechanic**: type `$3D` walks left-to-right at `$0040`
  pixels/frame; on collision with any ant, the ant's +`$10` timer is
  forced to `$00` and the type byte is zeroed. This is an inline kill
  at the entity's per-tick step — it does NOT go through the
  `mass_kill_sweep_EF1E` rect path because bicycles are continuous
  motion (the sweep is for instantaneous events like a stomp).

### Danger 7 — Hands (manual p.36)

- **Spawn**: type `$4B` at `$04:C653`. Spawned at fixed `(X=$3E, Y=$2A)`
  by the same `$00:BEF3` decoration handler that fires for Porch /
  House.
- **Entity handler**: 4-state machine triggered by long-press input
  events (the hand "appears" when the player holds the mouse button on
  a non-ant area; in practice the SNES port only spawns the 2 fixed
  instances at view-decoration time).
- **Kill mechanic** (`combat.c:hand_squash_EF02`, line 1252): rectangle
  test, **100% kill** if the ant's `dp[$46]` and `dp[$48]` lie inside
  `[rect_x1..rect_x2] × [rect_y1..rect_y2]`. The kill dispatcher code
  passed to `D334` is `5` (the "Cat's Paw / Hand" event, which
  increments `$E844`).

```c
if (dp[0x46] < rect_x1) return;
if (dp[0x46] > rect_x2) return;
if (dp[0x48] < rect_y1) return;
if (dp[0x48] > rect_y2) return;
kill_dispatcher_D334(5);
```

The caller loops over all entities in the rect, so a single hand swing
can kill multiple ants per frame (the per-ant probability is 1 inside
the rect, 0 outside — no chance of a survivor).

---

## 3. The mass-kill sweep — `mass_kill_sweep_EF1E`

Used by **Lawn Mower, Human Foot, Cat's Paw**. See `combat.c:1177`.

```c
if (dp[0x99] == 0) return;            // tutorial-mode gate

unsigned i = B_COUNT;
while (i-- > 0) {
    if (B_TYPE(i) == 0) continue;     // empty slot
    if (rand_modulo_F3BD(4) != 0)     // 25% per ant (1-in-4)
        continue;
    slotmap_select_a_F59F();          // resolve into tile-map cursor
    wram[0x13000 + (B_ATTR(i)*0x80 + B_X(i))] = 0;
    B_TYPE(i) = 0;                    // kill
}

// 50% post-sweep fanfare
if (dp[0x66] > 1) return;
if (dp[0x4A] != 1) return;
if (rand_modulo_F3BD(2) == 0) return;
kill_dispatcher_D334(6);              // event $45 + INC $E844
```

Key invariants:

- **Colour-blind**: only B-colony is swept (the player's colony). The
  R-colony parallel array at `$7F:D964` is NOT scanned here. (R is
  scanned by other paths — e.g. the player's B-click rect at
  `$03:EE66`.)
- **No bounding-rect check**: every ant in the colony parallel-array is
  a candidate, regardless of screen position. The mower/cat/foot kills
  ants that are *anywhere on the map*, not just under the visible
  swipe.
- **Damage granularity**: per-slot. Each ant either dies or survives;
  there is no HP subtraction.
- **The `$E77E` count is NOT decremented here**. The sim tick's
  compaction pass in `simulation.c:pop_aggregator_956E` rebuilds the
  count next frame.

### Manual ↔ code damage formula

| Manual claim | Code behavior |
|--------------|---------------|
| "Mower grinds up 1/4 of ants" | **25% per-ant kill** (1-in-4) + a SEPARATE 50% fanfare-announce gate that doesn't affect kill rate |
| "Cat's paw squashes ants" | Same `mass_kill_sweep_EF1E` |
| "Feet squash ants" | Same; just different visual timer |
| "Hand grabs ants" | `hand_squash_EF02` — 100% inside rect |

---

## 4. The hand-squash kernel — `hand_squash_EF02`

Used by **Hand only**. See `combat.c:1252`.

Single-ant test against a bounding rectangle. ROM at `$03:EF02`:

```
LDA $46 / CMP $E7   ; ant.X >= rect.X1
BCC fail
CMP $F68F           ; <= rect.X2
BEQ ok / BCS fail
LDA $48 / CMP $E7   ; ant.Y >= rect.Y1 (note: the ROM uses $E7 for Y1
                    ; too — this looks like a bug or a deliberate reuse
                    ; of the same byte for X and Y rectangle starts)
BCC fail
CMP $F691           ; <= rect.Y2
BEQ ok / BCS fail
LDA #$0005          ; kill code 5
JSL $03:D334
RTL
```

The caller (the hand-danger event tick at `$02:DD5F`) invokes
`hand_squash_EF02` once per entity in the rect, so multi-ant kills per
swing are possible.

---

## 5. The kill dispatcher — `kill_dispatcher_D334`

After a kill is committed (either by mass sweep or hand squash), the
event is reported through `kill_dispatcher_D334(code)` at `$03:D334`.
The verified D3C0 jump table maps codes to events:

| Code | Event | Effect |
|------|-------|--------|
| 5 | Cat's Paw / Hand | `INC $E844` (fights-won) + queue event `$45` |
| 6 | Mass-kill fanfare | `INC $E844` + queue event `$45` |
| 7 | Mower (corrected) | `INC ???` (separate counter — V4-8 audit) |

The "fights won" counter at `$E844` is what the Status Screen renders as
"fights won %". The fact that the dispatcher INCs this counter for
cat's-paw / hand events as well as actual fights explains why playing on
House / Porch inflates the player's apparent combat score.

---

## 6. Predator-vs-ant damage rates (vs danger-vs-ant)

For completeness (these are not "dangers" per the manual page 36, but
they're related kill paths):

- **Spider / Ant Lion vs Ant** (`spider_predation_tick_C0FD` in
  `combat.c`): 1/128 per check, every 16 sim-ticks per spider. The Ant
  Lion variant checks every 4 ticks (4× faster).
- **Predator kill radius**: spiders/ant lions check only ONE cell —
  strictly grid-adjacent, no radius.
- **Hand/Cat's Paw radius**: 2-cell (the typical sprite is ~16×16 px =
  2×2 tiles).
- **Lawn Mower**: NO bounding check (see §3).

---

## 7. Surprising findings

- **The mass-kill sweep is colour-blind to player colony.** It always
  sweeps `B_TYPE` at `$7F:CBB8` — the player's colony. The R-colony
  isn't endangered by mowers / cat / foot. This is gameplay-balanced:
  the player has more to lose, and the dangers are pressure on the
  player.
- **Cat's Paw / Mower / Foot share the same kernel.** This is the
  reason the manual's wording is identical ("squash") — the code
  literally is identical. The only difference is the per-entity
  motion timer.
- **Snails are not damaging.** They're a *trail-blocking* obstacle.
  The manual lists them as a danger because they obstruct routing, not
  because they kill.
- **Rain doesn't kill.** It erases scent. Ants die only indirectly,
  from getting lost and starving.
- **Hand is 100% kill, but only inside its rect.** No probability roll.
  The "rect" is small (~2×2 tiles), so the player can usually dodge it.
- **The Bicycle uses its own inline kill, not the mass sweep.**
  Bicycles continuously move; the kill is on the per-frame collision
  step inside the type-`$3D` body, not in the centralized
  `mass_kill_sweep_EF1E`.
- **`mass_kill_sweep_EF1E` is gated by `dp[$99] != 0`.** This is the
  "scenario or full game" gate — in tutorial mode (`dp[$99] == 0`),
  mowers and cats can't kill. Helpful for the learning curve.

---

## 8. Inline source pointers

Source files annotated with cross-references back to this page:

- `combat.c:mass_kill_sweep_EF1E` (line 1177) — mower/foot/paw sweep.
- `combat.c:hand_squash_EF02` (line 1252) — hand rectangle test.
- `combat.c:kill_dispatcher_D334` — event reporter (`$03:D334`).
- `scenarios.c:danger_rain_spawn`, `_feet_spawn`, `_snails_spawn`,
  `_cat_paws_spawn`, `_bicycles_spawn` — the `$00:BE9A` body
  equivalents.
- `scent.c:scent_rain_wash_cell_02_96A0` (line 471) — rain wash body.

---

## 9. Manual references

- **p.36**: "Dangers" — lists Rain, Lawn Mowers, Human Feet, Snails,
  Cat's Paws, Bicycle Tires, Hands. The seven map directly to the
  five spawn handlers above (with timer-distinguished mower vs foot,
  and decoration-handler-shared cat vs hand).
- **p.22..23**: Per-scenario "Dangers" line in each level briefing
  matches the decoration handler at `$00:BE9A + view*2`.

What the manual does NOT say but the code reveals:

- The mass-kill sweep ignores screen position (kills ants anywhere on
  the map), not just ants under the visible swipe.
- Cat's Paw and Lawn Mower use literally the same kill kernel.
- "Snails" are decorative — they obstruct trails but don't damage.
- Rain is an indirect killer via scent erasure, not direct damage.

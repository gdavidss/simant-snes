# 13 — Player Actions: Cursor → Game Effect

This page documents how a button-press on the SNES controller becomes a
game effect. The manual (pages 10..13) describes the Yellow Ant's actions
abstractly ("press B on a food crumb to pick it up"); the ROM implements
them through a surprisingly **decentralized** chain that spans three
distinct layers — there is no central "router" function.

For the simulation tick that consumes these queued actions, see
[02 — Simulation Tick](02-simulation-tick.md). For the entity types that
detect clicks, see [ENTITIES](../ENTITIES.md). For combat (the kill side
of "attack"), see [16 — Dangers](15-dangers.md).

Manual cross-reference: **pages 10..13** ("Yellow Ant — Action Buttons").

---

## 1. The three layers of input

Player input is distributed across three concentric polling loops. There
is no single "read joypad, dispatch action" function:

| Layer | Where it lives | What it does |
|-------|----------------|---------------|
| 1. Per-view run loop | `states_gameplay.c` (each of view states `$1E/$20/$22/$23/$25/$27`) | Polls `JOY1L`/`JOY1H` directly each frame; handles SELECT, X (recenter), L (sel box), and the view-local A/B semantics. |
| 2. Per-entity click test | `entities_a..d.c`; each clickable entity calls `sub_DC84_entity_clicked` (`$04:DC84`) | A `$20 × $20` window around the cursor; returns "I am clicked" iff any of A/B/X/Y is held *and* the entity is inside the box (or a menu-internal fast-tick fires while `dp[$71]` is locked). |
| 3. Popup state machine | Entity type 29 at `$04:AD01` | Once cued via bits in `dp[$02A7]`, owns the screen for 10 sub-states until the player confirms or cancels. Recruit menu, Queen menu, dialog popups, all funnel through here. |

The KEY consequence: there's no place to set a breakpoint that catches
every player action. To trace "B clicked on red ant", you have to follow
the red ant's own state-1 handler, which is what calls `sub_DC84` and
mutates its state byte in response.

---

## 2. A button — cursor confirm / select / dig

The A-button is handled by `cursor_action_A_9DB9` at `$04:9DB9` (see
`entities_a.c:cursor_confirm_action_9DB9`). It reads the edge-detected
press latch at `dp[$60]/dp[$61]`:

| `dp[$60]` bit 7 | A pressed this frame |
| `dp[$61]` bit 7 | B pressed this frame |
| `dp[$61]` bit 4 | Y pressed this frame |

When A or B/Y fires AND `dp[$0071] == 0` (no menu lock), the cursor
entity destroys itself (`self->type = 0`), the scheduler SP is yanked to
`$04FF`, and `dp[$0B]` is forced to `$16` (the "menu-accepted" state).
This is the canonical "press A to confirm a menu option" path.

For the close-up A-button on a *world tile*, see
`player_actions.c:close_up_nest_a_button_action_CD30` — clicking inside
the nest panel pans the camera to the chamber under the cursor. Note:
this is **NOT** "send ant to (X, Y)" — it's a camera scroll target.

---

## 3. B button — pickup / cancel / context menu

The B-button does NOT go through `$04:9DB9`. Instead, every clickable
entity polls its own click box via `sub_DC84_entity_clicked` and reads
`dp[$60]`/`dp[$61]` directly to test which button is held. The
`player_b_button_action` skeleton (`player_actions.c:1161`) shows the
intended fan-out:

```
B-click on Yellow Ant himself  -> dp[$02A7] |= 8 (Recruit cue)
                                  OR |= $10 (Queen cue, if Yellow is Queen)
B-click on food / egg / pebble -> simulate_pickup_food_for_yellow_lift()
                                  (or _eat_ if hunger < $30 AND target is food)
B-click on red ant             -> simulate_attack_red_for_yellow()
B-click on black ant + hungry  -> simulate_trophallaxis_for_yellow()
B-click on empty               -> no-op
```

The pickup vs eat decision uses `WRAM_HUNGER_E7B8` at `$7E:E7B8` — the
hunger meter mirror produced by the live-stats summary. The threshold
`< $30` matches tutorial string `$01:B07E` ("...is HUNGRY!!!").

---

## 4. Recruit menu — `$01:86E8`

The Recruit menu is a 5-entry popup whose pointer table lives at
`$01:86E8` (1 count byte + 5 × 2-byte pointers into bank `$01`):

```
$01:86E8: 05                ; count = 5
$01:86E9: F3 86             ; -> "Recruit 5"
$01:86EB: FF 86             ; -> "Recruit 10"
$01:86ED: 0B 87             ; -> "Recruit All"
$01:86EF: 17 87             ; -> "Release 1/2"
$01:86F1: 23 87             ; -> "Release All"
```

`recruit_menu_open_009D1A` in `player_actions_full.c:542` opens the
popup. On confirm, the menu dispatcher returns the selected slot in
`dp[$1A]` (0..4) and the code writes `slot + 1` to `dp[$02B7]` — the
shared "player action ID" cell.

**Surprising finding #1: there is no caller-side recruit logic.** The
menu commit *only* writes `dp[$02B7]` and plays an SFX. One sim-tick
later, the colony-tick router at `$02:8047` calls
`player_action_dispatch_03D792` (the 14-slot dispatcher at `$03:D7A3`),
which reads `dp[$02B7]`, runs the matching handler, then zeroes
`dp[$02B7]` so the action fires exactly once.

### The dispatcher (`$03:D792`, 14 slots)

| Slot | Handler | Effect |
|------|---------|--------|
| 0 | RTL | no-op (cancel) |
| 1 | `recruit_apply_02A1F4(5)` | Recruit 5 |
| 2 | `recruit_apply_02A1F4(10)` | Recruit 10 |
| 3 | `recruit_apply_02A1F4(1000)` | Recruit All (drains colony) |
| 4 | `release_apply_02A2CB(0)` | Release half |
| 5 | `release_apply_02A2CB(1)` | Release all |
| 6,7 | RTL | reserved |
| **8** | `dig_action_03D7EA` | DIG (Queen-menu "Dig New Nest" slot 8) |
| **9** | `neighbour_action_03D808` | LAY (Queen-menu "Lay Eggs" slot 9) |
| 10–12 | RTL | reserved |
| 13 | `rect_sweep_action_03EE66` | RECT SWEEP (attack — see §6) |

### Recruit body (`$02:A1F4`)

`recruit_apply_02A1F4` (`player_actions_full.c:116`) iterates BOTH the
B-colony and R-colony parallel arrays (the Yellow Ant can be in either,
e.g. Scenario 8 "The Other Side"). For each ant:

- B-colony tables: `type=$7F:CBB8+i`, `state=$7F:C7D0+i`, `timer=$7F:CFA0+i`
- R-colony tables: `type=$7F:D964+i`, `state=$7F:D770+i`, `timer=$7F:DB58+i`

The accept rule: non-fighting ant whose caste (= `(type & $7F) >> 3`) is
Worker (2) or Soldier (6); or Breeder (4) / Queen (8) when
`dp[$50] == $20` (mating-flight mode).

**Surprising finding #2: "recruit" is just a state-byte flip.** The
action sets the ant's state byte to **`6` = "follow Yellow Ant"** and
zeros its per-ant timer. There is no escort list, no radius check (the
manual implies a proximity gate; the ROM has none), and no in-frame
visual change. The state-6 AI lives in `entities_*.c` and consumes the
state byte the next sim tick.

`release_apply_02A2CB` reverses the flip: scans for ants whose state ==
6 and zeros it. "Release 1/2" reads `dp[$E7D2]` (the hunger-feeder
*reused* as an escort-pressure counter) and releases that many divided
by two. "Release All" passes `feeder + $64` to overshoot.

---

## 5. Queen menu — `$01:872F`

The Queen menu pointer table lives at `$01:872F`:

```
$01:872F: 02                ; count = 2
$01:8730: 34 87             ; -> "Dig New Nest"
$01:8732: 41 87             ; -> "Lay Eggs"
```

`queen_menu_open_009CF0` (`player_actions_full.c:522`) reads
`dp[$02B1]`:

- If `dp[$02B1] != 0`: open the **single-entry** variant at `$01:874A`
  ("Lay Eggs" only, count=1). On confirm: `dp[$02B7] = 9`.
- Else: open the 2-entry table at `$01:872F`. On confirm:
  `dp[$02B7] = slot + 8` (slot 0 → 8 = Dig, slot 1 → 9 = Lay).

The dispatcher routes:

- Slot 8 ("Dig New Nest"): `dig_action_03D7EA` → calls `$03:B7A7` kernel
  with `(A=dp[$4A], X=dp[$46], Y=dp[$48])`. Gates on `dp[$4A] == 1`
  (Worker form). Returns `$FFFF` on failure; success is silent, failure
  plays SFX `$003C`.
- Slot 9 ("Lay Eggs" / neighbour spawn): `neighbour_action_03D808` →
  calls `$03:D10D` after staging target at `(dp[$46]+2·dx, dp[$48]+2·dy)`
  with `dir = dp[$4C] ^ 4` (180° flip). Gates on `dp[$4A] == 2` (Queen)
  AND `dp[$48] >= 3` (away from top edge).

Note: slot 8's gate of `dp[$4A] == 1` (Worker) is unexpected — the
manual implies the Queen digs. The actual ROM behavior matches: the
*player* must be in Worker form to dig; the Queen-menu's slot 8 only
fires for the alternate Queen→Worker form.

---

## 6. Attack — `rect_sweep_action_03EE66`

The Attack action does NOT call a per-entity hit-test. The dispatcher
slot 13 at `$03:EE66` is a **rectangular sweep** over the entire
B-colony parallel array (`player_actions_full.c:268`):

```c
rect_x0 = W16(0xE5);   rect_x1 = rect_x0 + W16(0xEB);
rect_y0 = W16(0xE7);   rect_y1 = rect_y0 + W16(0xE9);
for i in 0..B_COUNT:
    if B_TYPE(i) == 0:    continue
    if B_ATTR_Y(i) outside [rect_y0..rect_y1]: continue
    if B_X(i)      outside [rect_x0..rect_x1]: continue
    type = B_TYPE(i); B_TYPE(i) = 0
    kill_resolver_02C379(type & $80, x, y)
```

**Surprising finding #3: the rect-sweep kernel is colour-blind.**
The kernel `rect_sweep_action_03EE66` itself does NOT have an
"is the target red?" gate. But the player-action surface layer
(`surface_closeup_b_press_A86A` in `player_actions_full.c`) DOES gate
the trigger — it only dispatches slot 13 when the cursor's tile
resolves to a red ant. Net behaviour: every ant inside the rect dies,
but the rect is only sized/dispatched when the cursor is over a red
ant in the first place.

**Layering with the combat pool (see [`08-combat.md`](08-combat.md) §6).**
The rect sweep is **layer A** of the B-click cascade. Any red entities
in the rect that aren't outright zeroed by the sweep are funnelled into
the active-combatant pool via `combatant_append_96B0` and resolved by
`fight_resolver_96D7` on the next sim tick. Earlier wiki drafts
described these as alternative paths — they are not, they are
sequential layers.

**Distinct from danger kernels.** This kernel `$03:EE66` is *only* the
player attack action and only sweeps the B-colony (or R-colony when
the player is on R). The danger kernels at `$03:EF1E`
(`mass_kill_sweep_EF1E` — mowers / cat / feet) and `$03:EF02`
(`hand_squash_EF02` — hand, 100% inside rect) are **separate kernels**
that share the *idiom* of rect sweeping but not the kernel function.
Earlier wiki drafts said "Cat's Paw, Lawn Mower, Foot, and the
player's B-click share the same kernel" — that was wrong; they share
the idiom, not the code. See [15 — Dangers](15-dangers.md) for the
danger kernels.

The dispatcher also has a per-tile follow-up: if the cursor's world
tile (`dp[$02..$05] >> 4`) is inside the rect, it calls
`tile_event_handler_03E1DC` which handles the special "B-click on egg
to pick up" path.

---

## 7. Eating — no dispatcher

**Surprising finding #4: there is no dedicated "Eat" handler.** The
B-click on a food crumb sets carry-state via
`simulate_pickup_food_for_yellow_lift`; the actual food
*consumption* is implicit in the worker AI per-tick step:

- The ant walks onto a food tile.
- The food tile is *moved* 2 cells in the chosen direction (a phase-1
  re-write to tile `$60+variant`), then the destination is rewritten to
  tile `$68+variant` (the "consumed" marker).
- `INC EATEN_COUNTER` at `$7E:E764` (read by `combat.c:eat_food_8C00`,
  line 1307).
- Hunger meter at `$7E:E7B8` is bumped via the volatile feeder at
  `$7E:E7D2`.

So "eat" is a worker-AI sub-tick, not a player action. The B-click on
food while hungry is functionally `pickup + simultaneous-eat` because the
carry-then-walk-onto-target collapses into one frame, but there is no
separate `simulate_eat` entry point in the player-action layer.

---

## 8. Trophallaxis — manual p.11

"Worker Ants share food with each other through a process called
Trophallaxis." Implementation in `combat.c:trophallaxis_attempt`
(line 1416):

- Donor hunger byte (read from `B_ATTR(i)` or `R_ATTR(i)`, the
  parallel-array attribute column) must be `>= $80`.
- Donee hunger must be `< $30` (the manual's "hungry" threshold).
- Transfer amount = min(`$80`, `donor_hunger - $10`) — donor isn't
  allowed to starve from giving (caps at $10 remaining).
- Donee gains the amount (clamped to `$FF`); `$7E:E7D2` (hunger feeder)
  is bumped immediately so the next sim tick reflects it.
- SFX `$4F` plays. Tutorial message `$01:A219` ("The Yellow Ant was fed
  by its nestmate") is queued.

---

## 9. Carry-item state

Per the `Entity` struct in `simant.c`, byte +`$10` (`scratch10` in
`player_actions.c`) is the carry tag for the Yellow Ant and for regular
worker entities:

| Value | Carried item |
|-------|-------------|
| 0 | nothing |
| 1 | pebble |
| 2 | food crumb |
| 3 | egg |
| 4 | larva |
| 5 | pupa |

`simulate_pickup_food_for_yellow_lift` (`player_actions.c:928`) handles
the swap-drop: if Yellow already holds something, the old item is spawned
at his position (with type 16/9/24/25/12 depending on the kind) before
the new pickup.

`player_actions.c:simulate_yellow_ant_dies` also reads `scratch10` to
drop the carried item at the death position before rebirth-scanning the
entity table for an egg/larva/pupa host.

---

## 10. Direct-page summary

The cells touched by the player-action layer:

| Cell | Purpose |
|------|---------|
| `dp[$14]`/`dp[$15]` | cursor screen X / Y |
| `dp[$60]`/`dp[$61]` | edge-detected button latches |
| `dp[$71]` | menu open lock (0 = world input live) |
| `dp[$7B]` | menu-internal fast-tick latch |
| `dp[$02A7]` | popup gate bits (bit 3 = Recruit, bit 4 = Queen) |
| `dp[$02B1]` | popup variant flag (Queen-menu A vs B) |
| `dp[$02B7]` | committed action ID (1..5 = Recruit/Release, 8..9 = Queen, etc.) |
| `dp[$E7B8]` | hunger meter mirror (≤ `$30` = "HUNGRY!!!") |
| `dp[$E7D2]` | volatile hunger feeder |
| `dp[$E764]` | EATEN_COUNTER (global "ants eaten" tally) |
| `dp[$4A]` | current sim-entity state byte (1=Worker form, 2=Queen form during dispatch) |

---

## 11. Manual references

- **p.10**: "The Cursor". Maps to layers (1) and (2) above.
- **p.11**: "Action Buttons — A, B, X, Y, L, R, SELECT, START". The B
  button context-action ladder is exactly the
  `player_b_button_action` fan-out in §3.
- **p.11**: "Trophallaxis" — §8.
- **p.12**: "The Recruit Menu" — §4. The manual labels the 5 entries
  exactly as the pointer table at `$01:86E8`.
- **p.13**: "The Queen Menu" — §5. Manual covers slot ordering and the
  prerequisite that the Yellow Ant must be Queen.

What the manual does NOT say but the code reveals:

- The Recruit and Queen menus go through a single tick-delayed action ID
  cell (`dp[$02B7]`) and a 14-slot jump table. They are NOT direct
  function calls.
- "Recruit" is a state-byte flip to `6`, applied colony-wide with no
  proximity check.
- The Attack action is a colour-blind rectangular sweep that is the same
  kernel used by Cat's Paw, Lawn Mower, and Foot. The danger-shared
  code path is what makes the game's lethal-cursor behavior
  consistent.
- "Eat" has no dispatcher slot. It is implicit in the worker AI's
  per-tick walk-onto-food step.

---

## 12. Inline source pointers

Source files annotated with cross-references back to this page:

- `player_actions.c` — full per-stage commentary; see file header.
- `player_actions_full.c` — ROM-verified bodies for stages 4..9.
- `entities_a.c:cursor_confirm_action_9DB9` — A/B router.
- `combat.c:trophallaxis_attempt` — feeding (§8).

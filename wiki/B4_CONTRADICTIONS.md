# B4 — Cross-Page Contradictions (Wiki Audit)

Audit of the 19 wiki pages under `wiki/` for facts that two or more pages
disagree on. Scope is **wiki-only**: I did not re-verify against the
decomp, only flagged where pages disagree with each other (and, in a few
cases, against `ENTITIES.md` which the pages explicitly cite as the
canonical type table).

**Result: 14 contradictions flagged.** Severity breakdown:

- **Critical (3)**: factual disagreement that would mislead any reader of
  the affected pages — entity type number for the Spider, B-click attack
  routing (rect-sweep vs combat-pool), tick-rate base used to derive
  predation rates.
- **Major (5)**: wrong address, wrong arithmetic, or wrong kernel
  attribution — material to reverse-engineering but a careful reader
  would notice.
- **Minor (6)**: ambiguous hex/decimal usage, parallel-language drift,
  one-byte address typos, slightly different framings of the same fact.

---

## C-1 (CRITICAL) — Spider entity type: `$11` vs `$17`

**Fact:** the entity-table index of the Spider visual handler.

- `09-predation.md` §"Predators" table: **"Spider (type 17)"** and
  `entities_c.c::type17_handler_A43B` (calling it "type 17" — naming
  convention is **decimal** in our function names, so 17 = `$11`).
- `15-dangers.md` Danger 5 / Cat's Paws and §6 Bicycles: **"type
  `$17`"** at `$04:A8D9`, repeatedly, and §"Predator-vs-ant" section
  also says Spider type `$17`.
- `ENTITIES.md` (the explicitly-canonical table both pages cite):
  - `$11`: `$04:A43B`, "5-cell creature, top/bottom row attrs
    (**Spider** candidate)".
  - `$10`/`$15`: `$04:A356` (caterpillar / rain puddle).

**Likely correct:** Spider = **`$11`** (= 17 decimal). The handler at
`$04:A43B` matches Page 09's `type17_handler_A43B` name (decimal
suffix). Page 15 has the hex-vs-decimal confusion — it sees the function
name "17" and writes it as `$17`.

**Suggested fix:** correct `15-dangers.md` Danger 5 spawn line, the
"`type $17`" reuse-as-cat's-paw mention, and §6 §7 to "type `$11`". Also
re-check the Cat's Paw spawn-handler claim (`$00:BEF3 — 3× type $17`):
if the spawner actually emits `$17` (decimal 23) that's a different
sprite from the Spider and the "reused as the cat's paw" claim breaks.

---

## C-2 (CRITICAL) — Yellow Ant B-click on red ant: combat-pool vs rect-sweep

**Fact:** how the player's "B-click on a Red ant" resolves.

- `08-combat.md` §6 "Yellow Ant attack (B-button on Red ant)" describes
  the chain as: state-3 pose → set `target->attr |= 0x40` (IN_FIGHT) →
  `combatant_append_96B0(...)` push pair into active-combatant pool →
  next sim tick `fight_resolver_96D7` resolves → on B-win, kill
  dispatcher code 3 (silent B-win).
- `13-player-actions.md` §6 "Attack — `rect_sweep_action_03EE66`"
  describes the same action as: dispatcher slot 13 fires
  `rect_sweep_action_03EE66` at `$03:EE66`, which **iterates the
  B-colony parallel array** within a rectangle, zeros `B_TYPE` and calls
  `kill_resolver_02C379` — explicitly **does not** go through the
  combat pool or `fight_resolver_96D7`. The page even says this kernel
  is "**shared** by Cat's Paw, Lawn Mower, Foot, and the player's
  B-click".

**Likely correct:** these are probably **two different code paths the
player can hit** — `combatant_append_96B0` for "B-click on a single
red ant that's currently a visual entity", `rect_sweep_action_03EE66`
for the dispatcher-slot-13 form. Page 13 mis-attributes the kernel-
sharing claim though: page 15 says rect-sweep is for the *player's
attack action*, while Cat's Paw / Mower / Foot use a **different**
kernel — `mass_kill_sweep_EF1E` at `$03:EF1E`, which sweeps **the
B-colony** (i.e. the *player's own* ants), not the R-colony, and is
specifically for the danger entities. So `$03:EE66` and `$03:EF1E` are
two distinct rect-sweep routines and Page 13 conflates them.

**Suggested fix:** in `13-player-actions.md` §6, drop the claim that
"Cat's Paw, Lawn Mower, Foot, and the player's B-click ... share the
same kernel". They share an **idiom** (rect sweep) but not the kernel.
Cross-link to `15-dangers.md` §3 which correctly distinguishes
`$03:EE66` (player attack against R-colony) from `$03:EF1E` (dangers
against B-colony). Also reconcile with `08-combat.md` §6 — clarify
which code path B-click actually takes (most likely: combat-pool path
when target is a visual entity, rect-sweep when the cursor is over
empty space).

---

## C-3 (CRITICAL) — Yellow Ant attack: "color-blind" vs "only red"

**Fact:** does the Yellow Ant's B-click attack discriminate by color?

- `08-combat.md` §6 step 1: "Player-action layer detects the B-click on
  **a red ant**." Implies a red-only gate.
- `13-player-actions.md` §6: "**The attack is colour-blind.** There is
  no 'is the target red?' gate. Every ant in the rect dies."

**Likely correct:** these descriptions are of **different layers**.
Page 08 is describing the *trigger* (player aims at a red ant, the
player-action layer reads the target and fires the attack). Page 13 is
describing the *kernel* (`rect_sweep_action_03EE66` sweeps every
B-colony entity in the rect, not just reds — and even mentions this is
"colour-blind"). But Page 13's wording leaves the reader thinking the
player can self-genocide their own colony with a B-click, which doesn't
match the manual or page 08.

**Suggested fix:** in `13-player-actions.md`, soften the claim to "the
kernel is colour-blind; the player-action layer gates the trigger so it
only fires when the cursor's tile contains a red ant". Add a forward
reference to `08-combat.md` §6 for the trigger-side red-only gate.

---

## C-4 (MAJOR) — Predation rate denominator: 60 Hz vs 8.5 Hz

**Fact:** what tick rate underlies the "1/128 chance per check, every
16 ticks" math for the Spider.

- `02-simulation-tick.md` §"Why 8.5 Hz?" definitively establishes that
  the sim tick (which the spider rides) is ~8.58 Hz. NMI is 60 Hz, sim
  is gated by `dp[$B9] >= 7`.
- `09-predation.md` §"Net rate": "1 check every 16 ticks at **60 Hz** =
  **3.75 checks/sec/spider** ... ~1 kill / 34 seconds".

**Likely correct:** if cadence is "every 16 sim-ticks" and sim is
8.58 Hz, then check rate = 8.58/16 ≈ **0.54 checks/sec/spider**, not
3.75. Time-to-kill = 128 / 0.54 ≈ **237 seconds per contested ant**,
not 34. Page 09's number is off by ~7×.

(However, look carefully at `09-predation.md` §1: spider cadence is
gated by `$E788 & $0F == 0`. `$E788` is `SIM_COUNTER` per
`02-simulation-tick.md` — i.e. sim-tick counter, not NMI. So the
cadence is in *sim ticks*, confirming the 0.54 Hz figure.)

**Suggested fix:** in `09-predation.md` §"Net rate", change "60 Hz" to
"8.58 Hz (sim tick)", recompute to 0.54 checks/sec and ~237 sec/kill
(or ~4 minutes per contested ant). The Ant Lion variant: 8.58/4 ≈ 2.14
checks/sec → ~60 sec/kill, not 8.5 sec.

---

## C-5 (MAJOR) — Mass-exodus / round-robin counter address: `$E788` vs `$E878`

**Fact:** which WRAM word is the master sim-tick counter.

- `02-simulation-tick.md` §"Master Counter": `SIM_COUNTER` at
  `$7E:E788`; the slow-phase gates are `(SIM_COUNTER & 0x3F)` and
  `(& 0x1F)`.
- `09-predation.md` §1: spider's tick check gates on `$E788 & $0F == 0`
  (consistent with Page 02).
- `10-territory-49areas.md` §5: mass exodus "Triggered when
  **`$E878 & 0x1F == 0`** (every 32 sim-ticks)".

**Likely correct:** `$E788`. The transposition `78 ↔ 87` looks like a
typo in Page 10 — and the matching gate `& 0x1F` (every 32 ticks)
matches the slow round-robin in Page 02, which uses `$E788`.

**Suggested fix:** in `10-territory-49areas.md` §5 first paragraph,
change `$E878` to `$E788`.

---

## C-6 (MAJOR) — `mass_kill_sweep_EF1E` "1/4 of ants" math

**Fact:** the per-ant kill probability of the mower/foot/cat sweep.

- `09-predation.md` §3: "20% per-ant kill + 50% post-fanfare gate" but
  also "produces **exactly 1/4 on average**".
- `15-dangers.md` §3 "Manual ↔ code damage formula": "20% per-ant kill +
  50% fanfare event ≈ 25%".

Both pages claim the average kill rate is 25%, but the *code* both
pages quote only has **one** probability gate (20%) before the slot is
zeroed. The 50% fanfare gate gates the **kill_dispatcher_D334(6)
fanfare call**, not the kill itself — Page 09 §3 even spells this out:
"The 50% post-fanfare gate determines whether the kill is *announced*
(via D334 code 6/7 fanfare), not whether it occurs. **Kills happen
regardless of fanfare.**"

So the actual per-ant kill rate is **20%**, not 25%. The 25% / "1/4"
phrasing exists only to match the manual's wording ("1/4 of ants") and
is misleading both pages' summary tables.

**Likely correct:** 20% per-ant per-sweep, not 25%.

**Suggested fix:** in both pages' summary tables, replace "≈ 25%" with
"20% (sweep) + 50% fanfare-announce gate". The "1/4" is the manual
saying ¼, but the actual implementation is closer to ⅕. Worth a
"manual says 1/4, code says 1/5" reconciliation note rather than
massaging the numbers to match.

---

## C-7 (MAJOR) — Yellow Ant body decay time: Worker / Soldier / Queen

**Fact:** the post-fight tile-hold ("HP") for the Yellow Ant.

- `05-yellow-ant.md` §3 "Fight": "the player does **not** get HP, just
  the post-engagement decay timer (Worker = **25** ticks, **Queen
  body = 50**)".
- `06-pathfinding.md` §3 "Walking-ant tile-hold (post-fight decay)"
  table: Worker = 25, **Soldier = 50** (no Queen entry).
- `08-combat.md` §3 "State 0" + "Worker vs Soldier — NOT raw HP" table:
  Worker = 25, **Soldier = 50**.

Page 05 attributes the 50-tick decay to "Queen body". Pages 06 and 08
attribute it to **Soldier**. The Queen is type `$12`, Soldier is type
`$0F`; they're separate entities with separate handlers.

**Likely correct:** Soldier = 50 (Pages 06 and 08 agree, both with
direct ROM-line citations). Page 05 either (a) found a special-case
where the Queen body also gets 50, which would need a citation, or
(b) confused Soldier-vs-Worker with Queen-vs-Worker because both are
"better fighters than Worker".

**Suggested fix:** in `05-yellow-ant.md` §3, change "Queen body = 50"
to "Soldier body (Yellow ant in Soldier form) = 50" or cite the path
through which the Queen body inherits the 50-tick decay.

---

## C-8 (MAJOR) — `dp[$2A]` overloaded use: PAUSE flag vs LCG state

**Fact:** what `dp[$2A]` stores.

- `01-architecture.md` §3 NMI handler: `pause_toggle_on_start_8101()`
  sets `dp[$2A] = 1` on START press.
- `03-rng.md` §1: "**`dp[$2A]`** | 8-bit LCG | Player input (V4-F):
  START button on title screen perturbs it via the pause toggle at
  `$00:8101`".

Page 03 explicitly acknowledges the overload (this is how the LCG is
seeded by player input). So the two pages don't contradict — Page 03
even cross-references Page 01's pause-toggle entry-point. Listed here
only because a careful audit will trip over the fact that the LCG
**state** doubles as the pause **flag** — every pause toggle smashes
the LCG seed to `1`, and the LCG then walks `1 → 6 → 31 → 156 → …` for
its next several outputs.

**Likely correct:** both pages are right; the overload is real.

**Suggested fix:** `03-rng.md` already documents this. `01-architecture
.md` could add a one-liner that `pause_toggle_on_start_8101`'s `STA $2A
= 1` is also the LCG seed perturbation referenced in Page 03 — this
would close the loop and make the cross-reference bidirectional.

---

## C-9 (MAJOR) — Scent-cell resolution: "32×32 pixels" vs "2×2 cells"

**Fact:** how many world pixels each scent cell covers.

- `07-scent-system.md` §1 "Cell grid": "1 scent cell = **32 × 32**
  world pixels" (derived from "playfield is 2048×1024" and map is
  64×32 cells).
- `07-scent-system.md` §1 same section just below: index helper says
  `idx = (Y_pixel / 2) * 64 + (X_pixel / 2)` — a **2:1** ratio (1
  scent cell = 2×2 world pixels), not 32:1.
- `07-scent-system.md` §6 "SEED": "**Scent cells are half the
  resolution of the nest tile grid** — one scent cell covers a 2×2
  block of nest tiles".

Three different scales claimed within the same page. The "32×32 world
pixels" derives playfield-pixels/cell. The "2×2 nest-tiles" derives
nest-tile-pixels/cell. The `>> 1` in the helper is "callers have
already converted world coords to half-resolution cell coords (one LSR
each)". So:

- 1 scent cell = 2×2 nest tiles
- 1 nest tile = 16×16 world pixels
- ⇒ 1 scent cell = 32×32 world pixels ✓

The three claims are *all* consistent **if** the reader unpacks
"caller has already LSR'd both axes" correctly. But the index helper as
written (`(x>>1)<<6 + (y>>1)`) **alone** suggests a 2:1 ratio if you
read it without the caller-convention footnote.

**Likely correct:** 1 cell = 2×2 nest tiles = 32×32 world pixels. The
helper's `>> 1` is *additional* on top of the caller's own `>> 1`.

**Suggested fix:** in `07-scent-system.md` §1, add a single sentence
explicitly saying "two LSRs total: the caller does one, the helper
does another, so net divisor is 4 in each axis, i.e. each cell covers
a 4-nest-tile × 4-nest-tile region — wait, no — see §6 — actually it's
2 nest-tiles × 2 nest-tiles because nest tiles are at 8×8 world
pixels". A worked example with explicit pixel/tile/cell arithmetic
would prevent re-confusing the next reader.

---

## C-10 (MAJOR) — Active combatant pool capacity: 5 vs 6

**Fact:** how many concurrent fights can be in the pool.

- `04-entity-system.md` §6: "Combatant pool `$7F:E87E` (**max 5
  entries**, 5 WORD fields each)".
- `08-combat.md` §2: "**Max 5 slots**, each slot has 5 word fields".
- `08-combat.md` §2 "Pool append" code excerpt: `if (i < 6) { COMBAT_X
  (i) = new_x; ... } if (i < 5) { COMBAT_COUNT = i + 1; }` — i.e. the
  write happens for `i = 0..5` (six slots) but the *count* caps at 5.

The wiki text says "5 slots". The code excerpt embedded in Page 08
shows storage for **6** (indexes 0..5), with the **count** capped to
**5**. This is "5 active, 6 allocated", but the pages just say "5".

**Likely correct:** 6 slots physically allocated, 5 logically usable
(ROM matches `if (i < 6)`/`if (i < 5)` exactly). Probably one is a
sentinel/scratch.

**Suggested fix:** in `08-combat.md` §2, note that the pool has 6
physical slots but the count saturates at 5; the 6th is reachable for
writes but not counted. Cross-reference to `04-entity-system.md`.

---

## C-11 (MINOR) — Hex vs decimal in entity type labels

**Fact:** when Page 09 writes "type 17", does it mean `$17` (= 23) or
17 decimal (= `$11`)?

- `09-predation.md`: "**Spider (type 17)**", "**Ant Lion (28)**",
  "spider tick gated on `dp[$50] == $60`". Function names referenced
  are `type17_handler_A43B`, `type28_dispatch_AC3A` — file naming
  convention is decimal, so these resolve to `$11` and `$1C`.
- `04-entity-system.md`: range table says `$09-$15` lives in
  `entities_b.c` and `$16-$1F` in `entities_c.c`. But Page 09 places
  `type17_handler_A43B` in `entities_c.c` — which only fits if
  "type 17" = `$17` (= 23). Then it doesn't fit the ENTITIES.md row
  for `$11` (Spider candidate).

**Likely correct:** function suffixes throughout the codebase are
**decimal** (`type14 = $0E = Worker`, confirmed multiple places). So
"type 17" = `$11`. But Page 09 §1 names the file as `entities_c.c`,
which contradicts Page 04's range table.

**Suggested fix:** in `09-predation.md`, audit every "type N" mention
and standardize to one notation (`$NN` hex preferred, since the rest of
the wiki uses hex). Reconcile the entities_b/c.c file placement with
Page 04 §2.

---

## C-12 (MINOR) — Worker/Soldier handlers: type 14/15 in name, $0E/$0F in body

**Fact:** what number is "Worker" vs "Soldier"?

- `04-entity-system.md` §3: "Example state table (type 14 / Worker, see
  `entities_b.c` line 754)".
- `05-yellow-ant.md` §1: "`type=14` (Worker) or `type=18` (Queen)".
- `06-pathfinding.md` §4: "The Worker (type **`$0E`**) and Soldier
  (type **`$0F`**) walking handlers".
- `08-combat.md` §1: "Workers (14), Soldiers (15), Queen (18)".

Worker = 14 (decimal) = `$0E` (hex). Soldier = 15 = `$0F`. Queen =
18 = `$12`. All four pages are consistent **if** you know the
codebase's "function-name suffix is decimal, prose is hex" convention.
But they look inconsistent because pages 04/05/08 use decimal while
page 06 uses hex.

**Likely correct:** both — same numbers.

**Suggested fix:** add a "notation conventions" line to the wiki
README (or to `04-entity-system.md` §1) explicitly stating "type N in
function names is decimal; type `$NN` in prose is hex". Then standardize
prose to hex throughout (Pages 04, 05, 08 all need touch-ups).

---

## C-13 (MINOR) — `dp[$02A7]` "POPUP_ACTIVE" vs "popup gate bits"

**Fact:** what `dp[$02A7]` encodes.

- `05-yellow-ant.md` §2: "**`dp[$02A7]` POPUP_ACTIVE** — nonzero while
  any popup is on screen".
- `13-player-actions.md` §10 DP summary: "**`dp[$02A7]`** | popup gate
  bits (**bit 3 = Recruit, bit 4 = Queen**)".

Page 05 treats it as a single "in a popup or not" flag. Page 13 says
it's a bitfield with bit 3 = Recruit-cue and bit 4 = Queen-cue.

**Likely correct:** Page 13 is more specific; Page 05 is a
simplification. Both pages are *consistent* in the sense that "nonzero
implies popup", but Page 05 loses information.

**Suggested fix:** in `05-yellow-ant.md` §2, replace the one-liner with
"`dp[$02A7]`: bit 3 = Recruit menu cue, bit 4 = Queen menu cue;
nonzero ⇒ popup active". Cross-reference Page 13.

---

## C-14 (MINOR) — Per-view config block count: 16 vs 16 (but only 8 = scenarios)

**Fact:** how many view-mode blocks are at `$01:81F3`.

- `14-scenarios.md` §1 cooperating tables: "Per-view config |
  `$01:81F3` (**78 B × 16 views**)".
- `14-scenarios.md` §3: "`$01:81F3` holds **16** 78-byte view-config
  blocks. Of these, **8** are the scenarios above; the remaining 8 are
  the Full Game's view modes".
- `16-rendering-pipeline.md` §6: "SimAnt has **16 view-modes** (the 8
  scenarios × 2 nest factions: Black overview, Black close-up, Red
  overview, Red close-up)".

Page 14 says "16 = 8 scenarios + 8 Full Game variants". Page 16 says
"16 = 8 scenarios × 2 nest factions". These describe the same 16
blocks but with **different decompositions**:

- Page 14: 8 scenarios + 8 Full-Game view modes (e.g. overview,
  close-up, marriage flight, etc.).
- Page 16: 8 scenarios × 2 factions (Black/Red overview and close-up).

Both can't be true — `2 × 8 = 16` only if both factions × both
zoom-levels exist for all 8 scenarios, which the scenario picker
(8 levels) doesn't support.

**Likely correct:** Page 14's decomposition (8 scenarios + 8 Full-Game
modes). Page 16's "8 scenarios × 2 factions" is suspicious — Page 14
§3 explicitly says "Full Game uses indices 0, 2, 9, and 11..15" (= 8
indices), confirming 8+8.

**Suggested fix:** in `16-rendering-pipeline.md` §6, change "the 8
scenarios × 2 nest factions" to "8 scenarios + 8 Full-Game view modes
(overview / close-up / marriage flight / etc.)". The "Black overview /
close-up + Red overview / close-up" decomposition probably applies to
the **Full Game**'s 8 modes, not to all 16.

---

## Summary

| # | Severity | Pages | One-liner |
|---|----------|-------|-----------|
| C-1 | CRITICAL | 09 vs 15 vs ENTITIES.md | Spider type = `$11` (page 09 right, page 15 wrong) |
| C-2 | CRITICAL | 08 vs 13 | B-click attack: combat-pool vs rect-sweep — two paths conflated |
| C-3 | CRITICAL | 08 vs 13 | B-click attack: trigger gates on red; kernel doesn't |
| C-4 | MAJOR | 09 vs 02 | Predation rates derived from 60 Hz; should be 8.58 Hz |
| C-5 | MAJOR | 10 vs 02 / 09 | Master counter `$E788` (Page 10 says `$E878` typo) |
| C-6 | MAJOR | 09 / 15 | Mower per-ant kill rate is 20%, not 25% |
| C-7 | MAJOR | 05 vs 06 / 08 | 50-tick decay: Soldier (right) vs Queen body (Page 05 wrong) |
| C-8 | MAJOR | 01 / 03 | `dp[$2A]` is both pause flag AND LCG state — overload documented but cross-link is unidirectional |
| C-9 | MAJOR | 07 internal | Scent cell scale: 32×32 px vs 2×2 cells — three scales not reconciled in one place |
| C-10 | MAJOR | 04 / 08 | Combatant pool: 5 logical / 6 physical slots — pages say "5" |
| C-11 | MINOR | 09 vs 04 | Hex/decimal confusion: "type 17" = `$11` (entities_b.c) but Page 09 names entities_c.c |
| C-12 | MINOR | 04 / 05 / 06 / 08 | Mixed hex/decimal in entity type labels |
| C-13 | MINOR | 05 vs 13 | `dp[$02A7]`: simplified flag vs bitfield description |
| C-14 | MINOR | 14 vs 16 | 16 view-mode blocks: 8+8 (Page 14) vs 8×2 (Page 16) decomposition |

**Top 5 worst** (most likely to mislead a downstream reader):

1. **C-1** — wrong hex/decimal for Spider type number; the function-
   name suffix is `17` (decimal = `$11`) but Page 15 calls it `$17`.
2. **C-2** — Yellow Ant's B-click attack is described as two completely
   different code paths on two different pages.
3. **C-4** — Page 09's predation rate math is off by ~7× because the
   denominator confuses sim-tick (8.58 Hz) with NMI (60 Hz).
4. **C-7** — Page 05 attributes the 50-tick post-fight decay to the
   "Queen body" while Pages 06 and 08 attribute it to the Soldier.
5. **C-6** — both Pages 09 and 15 say "≈ 25%" mower kill rate when
   their own code excerpts clearly show 20%; the 50% fanfare gate is a
   *display* gate, not a kill gate.

Pure read; no wiki edits made.

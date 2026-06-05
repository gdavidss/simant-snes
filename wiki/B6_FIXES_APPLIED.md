# B6 — Documentation Fixes Applied

Applied 18 corrections flagged by the B1 (wiki accuracy), B2 (wiki
xrefs / line numbers), and B4 (cross-page contradictions) audits.
Each entry below shows the BEFORE state (the bad claim or location)
and the AFTER state (the fix). All wiki edits are in-place; the two
source-file edits (`combat.c:1211`, `entities_b.c:894`) were verified
to still compile with `clang -Wall -Wextra -c <file>.c -o /tmp/check.o`
(exit 0, no diagnostics).

## Fix #1 — "20%" mass-kill probability → 25%

**Math:** `rand_modulo_F3BD(0x0004)` returns 0..3 uniformly; the
condition `!= 0 ⇒ skip` means we keep only `r == 0`, which is
**1-in-4 = 25%**. The earlier "20%" figure was simply wrong.

`combat.c:1211` BEFORE:
```c
if (rand_modulo_F3BD(0x0004) != 0) continue;  /* 20% kill chance */
```
AFTER:
```c
if (rand_modulo_F3BD(0x0004) != 0) continue;  /* 25% kill chance (1-in-4) */
```

`wiki/09-predation.md` — 6+ occurrences updated:
- predator table row for Lawn Mower: `20% per B-ant + 50% fanfare` →
  `25% per B-ant + 50% fanfare-announce`.
- code-block comment: `/* 20% kill chance */` → `/* 25% kill chance (1-in-4) */`.
- §3 "The 1/4 math": rewrote "20% per-ant kill" → "25% per-ant kill
  (`rand_modulo_F3BD(4) == 0` — 1-in-4)" and added explicit note that
  the 50% post-fanfare gate is **NOT** part of the kill rate.
- mermaid node label `20%` → `25%`.
- summary table rows (Cat/Foot/Mower): `20% × 50% fanfare` → `25%
  per-ant kill (50% fanfare-announce is independent)`.
- "Surprising findings" paragraph: "20% die" → "25% die (1-in-4)".
- "Hand is the only 100%" parenthetical: removed "1/4 effective for
  cat/foot" (which mixed two different rates) and kept "25% per-ant
  for mower/cat/foot".

`wiki/15-dangers.md` — 4 occurrences updated:
- Danger 2 Lawn Mower bullet: "20% probability" → "25% probability
  (1-in-4)".
- Manual-claim line: "20% × 50% post-fanfare ≈ 25%" → "25% per-ant
  (50% gate is fanfare-announce only)".
- Code block: `// 20% per ant` → `// 25% per ant (1-in-4)`.
- Table row: "Mower grinds up 1/4 of ants" → kept manual quote, fixed
  RHS to "25% per-ant kill + separate 50% fanfare-announce gate".

## Fix #2 — dy direction table at `$02:8077`

**Verification:** raw bytes from `simant.sfc` at LoROM offset
`$02:8077` = ROM byte offset `0x10077` are `FF FF FF FF 00 00 01 00 01
00 01 00 00 00 FF FF`. As 8 int16 LE words = `{-1, -1, 0, 1, 1, 1, 0,
-1}` — exactly the sequence in `scent.c:373` and the wiki. The B1
audit's claim that "raw byte read doesn't reproduce the wiki sequence"
was itself in error (likely read the wrong window). **No edit needed.**
The wiki + source comments in `scent.c` are correct.

## Fix #3 — Mass-exodus cap call site

`wiki/10-territory-49areas.md` §5 BEFORE:
> "lifted at `territory.c:482` ... called from per-tick
> `pop_aggregator_956E`."

AFTER: cited at `territory.c:488` (function header) / 489 (body), and
explicit note that the per-area-scan `area_grid_scan_F02A`
(`territory.c:651`) is what **calls** `mass_exodus_cap_and_presence_F050`
at `territory.c:663`. Earlier `pop_aggregator_956E` attribution
removed.

## Fix #4 — History buffer lap time inconsistency

`wiki/02-simulation-tick.md` BEFORE:
> "Phase 0 and phase 2 both push ... samples every other slow-tick,
> i.e. once every ~7.5 seconds. ... covers about **8 minutes**."

The two statements only reconcile if "phase 0 and phase 2 push" is
read as "one of these two phases pushes once per 128-tick round, not
once per 32-tick slow-tick". Code (`simulation.c:674-707`,
`round_robin_slow_ABEF`) confirms each phase fires every 4 slow-ticks
= every 128 sim-ticks; two of them (0 and 2) push history snapshots,
offset by 64 ticks within a round, so the *effective* push rate is 1
sample per 64 ticks ≈ 7.45 sec. 64 entries × 7.45 sec ≈ 8 min ✓.

AFTER: rewrote the paragraph to spell this out and explicitly call
out the earlier "~2 minutes" derivation as a misread of the cadence.

## Fix #5 — "Two parallel entity systems sync ONLY at fight/kill"

`wiki/04-entity-system.md` §6 BEFORE:
> "The two systems sync **only** at: Fight ingest / Kill events."

AFTER: kept Fight ingest + Kill events, added **Spawn** (kill_alloc
refilling the visual pool) and **Per-tick read** (motion update
reading colony positions), and added a paragraph clarifying that
*only the destructive sync* is gated by fight/kill — non-destructive
reads and spawn paths also cross the boundary.

Page `05-yellow-ant.md` had no parallel-claim — no edit needed there.

## Fix #6 — Spider entity type `$17` vs `$11`

The decompiled handler `type17_handler_A43B` uses **decimal** 17 =
hex `$11`. Page 09 already had this right. Page 15 used `$17` (= 23
decimal) in several places.

`wiki/15-dangers.md` BEFORE (Danger 5):
> "3× type `$17` (`$04:A8D9`) ... Type `$17` `$04:A8D9` — 5-cell
> predator ..."

AFTER:
> "3× type `$11` (decimal 17, Spider; handler at `$04:A43B`) ..."
plus an explicit note "earlier wiki drafts wrote `$17` here — that
was a hex/decimal confusion".

Also fixed:
- Decoration-block table row: `3× $17 (spider/paw)` → `3× $11
  (decimal 17, spider/paw)`.
- "Hand reuses `$4B`, Cat's Paw reuses `$17` (spider)" → "Cat's Paw
  reuses `$11` (decimal 17, spider)".

Page 09 hex/decimal: added explicit "type `$11` (decimal 17)" to the
predator table and a note on the function-name convention.

## Fix #7 — Yellow Ant B-click resolution — LAYERED cascade

Both pages claimed sole ownership of the chain. **Both are right at
different layers.** Edited both pages to document the cascade.

`wiki/08-combat.md` §6 — added preamble explaining the chain is
**Layer A (rect sweep `rect_sweep_action_03EE66`) → Layer B
(combatant pool + `fight_resolver_96D7`)** and cross-linked to
Page 13.

`wiki/13-player-actions.md` §6 — replaced "the attack is colour-blind
... shares the same kernel with Cat's Paw, Lawn Mower, Foot" with:
- "The **kernel** is colour-blind; the surface dispatcher gates on
  red ant."
- Distinct paragraph explaining the cascade into the combat pool.
- Distinct paragraph stating that `$03:EE66` (player attack) is a
  **separate kernel** from `$03:EF1E` (`mass_kill_sweep_EF1E` —
  mowers/cat/feet) and `$03:EF02` (hand). They share the *idiom*
  (rect sweep) but not the function.

## Fix #8 — Predation rate denominator (60 Hz vs 8.58 Hz)

`wiki/09-predation.md` §1 "Net rate" BEFORE:
> "1 check every 16 ticks at **60 Hz** = 3.75 checks/sec/spider ...
> ~1 kill / 34 seconds per spider."

AFTER:
> "Cadence keys off `SIM_COUNTER` (sim tick ~8.58 Hz, NOT NMI 60 Hz).
> 8.58/16 ≈ 0.54 checks/sec/spider, 1/128 success → ~237 sec/contested
> ant (~4 min)."

§2 Ant Lion BEFORE: "~1 ant per 8.5 seconds vs Spider's 34".
AFTER: "~1 ant per ~60 seconds vs Spider's ~237 seconds (~4 minutes)".

## Fix #9 — 50-tick decay attribution

`wiki/05-yellow-ant.md` §3 BEFORE:
> "Worker = 25 ticks, **Queen body = 50**."

AFTER:
> "Worker = 25 ticks, **Soldier** = 50 — see `combat.c:540` (`$19=25`,
> Worker) and `combat.c:619` (`$32=50`, Soldier); earlier wiki drafts
> attributed 50 to the Queen body, which was wrong."

## Fix #10 — Mower kill rate "≈ 25%"

Covered by Fix #1: both pages now consistently say 25% per-ant kill
+ separate 50% fanfare-announce gate that doesn't affect kill rate.

## Fix #11 — C-5 master counter `$E878` → `$E788`

`wiki/10-territory-49areas.md` §5 BEFORE:
> "Triggered when `$E878 & 0x1F == 0`."

AFTER:
> "Triggered when `SIM_COUNTER & 0x1F == 0` ... `SIM_COUNTER` lives
> at `$7E:E788` — earlier drafts wrote `$E878`, which was a 78↔87 byte
> transposition typo."

## Fix #12 — Direction tables ($02:8065 / $02:8077)

Verified — see Fix #2 above. The wiki and `scent.c` source comment
both already correctly state `{-1,-1,0,1,1,1,0,-1}` for dy at `$02:8077`.
B1's contradicting byte read was wrong. **No edits applied.**

## Fix #13 — `17-audio.md:31` → `simant.c:954-967`

BEFORE:
> "The uploader sits at `$08:8006` (lifted summary: `simant.c:954-967`)."

`simant.c:954-967` is asset-decompression / VRAM DMA, unrelated.
Real SPC summary header is at `simant.c:971-981`.

AFTER:
> "The uploader sits at `$08:8006` (lifted summary header at
> `simant.c:971-981`). Earlier wiki drafts cited `simant.c:954-967`,
> which is actually the asset-decompression / VRAM DMA helper —
> unrelated to the SPC uploader."

## Fix #14 — `17-audio.md:37` → `simant.c:1007-1008`

BEFORE:
> "Boot-time SPC dp shadows (set by `simant.c:1007-1008`):"

`simant.c:1007-1008` is `sub_BC7F()` + `dp[$0A] = 0x81` NMITIMEN
setup. Real shadow writes at `simant.c:1024-1025`.

AFTER:
> "Boot-time SPC dp shadows (set by `simant.c:1024-1025` — earlier
> drafts cited `simant.c:1007-1008`, which is `dp[$0A] = 0x81`
> NMITIMEN setup, not the SPC shadows):"

## Fix #15 — `17-audio.md:213` → `audio_driver.c:1568-1572`

BEFORE:
> "see the A1 regression-fix note at `audio_driver.c:1568-1572`."

`audio_driver.c:1568-1572` is the "this is SPC700 not 65816" comment
banner. The actual `if (spc_ram[0x0140 + x] == 0) return;` gate is
at `audio_driver.c:1582-1586`.

AFTER:
> "the actual `if (spc_ram[0x0140 + x] == 0) return;` gate is at
> `audio_driver.c:1582-1586` — earlier drafts cited `audio_driver.c:
> 1568-1572`, which is actually the 'SPC700 not 65816' documentation
> comment, not the gate itself."

## Fix #16 — `10-territory-49areas.md:112` → `territory.c:482`

Already addressed by Fix #3 above. Function header is at
`territory.c:488`, body at 489. Updated wiki citation and the
related row at line 235 of the wiki (the symbol/address table).

## Fix #17 — `00-TCRF-FINDINGS.md:103` → `simulation.c:293`

`simulation.c:293` is a `/* ... */` comment line inside the FEED_*
`#define` block, not a renderer. The actual rendering of dashboards
lives in `ui_menus.c` (per `ui_menus.c:17` header comment naming the
EVALUATION SCREENS). The TCRF row #28 line on the wiki is line 112
(after table row offsets), not 103 — but the substance of the fix is
the bad citation.

BEFORE:
> "Status-bar dashboard renderer present (`simulation.c:293`)"

AFTER:
> "Status-bar dashboard data layout (the `SUMM_*` `#define` block at
> `simulation.c:277-289` — earlier drafts incorrectly cited
> `simulation.c:293`, which is just a comment line in the FEED_*
> feeders block, not a renderer; the evaluation/dashboard rendering
> itself lives in `ui_menus.c` per its file-header note at
> `ui_menus.c:17`)."

## Fix #18 — `entities_b.c:894` inline anchor

BEFORE (`entities_b.c:894`):
```c
 * tile-hold in the sim layer. See wiki/08-combat.md#worker-vs-soldier. */
```

AFTER:
```c
 * tile-hold in the sim layer. See wiki/08-combat.md#worker-vs-soldier--not-raw-hp. */
```

The header in `08-combat.md:129` is `### Worker vs Soldier — NOT raw HP`,
which slugifies to `worker-vs-soldier--not-raw-hp` (the em-dash collapses
to two dashes in markdown-rendered anchors). The other reference in
`entities_b.c:9` already used the correct slug.

Verified with `clang -Wall -Wextra -c entities_b.c -o /tmp/check.o`
(exit 0).

## Source-file compilation verification

Both source files touched by this patch still compile cleanly:

```
$ clang -Wall -Wextra -c combat.c -o /tmp/check.o ; echo $?
0
$ clang -Wall -Wextra -c entities_b.c -o /tmp/check.o ; echo $?
0
```

## Files touched

Source:
- `combat.c` (1 line — comment-only edit)
- `entities_b.c` (1 line — comment-only edit)

Wiki:
- `wiki/02-simulation-tick.md`
- `wiki/04-entity-system.md`
- `wiki/05-yellow-ant.md`
- `wiki/08-combat.md`
- `wiki/09-predation.md`
- `wiki/10-territory-49areas.md`
- `wiki/13-player-actions.md`
- `wiki/15-dangers.md`
- `wiki/17-audio.md`
- `wiki/00-TCRF-FINDINGS.md`

## Judgment calls

1. **B1 vs user on 20% vs 25%** — the B4 audit document also flagged
   this but in the OPPOSITE direction (claiming the kill is 20% and
   the wiki's 25% wording was misleading). The user's instructions
   were explicit: `rand_modulo_F3BD(4) == 0` is 1/4 = 25%. The math
   bears that out (the condition keeps the `r == 0` case only, which
   is 25% of uniform 0..3). I followed the user.

2. **Fix #17 line number** — user described the bad citation at wiki
   line 103, but the actual `simulation.c:293` citation is at wiki line
   112 (TCRF row #28). I edited the citation at line 112 since that is
   the line with the actual bad reference. Line 103 is a different
   TCRF row (row #19) that doesn't cite `simulation.c:293`.

3. **Fix #15 audio gate line range** — user wrote `audio_driver.c:
   1582-1584`. The actual gate line (`if (spc_ram[0x0140 + x] == 0)
   return;`) is at line 1586; the preceding comment block runs
   1582-1585. I used `1582-1586` (gate inclusive) in the wiki edit.

4. **Fix #2 / #12 — `$02:8077` dy table** — B1 claimed the byte read
   contradicted the wiki, but a Python re-read of the ROM at LoROM
   `$02:8077` (ROM offset `0x10077`) gives exactly the wiki's
   `{-1,-1,0,1,1,1,0,-1}` sequence. So the wiki was right and B1 was
   wrong; no edit needed but documented here for clarity.

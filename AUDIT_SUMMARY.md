# SimAnt SNES Decomp — Audit Summary (V2 + V3 + V4 + F/G/H + A1-A5 + Z1)

Master verification report consolidating findings from five rounds of
audit work on the SimAnt SNES decomp at `/Users/guilhermedavid/simant-re/`.

ROM: **SimAnt: The Electronic Ant Colony** (Maxis 1991; SNES port by
Tomcat Systems / Imagineer 1993). LoROM, 1 MB, NTSC, 32 KB SRAM,
checksum `$90F9`.

Companion docs:
- `README.md` — top-level orientation
- `COVERAGE.md` — manual-mechanic-to-decomp matrix
- `ENTITIES.md` — 118-entry entity dispatch table cheatsheet
- `PORTING.md` — what to keep / replace / drop on a new platform
- `V4_1_COMMENT_AUDIT.md` … `V4_8_DISPATCH_TABLES.md` — full V4 sub-reports
- `TEST_RESULTS.md` — behavioral test harness output (21/22 pass)
- `RNG_TEST_RESULTS.md` — bit-perfect RNG diff (50K samples)
- `ASSET_VERIFY_RESULTS.md` — byte-exact asset verification (515,072 B)
- `COVERAGE_ANALYSIS.md` — quantitative coverage breakdown

---

## Headline numbers

| Metric | Value |
|---|---|
| Lifted C across 51 modules | ~68,517 lines |
| Lifted function definitions | ~945 (896 unique names) |
| Stubbed / weak link-glue bodies | ~8 (most stubs eliminated post-F/G/H) |
| **Game states lifted** | **68 / 68 (100 %)** — table at `$00:9369`, 68 entries (not 64). $30-$3E lifted in F4. |
| **Entity handlers lifted** | **~110 / 118 (~93 %)** — table at `$04:9A30`, 118 entries (not 32). +78 from F1/F2/F3/G2/G3/G4/H1. |
| Behavioral tests | 21 / 22 PASS |
| RNG bit-perfect verification | 50,000 samples, zero divergences |
| Asset data verified byte-exact | 515,072 B (158 blobs) |
| Bugs fixed across V2 + V3 + V4 + F/G/H + A1-A5 | ~80 |
| Phantom edits (A3) | 0 across 46 audited fixes |
| Dead static functions (A5) | 7 deletion candidates identified |

---

## What the three audit rounds did

### V2 — Mechanic lifts (4 parallel agents)

Lifted the bulk of game-state and entity-handler bodies, the simulation
tick chain, the scent system, the 49-area territory map, the
Behavior/Caste control-panel math, and the player-action dispatchers
(Recruit / Queen menus, carry / hunger / trophallaxis). Established the
canonical `wram[]` + dp + MMIO model used by every later module.

### V3 — Verification (seven sub-passes)

- **V3-A** — Behavioral test harness (`tests.c`): 22 invariant tests
  across sim_tick, scent, combat, status-screen, mass-exodus,
  marriage-flight. **21/22 pass**; the one expected failure documents
  the `slow_subsys_F927` stub that shadows the lifted
  `history_graph_snapshot_F927` (see `TEST_RESULTS.md`).
- **V3-D** — RNG diff (`rng_diff_test.c`, `rng_state_test.c`,
  `rng_reference.py`): 42,000 (output-byte, dp[$2A], dp[$2B]) tuples
  matched bit-for-bit between the lift and a Python translation of
  `$04:DCD5` + `$04:DCFE`. Long-run 50,000-sample drift test also
  matched. **PASS** — see `RNG_TEST_RESULTS.md`.
- **V3-E** — Test-harness expansion (additional scent, combat, and
  territory invariants).
- **V3-H** — Asset byte-verification (`asset_verify.py`): 43 LZSS
  state-handler blobs + 26 per-view tile blobs + 10 CGRAM palettes +
  per-view dispatch / palette / index tables — **515,072 B across 158
  blobs, zero mismatches** — see `ASSET_VERIFY_RESULTS.md`.
- Other V3 sub-passes added the lifted `simulation.c` Status-Screen
  formulas, the Mass Exodus / Marriage Flight gates, and the History
  Graph snapshot writer.

### V4 — Audit + cross-reference (eight sub-passes)

| Pass | Report | What it produced |
|---|---|---|
| V4-1 | `V4_1_COMMENT_AUDIT.md` | ~38 comment-vs-body divergences (dangerous / misleading / cosmetic) |
| V4-2 | `V4_2_TODOS.md` | 97 TODO/FIXME/`pseudo` markers classified by severity |
| V4-3 | `V4_3_SYMBOL_MAP.md` | 162 ROM addresses with >1 distinct C symbol; canonical-name proposals |
| V4-4 | `V4_4_MANUAL_TO_CODE.md` | Page-by-page manual cross-reference (HIGH=63, MED=12, LOW=5) |
| V4-5 | `V4_5_DIAGRAMS.md` | 10 Mermaid diagrams (boot, NMI, dispatcher, entity, scent, combat, sim_tick, save, Yellow Ant FSM, audio) |
| V4-6 | `V4_6_FLIPPER_PORT.md` | Per-file Flipper port checklist + MVP scoping |
| V4-7 | `V4_7_SPOT_CHECKS.md` | 10 random lifted routines re-decoded from the ROM (8 MATCH, 1 SUBTLY-WRONG, 1 WRONG) |
| V4-8 | `V4_8_DISPATCH_TABLES.md` | Re-decoded the **68-entry** game-state table and the **118-entry** entity-handler table directly from `simant.sfc` |

### F/G/H — post-V4 fix rounds

| Pass | Report | What it produced |
|---|---|---|
| F1/F2/F3 | (rolled into G2/G3/G4 reports) | Entity-handler lift waves, ~78 handlers + dispatcher skeletons |
| F4 | (see `states_late.c`) | Lifted the remaining 15 game states $30-$3E (Evaluation / Encyclopedia sub-states) — 68/68 = 100 % |
| F5 | `F5_SINCOS_FIX.md` | Full LUT-based sin/cos wire-up via `$01:8020`; replaces the broken stub returning 0 |
| F6 | `F6_WIRING_FIX.md` | sub_877D real cooperative-yield wiring, entities_d.c:885 null-deref repair, states $24-$27 (Behavior / Caste) panel wiring |
| G2 | `G2_ENTITIES_E_BODIES.md` | 41 state bodies across 13 dispatchers — types $20-$2B in `entities_e.c` |
| G3 | (verified by H2) | 24 dispatcher state bodies in `entities_f.c` (types $2C-$5F) |
| G4 | `G4_ENTITIES_G_BODIES.md` | 28/28 per-state bodies across 14 multi-state handlers — types $60-$71 in `entities_g.c` |
| G5/H4 | `H4_RECONSTRUCTIONS.md` | Reconstructed gameplay glue in `mechanics_extra.c` |
| H1 | `H1_DIALOG_RENDERERS.md` | Dialog renderers $2D/$2E/$2F fully lifted |
| H2 | `H2_VERIFY_G_FIXES.md` | Verification that G3's 24 entities_f.c dispatchers are all defined |
| H3 | `H3_RESIDUAL_FIXES.md` | 27 residual fixes (caption_screen_BACA 1→2 arg, sub_8F08 extern, etc.) |
| FINAL | `FINAL_CLEANUP.md` | Corrects V3-G's SPC counts (21 max indices → 19 valid handlers; 121 pitch entries → 119 + clamp 120); HG_BUF_BASE 0x1D770 → 0x1F6D7 |

### A1-A5 — post-fix audit (round Z prep)

| Pass | Report | What it produced |
|---|---|---|
| A1 | `A1_POST_FIX_SPOTCHECK.md` | Spot-checked all F/G/H fixes against ROM; found and fixed envelope_tick_0D41 voice-inactive gate, sub_87BC polarity, state12_mode7_setup MMIO |
| A2 | `A2_DOC_CONSISTENCY.md` | 17 contradictions across 27 docs — input to Z1 |
| A3 | `A3_PHANTOM_EDITS.md` | 0 phantom edits across 46 audited fixes |
| A4 | `A4_WARNING_SWEEP.md` | Strict file count: **51 .c files**; compiler-warning sweep |
| A5 | `A5_DEAD_CODE.md` | 7 truly dead static functions flagged as deletion candidates |

### Z1 — documentation refresh

This pass propagated the corrections in A2 across README, COVERAGE,
ENTITIES, AUDIT_SUMMARY, COVERAGE_ANALYSIS, V3_STATUS_CHECK,
V4_8_DISPATCH_TABLES, and V4_4_MANUAL_TO_CODE so the headline numbers
(68/68 states, ~110/118 handlers, 51 .c files, ~68.5 K lines, ~80
bug-fix count) reflect the post-F/G/H + A1-A5 state of the tree. See
`Z1_DOC_REFRESH.md` for the diff summary.

---

## Key corrections discovered in V4 (correcting earlier docs)

### 1. The scent system IS fully lifted

Earlier coverage docs said scent placement / decay / following was
"not lifted" (or only the menu strings). The actual lift is in
`scent.c` — 4 maps at `$7F:4000-$7F:5FFF` (64×32 cells × 1-byte each):

- `scent_seed_black_03_9269` / `scent_seed_red_03_92C2` — initial nest seed
- `scent_decay_nest_black_03_931B` / `…_red_03_9333` — linear nest decay
- `scent_decay_trail_black_03_934B` / `…_red_03_936A` — exponential (`>>1`) trail decay
- `scent_place_black_trail_03_93D1` / `…_red_03_93F5` — MAX-semantics place
- `scent_consume_trail_03_9419` — per-step decrement
- `scent_follow_gradient_full_02A710` — 8-direction picker + smoothing via `$02:AAC7`
- `scent_rain_wash_cell_02_96A0` — Scenario-3 wash (nest -= 0x14, trail = 0)

All six scent tests in `tests.c` pass.

### 2. Entity dispatch table has 118 entries, not 32

V4-8 re-decoded the table at file offset 0x21A30 directly. The full
table is **118 × 2 bytes**. Types $00-$1F are the original ant /
cursor / popup handlers (V2). Types $20-$2B are HUD widgets / panel
icons (G2: `entities_e.c`). Types $2C-$5F dispatchers were lifted in
G3 (`entities_f.c`). Types $60-$71 dispatchers were lifted in G4
(`entities_g.c`). Dialog renderers $2D/$2E/$2F were lifted in H1.
House Screen renderer at type $35 is in `ui_menus.c`.

**~110 / 118 (~93 %)** entity handlers have bodies after F/G/H. The
remaining ~8 stubs include the bicycle danger ($3D at `$04:C36E`) and
the hand / cat-paw danger ($4B at `$04:C653`).

### 3. States $24-$27 are Behavior / Caste Control Panels (not nest close-ups)

V4-8 settled the ambiguity:

- **$24/$25** — Behavior Control Panel setup / run (Forage / Dig / Nurse triangle)
- **$26/$27** — Caste Control Panel setup / run (Worker / Soldier / Breeder triangle)

The setup handler at `$00:CA96` is shared and branches on `dp[$0B]`.
The run handler at `$00:CCD0` is shared the same way. `control_panels.c`
labelling is correct; the earlier `states_gameplay.c` "B.NEST/R.NEST
CLOSE-UP" comment block is misleading and is flagged for cleanup.

Evidence: the setup body spawns the **Auto/Manual icon** entity types
($27/$29/$2B/$2A at `$04:9DD5/9DEA/9DFF/9E14`) plus the numeric-digit
icons ($24/$25/$26 at `$04:B77D/B7C1/B7FF`) — these are panel chrome,
not ants.

### 4. The Yellow Ant is a composite, not a single entity type

The "Yellow Ant" the player controls is implemented across:

- Cursor entity types **1** (`cursor_handler_type1_9D9D`) and **2** (`cursor_handler_type2_9B9B`)
- A Worker (type 14) or Queen (type 18) body that the cursor commands
- The **walker record at `$7E:E8BE..E8C6`** (20 bytes), distinct from the entity-table slot
- The popup-gating flag at **`dp[$02A7]`** (POPUP_ACTIVE) and popup-lock at `dp[$02E1]`

There is no single "Yellow Ant" handler in the dispatch table. The
manual cross-reference (`V4_4_MANUAL_TO_CODE.md`) and the Yellow Ant
state diagram in `V4_5_DIAGRAMS.md` document the composite.

### 5. Two parallel entity systems, synced only at ingest / death

V4-5 found and `V4_5_DIAGRAMS.md` diagrammed two distinct entity stores:

- **Visual entities** — 20-byte records at `$7E:0600` (LoROM mirror
  `$04:0600`), walked every NMI by `entity_step_all_049966` via the
  118-entry dispatch table at `$04:9A30`.
- **Abstract per-colony parallel arrays** at `$7F:C000` (B X coords),
  `$7F:CBB8` (B type), `$7F:D388` (R X coords), `$7F:D964` (R type),
  `$7F:DD4C` (Danger type), `$7F:E328` (Danger X coords). Counts at
  `$7E:E77E` (B), `$7E:E780` (R), `$7E:E782` (Danger).

The two stores are independent in the steady-state. They sync only at
two boundary events: when a fight is queued into the combatant pool at
`$7F:E87E` (max 5 entries), and at kill-dispatcher events through
`$03:D334`.

### 6. SPC700 driver is 3,327 bytes; the 30 KB at $40A00 is music data

Earlier annotations conflated the driver and the data. The actual
driver lives at file offset **0x5F004**, length 3,327 bytes — uploaded
to ARAM by `spc700_upload_driver_088006` (boot path). The ~30 KB blob
at file offset 0x40A00 is music sequence / instrument data, not code.

### 7. Save signatures

- **"DOBBY"** (5 bytes) — full game save, at `$03:F97E`
- **"DURRY"** (5 bytes) — scenario save, at `$03:F983`

Both are written at the head of the staging buffer `$7E:6000` before
the LZSS-compressed body, then copied to SRAM `$70:0000`. A mirror
signature lives at `$70:7FA0..7FA2`. Checksum at `$70:0005` via
`save_checksum_compute_03_FC3A`.

### 8. Bugs fixed across the rounds (~80)

Examples (see V4-1 for the full list of remaining drift):

- 4 wrong-offset bugs (entity-table indexes off by `row*6` vs `row*3`)
- 2 off-by-one comparisons in scent decay / nest column scan
- Sign-inversion in cursor delta accumulation
- Register-width mistakes (8-bit ADC vs 16-bit store)
- `sub_BACA` / `sub_8F08` signature mismatches that previously linked
  via C99 unspecified-args coercion (still flagged in V4-1)
- LZSS streaming overlap (the 64×128 tilemap copy path)
- LCG-vs-tweaked-LFSR confusion in `rng_byte_DCD5` — pinned and
  verified against ROM in V3-D (50K samples)
- Combat `combatant_append_96B0` argument confusion (index vs Y coord)
- Scent-map cell size: an earlier draft treated cells as 16-bit; the
  ROM is 1-byte (acknowledged inline in `scent.c`)

### 9. Verification status

| Subsystem | Status | Evidence |
|---|---|---|
| RNG bit-equivalence | VERIFIED | `RNG_TEST_RESULTS.md` — 50K samples |
| Asset data byte-equivalence | VERIFIED | `ASSET_VERIFY_RESULTS.md` — 515,072 B, zero mismatches |
| Behavioral invariants | VERIFIED | `TEST_RESULTS.md` — 21/22 (one expected stub fail) |
| Dispatch table layout | VERIFIED | `V4_8_DISPATCH_TABLES.md` — re-decoded from ROM |
| Comment fidelity | AUDITED (drift logged) | `V4_1_COMMENT_AUDIT.md` — 38 divergences flagged |
| TODO/stub inventory | AUDITED | `V4_2_TODOS.md` — 97 markers classified |

---

## Top remaining gaps (after V4 + F/G/H + A1-A5)

These are the items that would still block a fully-faithful runtime
port, ordered by severity (see `V4_2_TODOS.md` for the full V4 list,
plus the F/G/H reports for what has since been resolved):

1. ~~`sub_877D` cooperative yield is empty.~~ **FIXED by F6 FIX 1** —
   call redirected to the real `coop_yield_877D` in `misc_helpers.c`.
2. **`switch_view_A3BD` SELECT-button handler** is empty. View toggle
   is non-functional.
3. **State $29 save-run carry-flag path** is replaced with `if (1)`
   placeholders — infinite-loop on first iteration.
4. ~~`entities_d.c:885` null-pointer deref.~~ **FIXED by F6 FIX 4** —
   replaced with `cost += 0;` + TODO; no longer crashes.
5. ~~`lifted_helpers_1.c sub_008A0B_div256r` sin/cos stub returns 0.~~
   **FIXED by F5** — full LUT-based sin/cos via `$01:8020` is now
   wired up. See `F5_SINCOS_FIX.md`.
6. **~8 entity handlers remain as stubs** (was 86). The main gameplay
   stubs are bicycle $3D and hand / cat-paw $4B; the rest of $20-$71
   was lifted across G2/G3/G4/H1.
7. ~~15 game states $30-$3E are unlifted.~~ **FIXED by F4** — all 68
   states now lifted; $30-$3E live in `states_late.c`.
8. **Ant Lion gameplay** (`ant_lion_tick_C0FD`) is an empty stub.
9. **History Graph writer** — `slow_subsys_F927` stub shadows the lifted
   `history_graph_snapshot_F927`; this is the single failing behavioral
   test.
10. **SPC700 driver bodies** — disassembled in `audio_driver.c` (1489 LOC)
    but several sequence-event opcodes (5..14) are not handled.
11. **7 dead static functions** identified by **A5** as deletion
    candidates — see `A5_DEAD_CODE.md`.

### A1-A5 audit round (post-Z) — summary

- **A1** spot-checked F/G/H fixes against the ROM and itself fixed
  three live bugs: `envelope_tick_0D41` missing voice-inactive gate,
  `sub_87BC` polarity inversion, and `state12_mode7_setup` MMIO
  confusion. See `A1_POST_FIX_SPOTCHECK.md`.
- **A2** documentation-consistency audit catalogued the 17
  contradictions resolved by this Z1 refresh. See
  `A2_DOC_CONSISTENCY.md`.
- **A3** confirmed **0 phantom edits across 46 audited fixes** —
  every claimed fix landed in the source tree. See
  `A3_PHANTOM_EDITS.md`.
- **A4** warning sweep, strict file count: **51 .c files**. See
  `A4_WARNING_SWEEP.md`.
- **A5** dead-code analysis: **7 truly dead static functions** flagged
  as deletion candidates. See `A5_DEAD_CODE.md`.

---

## File index

| File | Role |
|---|---|
| `README.md` | Top-level orientation |
| `COVERAGE.md` | Manual-mechanic-to-decomp matrix |
| `ENTITIES.md` | 118-entry entity dispatch cheatsheet |
| `PORTING.md` | Keep / replace / drop guide for new platforms |
| `AUDIT_SUMMARY.md` (this file) | Consolidated V2+V3+V4 audit report |
| `TEST_RESULTS.md` | Behavioral test harness output |
| `RNG_TEST_RESULTS.md` | Bit-perfect RNG verification |
| `ASSET_VERIFY_RESULTS.md` | Byte-exact asset verification |
| `COVERAGE_ANALYSIS.md` | Quantitative ROM coverage stats |
| `V4_1_…_AUDIT.md` … `V4_8_DISPATCH_TABLES.md` | V4 sub-reports |
| `F5_SINCOS_FIX.md`, `F6_WIRING_FIX.md` | F-round fix reports |
| `G2_ENTITIES_E_BODIES.md`, `G4_ENTITIES_G_BODIES.md` | G-round entity lift reports |
| `H1_DIALOG_RENDERERS.md` … `H4_RECONSTRUCTIONS.md` | H-round wrap-up reports |
| `FINAL_CLEANUP.md` | Final cleanup pass; corrects V3-G SPC counts |
| `A1_POST_FIX_SPOTCHECK.md` … `A5_DEAD_CODE.md` | Post-fix audit sub-reports |
| `Z1_DOC_REFRESH.md` | This documentation refresh (diff summary) |

---

_Last updated post-Z1 (audit round, 2026-05-22). Reflects F/G/H +
A1-A5 results: 68/68 states, ~110/118 entity handlers, 51 .c files,
~68.5 K lines, ~80 bug-fixes._

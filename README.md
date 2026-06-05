# SimAnt (SNES) — C decomp scaffold

ROM: **SimAnt: The Electronic Ant Colony** (Maxis 1991; SNES port by
Tomcat Systems / Imagineer 1993). LoROM, 1 MB, NTSC, 32 KB SRAM,
checksum `$90F9`.

After five rounds of audit work (V2 + V3 + V4 + F/G/H fix rounds + A/Z audit refresh):
- **68 / 68 (100 %) game states** lifted (full game-state table is **68
  entries**, not 64 — see `V4_8_DISPATCH_TABLES.md`; the trailing
  $30-$3E sub-states were lifted in round **F4**)
- **~110 / 118 (~93 %) entity handlers** lifted with bodies (rounds
  F1/F2/F3 + G2/G3/G4 + H1 added ~78 handlers and dispatcher state
  bodies on top of the original 32; ~8 stubs remain — see
  `V4_8_DISPATCH_TABLES.md` and the lift addendum below)
- ~68,517 lines of C across 51 modules; ~945 lifted function definitions
- Behavioral tests: **21 / 22 pass** (one expected stub-related fail)
- RNG verified **bit-perfect** across 50,000 samples
- All **515,072 bytes** of asset data verified byte-exact
- **~80 verification fixes** across V2-V4 + F/G/H + A1-A5 rounds
  (envelope_tick_0D41 voice-inactive gate, sub_87BC polarity,
  state12_mode7_setup MMIO confusion most recently — see A1 report)

See `AUDIT_SUMMARY.md` for the consolidated verification report.

## Build

```
make           # compiles + links all .c files into ./simant_decomp
make count     # line counts per module
make check     # per-file compile-check
make clean
sh run_tests.sh  # build + run behavioral test harness (21/22 pass)
```

The resulting binary is a structural artifact (it has empty stubs for
every unresolved ROM subroutine). It proves the lifted code pieces all
agree on the same types, MMIO definitions, WRAM layout, and entity
struct. To port any subset, replace the stubs and SNES-specific
modules with target-platform code.

## Files

### Lifted code (compiles + links) — 51 modules, ~68,517 lines
| File | Purpose |
|---|---|
| `simant.c` | Scaffold: boot, NMI, cooperative SP-swap scheduler, LZSS decompressor, common types |
| `entities_a.c` | Entity types 1-7 (cursors, selection rect, HUD blink indicators) |
| `entities_b.c` | Entity types 8-15 (incl. **Worker** type 14 and **Soldier** type 15 walking-ant AI) |
| `entities_c.c` | Entity types 16-23 (incl. **Queen** type 18/19, snail $13 = Queen alias, **Dig-New-Nest** carver type 20) |
| `entities_d.c` | Entity types 24-31 (incl. **Egg** 24/25, **HUD strip** 26, **dialog popup** 29, **menu cursor** 30); dialog renderers $2D/$2E/$2F lifted in **H1** |
| `entities_e.c` | Entity types $20-$2B HUD widgets and Auto/Manual / digit icons (lifted in **G2**: 41 state bodies across 13 dispatchers) |
| `entities_f.c` | Entity types $2C-$5F dispatchers (lifted in **G3**: 24 dispatcher state bodies; verified by **H2**) |
| `entities_g.c` | Entity types $60-$71 dispatchers (lifted in **G4**: 28 per-state bodies across 14 multi-state handlers) |
| `mechanics_extra.c` | Reconstructed gameplay glue (round **G5/H4**) |
| `states_late.c` | Game states $30-$3E (round **F4**) — Evaluation Screen and Encyclopedia sub-states |
| `states_menu.c` | 10 menu / transition state handlers ($00-$09) |
| `states_gameplay.c` | 38 gameplay state handlers ($0A-$2F), incl. 6 view-state handlers and the Save UI flow |
| `audio_intro.c` | Intro + credits + winter-ending state handlers ($3F-$43) + APU dispatch front doors |
| `audio_driver.c` | Full SPC700 driver lift (1489 LOC); the **3,327-byte** driver itself lives at file 0x5F004 — the 30 KB blob at 0x40A00 is music data, not code |
| `simulation.c` | Sim_tick body ($02:AB58), 8.5 Hz, master counter `$E788`, Mass Exodus + Marriage Flight gates, History Graph snapshot, Status Screen formulas |
| `scent.c` | **Full** scent system: 4 maps at `$7F:4000-$7F:5FFF`, seed / place (MAX) / decay (linear nest, `>>1` trail) / 8-direction gradient follow / Rainy-Yard wash |
| `combat.c` | Fight resolver `$03:96D7`, kill dispatcher `$03:D334`, combatant pool at `$7F:E87E` (max 5) |
| `control_panels.c` | Behavior + Caste control panels — triangle barycentric math, percentages at `$0286-$0294`. **States $24/$25 = Behavior, $26/$27 = Caste** (NOT nest close-ups — verified V4-8) |
| `scenarios.c` | 8 scenario configs at `$01:81F3` (78 B each) + danger entity mapping (rain $0F/$10, snails $13, cat $17/$4B, bicycles $3D, hands $4B, feet/mowers $1B/$1C) |
| `territory.c` | 49-area world map at `$7E:EA46` (B) / `$7E:EAC6` (R), neighbour balance, Mass Exodus split |
| `player_actions.c` + `player_actions_full.c` | Recruit / Queen menus, carry-state, hunger, eat, trophallaxis. `_full` supersedes `_pseudo` variants |
| `mouse.c` | Full SNES Mouse BIOS lifted from `$00:E3FD-$00:E516` |
| `vsync.c` | Vblank-sync helpers (`$E527` streaming, `$DF79` settle check, `$A354` A-button latch) |
| `ui_menus.c` | 6-icon in-game menu (`icon_click_dispatch_A734`) + House / History / Status Evaluation screens |
| `text_screens.c` + `text_content.c` | Encyclopedia (30 pages, 6 topics) + Tutorial (54 messages, pointer table `$00:E2C2`) |
| `save_options.c` | Save / Load / Erase + Sound / Speed options. Sigs: **"DOBBY"** (full) `$03:F97E`, **"DURRY"** (scenario) `$03:F983` |
| `render_helpers.c` | History Graph + Population Graph renderers, per-view OAM init, palette stamps |
| `gaps.c` + `gap_fillers.c` | RNG `$04:DCD5` (LCG+LFSR), Yellow-Ant walker record at `$7E:E8BE`, save checksum, gameplay states $11-$15 |
| `lifted_helpers_1..6.c` | Atomic helpers — sin/cos, multiply, joypad, entity physics, fades, VRAM/CGRAM uploads |
| `misc_helpers.c` | DPAD scroll math (`scroll_surface_view_A106`), fade-in/out, APU dispatchers |
| `assets.c` + `asset_data_1..6.c` | 79 asset-table entries + 515,072 B of compressed tile / palette / view data |
| `tests.c` | Behavioral test harness (22 tests across 8 categories) |
| `stubs.c` | Shared WRAM/MMIO storage + weak empty bodies for unresolved externs |

### Tooling
| File | Purpose |
|---|---|
| `disasm.py` | 65816 disassembler with LoROM mapping + MMIO names + M/X tracking |
| `disasm_spc.py` | SPC700 disassembler used by `audio_driver.c` |
| `dig.py` | Aggressive ROM exploration (HW scans, handler dumps, deep dives) |
| `strings.py` | Locates manual-vocabulary strings in the ROM |
| `extract_text.py`, `text_verify.py` | Encyclopedia / Tutorial text extraction + checks |
| `asset_extract.py`, `asset_verify.py`, `gen_asset_data.py` | Asset blob extraction + byte-verification + C-array generation |
| `coverage_analysis.py` | Coverage scanner — emits `COVERAGE_ANALYSIS.md` + `coverage_summary.json` |
| `gen_stubs.py` | Generates `stubs.c` from current unresolved-symbol list |
| `rng_reference.py` + `run_rng_diff.py` + `run_rng_state_diff.py` | RNG bit-equivalence harness |
| `disasm.txt` / `dig.txt` | Pre-built disassembler outputs |

### Reference
| File | Purpose |
|---|---|
| `simant.sfc` | Working copy of the ROM |
| `SimAnt - The Electronic Ant Colony (USA).pdf` | Official 40-page instruction manual |
| `AUDIT_SUMMARY.md` | **Master verification report** (V2 + V3 + V4) |
| `COVERAGE.md` | Manual-vs-decomp gap matrix |
| `ENTITIES.md` | 118-entry entity dispatch cheatsheet |
| `PORTING.md` | Keep / replace / drop guide for new platforms |
| `TEST_RESULTS.md` | Behavioral test harness output (21/22 PASS) |
| `RNG_TEST_RESULTS.md` | RNG bit-perfect diff (50K samples) |
| `ASSET_VERIFY_RESULTS.md` | Byte-exact asset verification (515,072 B) |
| `COVERAGE_ANALYSIS.md` | Quantitative ROM coverage stats |
| `V4_1_…` … `V4_8_…` | V4 audit sub-reports (comments, TODOs, symbol map, manual cross-ref, diagrams, Flipper port plan, spot checks, dispatch tables) |
| `Makefile` | Build / count / check targets |

## Architecture summary

### Boot
```
RESET ($00:8009)
  ↓ clear WRAM, enter native mode, SP=$03FF
main_9340 (task #0, idles forever after init)
  ├── spawn_task → task #1 (game-state dispatcher at $935C)
  ├── boot_init_BB8D
  │     ├── INIDISP = $80 (force blank)
  │     ├── sub_BC7F (PPU register-init via triples at $01:98A3)
  │     ├── seed_persistent_shadow ($7F:E710-$7F:E719)
  │     └── spc700_upload_driver_088006 (uploads SPC700 driver from $08:0A00)
  └── enable_nmi_896D → NMITIMEN = $81 (NMI + joypad auto-read)
```

### NMI (runs every frame; the entire game)
```
nmi ($00:803E)
  1. RDNMI ack
  2. OAM DMA: $00:0D00 → $2104 (0x220 bytes)
  3. vram_stream_step_814F      (uploads 1/8 of a streaming tileset)
  4. per_frame_even_8553        (even frames) OR per_frame_odd_85B2 (odd)
  5. vram_queue_flush_C804      (drains the queue at $0C00)
  6. oam_index_reset_8937
  7. bg_scroll_push_884A        (BG1/2/3 H+V from dp[$46-$51])
  8. shadow_oam_clear_88A5      (parks all 128 sprites, resets allocators)
  9. JSL entity_step_all        (walks entities at $04:0600 via 32-entry
                                 table at $04:9A30)
  10. pause_toggle_on_start_8101 (START button)
  11. wall clock tick at dp[$00..$04]
  12. scheduler_switch_and_rti  (SP-swap RTI to next ready task)
```

### Game-state machine
`dp[$0B]` indexes a **68-entry** table at `$00:9369`. Each handler runs
ONCE, sets up its screen, and `INC dp[$0B]` to advance.
States $00-$09 are menu/transition; $0A-$2F are gameplay (views,
evaluation screens, dialogs); $30-$3E are Evaluation / Encyclopedia
sub-states (lifted in round **F4** into `states_late.c`); $3F-$43 are
intro / credits / winter-ending.
**68 / 68 (100 %) lifted.** Full table in `V4_8_DISPATCH_TABLES.md`.

Note: states **$24/$25 are the Behavior Control Panel** (Forage / Dig /
Nurse triangle) and **$26/$27 are the Caste Control Panel** (Worker /
Soldier / Breeder). Earlier "nest close-up" labelling for these slots
was refuted in V4-8.

### Entity system
- 20-byte records at `$04:0600` (LoROM mirror of WRAM `$7E:0600`)
- `entity.type` (byte +0) dispatches via **118-entry table** at `$04:9A30`
  (types $00-$1F cover ant / cursor / popup / egg / HUD handlers;
  $20-$2B are HUD widgets and Auto/Manual / digit panel icons lifted
  in G2; $2C-$5F are dispatchers lifted in G3; $60-$71 are dispatchers
  lifted in G4; $3D bicycle / $4B hand / cat-paw danger bodies remain
  among the ~8 still-stubbed entries)
- `entity.state` (byte +1) usually indexes a per-type state machine
- Per-type init constants from `$01:EF59` (word) and `$01:F043` (byte)
- Spawn helper at `$04:99C1`: `entity_spawn(X=pos_x, Y=pos_y, A=type)`
- **~110 / 118 (~93 %) entity handlers have bodies.** Full table in
  `V4_8_DISPATCH_TABLES.md`; mapping in `ENTITIES.md`.

### Two parallel entity systems

There is a **visual** entity pool at `$7E:0600` (the table above) and an
**abstract per-colony** parallel-array system at `$7F:C000` (B X),
`$7F:CBB8` (B type), `$7F:D388` (R X), `$7F:D964` (R type), `$7F:DD4C`
(Danger type), `$7F:E328` (Danger X), with counts at `$7E:E77E/E780/E782`.
The two systems run independently and sync only when a fight is queued
into the combatant pool at `$7F:E87E` (max 5 entries) or at a kill
event through `kill_dispatcher_D334`.

### Yellow Ant — composite, not a single type

The player-controlled "Yellow Ant" is a composite across cursor types
1/2, a Worker (14) or Queen (18) body, the walker record at
`$7E:E8BE..E8C6`, and the popup-gating flag at `dp[$02A7]`. See the
state diagram in `V4_5_DIAGRAMS.md` and the Cast-of-Characters row in
`COVERAGE.md`.

### Hardware
- **PPU init**: register triples at `$01:98A3` interpreted by `$00:BC7F`
- **VRAM**: queue at `$00:0C00` flushed in NMI; streamer at `$00:814F`
  pushes 1/8 of a tileset per frame
- **OAM**: shadow at `$00:0D00`, DMA'd every NMI; cleared each frame so
  draw code can rebuild
- **APU/Sound**: music commands via `$00:8E88` (writes `dp[$0037]` to
  APUIO0 if `dp[$0033]` enable flag is set); SFX commands via
  `$00:8EA3` (writes to APUIO3 with alternation bit if `dp[$0036]` set)
- **Joypad/Mouse**: full BIOS in `mouse.c`. JOY1 auto-read shadowed at
  `dp[$0160-$0161]`; mouse adds delta accumulators at `dp[$007B-$007F]`
- **SRAM**: 32 KB at `$70:0000`; signature at `$70:7FA0-$7FA2`; bulk
  save serialized at `$03:FA74`

## Coverage status

After V2 (lifts) + V3 (verification) + V4 (audit), the decomp covers:

| Area | Status | Notes |
|---|---|---|
| Boot + NMI + scheduler | Full | Cooperative SP-swap scheduler, NMI vector `$00:803E` |
| Game-state dispatch | 68 / 68 lifted (100 %) | All states lifted; $30-$3E in `states_late.c` (round F4). `V4_8_DISPATCH_TABLES.md` |
| Entity handler dispatch | ~110 / 118 (~93 %) | Original 32 + ~78 from F1/F2/F3/G2/G3/G4/H1. Remaining ~8 stubs include $3D bicycle, $4B hand/cat-paw, a few HUD widgets. `ENTITIES.md` |
| **Scent system** | Full | 4 maps at `$7F:4000-$7F:5FFF` — place (MAX), nest decay (linear), trail decay (`>>1`), 8-direction gradient follow, Rainy-Yard wash. Verified in `scent.c` + `tests.c` |
| **Simulation tick** | Full | `sim_tick` body at `$02:AB58`, 8.5 Hz, master counter `$E788` |
| **49-area world state** | Full | `$7E:EA46/EAC6`, Marriage Flight + Mass Exodus triggers |
| **Behavior / Caste Control Panels** | Full | Triangle barycentric, percentages `$0286-$0294`. States $24/$25 = Behavior, $26/$27 = Caste |
| **8 Scenario configs** | Full | `$01:81F3`, 78 B each, with danger entity map |
| **Player actions** | Full | Recruit / Queen menus, carry-state, hunger, eat, trophallaxis |
| Mouse + Joypad BIOS | Full | `$00:E3FD` |
| Save / Load | Full | Sigs "DOBBY" / "DURRY" at `$03:F97E/F983`; serializer at `$03:FA74`; LZSS body |
| LZSS decompressor + SPC700 driver upload | Full | 3,327-byte driver at file 0x5F004 |
| SPC700 driver body | Lifted with gaps | 1489 LOC; sequence opcodes 5..14 unhandled |
| Icon menus + Evaluation Screens | Full | House, History, Status all in `ui_menus.c` |
| Encyclopedia + Tutorial text | Full | 30 pages / 54 messages in `text_content.c` |

See `AUDIT_SUMMARY.md` for the consolidated V2+V3+V4 verification
report, `COVERAGE.md` for the full mechanic-by-mechanic matrix, and
`ENTITIES.md` for the 118-entity-type role map. Post-V4 fix rounds
documented in `F5_SINCOS_FIX.md`, `F6_WIRING_FIX.md`,
`G2_ENTITIES_E_BODIES.md`, `G4_ENTITIES_G_BODIES.md`,
`H1_DIALOG_RENDERERS.md`, `H2_VERIFY_G_FIXES.md`,
`H3_RESIDUAL_FIXES.md`, `H4_RECONSTRUCTIONS.md`, and
`FINAL_CLEANUP.md`. Post-Z audit round in `A1_POST_FIX_SPOTCHECK.md`,
`A2_DOC_CONSISTENCY.md`, `A3_PHANTOM_EDITS.md`, `A4_WARNING_SWEEP.md`,
`A5_DEAD_CODE.md`, and this doc-refresh report `Z1_DOC_REFRESH.md`.

---

_Last updated post-Z1 (audit round, 2026-05-22). Reflects F/G/H +
A1-A5 results: 68/68 states, ~110/118 entity handlers, 51 .c files,
~68.5 K lines._

## Verification

- **RNG** — bit-perfect against ROM disassembly: 42,000 (output,
  dp[$2A], dp[$2B]) tuples + 50,000-sample drift test. See
  `RNG_TEST_RESULTS.md`.
- **Assets** — 515,072 B across 158 blobs (43 LZSS state-handler +
  26 per-view tile + 10 CGRAM palette + dispatch / palette /
  asset_data_index tables) — zero mismatches. See
  `ASSET_VERIFY_RESULTS.md`.
- **Behavior** — 21 / 22 invariant tests pass; the single failure is
  a documented stub-shadowing of `history_graph_snapshot_F927`. See
  `TEST_RESULTS.md`.
- **Dispatch tables** — re-decoded from `simant.sfc` in V4-8. See
  `V4_8_DISPATCH_TABLES.md`.

## Decomp note

This is a static reverse-engineering artifact, not a runnable port. The
SNES-specific stuff (PPU/OAM/DMA, SPC700, mode 7) is faithfully
documented but isn't usable as-is on another platform. The
**simulation mechanics** (entity AI, scent gradient, colony state,
player actions) are the portable part — they can be hooked to a
different rendering / input / audio backend on Flipper Zero, terminal,
etc.

# V4-6: Per-File Flipper Zero Port Checklist

**Target:** Flipper Zero (256 KB RAM, 128×64 mono LCD, 5 buttons, ARM Cortex-M4F, no FPU heavy use, FAP heap ~16-32 KB usable, total RAM budget for app ~50-100 KB realistic).

**Project root:** `/Users/guilhermedavid/simant-re/` — 44 lifted `.c` files (~3.6 MB of asset data plus ~1.1 MB of code).

**Categories used:**
- **KEEP AS-IS** — pure logic, no SNES deps. Compile straight.
- **KEEP WITH ADAPTATION** — logic portable, swap API calls (VRAM/OAM → `canvas_*`, joypad → `input_*`, vblank → `furi_delay_*`).
- **REPLACE ENTIRELY** — SNES hardware glue (PPU, OAM, DMA, SPC700, mode 7, NMI scheduler). Write a Flipper-native equivalent.
- **EMBED AS DATA** — pure data (graphics, text). Embed, but trim and re-encode for 1-bit display + flash.
- **DROP** — test/scaffold/stubs not needed in the runtime port.

---

## Per-File Action Table

| # | File | Size | Category | Effort | Guidance | RAM (KB) |
|---|------|-----:|----------|-------:|----------|---------:|
| 1 | `simulation.c` | 57 K | KEEP WITH ADAPTATION | 1.5 d | Core 8.5 Hz tick (food, fights, history, mating-flight, 49-area). Replace `wram[]` absolute addresses with a typed `Colony` struct. Drive from FuriHalRtc-based 7-frame divider off a 60 Hz timer. No MMIO touches. | 2.0 (colony + history buffer) |
| 2 | `scent.c` | 30 K | KEEP WITH ADAPTATION | 1 d | 4 maps × 2 KB = 8 KB. **Half-resolution to 32×16** to halve to 2 KB if RAM gets tight. Replace `wram[$7F:4000+]` with `uint8_t scent[4][2048]`. Gradient-follow + decay are pure math. | 8.0 (or 2.0 trimmed) |
| 3 | `combat.c` | 68 K | KEEP WITH ADAPTATION | 2 d | Parallel-array combat (B vs R populations, eat/starve/fight). Replace `$7F:Cxxx`/`$7F:Exxx` arrays with `struct AntPool { uint8_t type[N], attr[N], x[N], …; uint16_t count; }`. Cap N at ~64/colony to fit Flipper. | 1.5 (caps tightened) |
| 4 | `territory.c` | 46 K | KEEP AS-IS | 4 h | 49-area map + status %. Pure math. Reads ~256 B of state. Naturally renders as 7×7 mono blocks. | 0.5 |
| 5 | `scenarios.c` | 38 K | KEEP AS-IS | 4 h | 8 scenario configs + danger-entity dispatch. Trim asset pointers; keep config arrays. | 0.2 |
| 6 | `control_panels.c` | 42 K | KEEP WITH ADAPTATION | 1 d | Behavior + Caste triangle barycentric math (100×87). Pure geometry, but re-fit the triangle to 100×64-ish pixels for Flipper. Replace tile-text emit with `canvas_draw_str`. | 0.1 (8 % vars at `$0286-$0294`) |
| 7 | `player_actions.c` | 57 K | KEEP WITH ADAPTATION | 1.5 d | Recruit/Queen menus, carry-state, hunger, eat, trophallaxis. Replace cursor-A/B detection (`sub_DC84`) with Flipper InputKey events. Keep dp[$02B7] dispatch slot table verbatim. | 0.1 |
| 8 | `player_actions_full.c` | 33 K | KEEP AS-IS | 4 h | ROM-verified bodies for the dp[$02B7] dispatch (Lay Eggs, Recruit N, Release N, Dig New Nest). Pure. | 0.0 |
| 9 | `entities_a.c` | 28 K | REPLACE ENTIRELY | 4 d | Types 1-7 are **shadow-OAM emitters** for UI/cursor/HUD blink. They translate game state to sprite writes at `$0D00`. On Flipper, rewrite as `canvas_draw_*` calls reading the same dp[] state. Pattern: keep the type-handler dispatch shape; gut the OAM body. | 0.0 |
| 10 | `entities_b.c` | 36 K | KEEP WITH ADAPTATION | 1.5 d | Workers (type 14), Soldiers (type 15) — walk AI is pure math (velocity + sin/cos heading lookup). Strip the OAM emit at end of each handler; emit a render-command instead. | 0.0 |
| 11 | `entities_c.c` | 34 K | KEEP WITH ADAPTATION | 1 d | Queen (18/19) wander FSM, type 20 (Dig-New-Nest excavator). Same pattern as `entities_b`. | 0.0 |
| 12 | `entities_d.c` | 50 K | KEEP WITH ADAPTATION | 1.5 d | Egg (24), Dialog/Popup (29). Type 29 popup machine drives every menu — keep its FSM, point its text-draw at `canvas_draw_str`. | 0.0 |
| 13 | `mouse.c` | 10 K | REPLACE ENTIRELY | 4 h | SHVC Mouse BIOS — Flipper has no mouse. **Keep only** the `(current, prev) → just-pressed` edge-detect at the bottom; wire to `input_event`. | 0.0 |
| 14 | `simant.c` | 50 K | REPLACE ENTIRELY | 4 d | Boot path, NMI, cooperative SP-swap scheduler, OAM DMA, VRAM stream, BG scroll push. Replace with a single FAP `furi_thread_loop` ticking at 60 Hz that calls `dispatch_state(); update_entities(); maybe_sim_tick(); render();`. The Entity table (20 B × ~40 entries = 800 B) stays. | 1.0 (entity table + dp scratch) |
| 15 | `vsync.c` | 9 K | REPLACE ENTIRELY | 2 h | The `for(;;)` vblank loop is SNES-only. Replace with a `furi_message_queue` blocking on input + tick. | 0.0 |
| 16 | `states_gameplay.c` | 72 K | KEEP WITH ADAPTATION | 3 d | 38 game-state handlers ($0A-$2F). Trim the asset-loader prologues (they were VRAM DMA setup). Keep state transitions and per-state logic. Largest single file — biggest port effort. | 0.5 |
| 17 | `states_menu.c` | 29 K | KEEP WITH ADAPTATION | 1.5 d | 10 menu/transition state handlers. Each is "build screen + INC dp[$0B]". Replace the screen-build (palette/tilemap/OAM/audio) with a Flipper view. Keep the dp[$0B] dispatch. | 0.0 |
| 18 | `ui_menus.c` | 44 K | KEEP WITH ADAPTATION | 2 d | In-game icon menu (6 icons) + 3 Evaluation dashboards (House/History/Status). Replace tile-text + sprite icons with `canvas_draw_icon_animation`. Keep menu state machine. | 0.1 |
| 19 | `text_screens.c` | 35 K | KEEP WITH ADAPTATION | 1 d | Ant Encyclopedia + Tutorial readers. Replace per-page tilemap render with line-by-line text on 128×64. Page through with Up/Down. | 0.05 |
| 20 | `save_options.c` | 47 K | KEEP WITH ADAPTATION | 1 d | Save/Load + Sound/Speed/Erase. Replace SRAM bulk save with `storage_file_*` writing to `/ext/apps_data/simant/save.bin`. Drop the SNES LZSS — Flipper has flash to spare; just write raw. | 0.05 |
| 21 | `audio_intro.c` | 32 K | DROP (use trimmed table only) | 2 h | Documents SPC700 upload + the IPC command table. **Keep only** the music/SFX command-code constants and trigger them as Flipper PWM beeps via `furi_hal_speaker_*`. | 0.0 |
| 22 | `audio_driver.c` | 64 K | DROP | 0 | Full SPC700 driver lift. Useless on Flipper — no DSP. Don't port. | 0.0 |
| 23 | `assets.c` | 25 K | DROP (replaced by Flipper asset bundle) | — | SNES asset index (53 blob refs, LZSS pointers). Replace with a manifest pointing at `.frx`/`.icon` files in the FAP. | 0.0 |
| 24 | `asset_data_1.c` | 183 K | EMBED AS DATA (trimmed) | 1 d | 14 bank-$07 LZSS palette/tilemap chunks (28 KB uncompressed). Re-decode tiles offline, dither to 1-bit, pack as Flipper icons. Trim to ~10 % of original. | 1-2 KB flash (not RAM) |
| 25 | `asset_data_2.c` | 550 K | EMBED AS DATA (trimmed) | 2 d | Sprite/BG CHR (87 KB). Dither sprites to 1-bit 8×8, keep only Worker / Soldier / Queen / Yellow Ant / food / spider. ~30 sprites total. | 1 KB flash |
| 26 | `asset_data_3.c` | 515 K | EMBED AS DATA (trimmed) | 1 d | Scent/landing/save-game tiles (82 KB). Keep just the 49-area icons + scent overlay glyph. Most can drop. | 0.3 KB flash |
| 27 | `asset_data_4.c` | 635 K | DROP | 0 | Credits/mode-7/text tile chunks (101 KB). Skip credits; use Flipper system font for text. | 0 |
| 28 | `asset_data_5.c` | 1.3 M | EMBED AS DATA (heavily trimmed) | 2 d | Per-view scenario tile chunks (213 KB). Pick ONE scenario (Park) for MVP; one 128×64 backdrop per view (Surface / B.Nest / R.Nest) = 3 × 1 KB. | 3 KB flash |
| 29 | `asset_data_6.c` | 31 K | DROP | 0 | Raw CGRAM palettes (2880 B). Flipper is 1-bit; no palettes. | 0 |
| 30 | `text_content.c` | 41 K | EMBED AS DATA | 4 h | Encyclopedia (8963 B / 30 pages) + Tutorial (5369 B / 54 msgs). Decode the `$FF/$FE/$2C/$2E` grammar to plain UTF-8 strings; embed in flash. | 14 KB flash |
| 31 | `render_helpers.c` | 47 K | REPLACE ENTIRELY | 3 d | History-Graph + Population-Graph renderers, per-view OAM init, palette stamps. Re-write tiny Flipper versions: history graph as 128-px-wide line on canvas; pop graph as stacked bars. | 0.1 |
| 32 | `misc_helpers.c` | 15 K | KEEP WITH ADAPTATION | 6 h | DPAD math (`scroll_surface_view_A106`), fade-in/out, APU dispatch. Keep scroll math; replace fade with `canvas_set_color` toggle; replace APU calls with PWM trigger. | 0.0 |
| 33 | `lifted_helpers_1.c` | 15 K | KEEP WITH ADAPTATION | 4 h | RNG-adjacent atomic helpers (sin/cos table, multiply/divide, joypad poll). Keep math; drop CGRAM/VRAM uploaders. | 0.0 |
| 34 | `lifted_helpers_2.c` | 17 K | KEEP WITH ADAPTATION | 6 h | Entity-physics helpers (DCD5 PRNG, DC84 click gate, D721 heading→velocity, D747 integrate). Pure math — keep. Replace DB9E OAM-push with render-command emit. | 0.0 |
| 35 | `lifted_helpers_3.c` | 19 K | KEEP AS-IS | 3 h | Barycentric triangle math, cursor XY validation, hex-digit pushers. Pure. | 0.0 |
| 36 | `lifted_helpers_4.c` | 20 K | KEEP WITH ADAPTATION | 6 h | Mostly micro JSL-thunks + dp[$88] task-yield. Drop scheduler yields (no coroutines on Flipper); keep BG3 tile-buffer mask emit only if you keep tile rendering. Mostly delete. | 0.0 |
| 37 | `lifted_helpers_5.c` | 14 K | REPLACE ENTIRELY | 1 d | Cursor-move handlers tied to SNES heading-byte at dp[$0B]. Rewrite as Flipper Up/Down/Left/Right cursor mover. | 0.0 |
| 38 | `lifted_helpers_6.c` | 28 K | REPLACE ENTIRELY | 1 d | Fade-to-black, VRAM 8KB DMA, joypad-wait debounce, vertical-wipe transitions. Replace with no-ops or simple `canvas_clear` + 8-frame mono fade. | 0.0 |
| 39 | `gaps.c` | 26 K | KEEP AS-IS | 4 h | RNG ($04:DCD5 LCG), Yellow-Ant walker ($7E:E8BE 20-byte record), 49-area map init, extended dispatch ($3D bicycle, $4B cat's paw). Critical and pure. | 0.05 |
| 40 | `gap_fillers.c` | 45 K | KEEP WITH ADAPTATION | 6 h | Save checksum, RNG seed, text-tile renderer C91F, palette upload, gameplay states $11-$15. Keep checksum + RNG seed + state logic; drop palette upload + text-tile-as-VRAM. | 0.0 |
| 41 | `stubs.c` | 1.2 K | DROP | 0 | Weak-link glue for SNES helpers; not needed once real bodies replace them. | 0 |
| 42 | `stubs_for_test.c` | 0.3 K | DROP | 0 | Test runner glue. | 0 |
| 43 | `stubs_test_extras.c` | 0.2 K | DROP | 0 | Test runner glue. | 0 |
| 44 | `tests.c` | 44 K | DROP (port test logic later) | — | Useful host-side test harness. Don't ship in the FAP; keep in repo to validate the port matches SNES behaviour. | 0 |

---

## Tally

| Category | # Files | Notes |
|---|---:|---|
| KEEP AS-IS | 5 | gaps, scenarios, territory, lifted_helpers_3, player_actions_full |
| KEEP WITH ADAPTATION | 19 | Most of simulation + entities + UI |
| REPLACE ENTIRELY | 7 | All SNES hardware glue |
| EMBED AS DATA (trimmed) | 4 | asset_data_1/2/3/5 + text_content |
| DROP | 9 | audio driver, mode-7 assets, palettes, stubs, tests, asset index |

**Total effort estimate: ~38-45 dev-days** (~6-8 weeks for one engineer at steady pace, assuming familiarity with the SNES decomp).

---

## Critical-Path Ordering (port in this order)

Each step yields something runnable on the Flipper.

### Phase 1 — Scaffolding (1 week)
1. **`stubs.c` replacement** — Flipper FAP skeleton with `furi_thread_loop`, `canvas`, `input_event` queue. Define `Colony`, `Entity`, `AntPool` structs; allocate static state (~15 KB). (1 d)
2. **`simant.c` REPLACE** — main loop + state dispatch table + entity dispatch. Drives everything. (2 d)
3. **`gaps.c` KEEP** — RNG + Yellow-Ant walker. Needed by every AI handler. (4 h)
4. **`lifted_helpers_1.c` + `lifted_helpers_2.c`** — sin/cos, PRNG, physics integration. (1 d)

### Phase 2 — Simulation (1.5 weeks)
5. **`simulation.c`** — the 8.5 Hz tick. Game is now "alive" with no display. (1.5 d)
6. **`scent.c`** — 4 maps (or 2 trimmed). Drives ant motion. (1 d)
7. **`territory.c`** + **`scenarios.c`** — area + scenario config. (1 d)
8. **`combat.c`** — fight/eat/starve. (2 d)

### Phase 3 — Entities (1 week)
9. **`entities_b.c`** — Worker/Soldier AI. First visible ants. (1.5 d)
10. **`entities_c.c`** — Queen + Dig-New-Nest. (1 d)
11. **`entities_d.c`** — Egg + Popup machine (drives every dialog). (1.5 d)
12. **`entities_a.c` REPLACE** — UI/cursor/HUD as `canvas_draw_*`. (4 d, can parallel)

### Phase 4 — Player + UI (1.5 weeks)
13. **`mouse.c` REPLACE** — keep just edge-detect, wire to Flipper input. (4 h)
14. **`player_actions.c`** + **`player_actions_full.c`** — Recruit/Queen menus, dp[$02B7] dispatch. (2 d)
15. **`control_panels.c`** — Behavior + Caste triangles. (1 d)
16. **`ui_menus.c`** — 6-icon menu + 3 Evaluation screens. (2 d)
17. **`states_gameplay.c`** + **`states_menu.c`** — the 48 state handlers. Trim asset loaders. (4.5 d)

### Phase 5 — Polish (1.5 weeks)
18. **`render_helpers.c` REPLACE** — graphs. (3 d)
19. **`save_options.c`** — Flipper SD storage save/load. (1 d)
20. **`text_screens.c` + `text_content.c`** — Encyclopedia + Tutorial. (1.5 d)
21. **`asset_data_*.c` EMBED** — trim, dither, pack as Flipper icons. (~6 d, can parallel)
22. **`vsync.c` + `misc_helpers.c` + `lifted_helpers_4/5/6`** — fade, scroll, cleanup. (3 d)
23. **`audio_intro.c`** — pick 5 SFX, wire to PWM. (2 h)

### Top-10 critical-path files (must port first to get anything running)
1. `simant.c` (replace) — main loop
2. `stubs.c` (replace) — Flipper scaffold + structs
3. `gaps.c` (keep) — RNG + Yellow Ant
4. `simulation.c` (adapt) — 8.5 Hz tick
5. `lifted_helpers_1.c` (adapt) — math primitives
6. `lifted_helpers_2.c` (adapt) — entity physics
7. `entities_b.c` (adapt) — Worker/Soldier AI (first visible ants)
8. `scent.c` (adapt) — drives motion
9. `mouse.c` (replace) — input
10. `entities_a.c` (replace) — cursor + HUD render

---

## RAM Budget Breakdown

The PORTING.md estimate is **~15 KB simulation state**. With Flipper-specific trims:

| Region | Source | Size | Notes |
|---|---|---:|---|
| Direct page scratch (frame counters, scroll, joypad shadow, dp[$0000-00FF]) | `simant.c` / dp | 0.25 KB | Pack into a `DPScratch` struct |
| Entity table (20 B × 40 entries) | `simant.c` Entity | 0.80 KB | Cap at 40; SNES allows more but Flipper screen is tiny |
| Yellow-Ant walker record | `gaps.c` $7E:E8BE | 0.02 KB | 20 B standalone |
| Shadow OAM (96 sprites × 4 + 32 hi-attr) | render layer | — | **DROPPED** — Flipper draws directly to canvas |
| Colony summary block ($7E:E700-E7FF) | `simulation.c` | 0.25 KB | Status Screen data |
| 49-area pop map ($7E:EA46-EAC6, 8×8 × 2 B) | `territory.c` | 0.13 KB | B + R counts |
| History-graph buffer (64 × 8 channels × 2 B) | `simulation.c` | 1.00 KB | Circular |
| Parallel-array ant pools (B + R, capped 64 each, 6 fields × 1 B) | `combat.c` | 0.80 KB | Down from SNES's ~2 KB |
| **4 scent maps (64×32 cells × 4 maps × 1 B)** | `scent.c` | 8.00 KB | OR 2.0 KB if half-res |
| SRAM persistent shadow ($7F:E710-E720) | `save_options.c` | 0.02 KB | 16 B |
| Render command queue (sprites → canvas) | new | 1.00 KB | Bounded |
| Flipper canvas backbuffer | system | 1.00 KB | 128×64 / 8 = 1024 B (managed by FuriHal) |
| Stack + Flipper internals | system | ~4.00 KB | Out of our budget |
| **Subtotal (full scent)** | | **13.3 KB** | Fits with room |
| **Subtotal (half-res scent)** | | **7.3 KB** | Roomy |

**Verdict:** Comfortably within the 15-20 KB target. Full-res scent fits with margin; if other Flipper services chew through heap, half-res scent gets you back to ~7 KB.

Flash (code + trimmed assets) target: ~80-120 KB for the FAP binary — well within Flipper's per-FAP limit.

---

## Minimum Viable Port (smallest working Flipper port)

**Goal:** one scenario (Park), one view (Surface close-up), Yellow Ant walks, places trail scent, finds food, can recruit Workers. No Encyclopedia, no Evaluation screens, no Queen, no save, no audio, no R-colony.

### MVP file subset (16 files)

| File | Action |
|---|---|
| `stubs.c` | Replace with Flipper FAP main + structs (1 d) |
| `simant.c` | Replace with 60 Hz loop + state dispatch (2 d) |
| `gaps.c` | RNG + Yellow Ant walker — keep (4 h) |
| `lifted_helpers_1.c` | sin/cos + math — keep trimmed (3 h) |
| `lifted_helpers_2.c` | PRNG + entity physics — keep trimmed (4 h) |
| `simulation.c` | Strip to: tick counter + B-colony food/health/starvation only (1 d) |
| `scent.c` | Trim to 2 maps (B-Nest + B-Trail), half-res 32×16 — 1 KB total (4 h) |
| `combat.c` | Strip to feed + starve only; no fights, no Yellow predation (4 h) |
| `entities_b.c` | Worker (type 14) only — drop Soldier (1 d) |
| `entities_a.c` | Replace — draw Yellow Ant cursor + simple HUD (food count, health %) (1 d) |
| `mouse.c` | Replace with 5-button edge detect (3 h) |
| `player_actions.c` | Strip to: A=dig/pickup, B=eat, recruit menu only (1 d) |
| `player_actions_full.c` | Keep Recruit + Lay Eggs (the simplest dp[$02B7] slots) (3 h) |
| `misc_helpers.c` | Keep DPAD/scroll math only (3 h) |
| `asset_data_2.c` (trimmed) | Workers + Yellow Ant + 1 food sprite, 1-bit dithered (4 h) |
| Render glue (new) | `canvas_draw_*` calls for ant sprites + scent dots + HUD (1 d) |

**MVP effort: ~11-12 dev-days** (~2.5 weeks).
**MVP RAM: ~4 KB** state (mostly the trimmed scent + entity table).
**MVP flash: ~30-40 KB** FAP binary.

This MVP demonstrates the SNES sim logic running unmodified on Flipper — the proof-of-life. Everything from "MVP runs" to "full port" is incremental layer-by-layer addition of the remaining 28 files.

---

## Risks / Gotchas

- **Flipper FAP heap is small (~16 KB on older firmwares, larger on newer).** The 8 KB scent + 1 KB entity table eats most of it. Use `furi_alloc` once at app start; static allocation in BSS is friendlier than heap. Verify your FAP heap headroom early.
- **No FPU usage in entity physics.** Already integer math — good.
- **The cooperative SP-swap scheduler in `simant.c` is the biggest replacement.** Don't try to emulate it; a single ordered call sequence (state → entities → sim_tick → render) works because Flipper isn't competing for cycles with NMI.
- **Asset trimming is the largest single time sink.** 3.6 MB of `asset_data_*.c` → ~5 KB of Flipper icons is a manual curation job. Write `gen_flipper_assets.py` to dither + pack once.
- **`render_helpers.c` History Graph** is one of the most user-visible features; budget extra polish time.
- **Tutorial text** is the only narrative content; it's worth porting properly.

# Porting SimAnt to other platforms

This decomp is structured so that the **simulation mechanics** (the
portable bits) are cleanly separated from the **SNES-specific code**
(PPU, OAM, DMA, SPC700, mode 7) that doesn't map to other platforms.

Companion docs:
- `V4_6_FLIPPER_PORT.md` — per-file Flipper Zero checklist + MVP scope
- `V4_5_DIAGRAMS.md` — boot, NMI, dispatcher, entity, scent, combat,
  sim_tick, save, Yellow Ant FSM, audio (10 Mermaid diagrams)
- `AUDIT_SUMMARY.md` — verification status of every subsystem
- `COVERAGE.md` — manual-mechanic-to-decomp gap matrix

## What you keep when porting

These are the modules that contain real simulation logic (verified
against the ROM where possible — see `AUDIT_SUMMARY.md`).

| File | What it gives you on a new platform |
|---|---|
| `simulation.c` | The 8.5 Hz colony tick: food, fights, eaten, starvation, history-graph buffer, mating-flight + mass-exodus triggers, 49-area world map. **Tested** in `tests.c` (sim counter, wall clock, mass exodus cap, marriage flight gate, colony health grade) |
| `scent.c` | 4 pheromone maps (64×32 grid × 1 byte, 32×32 px cells), place (MAX) / decay (linear nest, `>>1` trail) / 8-direction gradient follow / Rainy-Yard wash. **Fully lifted; 6/6 tests pass** |
| `combat.c` | Fight resolver + kill dispatcher, combatant pool at `$7F:E87E` (max 5). Sync point between the visual entity pool and the abstract per-colony arrays |
| `territory.c` | 49-area `$7E:EA46/EAC6`, neighbour balance, Mass Exodus split. Pure math |
| `scenarios.c` | 8 scenario configs at `$01:81F3` (78 B each) + danger-entity mapping |
| `control_panels.c` | Behavior + Caste triangle barycentric math (100×87 equilateral); percentage storage at `dp[$0286-$0294]`. States $24/$25 = Behavior, $26/$27 = Caste (V4-8 confirmed) |
| `player_actions.c` + `player_actions_full.c` | Recruit / Queen menus, carry-state, hunger, eat, trophallaxis. `_full` supersedes the older `_pseudo` variants — wire the dp[$02B7] dispatch to `_full` |
| `entities_b.c` | Worker (type 14) and Soldier (type 15) walking-ant AI |
| `entities_c.c` | Queen (type 18/19 — also reused as Snail $13) wander FSM, type 20 (Dig-New-Nest excavator) |
| `entities_d.c` | Type 24 (Egg), type 29 (10-state Dialog/Popup machine — drives every menu) |
| `mouse.c` | SNES Mouse BIOS — keep only the `(current, prev) → just-pressed` edge-detect at the bottom |
| `gaps.c` | RNG `$04:DCD5` (LCG + tweaked LFSR) — **bit-perfect against ROM** (50K-sample verification, see `RNG_TEST_RESULTS.md`). Yellow Ant walker record at `$7E:E8BE` |
| `misc_helpers.c` | DPAD / L+R-scroll math (`scroll_surface_view_A106`), fade-in/out, APU command dispatchers |
| `lifted_helpers_1..3.c` | Atomic math: sin/cos, multiply/divide, joypad edge, entity physics, barycentric helpers. Pure |

## What you replace

| File / section | Replace with |
|---|---|
| `simant.c` NMI helpers (OAM DMA, VRAM stream, BG scroll push) | Your platform's render pipeline |
| `simant.c` cooperative SP-swap scheduler | Coroutine library, OS thread, or just a single 60Hz loop calling each "task" in turn |
| `states_menu.c` / `states_gameplay.c` per-screen asset loaders | Direct tile/sprite-data loads — the LZ77 decompressor (`lz_decompress_03_8467`) is portable if your assets are in the same format |
| `mouse.c` serial-protocol reads | Whatever your platform's input device gives you (the upstream BIOS shape is just `(current, prev) -> just-pressed`) |
| `stubs.c` | Most stubs are SNES-specific; you'll want to define real bodies for the ones the simulation tick calls (`ant_motion_update_9A86`, `area_event_tick_ACF9`, etc.) |

## Critical WRAM regions to recreate

These are the chunks of WRAM the simulation actually needs:

| Region | Size | Purpose |
|---|---|---|
| `$00:0000-$00:00FF` | 256 B | Direct page — frame counters, joypad shadows, scroll, working scratch |
| `$7E:0600-…` | ~800 B | Visual entity table (20 bytes/entity, up to ~40 entities) |
| `$7E:E700-$7E:E7FF` | 256 B | Colony summary block (Status Screen data) |
| `$7E:E8BE-$7E:E8C6` | 20 B | Yellow Ant walker record (player state, separate from entity table) |
| `$7E:E87E-…` | ~80 B | Combatant pool (5 entries × interleaved 16-bit fields) |
| `$7E:EA46-$7E:EAC6` | 128 B | 49-area pop map (B + R, 8×8 grid, 2 bytes each) |
| `$7E:F6D7-$7E:FBD7` | ~1280 B | History-graph 64-entry × 8-channel circular buffer |
| `$7F:4000-$7F:5FFF` | 8 KB | 4 scent maps (Black Nest, Red Nest, Black Trail, Red Trail) |
| `$7F:C000-$7F:E328` | ~9 KB | Abstract per-colony parallel arrays (B X/type, R X/type, Danger X/type) |
| `$7F:E710-$7F:E720` | 16 B | Persistent shadow (saved to SRAM at `$70:0000+`) |

**Two parallel entity systems**: the visual pool at `$7E:0600` (walked
every NMI) and the abstract per-colony arrays at `$7F:C000/D388/DD4C`
(walked by `sim_tick`). They sync only at fight ingest into the
combatant pool and at kill events through `kill_dispatcher_D334`. See
`V4_5_DIAGRAMS.md` §6 for the diagram. On a port you must implement
both — gameplay (entity AI, combat) reads/writes both stores.

Total simulation RAM: **~20 KB** with both stores, or **~15 KB** if you
collapse to a single store and skip per-colony aggregation. Fits
comfortably in a Flipper Zero's 256 KB RAM.

## Per-frame loop on a new platform

The minimum simulation loop:
```c
void simant_frame(void) {
    /* 1. read input (replace SNES joypad + mouse BIOS) */
    poll_input();

    /* 2. run the game-state dispatcher */
    game_state_dispatch();

    /* 3. update every entity in the table */
    for (Entity *e = ENTITY_TABLE; e < ENTITY_TABLE + entity_count; ++e)
        if (e->type) entity_handlers[e->type](e);

    /* 4. run the simulation tick (every 7th frame to match 8.5 Hz) */
    if ((++tick_div) >= 7) {
        tick_div = 0;
        sim_tick();
    }

    /* 5. render — your platform's renderer reads:
     *      - shadow OAM (sprite list) at wram[0x0D00]
     *      - BG tilemap from PPU VRAM (or your equivalent)
     *      - scent maps if scent display is on
     */
    render();
}
```

## Flipper Zero specifics

See `V4_6_FLIPPER_PORT.md` for the per-file checklist and an MVP scope
that's ~11-12 dev-days. Highlights:

- **Display**: 128×64 monochrome. The SNES is 256×224 color. Scale +
  dither — or design a Flipper-specific minimal UI showing the 49-area
  map as 7×7 monochrome blocks and a per-area zoomed view.
- **Buttons**: 5 buttons (Up/Down/Left/Right/OK + Back). Map A→OK,
  B→Back, plus a held-modifier for the L/R scroll variant.
- **RAM**: 256 KB. The full ~20 KB simulation state fits with margin;
  half-resolution scent (32×16) buys back 6 KB if the FAP heap is
  tight.
- **Persistence**: Flipper has SD storage. The 32 KB SRAM save fits
  trivially.
- **Audio**: tiny speaker. Don't port the SPC700 program (the driver
  is 3,327 B at file 0x5F004, but it needs the SNES DSP); pick a few
  key SFX bytes as PWM beeps. Observed command codes: $C4 view switch,
  $4E collision, $08 menu music, $02 encyclopedia music, $30 pause
  music.
- **Tick rate**: Flipper can easily do 60 Hz. The sim tick at 8.5 Hz =
  every 7 frames is fine.
- **Entity dispatch table**: the full ROM table has 118 entries. For a
  Flipper port you only need ~32 of them (the lifted ones); the
  remaining 86 are HUD widgets and control-panel chrome that you'd
  redraw natively with `canvas_draw_*`.

## Build

```
make           # compile + link to ./simant_decomp (proves it all links)
make check     # per-file compile-check (all should say OK)
make count     # line counts
make clean
```

The resulting binary is a static link artifact — every unresolved ROM
helper is a no-op weak stub. To make it runnable, you'd replace
`stubs.c` with real bodies for the ~200 helpers it currently stubs out.

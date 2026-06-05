# V4-2 TODO Consolidation — SimAnt SNES Decomp

**Date:** 2026-05-21
**Scope:** All `.c` and `.md` files in `/Users/guilhermedavid/simant-re/`
**Grep pattern:** `TODO|FIXME|XXX|pseudo|TBD|not lifted|not implemented|placeholder|superseded`
**Raw hits:** 97 lines across 21 files

---

## Summary Counts by Severity

| Severity | Count | Meaning |
|---|---|---|
| **BLOCKER** | 23 | Game won't run / core mechanic missing |
| **MAJOR** | 19 | Feature visibly missing or wrong (UI, save, audio cmd) |
| **MINOR** | 11 | Cosmetic, edge case, single-byte uncertainty |
| **DOCUMENTATION** | 44 | `sub_XXXX` naming, `pseudo` suffix in identifiers, doc-only markers |
| **Total classified** | 97 | |

Most "hits" are documentation noise: the codebase uses `sub_XXXX`/`*_pseudo` as deliberate naming conventions and the `superseded` markers indicate already-fixed lifts pointing readers to `player_actions_full.c`. The real action items are concentrated in `simant.c`, `vsync.c`, `simulation.c`, `states_gameplay.c`, and `COVERAGE.md`.

---

## Per-Subsystem Breakdown

### 1. Top-level state machine — `simant.c` (16 BLOCKER stubs)
Empty `static void sub_XXXX(void) { /* TODO */ }` bodies that are called by lifted game-state handlers. Every menu transition and view fade currently no-ops these:
- `simant.c:738` `cursor_action_A_9DB9` — A-button click action (MOVE/DIG/SELECT) — BLOCKER
- `simant.c:1116` `sub_8967` — BLOCKER (called from boot)
- `simant.c:1117` `sub_BC7F` — PPU/VRAM defaults — BLOCKER
- `simant.c:1118` `sub_C318` — BLOCKER (used in setup of states $2A, $2C)
- `simant.c:1119` `sub_8D94` — MAJOR
- `simant.c:1120` `sub_E494` — MAJOR
- `simant.c:1121` `sub_BC53` — MAJOR
- `simant.c:1122` `sub_E527` — MAJOR
- `simant.c:1123` `sub_DEEE` — superseded by lifted body in `vsync.c:146` (see below)
- `simant.c:1136` `sub_A3D6` — scroll-not-settled retry — MAJOR
- `simant.c:1137` `sub_A354` — MAJOR
- `simant.c:1138` `switch_view_A3BD` — view-change dispatcher — BLOCKER (SELECT key)
- `simant.c:1139` `after_view_change_8611` — MAJOR
- `simant.c:1141` `sub_BB38` — MAJOR
- `simant.c:1143` `sub_BACA` — screen-template loader called by every gs_* — BLOCKER
- `simant.c:1144` `sub_BAF2` — MAJOR
- `simant.c:1145` `sub_85FC` — BLOCKER (fade-in companion to fade-out)
- `simant.c:1147` `sub_877D` — cooperative task yield — **BLOCKER** (called everywhere)
- `simant.c:1148` `sub_8611` — MAJOR

### 2. VSync / scroll / view — `vsync.c`
- `vsync.c:149` `sub_DEEE` body @ $00:DEF9 — overlay tile-row update — MAJOR
- `vsync.c:176` `sub_DF79` body @ $00:DF86 — scroll animation update — MAJOR

### 3. Simulation tick — `simulation.c`
- `simulation.c:1107` Caste-specific population bin totalizers not lifted — MAJOR (Population Graph won't populate)
- `simulation.c:1130` `$03:FB07` save-state copy stage — MAJOR (save may be incomplete)

### 4. Audio driver — `audio_driver.c`
- `audio_driver.c:57` Song selector / APUIO0 table unknown — MINOR (music selection guess)
- `audio_driver.c:59` Asset re-upload via $FC handshake — MINOR
- `audio_driver.c:1008-1010` Sequence events 2, 3, 4 unknown — MINOR
- `audio_driver.c:1047` Sequence commands 5..14 not handled — MAJOR (music will glitch/silence on most songs)
- `audio_driver.c:1487` Placeholder bodies for declared-but-unlifted helpers — MAJOR

### 5. Player actions — `player_actions.c` / `player_actions_full.c`
The `_pseudo` suffixed handlers are SUPERSEDED by lifted equivalents in `player_actions_full.c`. The dispatch table at `player_actions.c:1236-1247` still routes to pseudo versions — fix is renaming/wiring.
- `player_actions.c:545` `worker_click_handler_pseudo` — MAJOR (still active, real lift partial)
- `player_actions.c:612` `food_click_handler_pseudo` — MAJOR
- `player_actions.c:617` `yellow_index` hardcoded to 0 — MAJOR (wrong slot under most conditions)
- `player_actions.c:674` `recruit_menu_apply_pseudo` — superseded; verify dispatch redirects to `recruit_apply_02A1F4`
- `player_actions.c:816` `queen_menu_apply_pseudo` — superseded by `player_actions_full.c::STAGE 9`

### 6. States — `states_gameplay.c` / `states_menu.c`
- `states_gameplay.c:110` `sub_86E4_etc` placeholder extern — MINOR
- `states_gameplay.c:1433` State $28 thumbnail loader not lifted — MAJOR (save-list UI blank)
- `states_gameplay.c:1460,1463` Carry-flag branches replaced with `if (1)` placeholders — **BLOCKER** (state $29 save run loop is broken — infinite continue)
- `states_gameplay.c:1556` State $2C scent overlay tail not lifted — MAJOR
- `states_menu.c:228` `$87DA` pause-lockout tail not lifted — MINOR

### 7. Entities — `entities_*.c`
- `entities_d.c:885` `*(uint16_t *)&((uint8_t *)0)[...AE06+y placeholder]` — **BLOCKER** (null-pointer deref will crash on first dialog purchase confirm)
- `entities_c.c:590` Dig-new-nest placeholder entity comment — DOCUMENTATION
- `entities_b.c:157` `sub_DCD5` rng comment — DOCUMENTATION

### 8. Scenarios — `scenarios.c`
- `scenarios.c:477` Rain-washes-scent body not lifted (~30 lines) — MAJOR (Scenario 3 broken)

### 9. Lifted helpers — `lifted_helpers_*.c`
- `lifted_helpers_1.c:205` LUT returning 0 placeholder — MAJOR
- `lifted_helpers_6.c:709` `0x200`-byte sizing matches gen_stubs.py placeholders — DOCUMENTATION

### 10. Misc placeholders
- `audio_intro.c:256` Queen dig-new-nest commit @ $00:D754 not lifted — MAJOR
- `audio_intro.c:527` `asset_chain_088003` extern placeholder — MINOR
- `gap_fillers.c:349` `rng_seed_XXXX` function name (delivered) — DOCUMENTATION

### 11. Coverage gaps documented in `COVERAGE.md` (high-level, no code action)
Several whole subsystems flagged "not lifted":
- L/R scroll-without-cursor logic — MAJOR
- Pickup / put-down logic — BLOCKER
- Eating (hunger consumption) — BLOCKER
- Trophallaxis — MAJOR
- Death + rebirth as next egg — BLOCKER
- Attack red ant (B button) — BLOCKER
- **Scent placement / decay / following** — **BLOCKER** (core gameplay)
- Behavior Control / Caste Control panels — BLOCKER
- All Evaluation screens (House, Population, History, Status) — MAJOR
- 49-area Full Game map data — MAJOR
- Mating Flight / Mass Exodus triggers — MAJOR
- Sound + Speed options — MINOR

---

## Top 20 BLOCKER Items (file:line — needed fix)

1. **`simant.c:1147` `sub_877D`** — Cooperative task yield. Without it, every gs_*/state_* `for(;;)` loop spins forever. Fix: implement frame yield (return-to-NMI scheduler).
2. **`states_gameplay.c:1460,1463`** — `if (1 /* placeholder for BCS/BCC */)` makes state $29 (save) infinite-loop on first iteration. Fix: thread through `sub_DDD7`/`sub_DF79` carry-flag return.
3. **`entities_d.c:885`** — Null-pointer deref `((uint8_t*)0)[AE06+y]`. Will crash on dialog confirm. Fix: replace with real ROM table read at `$01:AE06+y`.
4. **`simant.c:1138` `switch_view_A3BD`** — SELECT-button view switch handler empty. View toggle is non-functional. Fix: lift `$00:A3BD`.
5. **`simant.c:738` `cursor_action_A_9DB9`** — A-button click action. No move/dig/select on A press. Fix: lift `$04:9DB9`.
6. **`simant.c:1143` `sub_BACA`** — Screen-template loader, called by every gs_* state. All menu screens render blank. Fix: lift `$00:BACA`.
7. **`simant.c:1145` `sub_85FC`** — Fade-in counterpart to `sub_8616_fade_out`. Screens stay black after fade-out. Fix: counter at `dp[$6C]`, INIDISP $00→$0F.
8. **`simant.c:1117` `sub_BC7F`** — PPU/VRAM defaults. Without it, video init garbled. Fix: lift `$00:BC7F`.
9. **`simant.c:1118` `sub_C318`** — Setup helper used by states $2A and $2C. Sound options + scent display won't initialise. Fix: lift `$00:C318`.
10. **`simant.c:1116` `sub_8967`** — Called from boot path; current empty. Fix: lift `$00:8967`.
11. **COVERAGE.md:95** — **Scent placement** (ants drop chemical). Core gameplay mechanic absent. Fix: locate per-step sim hook that writes `$7F:E796+` overlay tiles when ants carry food.
12. **COVERAGE.md:96** — **Scent decay** over time. Fix: per-N-frames tilemap fade.
13. **COVERAGE.md:97** — **Scent following** by other ants. Fix: pathfinding read of scent strength.
14. **COVERAGE.md:63** — Pickup/putdown logic. Yellow Ant cannot collect food/eggs/rocks. Fix: lift per-state body for carry-state byte transitions.
15. **COVERAGE.md:64** — Eating (hunger → consume food). Yellow Ant cannot eat. Fix: lift eat path triggered by `WRAM_HUNGER_E7B8 < 0x30`.
16. **COVERAGE.md:68** — Death + rebirth as next egg. Game-over loop incomplete. Fix: lift Yellow respawn into next egg slot.
17. **COVERAGE.md:69** — Attack red ant (B button). Combat from player initiative absent. Fix: lift B-button handler in cursor action.
18. **COVERAGE.md:75** — Behavior Control panel (Forage/Dig/Nurse triangle). Player has no caste-task control. Fix: lift game states `$0F-$15`.
19. **COVERAGE.md:76** — Caste Control panel (Workers/Soldiers/Breeders). Fix: same state range.
20. **`player_actions.c:617`** — `yellow_index` hardcoded to 0. Yellow Ant identification wrong after death/respawn. Fix: track yellow slot via dp variable rather than literal 0.

---

## Notes on `_pseudo` / `XXXX` naming

These are intentional decomp conventions, **not all bugs**:
- `sub_XXXX` — ROM-address-derived function name (kept for grep against disasm)
- `*_pseudo` — handler whose body is inferred but not yet ROM-verified; the `_full` variants supersede them
- "superseded" markers in comments point to the canonical lift

Grep noise: ~44 of the 97 hits fall into this DOCUMENTATION bucket.

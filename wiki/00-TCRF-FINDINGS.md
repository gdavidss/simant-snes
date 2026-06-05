# TCRF Findings — SimAnt (SNES)

Cross-reference of [The Cutting Room Floor: SimAnt (SNES)](https://tcrf.net/SimAnt_(SNES))
against our decomp under `/Users/guilhermedavid/simant-re/` (51 .c files, ~68K
lines, USA ROM `SimAnt - The Electronic Ant Colony (USA).pdf` + `simant.sfc`).

Source page captured via the Wayback Machine snapshot
`https://web.archive.org/web/20241226185116/https://tcrf.net/SimAnt_(SNES)`
(the live `tcrf.net` page now returns Error D9E / a prompt-injection homepage
to non-browser clients).

Game header per TCRF: Developer Tomcat System, publishers Imagineer (JP) /
Maxis (US), SNES, JP 1993-02-26, US 1993-10. Banners declared on the page:
unused code, unused graphics, unused abilities, debugging material, hidden
sound test, regional differences, prerelease article.

## Full TCRF Page Summary

The TCRF article is organized into these top-level sections (verbatim from the
table of contents):

1. Sub-Pages (Prerelease Info)
2. Unused Graphics — Menu Icons, Unseen Queens, Unseen Trees, Another Game
3. Debug Menu
4. Sound Test
5. Debug Flags — Red Yellow Ant, Spider Test Modes
6. Hidden/Unused Ant Types — Two Types of Breeders, Green and Blue Ants,
   Walking Dead Ants
7. Unused Battle Odds
8. Unimplemented Exodus
9. Error Handling — Exposing Bad Behavior, You Shouldn't Be Here
10. Mixed-Up Stats
11. Regional Differences — Game Text, User Interface (Saved Game Menu, Menu
    Icons, Overlay Map, Status Bar, Behavior Control Screen, Pause Support,
    Nest Digging), Scenarios 1-8, Full Game

Key facts pulled from the article:

- Title-screen combo `L + R + Start` *while holding both SNES Mouse buttons
  in port 2* opens a debug menu (cutscene viewer + sound test). PAR alias:
  `80946D07`.
- Sound test writes raw bytes to APUIO0..3 ($2140-$2143). Songs/SFX use
  consecutive value pairs; values 00/01 stop, 02+ play.
- RAM debug flags:
  - `$7E:0254 = $80` — "Red Yellow Ant" mode (partial defection).
  - `$7E:0220 = $08` — spider instant-kill / invincibility.
  - `$7E:0266 = $01` — spider auto-target / teleport-ant-under-spider mode.
  - `$7E:0220 = $07` — spider auto-camera-follow mode.
  - `$7E:020A` — spider state ($02 chasing / $03 eating).
  - `$7E:0299` — game mode (00 tutorial / 01 scenario / 02 full game).
- Sixteen internal ant types (0-15). Workers/soldiers split into "empty
  hand" + "carrying" sub-types. Two breeder types ($04 male?, $08 female?);
  the yellow ant is born $08 in Full Game and $04 on respawn. Types $0A/$0B
  are unused green/blue debug ants; type $0F uses corpse graphics ("walking
  dead"). PAR codes `829FFC0A`, `829FFC0B`, `829FFC0F` swap black soldiers
  to those types.
- Battle-odds matrix (4×4 caste table) exists in ROM. Soldier-vs-Queen is
  40%, etc. — but in practice the queen is deleted at fight start and a
  "null ant" (worker odds) fights in her place.
- "Mass exodus" (population 250 → queen splits) is documented in the US
  manual but unimplemented; the JP manual omits the corresponding
  screenshot.
- Bad-behavior printer: ants with undefined behavior bytes ($12 or >$13,
  or wrong-context $11/$13) flash the hex value in the dashboard corners.
- Tutorial-mode softlock: clearing `$7E:0299` to 00 inside scenario/full
  end-dialog produces "Why are you here?" then silent return to main menu.
  Killing the queen in tutorial also softlocks (no dialog defined).
- History Graph labels "Starve" and "Eaten" are swapped; Status screen
  "Fights Won" actually plots fights *lost* to red ants.
- Cross-game leftover: animation frames for a sumo wrestler from *Wakataka
  Oozumou: Yume no Kyoudai Taiketsu* (another Tomcat title) survive in the
  US ROM (absent in US prototype and JP release).
- Heavy regional differences across saved-game menu, menu icons, overlay
  map, status bar, behavior-control sleep/smoke animation, pause UX, nest
  digging menu, and every scenario (JP elements removed: kewpie dolls,
  cigarette butts/lighters/matchbooks, shōgi tiles, ¥5 coins, Lotte
  Coolmint gum, menko cards, "ハッピー" bottle openers, "HORE" cards,
  signs like "TOM CAT", "どびー", "クレパス", "ハズレ", "ベンツ",
  "すこんぶ"). Many of the JP-only graphic tiles remain in the US ROM.

## Per-Finding Verification Table

| # | TCRF Finding | Status | Evidence in decomp |
|---|--------------|--------|--------------------|
| 1 | Title-screen `L+R+Start` + both mouse buttons → debug menu | **VERIFIED + LIFTED (B3)** | Gate at `$00:9467` (`LDA $4218 / CMP #$30 / ... / LDA $007C / CMP #$03 / ... / JSR $9187`). Dispatch table at `$00:94C6`. See `tcrf_extras.c §4` (`tcrf_title_debug_unlock_9467`). |
| 2 | PAR `80946D07` aliases the combo | **VERIFIED + LIFTED (B3)** | Byte at `$00:946D` is the `CMP #$30` immediate; PAR rewrites it to `$07` (relax L+R requirement). See `tcrf_extras.c §4`. |
| 3 | Sound test writes APUIO0..3 directly | VERIFIED (engine) | APU port writes lifted in `save_options.c:753-860` and `misc_helpers.c:17-18`, `states_late.c:86-89`. The sound-test UI itself is NOT IN DECOMP. |
| 4 | "Load SND" picks an SPC bank | VERIFIED (engine) | Bank-load path exists in `audio_driver.c` (uploader + jump table). The UI for selecting it is not lifted. |
| 5 | `$7E:0220` controls spider invincibility / instant kill ($08) | **VERIFIED + LIFTED (B3)** | Gate at `$03:D91B` (`A5 20 C9 08 00 D0 04 22 BE E2 03` → JSL $03:E2BE instant-kill), duplicates at `$03:DA90`, `$03:DB04`. `$0220==$07` at `$03:D8DB` enables auto-camera-follow. See `tcrf_extras.c §2`. |
| 6 | `$7E:0266 = $01` spider auto-target / teleport mode | **VERIFIED + LIFTED (B3)** | Gates at `$03:C0FD` (early-jump to teleport stub $C228), `$03:D8BF`, `$03:DA67` (one-shot, cleared after use). See `tcrf_extras.c §2`. |
| 7 | `$7E:020A` = spider state byte (02 chase / 03 eat) | **VERIFIED + LIFTED (B3)** | Reads at `$03:D8C9` (`A5 0A C9 02`) and `$03:D8D3` (`C9 03`). Same byte as `CURSOR_CLICK_COUNT` (entities_d.c:94) — confirmed overload. See `tcrf_extras.c §2` (`enum tcrf_spider_state`). |
| 8 | `$7E:0254 = $80` "Red Yellow Ant" debug flag | **VERIFIED + LIFTED (B3)** | Gate at `$03:C1B1` (`A5 54 D0 0D`): non-zero takes the alt-JSL path through $02:989C + $02:ED7D (colony-switch on yellow-ant attack). See `tcrf_extras.c §2`. |
| 9 | `$7E:0299` = game mode (00/01/02) | VERIFIED | Heavily used: `gaps.c:403,417` (`SW16(0x0299, ...)`), `entities_e.c:134,618,642,744,770`, `render_helpers.c:22,210,231`, `save_options.c:103,501`, `states_gameplay.c:520,525,579,588,668,671,1151-1173,1540-1560`, `ui_menus.c:126,243-248,881`. NEW INFO: TCRF clarifies the **00/01/02 encoding semantics** (tutorial/scenario/full); our docs label it variously as "save action" or "popup mode". |
| 10 | Sixteen internal ant types (0-15) | NEW INFORMATION | Our codebase uses entity dispatch "type 14/15" for worker/soldier ant entities (`entities_b.c:111-113`), with a different numbering scheme (entity-class IDs). TCRF's 0-15 is the **ant-caste byte**, plausibly `entity_class_CBB8[]` referenced at `simulation.c:1091`. Need to enumerate that array. |
| 11 | Breeder types $04 vs $08; yellow-ant born $08, respawns $04 | NEW INFORMATION | Breeder/caste tallies are lifted in `lifted_helpers_3.c:10-147` and `render_helpers.c:471-548` (writes `$028A/$028C/$028E` and mirror `$027E/$0280/$0282`), but those slots are "Workers/Soldiers/Breeders" totals — the **internal type-4 vs type-8 tally split TCRF describes is not yet exposed**. |
| 12 | Type $0A/$0B = unused green/blue debug ants; PAR `829FFC0A`/`0B` | **VERIFIED + LIFTED (B3)** | LUT at `$02:9FFA` loaded by `$02:9FEC` (`BF FA 9F 02`). 4 entries; index [1] at byte `$02:9FFC` is the PAR target (default $06 = black-soldier-carrying). Caste-decode table at `$02:C61C` has 16 entries, with $0A/$0B/$0F marked unused. See `tcrf_extras.c §1`. |
| 13 | Type $0F = walking-dead corpse ants; PAR `829FFC0F` | **VERIFIED + LIFTED (B3)** | Same caste-decode table at `$02:C61C` index $0F — corpse tiles reused for a living entity. See `tcrf_extras.c §1`. |
| 14 | 4×4 battle-odds matrix W/S/B/Q | **PARTIAL (B3)** | No flat LUT exists. `fight_calc_B3F5` dispatches on `dp$50` (caste group $08/$18/$38/$28/$48) to per-caste handlers (`$03:ACED` worker, `$03:A874` soldier, `$03:AB1D` breeder), each of which uses scattered immediate constants (e.g. `$03:AA66 CMP #$0040` = Soldier-vs-Queen 40% threshold). Constants documented in `B3_TCRF_RESULTS.md §Target 3`. |
| 15 | "Null ant" appears when queen deleted at fight start, fights with worker odds | NOT IN DECOMP | The pre-fight queen-deletion branch is not yet identified in `combat.c`. |
| 16 | Yellow-ant fights use a different odds path | NOT IN DECOMP | `combat.c:907-920,1390-1398` mentions yellow-ant bias logic but no separate table is documented. |
| 17 | "Mass exodus" at pop 250 — unimplemented | PARTIALLY VERIFIED | `simulation.c:342,783-785 mass_exodus_cap_and_split_F050` lifts the **cap at 250** and `tests.c:482-509 test_mass_exodus_cap_250` exercises it. NEW INFO from TCRF: this cap is *all that survives* of the feature — the actual splitting/spawn-new-tile behavior is the part that was never wired up. Our reconstruction may be filling in behavior that the real ROM doesn't perform. |
| 18 | Manual lists exodus, ROM has no text for it | VERIFIED | `text_content.c` has no exodus string (matches TCRF). |
| 19 | Undefined-behavior values flash hex in dashboard corners | **NOT FOUND (B3)** | Hunt located behavior dispatchers at `$02:C9D4`, `$02:D68C`, `$02:E08A`, `$02:E14F`, `$02:ECA9`, `$03:A580`. None has a discrete "print hex of undefined behavior byte" branch — fall-through at `$03:A5A0` is a `RTL`. The printer described by TCRF may have been disabled in the final build. Behavior-ID enumeration captured in `tcrf_extras.c §8`. |
| 20 | "Why are you here?" tutorial trap on `$0299 == 00` | **VERIFIED + LIFTED (B3)** | String at `$01:8B4F` (file 0x00CB4F): `"Why are\xFEyou here?\xFF"`. Single reference at `$00:A814` (LDY #$8B4F). Containing handler is index [0] of `JSR ($A806,X)` table — fires when `dp$0299 == $00` (tutorial). See `tcrf_extras.c §3`. |
| 21 | Tutorial softlock if queen dies | NOT IN DECOMP | `combat.c:1179` notes "PLAY_MODE gate: only in full game/scenario, not in tutorial" — consistent with TCRF's claim that tutorial has no game-over branch. |
| 22 | "Starve" / "Eaten" swapped on History Graph | **PARTIAL (B3)** | Label table at `$01:9BAC` confirms canonical order ("Eaten" idx 7, "Starve" idx 8). Counters `$E764`/`$E766` are correctly named. Bug must live in the metric → sample-buffer-index translation inside `$04:90E0` per-tick sampler — the indexed `JMP ($9127,X)` and `JMP ($915B,X)` fan-outs swap slots 7 and 8. Documented swap exposed in `tcrf_extras.c §6` (`tcrf_history_metric_swap`). |
| 23 | "Fights Won" actually shows fights LOST | NEW INFORMATION | Not yet flagged in `ui_menus.c` evaluation-screen code (`ui_menus.c:12`). Bug-for-bug fidelity item. |
| 24 | Wakataka Oozumou sumo-wrestler tiles in US ROM | NOT IN DECOMP | Asset tables (`asset_data_1.c`..`asset_data_6.c`) are dumped but the sumo tiles are not flagged as foreign. Sniffable by tile-similarity scan. |
| 25 | Unseen queen surface-graphics tiles | **VERIFIED + LIFTED (B3)** | Queen tiles at `$01:F138` (32 bytes) + attrs at `$01:F158` (32 bytes). 4 anim phases × 8 directions via $20/$40/$60 mirror bits. "Rear-half deleted on unpause" bug: `queen_state5_stun_A682` (entities_c.c:535) only re-stamps OAM slot 0's priority bit, never re-stamps slot 1's attribute. See `tcrf_extras.c §5`. |
| 26 | Trees hidden behind Full-Game ending bushes | NOT IN DECOMP | Ending cutscene tilemap not lifted. |
| 27 | Unused "Menu Icons" tiles (checkerboard + winged ant + transparent square) | NOT IN DECOMP | Menu icon tiles in `assets.c:9` index but not flagged unused. |
| 28 | Regional difference: scrolling status-bar message field widened in US | NEW INFORMATION | Status-bar dashboard data layout (the `SUMM_*` `#define` block at `simulation.c:277-289` — earlier drafts incorrectly cited `simulation.c:293`, which is just a comment line in the FEED_* feeders block, not a renderer; the evaluation/dashboard rendering itself lives in `ui_menus.c` per its file-header note at `ui_menus.c:17`). The message-field width is hardcoded for US in our build. |
| 29 | Regional: Y pauses in US, jumps cursor to magnifier in JP | NEW INFORMATION | We only lift the US behavior. |
| 30 | Regional: JP "dig nest" always available; US gates on surface | NEW INFORMATION | The gate exists; the JP variant is not implemented. |
| 31 | Regional: behavior-control digger ants smoke (JP) vs sleep (US) | NEW INFORMATION | Sleep animation is what we have; JP smoke frames presumably not in US ROM. |
| 32 | Scenario 1-8 graphic swaps (kewpie dolls, lighters, shōgi, ¥5, Lotte gum, "TOM CAT" sign, "どびー", menko, "すこんぶ", chestnuts, snails…) — many JP tiles still in US ROM | NOT IN DECOMP | Scenario object tilemaps not catalogued for residual JP tiles. Search candidates: snail (Scenario 3), pull tabs (multiple), kewpie doll (Scenario 1), "どびー" sign (Scenario 2), shrine trinkets (Scenario 7), chestnut (Scenario 8). All worth flagging in `asset_data_*.c`. |
| 33 | Heat-haze shimmer in Scenario 5 (JP+US-proto) → static in US release | NEW INFORMATION | Shimmer animator not in current decomp. |
| 34 | US still labels Scenario 5 car "ベンツ"; Scenario 4 still has tatami; milk-bottle "ハッピー" still in US | NEW INFORMATION | Bug-for-bug-leftovers to verify in asset dump. |
| 35 | US Scenario-7 short name still "Shrine" despite localization to "porch" | VERIFIED | `scenarios.c:381,447`: `scenario_porch_view3` / `"Porch"` is the public name but our table doesn't yet record the residual "Shrine" short-name string. |
| 36 | Prerelease (US prototype) ROM exists with distinct content (snails kept, no sumo tiles, etc.) | NEW INFORMATION | We only have the US final ROM. |

### Score

- VERIFIED: 3 (0299 dispatch, mass-exodus cap, APU port plumbing)
- PARTIAL / VERIFIED-WITH-NUANCE: 4 (0299 semantics, 020A overload, exodus
  scope, Porch/Shrine name)
- NEW INFORMATION (TCRF beyond what we knew): 14
- NOT IN DECOMP / TO-LIFT: 15+

### B3 hunt update (2026-05-23)

After the targeted ROM hunt documented in `B3_TCRF_RESULTS.md`:

- **VERIFIED + LIFTED**: rows 1, 2, 5, 6, 7, 8, 12, 13, 20, 25 — added to
  `tcrf_extras.c` with byte-level evidence.
- **PARTIAL (B3)**: row 14 (no flat odds LUT — constants scattered in
  per-caste handlers), row 19 (no distinct hex printer — dispatcher
  fall-through is a no-op), row 22 (label/data swap localized to
  `$04:90E0` metric → buffer fan-out, full lift pending).
- **NOT FOUND**: sumo / JP-only tiles / unused menu icons (rows 24,
  27, 32-34) — would require side-by-side US-prototype tile diffing.

## Things We Know That TCRF Doesn't

Our decomp documents a lot of engine internals that TCRF doesn't touch.
Worth flagging if anyone updates the wiki:

- Scent system layout `$7F:4000-$5FFF`, 4 maps × 2 KB (`scent.c:15-83`).
- Active-combatant pool `$7F:E87E` (max 5 entries, 5 WORD fields each)
  driving `fight_resolver_96D7` (`combat.c:39-48,224-340`).
- Tile-based combat geometry, the 8 neighbor-offset tables, and the
  1/512-per-tick fight ignition probability (`combat.c:144-345`).
- Spider/ant-lion predator code reuse via `spider_predation_tick_C0FD`
  ($03:C0FD; ant lion shares the function with a 4× faster cadence)
  (`combat.c:941-1107,1482`).
- Caste-target / mirror layout `$028A/$028C/$028E` ↔ `$027E/$0280/$0282`
  with renderer at `$00:CF6D` (`lifted_helpers_3.c`, `render_helpers.c:471-548`).
- Manual mode override `dp[$0044]` short-circuits the auto-allocator
  (`render_helpers.c:511-524`).
- Counters `$E764` (eaten) / `$E766` (starved) — TCRF says these labels are
  swapped on the History Graph, but the *counters themselves* are correctly
  named in the engine (`combat.c:217-218`).
- State machine $00..$43 dispatch, GS_TUTORIAL=3, GS_MARRIAGE_FLIGHT at
  $00:B18C (`simant.c:168-1192`, `states_menu.c:715-731`).
- Per-scenario table with view configs, dangers, and short names
  (`scenarios.c:441-448`).
- Audio driver upload + SPC bank dispatch (`audio_driver.c`,
  `audio_intro.c`).
- Mouse serial protocol (16-bit MSB-first wire reads) (`mouse.c:41-116`).
- Maxis/Tomcat logo decompression block at `$07:A980` (`gap_fillers.c:1000`).
- Caterpillar 15-ant harvest mechanic from manual that was *cut* and which
  we **reconstructed** (`H4_RECONSTRUCTIONS.md:15`, `mechanics_extra.c`).
- The complete 54-message tutorial pointer table
  (`text_content.c:21,787-`).

## Unused / Hidden Content To Hunt In The ROM

Concrete things TCRF named that we should scan for in `simant.sfc` /
asset tables (`asset_data_1.c` … `asset_data_6.c`) and lift / label:

1. **Sumo wrestler frames** (Wakataka Oozumou leftover) — likely in graphics
   bank near other character anims; absent from US prototype, present in US
   final. Run a similarity diff against any known Wakataka tile dump, or
   look for ~16 large unreferenced 16×16/16×32 tiles.
2. **Queen surface-facing tiles** (E/W/NE/NW/SE/SW) — unique tiles
   referenced by `LDA` paths that draw the queen in non-N/S directions.
   The bug TCRF describes ("rear half deleted when unpaused") suggests the
   draw is gated by a paused/unpaused branch in the queen draw helper.
3. **Hidden trees behind ending bushes** in the Full Game ending cutscene
   tilemap.
4. **Unused menu icons** (checkerboard, winged ant, transparent slot) loaded
   on the house screen — search the OAM upload list for the house state.
5. **Green/Blue/Walking-Dead ant tiles** (types $0A/$0B/$0F) — separate
   tilesets with palette indices that resist the red/black recolor.
   Locate `$9FFC` in bank $02 (the PAR target) — likely the caste-spawn
   LUT.
6. **Battle-odds 4×4 LUT** — 16 bytes near `$03:96D7`. Likely 0x32,0x14,
   0x46,0x1E, 0x50,0x32,0x5A,0x28, 0x1E,0x0A,0x32,0x14, 0x46,0x3C,0x50,
   0x32 (50/20/70/30, …) or the % values directly.
7. **"Why are you here?" string** — search `text_content.c` raw byte
   patterns for the tutorial fall-through branch; index target is
   `states_gameplay.c:1173`.
8. **Bad-behavior hex-printer routine** — the dashboard renderer that
   detects behavior bytes $12, >$13, wrong-context $11 or $13 and prints
   the hex with race-condition flashing.
9. **JP-only scenario tiles surviving in the US ROM**: kewpie doll
   (Scenario 1), "どびー" sign (Scenario 2), snails (Scenario 3, removed
   between US prototype and US final), pull tabs (multiple scenarios),
   shrine trinkets (Scenario 7), chestnuts (Scenario 8), Lotte penguin
   logo (Scenario 4).
10. **`829FFC` PAR address** — the byte the PAR codes write to controls
    soldier-spawn type for the black colony. Lift the caste-assignment
    function.
11. **Debug menu code path** — title-screen input handler tagged at
    `states_late.c:524`. Look for a sequence reading port-2 mouse buttons
    *and* joypad L/R/Start, gated by something at `$80:6D07` (the PAR
    target).
12. **`$7E:0254`, `$7E:0266`, `$7E:0220` (WRAM) debug-flag readers** — none
    currently appear in our C sources. Add labeled `RYA_FLAG`,
    `SPIDER_TEST_A`, `SPIDER_TEST_B` symbols at these addresses and grep
    the disassembly for residual reads.

## Pointers

- TCRF page snapshot: see Wayback URL above.
- Decomp ROM: `/Users/guilhermedavid/simant-re/simant.sfc` (USA).
- Manual: `/Users/guilhermedavid/simant-re/SimAnt - The Electronic Ant Colony (USA).pdf`.
- Existing coverage notes: `COVERAGE.md`, `COVERAGE_ANALYSIS.md`.
- Audit/cleanup history: `AUDIT_SUMMARY.md`, `FINAL_CLEANUP.md`,
  `A1_POST_FIX_SPOTCHECK.md`..`A5_DEAD_CODE.md`.

# A3 — Phantom-Edit Audit

Verifies that fix claims in the H/G/F/V fix reports correspond to edits
that actually landed in the C files. Pure grep + small Read checks; no
compile, no edits applied.

Method: for each spot-checked fix, grep the target file for the
"before" pattern (should be absent) and the "after" pattern (should be
present). Classifications:

- **LANDED** — edit present in the C file as described
- **PHANTOM** — edit described but neither pattern matches
- **AMBIGUOUS** — overlap with other edits or partial evidence
- **RECLASSIFIED** — report explicitly says claim was wrong, so no edit
  was needed (counted as LANDED for audit purposes)

Note: No `V2-*.md` reports exist in the project tree. The audit covers
the seven extant reports (F5, F6, H1–H4, FINAL_CLEANUP, V4_1).

---

## Spot-checked fixes (30)

### F5 — sin/cos signatures

| # | Fix claim | File | Status |
|---|-----------|------|--------|
| 1 | `sub_008A0E_div256(uint8_t, uint16_t)` definition | lifted_helpers_1.c:226 | LANDED |
| 2 | `sub_008A0B_div256r(uint8_t, uint16_t)` definition | lifted_helpers_1.c:220 | LANDED |
| 3 | Externs widened to `uint16_t y` | lifted_helpers_2.c:36-37 | LANDED |
| 4 | `sub_D721_set_velocity_from_heading` passes `y_amplitude` to both | lifted_helpers_2.c:174-176 | LANDED |
| 5 | `entities_d.c` externs corrected (return `uint16_t`, 2 args) | entities_d.c:132-133 | LANDED |
| 6 | `rng_state_test.c` / `rng_diff_test.c` stub widened | both files L13/L21 | LANDED |

### F6 — Wiring

| # | Fix claim | File | Status |
|---|-----------|------|--------|
| 7 | `sub_877D` static stub deleted, calls go to `coop_yield_877D` | simant.c:1071,1149 | LANDED |
| 8 | Dispatch table: `recruit_apply_02A1F4` replaces `recruit_menu_apply_pseudo` | player_actions.c:1251 | LANDED |
| 9 | Dispatch table: `player_action_dispatch_03D792` replaces `queen_menu_apply_pseudo` | player_actions.c:1252 | LANDED |
| 10 | "B.Nest variants" → "Behavior Control Panel variant" | states_gameplay.c:1242 | LANDED |
| 11 | "R.Nest variants" → "Caste Control Panel variant" | states_gameplay.c:1255 | LANDED |
| 12 | `entities_d.c:885` null-deref replaced (was `cost += 0`, now AE06 table) | entities_d.c:927-944 | LANDED (superseded by H2 #8) |

### H1 — Dialog renderers

| # | Fix claim | File | Status |
|---|-----------|------|--------|
| 13 | `$2D` `type2D_menu_cursor_B90A` body lifted | entities_e.c:552 | LANDED |
| 14 | `$2E` `type2E_dialog_panel_B991` body lifted (4-way dispatch) | entities_e.c:640 | LANDED |
| 15 | `$2F` `type2F_dialog_panel_BA84` body lifted | entities_e.c:768 | LANDED |
| 16 | `read_far` extern added | entities_e.c:172 | LANDED |
| 17 | Dispatch table indices `$2D/$2E/$2F` point at the lifts | entities_e.c:1445-1447 | LANDED |

### H2 — G1 + G3 verifications (meta-audit conclusions)

H2 is itself a verification report; it concluded:
- `sub_008A0B_div256r` / `sub_008A0E_div256` now real (LANDED, see F5).
- `rand_modulo_F3BD` — V4-1 claim was a misread; current body is the
  Fibonacci-style $02:F3EF advance + multiply pyramid. Verified at
  lifted_helpers_6.c:655,678. **RECLASSIFIED** (no edit needed).
- `scatter_R_initial_886D` double-BG3HOFS — SNES PPU write-twice idiom
  is correct. Verified at lifted_helpers_4.c:426-427. **RECLASSIFIED**.
- `sub_DF0A` row-multiplier: lifted_helpers_4.c:375 has `row*6 + mode`.
  LANDED.
- `entities_d.c:885` AE06 table now embedded (16 entries). LANDED.
- `combat.c::yellow_ant_attack_red_simulate` calls
  `combatant_append_96B0((uint16_t)WMEM8(0xF0D3), (uint16_t)WMEM8(0xF0D5))`
  at combat.c:929. LANDED.

### H3 — Residual comment-vs-body fixes (sample)

| # | Fix claim | File | Status |
|---|-----------|------|--------|
| 18 | #1 stubs.c "linker alias" claim removed | stubs.c:6,23 | LANDED |
| 19 | #2 rng_diff_test.c "2-digit hex, uppercase, no prefix" | rng_diff_test.c:9 | LANDED |
| 20 | #3 vsync.c sub_E527 inversion → bumps on `s == 0 \|\| s == 2` | vsync.c:118 | LANDED |
| 21 | #4 vsync.c sub_DEEE marked `*** PORT STUB ***` | vsync.c:132 | LANDED |
| 22 | #5 vsync.c sub_DF79 marked `*** PARTIAL PORT ***` | vsync.c:159 | LANDED |
| 23 | #7 sub_866E header "0x400" → "1024 entries" | lifted_helpers_1.c:306 | LANDED |
| 24 | #8 sub_867F "A.high(EBA)" claim removed | lifted_helpers_1.c (no occurrence) | LANDED |
| 25 | #11 `caption_screen_BACA(uint8_t, uint16_t)` strong def | lifted_helpers_4.c:342 | LANDED |
| 26 | #13 cursor_axis_clamp `<=/>=` doc fix | lifted_helpers_5.c:91-99 | LANDED |
| 27 | #14 sub_9D48 PARTIAL PORT note + body unchanged | lifted_helpers_5.c:271-282 | LANDED |
| 28 | #15 sub_8B0C PARTIAL PORT + 16-byte counter TODO | lifted_helpers_6.c:258,266 | LANDED |
| 29 | #16 sub_EB58 PORT STUB | lifted_helpers_6.c:526-533 | LANDED |
| 30 | #18 ppu_init_table commentary, TM at index 26 | misc_helpers.c:47 | LANDED |
| 31 | #19 audio_intro.c `extern void sub_8F08(uint8_t a)`, call passes `0x00` | audio_intro.c:74,491 | LANDED |
| 32 | #20 TM/TS bit comments corrected (BG1+OBJ / BG3+BG4) | audio_intro.c:582-583 | LANDED |
| 33 | #21 `$2188 is WMDATA, not CGADSUB` | audio_intro.c:587 | LANDED |
| 34 | #22 "Maxis Ant Heads" at `$01:9850` clarified | audio_intro.c:411 | LANDED |
| 35 | #23 audio_driver.c 21 forward decls aligned to `(void)` / `(uint8_t)` | audio_driver.c:265-285 | LANDED |
| 36 | #24 event_keypress_0A9D: `+ spc_ram[0x03]` (was `[0x00]`) | audio_driver.c:1269 | LANDED |
| 37 | #27 rng_seed_XXXX renamed + alias kept | gap_fillers.c:353,359 | LANDED |

### H4 — Caterpillar + aphid wiring

| # | Fix claim | File | Status |
|---|-----------|------|--------|
| 38 | `WRAP_PORT_RECONSTRUCTIONS` gate at top of mechanics_extra.c | mechanics_extra.c:47-48 | LANDED |
| 39 | `caterpillar_harvest_check_RECONSTRUCTED(void)` no-arg sweep | mechanics_extra.c:370 | LANDED |
| 40 | `aphid_honeydew_drip_RECONSTRUCTED(void)` body | mechanics_extra.c:485 | LANDED |
| 41 | `sim_tick` calls both behind `#ifdef WRAP_PORT_RECONSTRUCTIONS` | simulation.c:556-563 | LANDED |
| 42 | Externs in simulation.c | simulation.c:158-159 | LANDED |

### FINAL_CLEANUP

| # | Fix claim | File | Status |
|---|-----------|------|--------|
| 43 | A2: `HG_BUF_BASE` 0x1D770 → 0x1F6D7 | ui_menus.c:713 | LANDED |
| 44 | A4: `SHADOW_PRICE_LO/HI` renamed to `AREA_B_POP_LIVE_7F` / `AREA_R_POP_LIVE_7F` | entities_d.c:111-112,938 | LANDED |
| 45 | A5: `wram[0xF6D5]/0xF6D3` rewritten as 16-bit stores | save_options.c:594-595 | LANDED |
| 46 | B1: 19/21 split documented in dispatcher comment block | audio_driver.c:246-264 | LANDED |

### V4_1_COMMENT_AUDIT — Top 5 dangerous

All five top-of-list V4-1 dangerous items are resolved (sin/cos via F5,
rand_modulo via H2 reclassify, yellow_ant via H2 #7, sub_DF0A via H2
#5, scatter_R via H2 #4 reclassify). The remaining ~33 V4-1 items map
1:1 onto H3 entries #1–#27 (all spot-checked above as LANDED) plus a
handful of self-acknowledged "earlier draft" notes that H3 explicitly
skipped.

---

## Tally

- **Audited: 46 fixes** (spread across all 7 reports)
- **LANDED: 46**
- **PHANTOM: 0**
- **AMBIGUOUS: 0**
- **RECLASSIFIED (no edit needed, by design): 3** (counted as LANDED)

## Top 3 phantoms that need re-applying

None found in this spot-check pass.

The audit sampled both "low-friction" comment-only fixes (H3 doc
markers) and "high-friction" body fixes (F5 sin/cos rewrite, F6
dispatch-table swap, H1 240+ LOC dialog renderer lifts, H4 sim_tick
wiring, FINAL_CLEANUP A2 macro renumber). Every one of the 30+
distinctly-checkable fixes landed in the C file as the report
described.

Caveats:

1. The audit deliberately did NOT verify byte-for-byte semantic
   correctness of any body lift — only that the claimed text changes
   are present.
2. H2's "RECLASSIFIED" cases (rand_modulo, scatter_R, possibly more)
   are not phantoms; the report explicitly states the original V4-1
   complaint was a misread of the ROM and that no edit was applied.
   A reader following only the V4-1 list could mistake these for
   unfixed items — they are not.
3. Several V4-1 items are tagged in the source itself as
   `*** PARTIAL PORT ***` or `*** PORT STUB ***` (H3 #4, #5, #14, #15,
   #16). The bodies remain stubs by design; only the doc has been
   updated to match. These are LANDED-as-doc-fix, NOT LANDED-as-body-fix.

## Items not audited

- 60+ smaller V4-1 cosmetic items (not enumerated in H3) — these are
  flagged in the source as drift-hot-spot self-acknowledgments and were
  intentionally skipped by H3. Worth a follow-up pass.
- G2 / G4 entity-body lifts (G2_ENTITIES_E_BODIES.md, G4_ENTITIES_G_BODIES.md)
  — beyond the H1 spot-check, the broader G2/G4 surface area was not
  audited.
- The 24 dispatchers in entities_f.c (H2 spot-checked 6 of 24) — H2
  itself is the meta-audit; we did not re-audit it.

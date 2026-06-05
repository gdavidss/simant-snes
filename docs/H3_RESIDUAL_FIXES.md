# H3 — Residual comment-vs-body fixes

Applied the ~30 MISLEADING/COSMETIC findings from V4-1 that G1 did not
sweep (G1 covered the top 5 DANGEROUS: sin/cos stub, rand_modulo LCG,
yellow_ant Y-coord arg, scatter_R double-BG3HOFS, sub_DF0A row*6).

Every edited file recompiled with `clang -Wall -Wextra` clean (no new
warnings beyond pre-existing unused-function ones in audio_driver.c).

## Fixes

| # | File | Severity | Class | Fix |
|---|------|----------|-------|-----|
| 1 | stubs.c | cosmetic | doc | Removed contradictory "dp as linker alias" claim — body uses `#define dp wram`. |
| 2 | rng_diff_test.c | cosmetic | doc | Clarified output format ("2-digit hex, uppercase, no prefix"). |
| 3 | vsync.c sub_E527 | dangerous (not in G1's top-5) | body | Flipped the `$B6` increment to match the doc's "even iters bump $B6" claim. |
| 4 | vsync.c sub_DEEE | misleading | doc | Marked as `*** PORT STUB ***` — body is only the early-return guard. |
| 5 | vsync.c sub_DF79 | misleading | doc | Marked as `*** PARTIAL PORT ***` — only the equality compare is implemented. |
| 6 | mouse.c mouse_set_speed_E494 | cosmetic | doc | Documented the early "no mouse" branch that also writes $80. |
| 7 | lifted_helpers_1.c sub_866E | cosmetic | doc | Changed "0x400 entries" header → "1024 entries" to match the loop exit math. |
| 8 | lifted_helpers_1.c sub_867F | misleading | doc | Removed "A.high(EBA)" claim — body is 1-byte A by design. |
| 9 | lifted_helpers_3.c sub_C4BB | misleading | doc | Added CAVEAT block clarifying the $2C alias/writeback approximation. |
| 10 | lifted_helpers_3.c sub_8F08 | misleading | doc | Added NOTE that the colour LUTs (rom_00_8F27/33/3F) are zero-filled placeholders. |
| 11 | lifted_helpers_4.c caption_screen_BACA | dangerous | body | Fixed signature from `(uint8_t)` to `(uint8_t, uint16_t)` to match the 2-arg extern decl in audio_intro.c/states_menu.c. |
| 12 | lifted_helpers_4.c sub_DD24 | misleading | doc | Marked PARTIAL PORT — caller's X-column index is never propagated. |
| 13 | lifted_helpers_5.c cursor_axis_clamp | cosmetic | doc | Corrected `</>` to `<=/>=` to match the body's branch shape. |
| 14 | lifted_helpers_5.c sub_9D48 | misleading | doc | Marked PARTIAL PORT — conditional return is omitted. |
| 15 | lifted_helpers_6.c sub_8B0C | misleading | doc | Marked PARTIAL PORT, added TODO for the 16-byte counter stamp. |
| 16 | lifted_helpers_6.c sub_EB58 | misleading | doc | Marked PORT STUB — LZSS source tables not linked, all-zero output. |
| 17 | lifted_helpers_6.c sub_0490D2 | misleading | doc | Added NOTE explaining why we store $04 (the bank) in place of the real DBR. |
| 18 | misc_helpers.c ppu_init_table | cosmetic | doc | Rewrote the index commentary ([12..28]) to match the actual triple layout — TM is at index 26, not 48. |
| 19 | audio_intro.c sub_8F08 extern | dangerous | body | Fixed extern to `(uint8_t a)` and call site to pass `0x00`. |
| 20 | audio_intro.c state_43 TM/TS bits | misleading | doc | Corrected bit comments: $11 → BG1+OBJ, $0C → BG3+BG4. |
| 21 | audio_intro.c $2188 "CGADSUB tweak" | misleading | doc | Corrected — $2188 is WMDATA, not CGADSUB (which is $2131). |
| 22 | audio_intro.c credits_pages[23] | misleading | doc | Clarified that "Maxis Ant Heads" at $01:9850 sits *between* entries 22 and 23 in the table. |
| 23 | audio_driver.c forward decls | dangerous | body | Aligned 21 `event_*_0A..` forward decls + song_event_dispatch_099B + event_note_0A46 to the `(void)` / `(uint8_t a)` bodies. |
| 24 | audio_driver.c event_keypress_0A9D | misleading | body | Fixed `0x0028 + spc_ram[0x00]` → `0x0028 + spc_ram[0x03]` (voice slot X, not port byte). Earlier code aliased every voice to slot 0. |
| 25 | combat.c case 0 (player_outcome) | cosmetic | doc | Spelled out that case 0 intentionally omits the SFX call. |
| 26 | player_actions.c DP_NEST_CURY_A0 | cosmetic | doc | Clarified macro comment — $A0 stores `$A7 - dp[$15]`, not "$A7 - cursor Y". |
| 27 | gap_fillers.c rng_seed_XXXX | misleading | both | Renamed to `rng_seed_from_frame_and_joypad`, kept `rng_seed_XXXX` as alias for legacy scripts. |

## Tally

- **Findings addressed: 27**
- **MISLEADING fixed in body: 4** (#11 caption_screen_BACA, #19 sub_8F08 extern, #23 audio_driver decls, #24 event_keypress_0A9D). Plus the dangerous-but-not-top-5 #3 vsync inversion → effectively 5 body fixes.
- **MISLEADING fixed in doc: 13** (4, 5, 8, 9, 10, 12, 14, 15, 16, 17, 20, 21, 22) — bodies were intentional port stubs; comments now match.
- **COSMETIC: 8** (1, 2, 6, 7, 13, 18, 25, 26).
- **Mixed body+doc: 1** (27 — renamed + alias).

## Skipped (and why)

- **vsync.c sub_A3D6** — audit complained about the "dp[$0026] zero-on-SELECT pattern"; body does `dp[$0026]++` which is correct. The "zero-on-SELECT" pattern lives at a *different* SELECT-press site, not in sub_A3D6. Doc is fine.
- **lifted_helpers_1.c unused `rom_018020` extern** — flagged cosmetic; harmless and removing would require touching call sites.
- **mouse.c L168 cross-reference** — audit's own sanity check, no fix needed.
- **scent.c, simant.c, simulation.c, territory.c, gaps.c, player_actions_full.c** — all self-acknowledged "earlier draft" notes that audit explicitly marks as "already fixed". No action.
- **audio_driver.c event_note_0A46 $07 stash** — audit flagged but the body's inline comment already explains the Y-stash convention extensively; the dispatcher in 099B maintains the bookkeeping. Leaving as documented.
- **combat.c L66/507/590/668/764/1049/1096/1123/1199** — self-noted earlier-draft repairs flagged as drift hot-spots, not active divergences.

## New bugs found during the pass

1. **audio_driver.c event_keypress_0A9D** was writing `spc_ram[0x0028 + spc_ram[0x00]]` instead of `+ spc_ram[0x03]`. `$00` is the IPL port byte (always the same value across voices), while `$03` is X (the active voice slot). Every voice's transpose would have been written into the same slot. Fixed (entry #24 above) — this is a real behavior bug, not just a comment slip, though the audit listed it as MISLEADING.
2. **audio_intro.c sub_8F08()** was being called with zero args while the strong def takes a `uint8_t`. Per C99 unspecified-args rules this links but passes garbage. The OAM-DMA prep at $00:8F08 then used `(garbage >> 4) & 0x0E` to index the colour LUTs — wrong index every frame the intro ran. Fixed (entries #19, #10).
3. **lifted_helpers_4.c caption_screen_BACA** had a 1-arg strong def while every call site (audio_intro.c, states_menu.c via static shadow) passes 2 args. C99 unspecified-args saved the link, but the caption pointer was silently dropped on every credits caption call. Fixed (entry #11).

These three are arguably "dangerous" in the audit's own terms — V4-1 graded them as MISLEADING because the bodies were "intentionally stubs". H3 promoted the body to match the doc.

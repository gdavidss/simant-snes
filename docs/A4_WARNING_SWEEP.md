# A4 — Compile-Warning Sweep

Per-file compile check with strict warnings. Each file built standalone (no link):

```
clang -Wall -Wextra -Wpedantic -c <file>.c -o /tmp/check.o
```

## Headline Numbers

- **.c files inspected:** 50 (project actually contains 50 `.c` files, not 46 as the brief stated)
- **Files that compile cleanly (exit 0, zero diagnostics):** 48
- **Files with warnings:** 2
- **Files that fail to compile (ERROR):** 0
- **Total warnings:** 18
  - **ERROR:** 0
  - **WARN-DANGEROUS:** 0
  - **WARN-COSMETIC:** 18

No file produced a `-Wall -Wextra -Wpedantic` diagnostic involving implicit conversion, signedness mismatch, dropped arguments, format-string mismatch, undefined-behavior-adjacent constructs, or aliasing concerns. The two flagged files are entirely cosmetic.

## Per-File Tally

| File | Warnings | Errors | Notes |
|------|---------:|-------:|-------|
| asset_data_1.c | 0 | 0 | clean |
| asset_data_2.c | 0 | 0 | clean |
| asset_data_3.c | 0 | 0 | clean |
| asset_data_4.c | 0 | 0 | clean |
| asset_data_5.c | 0 | 0 | clean |
| asset_data_6.c | 0 | 0 | clean |
| assets.c | 0 | 0 | clean |
| audio_driver.c | **17** | 0 | all `-Wunused-function` (cosmetic) |
| audio_intro.c | 0 | 0 | clean |
| combat.c | 0 | 0 | clean |
| control_panels.c | 0 | 0 | clean |
| entities_a.c | 0 | 0 | clean |
| entities_b.c | 0 | 0 | clean |
| entities_c.c | 0 | 0 | clean |
| entities_d.c | 0 | 0 | clean |
| entities_e.c | 0 | 0 | clean |
| entities_f.c | 0 | 0 | clean |
| entities_g.c | 0 | 0 | clean |
| gap_fillers.c | 0 | 0 | clean |
| gaps.c | 0 | 0 | clean |
| lifted_helpers_1.c | 0 | 0 | clean |
| lifted_helpers_2.c | 0 | 0 | clean |
| lifted_helpers_3.c | 0 | 0 | clean |
| lifted_helpers_4.c | 0 | 0 | clean |
| lifted_helpers_5.c | 0 | 0 | clean |
| lifted_helpers_6.c | 0 | 0 | clean |
| mechanics_extra.c | 0 | 0 | clean |
| misc_helpers.c | 0 | 0 | clean |
| mouse.c | 0 | 0 | clean |
| player_actions.c | 0 | 0 | clean |
| player_actions_full.c | 0 | 0 | clean |
| render_helpers.c | 0 | 0 | clean |
| rng_diff_test.c | 0 | 0 | clean |
| rng_state_test.c | 0 | 0 | clean |
| save_options.c | 0 | 0 | clean |
| scenarios.c | 0 | 0 | clean |
| scent.c | 0 | 0 | clean |
| simant.c | 0 | 0 | clean |
| simulation.c | 0 | 0 | clean |
| states_gameplay.c | 0 | 0 | clean |
| states_late.c | 0 | 0 | clean |
| states_menu.c | 0 | 0 | clean |
| stubs.c | **1** | 0 | `-Wnewline-eof` (cosmetic) |
| stubs_for_test.c | 0 | 0 | clean |
| stubs_test_extras.c | 0 | 0 | clean |
| territory.c | 0 | 0 | clean |
| tests.c | 0 | 0 | clean |
| text_content.c | 0 | 0 | clean |
| text_screens.c | 0 | 0 | clean |
| ui_menus.c | 0 | 0 | clean |
| vsync.c | 0 | 0 | clean |

## Warnings in Detail

### audio_driver.c — 17 × `-Wunused-function` (WARN-COSMETIC)

The Makefile uses `-Wno-unused-function` precisely because the lifted decomp keeps many forward-declared static handlers around for dispatch-table parity. With `-Wno-unused-function` removed (as in this stricter sweep), they re-surface.

| Line | Symbol |
|-----:|--------|
| 267  | `event_set_x90_0AD2` |
| 269  | `event_set_tempo_0ADA` |
| 270  | `event_set_xD0_0AE6` |
| 271  | `event_ptr_relative_0AEE` |
| 272  | `event_loop_setup_0B09` |
| 273  | `event_loop_iter_0B1B` |
| 274  | `event_voice_stop_0B4C` |
| 275  | `event_rest_0B52` |
| 276  | `event_set_base_pitch_0B67` |
| 277  | `event_subr_call_0B7C` |
| 278  | `event_subr_return_0BBB` |
| 279  | `event_pan_slide_0BD8` |
| 281  | `event_set_transpose_0B70` |
| 282  | `event_keyrest_0ABB` |
| 283  | `event_fine_pitch_0CB0` |
| 284  | `event_pitch_slide_0CB9` |
| 1693 | `commit_song_y_0D34` |

**Severity:** WARN-COSMETIC.
**Suggested action (later):** either wire each handler into the dispatch table it was lifted from, drop the static keyword once a header is introduced, or annotate them with `__attribute__((unused))`. Not a correctness concern.

### stubs.c — 1 × `-Wnewline-eof` (WARN-COSMETIC)

```
stubs.c:33:85: warning: no newline at end of file [-Wnewline-eof]
   33 | __attribute__((weak)) int main(void) { extern void reset(void); reset(); return 0; }
```

**Severity:** WARN-COSMETIC.
**Suggested action (later):** append a trailing newline. Pure pedantic style; no semantic impact.

## Classification Summary

| Severity            | Count |
|---------------------|------:|
| ERROR               |     0 |
| WARN-DANGEROUS      |     0 |
| WARN-COSMETIC       |    18 |

## Top 10 Dangerous Warnings Worth Fixing Later

None. There are **zero** dangerous warnings to triage. All 18 diagnostics are cosmetic and stylistic. If a future pass wants to drive the warning count to zero, the cheapest fix is:

1. Hook up the 17 `event_*` audio-driver handlers to the dispatch table that originally referenced them (or `__attribute__((unused))`).
2. Append a newline to `stubs.c` line 33.

## Methodology Notes

- Command per file: `clang -Wall -Wextra -Wpedantic -c <file>.c -o /tmp/check.o 2>&1`
- All raw outputs saved under `/tmp/warn_sweep/<file>.out`
- The project ships no headers and no `#include`s between modules — every `.c` is fully self-contained. This is why pedantic single-file builds even succeed (and explains the lack of cross-file type-mismatch warnings here; those would only appear at link or in a unity build).
- The default `Makefile` already passes `-Wno-unused-function`; that flag was intentionally dropped for this sweep so the suppressed warnings would re-surface.

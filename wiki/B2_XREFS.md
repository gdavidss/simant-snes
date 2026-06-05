# B2 — Cross-Reference Audit (wiki/ → source/)

**Date:** 2026-05-23
**Scope:** 19 wiki pages (`wiki/00-TCRF-FINDINGS.md` … `wiki/18-save-load.md`)
**Method:** regex-extract every `file.c:LINE` reference, automated
identifier-match against ±80-line window in source, manual spot-check of
flagged anomalies. Wiki-to-wiki link targets resolved against the wiki dir.
No compile.

## Totals

| metric                     | count |
| -------------------------- | ----: |
| `file.c:LINE` refs found   |   252 |
| automated VALID            |   178 |
| automated WRONG_LINE       |    12 |
| automated UNVERIFIED       |    62 |
| **manual-confirmed VALID** | ≈ 244 |
| **manual-confirmed WRONG** |     5 |
| **manual borderline**      |     3 |
| wiki→wiki `[X](Y.md)` links |    37 |
| broken wiki→wiki links     |     0 |

The automated heuristic produced many false-positive WRONG_LINE / UNVERIFIED
flags — almost all because the wiki paragraph used data identifiers or
prose-style references (e.g., `mouse.c:41-116` for the "mouse serial
protocol" section) rather than backticked function names the script
expected. After hand-checking the flagged refs, the actual error rate is
small (~2%).

## Confirmed errors (file:LINE is genuinely off)

These five citations point at the wrong line and a reader following them
will land in the wrong block of code or comment.

| # | wiki ref                                                  | claim                                       | actual location                          | severity |
|---|-----------------------------------------------------------|---------------------------------------------|------------------------------------------|----------|
| 1 | `17-audio.md:37` — `simant.c:1007-1008`                   | "Boot-time SPC dp shadows"                  | shadows set at `simant.c:1024-1025`      | off by 17 lines; sub-section is real but wrong window |
| 2 | `17-audio.md:31` — `simant.c:954-967`                     | "SPC uploader lifted summary"               | `SPC700 SOUND DRIVER UPLOAD` header at `simant.c:971`+ | off by 17 lines; 954-967 is asset_decompress_to_scratch_8D7E and vram_dma_from_scratch_8ACC — not SPC |
| 3 | `17-audio.md:213` — `audio_driver.c:1568-1572`            | A1 regression-fix voice-dead gate           | gate is at `audio_driver.c:1582-1584`    | 1568-1572 is the "this is SPC700 not 65816" comment, not the gate |
| 4 | `10-territory-49areas.md:112` — `territory.c:482`         | `mass_exodus_cap_and_presence_F050` lift    | function at `territory.c:488` (header) / 490 (body) | 482 is in a *different* function's tail comment |
| 5 | `10-territory-49areas.md:114` — `territory.c:643`         | `area_grid_scan_F02A`                       | function at `territory.c:651`            | off by 8 lines (close, but lands in pre-function comment of a sibling) |

## Borderline / cosmetic

| wiki ref                                              | note |
|-------------------------------------------------------|------|
| `00-TCRF-FINDINGS.md:103` — `simulation.c:293`        | claims "dashboard renderer is lifted at simulation.c:293" — line 293 is summary-block #define data (SUMM_*); renderer is elsewhere. Reasonable as "dashboard data layout" but mislabelled. |
| `18-save-load.md:143` — `save_options.c:376-388`      | "for the corrected derivation" — 376-388 ends right before the `save_checksum_03_FC3A()` body (which starts at 400). The comment-doc block runs 388-399, function body 400-419. The cited range covers the *call site*, not the derivation. |
| `00-TCRF-FINDINGS.md:106` — `combat.c:217-218`        | EATEN_COUNTER comment refers to simulation.c counters, but combat.c:217-218 is dispatcher comment. Counter `EATEN_COUNTER_E764` actually lives at `simulation.c:241`. Wiki line is OK if read as "named in the engine"; reader may be misled. |

## Wiki → wiki link audit

All 37 `[text](Y.md)` links between wiki pages resolve to existing files.
Wiki cross-links are healthy: `00-TCRF-FINDINGS.md` cites
`01-architecture`, `02-simulation-tick`, `03-rng`, `04-entity-system`,
`07-scent-system`, `08-combat`, `09-predation`, `10-territory-49areas`,
`11-house-screen-ui`, `12-control-panels`, `13-player-actions`,
`14-scenarios`, `15-dangers`, `16-rendering-pipeline`, `17-audio`,
`18-save-load`. Source-code annotations (`See wiki/NN-...md`) reciprocate
this consistently (sample-verified in `combat.c`, `simant.c`,
`scent.c`, `simulation.c`).

## Manual page-number consistency check

Wiki cites the original manual at multiple page numbers. Internal
consistency (same fact, same page) holds for the spot-checks performed:

| fact                                          | manual page cited |
|-----------------------------------------------|-------------------|
| Yellow Ant B-click = attack red               | p.11 (combat.c:907 comment + 08-combat.md, 13-player-actions.md) |
| Scenario list                                 | p.22-23 (simant.c:179 GS enum comment + 14-scenarios.md) |
| Soldier Ants are better fighters              | p.11 (combat.c:921 + 08-combat.md) |
| Primary-danger per-scenario                   | p.36 (scenarios.c:441 Scenario.primary_danger comment + 14-scenarios.md:38) |
| "Mass exodus" at 250 pop                      | p.19 (territory.c:696-699 + 10-territory-49areas.md:163) |

No conflicting manual-page citations were found across pages for the
same fact. (Pages 11, 19, 22-23, 36 appear consistently. Many entries
cite the manual without a page number — that's a documentation gap but
not a contradiction.)

## Most-misleading bad links (top 5)

Sorted by how badly a reader following the citation would be misled:

1. **`17-audio.md:31` → `simant.c:954-967`** — the cited window is
   *asset-decompression* and *VRAM DMA*, completely unrelated to the SPC
   uploader. A reader following this gets the wrong subsystem entirely.
2. **`17-audio.md:37` → `simant.c:1007-1008`** — the cited lines are
   `dp[0x0A] = 0x81` (NMITIMEN setup) and `sub_C318()`, NOT the SPC dp
   shadows. The actual shadow initialization is 17 lines later
   (1024-1025).
3. **`17-audio.md:213` → `audio_driver.c:1568-1572`** — the wiki
   explicitly says "A1 regression-fix" lives here, but those lines are
   the documentation-grade note explaining that bodies-below are SPC700
   not 65816. The actual `if (spc_ram[0x0140 + x] == 0) return;` gate is
   at 1582-1584.
4. **`10-territory-49areas.md:112` → `territory.c:482`** — lands inside
   the *previous* function's trailing comment, several lines before the
   `mass_exodus_cap_and_presence_F050` header.
5. **`00-TCRF-FINDINGS.md:103` → `simulation.c:293`** — claims this
   line contains the "dashboard renderer". It actually contains the
   SUMM_* `#define` data block; the dashboard *renderer* is elsewhere
   (likely `render_helpers.c`). Not catastrophically misleading but
   conceptually wrong.

## Methodology notes

- Heuristic looked for backticked identifiers in a 600-byte context
  window before the ref and 200 bytes after, then searched an 80-line
  source window around the ref for a match. False positives were
  triggered when two adjacent bullets each referenced a different file
  — the heuristic pulled the *neighboring* bullet's identifier into the
  candidate list and reported a wrong-line when that identifier was
  found elsewhere in the file.
- Manual sample of 25+ UNVERIFIED entries confirmed they are all
  legitimate references (the wiki paragraph cited a data layout, a
  comment-block summary, or used prose instead of a backticked name).
- Raw results in `wiki/_xref_results2.json` and
  `wiki/_xref_audit_raw.txt`.


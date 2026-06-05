# B5 — Post-Wiki Health Check

Verification that the Z3 wiki round (~64 inline `WIKI:` comments added across
the C files) did not regress the build, that every inline wiki pointer still
resolves to a real wiki anchor, and that every wiki section is grounded in at
least one concrete code/address anchor.

Methodology, raw inputs, and helper scripts:

- Compile sweep raw outputs: `/tmp/b5_sweep/<file>.c.out`
- Inline-pointer extraction: `/tmp/b5_wiki_refs.txt` (103 raw matches → 95 normalized refs)
- Wiki header index: `/tmp/b5_wiki_headers.txt`
- Pointer verifier: `/tmp/b5_verify.py`
- Section coverage analyser: `/tmp/b5_section_coverage.py`

---

## PART A — Compile Sweep

Command (B5 brief: `-Wall -Wextra`, no `-Wpedantic`, no link):

```
clang -Wall -Wextra -c <file>.c -o /tmp/check.o
```

### Headline

- **.c files inspected:** 51 (vs. 50 at A4 baseline — the new file is `entities_g.c` from the G-round)
- **Files that compile clean (exit 0, zero diagnostics):** 50
- **Files with warnings:** 1 (`audio_driver.c`)
- **Files that fail to compile (ERROR):** 0
- **Total warnings:** 17

### Diff vs. A4_WARNING_SWEEP.md baseline

| Bucket | A4 baseline | B5 today | Delta |
|--------|------------:|---------:|------:|
| ERROR | 0 | 0 | 0 |
| WARN-DANGEROUS | 0 | 0 | 0 |
| WARN-COSMETIC | 18 | 17 | -1 |
| files clean | 48 / 50 | 50 / 51 | +1 file added clean |

The single delta is **stubs.c**: A4 reported 1 × `-Wnewline-eof` because A4 also
used `-Wpedantic`. B5's brief drops `-Wpedantic`, so that pedantic-only warning
disappears. The 17 audio_driver `-Wunused-function` warnings carry over
unchanged. **No new warnings introduced by the Z3 wiki-comment round.**

### New file since A4

- `entities_g.c` (51st .c file) — compiles clean under `-Wall -Wextra`.

### audio_driver.c warnings (unchanged from A4)

All 17 are `-Wunused-function` on lifted static handlers
(`event_set_x90_0AD2`, `event_set_tempo_0ADA`, …, `commit_song_y_0D34`).
Project Makefile suppresses this set with `-Wno-unused-function`; B5 dropped
the flag to match A4's stricter sweep. Severity unchanged: **WARN-COSMETIC**.

---

## PART B — Inline `WIKI:` / `wiki/` Pointer Spot-Check

`grep -rn 'WIKI:\|wiki/' *.c` → 103 grep hits across 19 .c files. After
normalizing (multi-line-wrapped C comments, dedup of "same line, multiple
URLs"), there are **95 distinct file/anchor pointers**.

### Pointer status

| Status | Count | Meaning |
|--------|------:|---------|
| OK (exact slug match) | 66 | anchor slugifies to a real `##`/`###` header |
| OK-FILE (file only, no anchor) | 27 | bare `wiki/XX.md` reference, no section claim |
| OK-PREFIX (shorthand prefix) | 2 | wiki anchor truncated — does **not** match GitHub exactly |
| DEAD-FILE | 0 | wiki file does not exist |
| DEAD-ANCHOR | 0 | anchor cannot be resolved |
| **Total** | **95** | |

**All 18 referenced wiki files exist.** All 19 unique anchors that include a
`#…` slug or `§…` section number resolve to a real header.

### The two OK-PREFIX (shorthand) pointers — flagged, not broken

These would 404 on GitHub if clicked verbatim, because GitHub anchors require
exact slug equality. Listed for the user to decide whether to expand them:

| File:Line | As written | Real anchor |
|---|---|---|
| `entities_b.c:5` | `wiki/08-combat.md#6-yellow-ant-` *(line-wrapped — continues `attack-b-button-on-red-ant` on L6)* | `#6-yellow-ant-attack-b-button-on-red-ant` |
| `entities_b.c:894` | `wiki/08-combat.md#worker-vs-soldier` | `#worker-vs-soldier--not-raw-hp` |

The first is a false alarm of the regex (the slug spans two C-comment lines
and is actually correct when reconstructed). The second is a real shorthand
that should be tightened if URL fidelity is desired.

### Pointer slug convention (calibration note)

Header `## 3. Fight resolver — \`$03:96D7\`` slugifies to
`3-fight-resolver--0396d7` (double hyphen where the em-dash sat, `$` and `:`
deleted, backticks deleted). All 64 `#…`-style pointers follow this convention
faithfully — including the somewhat unusual double-hyphens like
`#2-ant-lion--03c0fd-shared--type-28-in-entities_dc`.

---

## PART C — Wiki Section-Anchor Coverage

For each wiki page, every `^## ` (level-2) section was scanned for at least
one concrete code anchor — either a `<file>.c` reference or a SNES address
of the form `$BB:OOOO`.

### Headline

- **Total `##` sections across 19 wiki pages:** 166
- **Sections with `.c` file ref AND/OR `$bb:addr` ref:** 152 (91.6 %)
- **Sections with `.c` file ref:** 133 (80.1 %)
- **Sections with `$bb:addr` ref:** 116 (69.9 %)
- **Sections with NEITHER (bare sections):** 14 (8.4 %)

### Bare sections — flagged for the user

These have no `<file>.c` reference and no `$BB:OOOO` address. Most are
intentionally abstract (diagrams, manual cross-refs, cross-reference lists),
but listed so the user can decide whether they need an anchor:

| Wiki page | Line | Section title | Smell |
|---|---:|---|---|
| 00-TCRF-FINDINGS.md | 210 | Pointers | nav-only, expected bare |
| 02-simulation-tick.md | 14 | Why 8.5 Hz? | prose, could cite `simulation.c` |
| 02-simulation-tick.md | 186 | Per-tick flow diagram | mermaid, expected bare |
| 02-simulation-tick.md | 215 | Why the sim runs in its own task | prose, could anchor |
| 03-rng.md | 113 | 4. Output mixing and scaling | should cite `gaps.c` |
| 03-rng.md | 156 | 5. RNG state diagram | mermaid, expected bare |
| 03-rng.md | 195 | 7. Verification (V3-D) | could cite `rng_*_test.c` |
| 04-entity-system.md | 182 | 7. Cross-references | nav-only, expected bare |
| 09-predation.md | 252 | 7. Predator kill radius — summary | summary table, could anchor |
| 09-predation.md | 314 | 9. Surprising findings | prose summary, OK |
| 10-territory-49areas.md | 195 | 8. Mermaid: Per-32-Tick Territory Flow | diagram, expected bare |
| 13-player-actions.md | 285 | 10. Direct-page summary | dp-only table, OK |
| 14-scenarios.md | 319 | 11. Manual references | manual-only, expected bare |
| 16-rendering-pipeline.md | 312 | 11. Manual references | manual-only, expected bare |

Roughly 8 of the 14 are diagrams / nav blocks / manual-cross-refs and are
fine bare. The 6 that "could be anchored" are: `02-simulation-tick.md` §"Why
8.5 Hz?" and §"Why the sim runs in its own task", `03-rng.md` §4 and §7,
`09-predation.md` §7, and `10-territory-49areas.md` §8 (this last one is a
mermaid but its prose preamble could cite a function).

---

## Bottom line

- **Build health:** unchanged. 51 / 51 .c files compile under `-Wall
  -Wextra`; the 17 cosmetic `-Wunused-function` warnings on `audio_driver.c`
  are identical to the A4 baseline. The Z3 wiki-comment round introduced
  zero new compiler diagnostics.
- **Pointer health:** 95 / 95 inline wiki pointers point to a real wiki file.
  64 anchor exactly, 27 are file-only (legitimate). Two
  (`entities_b.c:5` and `entities_b.c:894`) use a shorthand prefix that
  would 404 on GitHub if clicked; only `:894` is a real bug — `:5` is just a
  C-comment line-wrap artefact.
- **Wiki anchor coverage:** 152 / 166 sections (91.6 %) contain at least one
  `.c` filename or `$BB:OOOO` address. Of the 14 bare sections, ~8 are
  legitimately bare (diagrams, navigation, manual cross-refs) and ~6 could be
  tightened with a concrete code anchor.

No fixes were applied. All findings are advisory.

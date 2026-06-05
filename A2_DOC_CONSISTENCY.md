# A2 — Documentation Consistency Audit

Pass: pure read of all .md files in /Users/guilhermedavid/simant-re/.
Cross-checked key facts (state count, entity count, lift coverage, line
totals, save signatures, RNG / asset verification, HG_BUF_BASE,
pitch / dispatcher counts, $24-$27 semantics, Yellow-Ant composite).

**Docs checked: 27**

- README.md
- COVERAGE.md
- ENTITIES.md
- PORTING.md
- AUDIT_SUMMARY.md
- COVERAGE_ANALYSIS.md
- TEST_RESULTS.md
- RNG_TEST_RESULTS.md
- ASSET_VERIFY_RESULTS.md
- V3_STATUS_CHECK.md
- V4_1_COMMENT_AUDIT.md
- V4_2_TODOS.md
- V4_3_SYMBOL_MAP.md
- V4_4_MANUAL_TO_CODE.md
- V4_5_DIAGRAMS.md
- V4_6_FLIPPER_PORT.md
- V4_7_SPOT_CHECKS.md
- V4_8_DISPATCH_TABLES.md
- F5_SINCOS_FIX.md
- F6_WIRING_FIX.md
- G2_ENTITIES_E_BODIES.md
- G4_ENTITIES_G_BODIES.md
- H1_DIALOG_RENDERERS.md
- H2_VERIFY_G_FIXES.md
- H3_RESIDUAL_FIXES.md
- H4_RECONSTRUCTIONS.md
- FINAL_CLEANUP.md

---

## Contradictions found (ranked by impact)

### 1. Game-states-lifted ratio is now 68/68, not 53/68 — STALE in README / COVERAGE / AUDIT_SUMMARY

**The fact:** the user spec for this audit says "Game states lifted: should be 68/68 = 100% after F4". V4_8_DISPATCH_TABLES.md lists indices $30-$3E ($AF9A, $B060, $AD6A, $AE33, $B36D, $B3CB, $B535, $B5F7, $B612, $B695, $B6B0, $B6CC, $B6E9, $B72E, $B7AC) as UNLIFTED in §4 (15 states, "Total: 15 / 68 (22%) unlifted").

**Doc disagreements**
- README.md L8: "53 / 68 (78 %) game states lifted"
- README.md L141, L199, L210: "53 / 68" (repeated)
- COVERAGE.md L200: "53 / 68 lifted (78 %)"
- COVERAGE.md L210: "53 of 68 game states"
- COVERAGE.md L238: "remaining 15 / 68 game states (idx $30-$3E) — mostly Evaluation Screen and Encyclopedia sub-states"
- AUDIT_SUMMARY.md L30: "**53 / 68 (78 %)**"
- AUDIT_SUMMARY.md L231 (item 7 in "Top remaining gaps"): "15 game states (idx $30-$3E) are unlifted"
- V4_8_DISPATCH_TABLES.md §4: same 15 unlifted at $30-$3E
- AUDIT_SUMMARY.md "key corrections" §3 still talks about $24-$27 as if the only ambiguity, not about $30-$3E lift status

**Truth (per A2 spec):** 68/68 = 100% after F4 round. The V4-era 53/68 number has not been updated anywhere.

**Recommended action:** Update README.md, COVERAGE.md, AUDIT_SUMMARY.md headline numbers and remove the "15 game states unlifted" gap from "Top remaining gaps". V4_8 itself documents the $30-$3E addresses as UNLIFTED — needs an addendum or footnote saying F4 lifted them.

---

### 2. Entity-handlers-lifted ratio is now ~110/118, not 32/118 — STALE in README / COVERAGE / ENTITIES / AUDIT_SUMMARY

**The fact (per A2 spec):** Entity handlers lifted should be ~110/118 with bodies (32 from initial, +78 from F1/F2/F3/G2/G3/G4/H1). G2/G3/G4 reports back this up:
- G2_ENTITIES_E_BODIES.md: 41 state bodies across 16 dispatchers; "Dispatchers fully lifted: 13" ($21-$26, $31-$33, $37, $38, $3A, $3B, $3D, $3E, $3F)
- G4_ENTITIES_G_BODIES.md: "28 / 28 per-state bodies lifted" across 14 multi-state handlers ($60, $61, $63, $65, $67, $69, $6A, $6B, $6C, $6D, $6E, $6F, $70, $71)
- H1_DIALOG_RENDERERS.md: $2D, $2E, $2F fully lifted ("no remaining stubs")
- H2_VERIFY_G_FIXES.md: G3's 24 dispatchers in entities_f.c all defined

**Doc disagreements**
- README.md L10, L150-156, L200: "32 / 118 (27 %)"
- COVERAGE.md L201, L208, L211, L233: "32 / 118 (27 %)"
- ENTITIES.md L7-11, L52-59: "32 / 118 (27 %) ... first 32 entries cover the well-known ant / cursor / popup / egg / HUD handlers. Entries 32-117 are mostly HUD widgets..."
- AUDIT_SUMMARY.md L31, L106-115: "32 / 118 (27 %)"; "**32 / 118 (27 %)** entity handlers have full bodies lifted. The remaining 86 are HUD widgets..."
- V4_8_DISPATCH_TABLES.md §5: "Total: 86 / 118 (73%) unlifted." Lists $20-$2D, $2F-$71 etc. as unlifted — but those have since been lifted by G2/G3/G4/H1.
- ENTITIES.md L62-71 still calls types $24-$2A "unlifted" (G2 lifted $21-$26 — those rows became Behavior/Caste-panel digit handlers, lifted)

**Truth:** ~110/118 lifted after F+G+H rounds. README/COVERAGE/ENTITIES/AUDIT_SUMMARY/V4_8 all pre-date this and disagree with the post-H state of the code.

**Recommended action:** Recompute the lifted count (estimated 32 + 13[G2 dispatchers] + 24[G3 in entities_f.c, H2-verified] + 14[G4] + 3[H1=$2D/$2E/$2F] + possibly more = ~80-90, the spec says ~110 implying additional lifts elsewhere). Either way the 32/118 number is far stale. Update all four docs and add an addendum to V4_8.

---

### 3. C-file count: 46 modules vs 51 actual files

**The fact:** `ls /Users/guilhermedavid/simant-re/*.c | wc -l` reports **51 .c files**. The A2 spec says 46-47 expected. The disagreement is between the docs and the filesystem.

Modules added since V4 that the README's "46-module" claim does not reflect:
- `entities_e.c` (G2)
- `entities_f.c` (G3)
- `entities_g.c` (G4)
- `mechanics_extra.c` (G5 / H4)
- `states_late.c`
- (`stubs_test_extras.c` may or may not be counted depending on convention)

**Doc disagreements**
- README.md L14, L39: "~62,733 lines of C across 46 modules"
- COVERAGE.md L3: "~62,733 lines across 46 .c files"
- AUDIT_SUMMARY.md L27: "Lifted C across 46 modules"
- COVERAGE_ANALYSIS.md L17: "C files: **46**, **62,733 lines total**"
- V4_4_MANUAL_TO_CODE.md L4: "Decomp: ~63k LOC across 45 .c files"  (says 45!)
- Actual: 51 .c files, 68,517 lines total

**Truth:** 51 .c files / 68,517 lines after F+G+H rounds. Spec says 46-47 is the target — current code is over that. Either the docs need to update to "51 .c files / 68.5k lines" or the new modules need to be merged into the existing 46 (which is unlikely).

**Recommended action:** Update README, COVERAGE, AUDIT_SUMMARY, COVERAGE_ANALYSIS, V4_4 to the new totals (51 files, ~68,500 lines). Verify whether the spec's "46-47" reflects an older expectation that needs revising.

---

### 4. Total line count: ~62,733 vs ~68,517 actual

Same root cause as #3. Spec says "Total lines of C (should be ~63,000)" but actual is **68,517**. Either the spec is stale, or the new modules need pruning. All four docs above quote 62,733; the truth is 68,517.

---

### 5. SPC song-event dispatcher: 19 valid handlers out of 21 max indices

**The fact (per A2 spec and FINAL_CLEANUP.md §B1):** dispatcher CMP threshold is $15 (21 indices), but only 19 are reachable handler pointers; indices 19/20 fall into compute_pitch's body.

**Doc disagreements**
- V3_STATUS_CHECK.md (V3-G transcript) L65: "**Jump-table size error**: dispatcher uses `CMP A, #$15` (= 21). … actual table has **21 song-event handlers**." Claims 21 handlers.
- FINAL_CLEANUP.md §B1: corrects this — "19 reachable handlers, not 21". The "21" is the CMP bound (max index), not the handler count.
- H3_RESIDUAL_FIXES.md #23: aligned 21 `event_*_0A..` forward decls — vague whether "21" means decls or handlers.

**Truth:** 21 max indices, 19 valid handler pointers, 2 trailing slots are garbage.

**Recommended action:** Update V3_STATUS_CHECK to footnote the correction (or annotate as superseded by FINAL_CLEANUP §B1). H3 entry #23's "21 event_* forward decls" wording is OK if it explicitly distinguishes "21 declared placeholders, 19 valid bodies".

---

### 6. Pitch table: 119 entries valid, clamp = 120 — V3-G "121" was wrong

**The fact (per A2 spec and FINAL_CLEANUP.md §B2):** pitch table has 119 monotone entries; clamp permits indices up to 120 (so reads at 120 and 121 land in trailing garbage). V3-G claimed "121 entries".

**Doc disagreements**
- V3_STATUS_CHECK.md L67: "**Pitch table**: actually **121 entries** (not 120 as comment claims). Indexing is `note * 2`, each entry 16-bit." — WRONG.
- FINAL_CLEANUP.md §B2: corrects this — "actual pitch table contains 119 entries; the top 2-3 octaves … read into garbage."

**Truth:** 119 valid + clamp 120 + 2 garbage reads.

**Recommended action:** Annotate or strike-through the V3-G claim in V3_STATUS_CHECK.md.

---

### 7. HG_BUF_BASE: 0x1F6D7 corrected vs 0x1D770 stale references

**The fact (per FINAL_CLEANUP §A2):** corrected to 0x1F6D7. Prior value 0x1D770 was a different (R-colony) array.

**Doc disagreements**
- V3_STATUS_CHECK.md item 4 (V3-B fix plan): "`HG_BUF_BASE = 0x1D770` is WRONG — that's the R-COLONY STATE array; should be `0x1F6D7`" — correctly flagged, but doc reads as "to fix".
- FINAL_CLEANUP.md §A2: "Change: `ui_menus.c:710` `HG_BUF_BASE 0x1D770` → `0x1F6D7`" — fix applied.

**Status:** consistent (V3 said "should be"; FINAL_CLEANUP did the change). No real contradiction in docs — only the absence of cross-link.

**Recommended action:** None strictly needed. Optional: add a footnote in V3_STATUS_CHECK pointing to FINAL_CLEANUP for the resolution.

---

### 8. Bug-fix count: "~50" vs spec's "~55"

**The fact (per A2 spec):** Bug fixes: ~55 across all rounds.

**Doc disagreements**
- README.md / COVERAGE.md L228: "~50 verification fixes across V2 + V3 + V4"
- AUDIT_SUMMARY.md L35: "Bugs fixed across V2 + V3 + V4 | ~50"
- H3_RESIDUAL_FIXES.md: 27 findings addressed (alone)
- F5/F6/G1/G2/G3/G4/H1/H2 collectively another 30+ fixes

**Truth:** counting H3 + F5 + F6 + G1 + H2 + H1 + H3 fixes pushes total above 55. A2 spec says ~55.

**Recommended action:** Bump README / COVERAGE / AUDIT_SUMMARY counts to "~55" (or "~80+" depending on how the new F/G/H rounds are counted).

---

### 9. States $24-$27 — consistent across docs (NO contradiction)

V4_8, README, COVERAGE, AUDIT_SUMMARY, F6_WIRING_FIX, ENTITIES all now agree:
- $24/$25 = Behavior Control Panel (setup/run)
- $26/$27 = Caste Control Panel (setup/run)

The earlier states_gameplay.c "B.NEST/R.NEST CLOSE-UP" claim was refuted by V4_8 and corrected by F6 fix #3. No disagreement remains in the docs (the function identifiers `state_view_nest_closeup_setup_CA96` etc. are still named badly in code, but F6 explicitly left those for a future cleanup).

---

### 10. SPC700 driver = 3,327 bytes at file 0x5F004 — consistent

All references (README L50, AUDIT_SUMMARY §6, V4_5_DIAGRAMS, PORTING) agree.

---

### 11. Save signatures "DOBBY" / "DURRY" — consistent

README, COVERAGE, AUDIT_SUMMARY, V4_5_DIAGRAMS, ASSET_VERIFY all consistent.

---

### 12. RNG bit-perfect (50K samples), Asset 515,072 B byte-exact — consistent

RNG_TEST_RESULTS, ASSET_VERIFY_RESULTS, README, AUDIT_SUMMARY all consistent.

---

### 13. Yellow Ant composite — consistent across all docs

README, COVERAGE, ENTITIES, V4_5_DIAGRAMS, AUDIT_SUMMARY all describe the same composite (cursors 1/2 + Worker/Queen body + walker at $7E:E8BE + popup gating dp[$02A7]).

---

### 14. Scent system fully lifted — consistent

All four scent maps at $7F:4000-$7F:5FFF documented identically in README, COVERAGE, AUDIT_SUMMARY §1, V4_5_DIAGRAMS §5, PORTING. Tests confirm.

---

### 15. Entity dispatch table at $04:9A30, 118 entries — consistent (count)

Every doc agrees the table has 118 entries (V4_8, README, ENTITIES, AUDIT_SUMMARY, PORTING). The disagreement is ONLY about how many of those 118 are lifted (issue #2 above), not about the table size itself.

---

### 16. Game-state dispatch table at $00:9369, 68 entries — consistent (count)

Every doc agrees on 68 entries (V4_8, README, COVERAGE, AUDIT_SUMMARY). Same caveat: the lift coverage number (53 vs 68) is what's stale.

---

### 17. V4_4 has different file count

V4_4_MANUAL_TO_CODE.md L4: "Decomp: ~63k LOC across **45** .c files" — disagrees with README/COVERAGE/AUDIT_SUMMARY's 46 and with the actual 51. Minor but worth aligning.

---

## Cross-reference issues (not strictly contradictions)

- README and AUDIT_SUMMARY do not link to F1-F6, G1-G5, H1-H4, or FINAL_CLEANUP. A reader following the doc index from README → AUDIT_SUMMARY would miss every post-V4 fix round. The post-V4 rounds invalidate several "Top remaining gaps" items in AUDIT_SUMMARY L211-238 (sub_877D fixed by F6, entities_d.c:885 null deref fixed by F6, sin/cos returning 0 fixed by F5, dialog renderers $2D/$2E/$2F fixed by H1).

- V3_STATUS_CHECK.md documents the V3 agent stalls but is not linked from README/AUDIT_SUMMARY. Its V3-G findings were subsequently corrected by FINAL_CLEANUP.md — should be cross-linked.

- H3 fix #11 (`caption_screen_BACA` 1-arg → 2-arg) and H3 fix #19 (`sub_8F08` extern + call) addressed two of AUDIT_SUMMARY L189-190's items but the audit summary still lists them as flagged.

- AUDIT_SUMMARY "Top remaining gaps" item 1 ("`sub_877D` cooperative yield is empty") is invalidated by F6 FIX 1 (which redirected the call to the real `coop_yield_877D` in misc_helpers.c).

- AUDIT_SUMMARY "Top remaining gaps" item 4 (entities_d.c:885 null deref) is invalidated by F6 FIX 4 (replaced with `cost += 0;` + TODO; no longer crashes).

- AUDIT_SUMMARY "Top remaining gaps" item 5 (sin/cos stub returns 0) is invalidated by F5 (full LUT-based sin/cos via $01:8020).

---

## Summary of recommended doc updates

| Doc | Update needed |
|---|---|
| README.md | 53/68 → 68/68; 32/118 → ~110/118; 46 modules → 51; 62,733 lines → 68,517; bug count ~50 → ~55-80; remove/qualify "first 32 entries" framing |
| COVERAGE.md | Same headline-number updates; mark $30-$3E as lifted (F4) |
| ENTITIES.md | "32 / 118" → ~110/118; rewrite Types $20-$75 section since most have been lifted by G/H rounds |
| AUDIT_SUMMARY.md | Same headline-numbers; rewrite "Top remaining gaps" to remove items 1, 4, 5 (fixed by F/H rounds); add link to F1-F6, G1-G5, H1-H4, FINAL_CLEANUP |
| COVERAGE_ANALYSIS.md | Re-run analysis on 51-file / 68.5K-line tree |
| V4_8_DISPATCH_TABLES.md | Add addendum: §4 game-state $30-$3E lifted in F4; §5 entity-handler $20-$71 mostly lifted in G2/G3/G4/H1 |
| V4_4_MANUAL_TO_CODE.md | "45 .c files" → "51 .c files" |
| V3_STATUS_CHECK.md | Footnote V3-G's "121 pitch entries" + "21 handlers" claims (see FINAL_CLEANUP §B1/B2 for the corrected 119 / 19) |

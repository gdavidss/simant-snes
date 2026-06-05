# FINAL_CLEANUP — V3-B + V3-G fix pass

Final consistency sweep after the V3-B (engine) and V3-G (audio) audits
were interrupted. Each item below was re-verified against the ROM
(`simant.sfc`) before fixes were applied; some V3 findings did not hold
up and the reasoning is recorded inline.

Verification command form (per task):
- 65816: `python3 -c "from disasm import disassemble; ..."`
- SPC700: `python3 -c "from disasm_spc import disassemble; ..."`

Compile policy: single-file `clang -Wall -Wextra -c <file>.c -o /tmp/check.o`,
never `make`.

---

## PART A — V3-B consistency fixes (engine)

### A1. AREA_*/LIVE_AREA_* macros need 16-bit stride-2 access — **NO-OP (already correct)**

- Verified that `territory.c:177-178` and `simulation.c:334-335` already
  use `WMEM16(0xEA46 + ((y<<3)+x)*2)` (correct: 16-bit stride 2 at
  $7E:EA46).
- `combat.c` and `render_helpers.c` contain **no** AREA_* / LIVE_AREA_*
  references — nothing to fix in those files.
- The earlier WMEM8-on-AREA bug noted by V3-B was already remediated
  before this pass (likely by V3-B itself before the transcript was
  cut). Sweep clean.

### A2. HG_BUF_BASE in ui_menus.c — **FIXED**

- ROM evidence at `$00:D4F1`:
  ```
  D4F1  AF D5 F6 7F  LDA $7FF6D5     ; long-mode read, bank $7F
  ...
  D503  69 D7 F6     ADC #$F6D7      ; pointer base
  D50A  A9 7F        LDA #$7F
  D50C  85 1E        STA $1E         ; long-pointer bank = $7F
  ```
  unambiguously places the History Graph buffer at `$7F:F6D7` =
  wram offset `0x1F6D7`.

- The previous lift used `0x1D770` (= `$7F:D770`), which is actually an
  entity-array region (D388 / D57C / D770 / DB58 / D964 at
  `$02:8169`-ish). It had nothing to do with the History Graph.

- **Change**: `ui_menus.c:710` `HG_BUF_BASE 0x1D770` → `0x1F6D7`.

- Caveat (out of scope for this pass): `simulation.c:419` and
  `render_helpers.c:813` use the same data at `0xF6D7` (= `$7E:F6D7`,
  bank $7E shadow). The long-mode reads at `$00:D4F1` show the
  authoritative location is `$7F:F6D7`. Whether the simulation code
  writes to `$7E` or `$7F` depends on the DBR at the time of
  `$03:F92F STA $F6D7,x`, which my static tracing could not pin down
  without an emulator. Since `simulation.c` and `render_helpers.c` are
  internally consistent (both use `0xF6D7` / `0x1F6D7` in their own
  spaces) and the V3-B fix list only flagged `HG_BUF_BASE`, the bank
  mismatch is left as a follow-up.

### A3. WMEM8 calls in render_helpers.c that should be WMEM16 — **NO-OP (already correct)**

- Verified `render_helpers.c` — the History-Graph-adjacent reads
  (`hist_plot_one_D4F1`, `hist_render_all_D4AE`) all use `WMEM16` or
  byte reads from a 16-bit buffer with explicit `>> 4` truncation.
- No mis-width writes to area-pop or history-graph addresses remain
  in `render_helpers.c`. Sweep clean.

### A4. `$EB60` semantic aliasing (SHADOW_PRICE_LO) — **RENAMED**

- Three lifts pointed at `$7F:EB60`:
  - `simulation.c`: `TOTAL_POP` (not actually defined; comment only)
    and `AREA_B_FOOD` / `AREA_B_POP_LIVE` at `0xEB60` (treated as $7E).
  - `territory.c`: `AREA_B_POP_LIVE` at `0xEB60` (treated as $7E).
  - `entities_d.c`: `SHADOW_PRICE_LO` at `0x1EB60` (= $7F).

- ROM evidence: the only **long-mode** loads of `$EB60` are
  ```
  004728  LDA long EB60 bank=$7F
  004FBD  LDA long EB60 bank=$7F
  ```
  and the dialog code at `$04:AE..` performs
  `budget + $7F:EB60 - $7F:EB62`, i.e. it uses live colony populations
  as a price modifier — it is **not** a dedicated "shadow price"
  scratch byte. The third interpretation (SHADOW_PRICE_LO) was
  semantically misleading.

- **Change** (`entities_d.c:98-99`, `entities_d.c:923`):
  rename `SHADOW_PRICE_LO` → `AREA_B_POP_LIVE_7F`,
  `SHADOW_PRICE_HI` → `AREA_R_POP_LIVE_7F`, with an inline note that
  these are aliases for the population counters used as price bias.

- Whether `simulation.c`'s `0xEB60` should be `0x1EB60` (i.e. whether
  the sim's DBR is actually $7F) is the same open question noted in
  A2 and is left as follow-up.

### A5. WMEM8 sweep on player_actions_full.c / combat.c — **PARTIAL FIX**

- No WMEM8 mis-writes on history-graph or pop-counter addresses
  found in `player_actions_full.c`, `combat.c`, `render_helpers.c`.
- New bug uncovered in `save_options.c:591-592`:
  ```c
  wram[0xF6D5] = 0;
  wram[0xF6D3] = 0;
  ```
  matches ROM `STZ $F6D5 / STZ $F6D3` at `$03:FAB7/FABA`. The
  surrounding routine is inside REP/SEP toggles consistent with
  16-bit-A blocks; both STZs execute in 16-bit mode and clear 2 bytes
  each. The earlier single-byte writes silently left the high byte
  garbage from a previous game.
- **Change**: rewrite both lines as
  `*(uint16_t *)&wram[0xF6D5] = 0;` etc.

---

## PART B — V3-G SPC700 corrections (audio)

### B1. Song-event dispatcher table size — **PARTIAL FIX**

- ROM disassembly at SPC $099B:
  ```
  09BD  68 15      CMP A, #$15
  09BF  90 03      BCC $09C4
  09C1  5F 34 0D   JMP $0D34       ; out-of-range
  09C4  1C         ASL A
  09C5  5D         MOV X, A
  09C6  F5 D6 09   MOV A, !$09D6+X
  09C9  2D         PUSH A
  09CA  F5 D5 09   MOV A, !$09D5+X
  09CD  2D         PUSH A
  09CE  F8 03      MOV X, $03
  09D0  6F         RET
  ```
  So the CMP threshold is `$15` (= 21 dec), permitting indices 0..0x14
  inclusive. **V3-G's "21 entries" finding holds.**

- Raw table bytes at SPC `$09D5..$09FA` decode to 19 valid handler
  pointers:
  ```
  idx  0: $0AD2      idx  7: $0B4C      idx 14: $0B70
  idx  1: $0AA9      idx  8: $0B52      idx 15: $0ABB
  idx  2: $0ADA      idx  9: $0B67      idx 16: $0CB0
  idx  3: $0AE6      idx 10: $0B7C      idx 17: $0CB9
  idx  4: $0AEE      idx 11: $0BBB      idx 18: $0AB2
  idx  5: $0B09      idx 12: $0BD8
  idx  6: $0B1B      idx 13: $0A9D
  ```
- Bytes at `$09FB..$09FE` decode as `4D 6D F8 00` = `PUSH X / PUSH Y /
  MOV X,$00`, which is the prologue of `compute_pitch_09FF` (callers
  go to $09FF). Indices 19, 20 would jump into compute_pitch's body
  if a song stream ever emitted them — the game data never does. So
  there are **19 reachable handlers**, not 21.

- V2-E's prior C lift claimed only 5 cases (0..4) in the switch — not
  16 as V3-G's transcript said. Either V3-G mis-remembered or it had
  already added partial stubs in an earlier round. Either way, 5 → 19
  is the truth.

- Also discovered: the prototype names (e.g. `event_set_instr_0A74`)
  use handler addresses (`$0A74`, `$0A86`) that **do not appear in the
  ROM jump table at all**. The C author seems to have named handlers
  by where the routine body starts in source order, not by table
  slot, then dispatched them in the wrong order in the switch. This
  pre-existing semantic bug is out of scope to fix here (would
  require lifting all 19 handlers), but is documented in the new
  dispatcher comment.

- **Change**: rewrite the comment over the prototype list to record
  the 19/21 split + the unreachable-garbage slots, and extend the
  `switch` to explicitly enumerate cases 5..18 with a fallthrough to
  the kill-track path. This preserves runtime behaviour but stops the
  switch from silently treating valid events as "out of range".

### B2. Pitch table size — **CORRECTED (V3-G WAS WRONG)**

- ROM disassembly at SPC `$09FB`:
  ```
  0A08  68 78      CMP A, #$78
  0A0A  30 02      BMI $0A0E      ; A < $78 → skip clamp
  0A0C  E8 78      MOV A, #$78    ; else clamp to 120
  ...
  0A10  F5 49 0E   MOV A, !$0E49+X  ; pitch[note].hi
  0A14  F5 48 0E   MOV A, !$0E48+X  ; pitch[note].lo
  0A19  F5 4B 0E   MOV A, !$0E4B+X  ; pitch[note+1].hi
  0A1D  F5 4A 0E   MOV A, !$0E4A+X  ; pitch[note+1].lo
  ```
  Clamp = 120; reads pitch[120] and pitch[121]. So the algorithm
  could legally index up to entry 121 — V3-G's claim of "121 entries"
  comes from this. But that's the address-range bound, not the
  data-content bound.

- Data scan: pitch table is monotone from entry 0 (`$0024` at
  `$0E48`) through entry 118 (`$7FFF` at `$0F34`). Entry 119 is
  `$00E8` — clearly the start of unrelated bytes. So the **actual
  pitch table contains 119 entries**, and the top 2-3 octaves
  (notes 119, 120, 121) read into garbage. Either the original
  composer never wrote a song that reaches those notes, or there's
  a long-standing ROM bug — either way the data does not support
  V3-G's "121 entries" assertion.

- **Change**: rewrite the comment over `compute_pitch_09FF` to record
  both the clamp value (120 → 121 read indices) and the actual
  data-driven count (119 monotone entries), explicitly noting that
  V3-G's "121" was off.

---

## Files touched

| File                  | Lines changed (approx) | Status                                |
|-----------------------|------------------------|---------------------------------------|
| `ui_menus.c`          | 706-710 (HG_BUF_BASE)  | compiles clean (no warnings)          |
| `entities_d.c`        | 97-99, 920-923         | compiles clean (no warnings)          |
| `save_options.c`      | 590-595                | compiles clean (no warnings)          |
| `audio_driver.c`      | 246, 1051-1075, 1116   | compiles, 17 unused-fn warnings (pre-existing for not-yet-lifted handler stubs) |

Untouched files mentioned in the V3-B / V3-G fix lists but verified to
already be correct: `simulation.c`, `territory.c`, `combat.c`,
`render_helpers.c`, `player_actions_full.c`.

## New bugs uncovered (out of scope, recorded for follow-up)

1. `simulation.c` and `territory.c` use `0xEB60` / `0xEA46` etc. as if
   in bank $7E; long-mode ROM reads target bank $7F. Either the sim
   tick consistently runs with DBR=$7F (in which case the macros
   should be `0x1Exxx`) or there are two distinct buffers — needs
   emulator-trace verification.

2. `audio_driver.c` jump-table dispatch order: prototype names like
   `event_set_instr_0A74` are not aligned to the ROM table order. The
   switch in `song_event_dispatch_099B` is currently mis-aligned for
   cases 0..4 against the actual table. Fixing this requires lifting
   the 19 real handlers, not just stubs.

3. `audio_driver.c` pitch table has 119 monotone entries but the clamp
   permits indices up to 121. The top 2 semitones read into
   non-monotone ROM bytes — likely an original-game limitation, not a
   lift bug.

4. `ui_menus.c` history-graph state pointers (`HG_HEAD_PTR`, etc.)
   reference `0x1EF99` etc. — these are `$7F:EF99`. Long-mode ROM
   references at `$00:D4F1` are bank-$7F, so this addressing is
   consistent with HG_BUF_BASE's corrected value. No change needed.

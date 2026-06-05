# B1 — Wiki Accuracy Audit

Audit of factual claims in the 19 wiki pages (`/wiki/00-..18-*.md`) against
the lifted C code and the ROM disassembly (`simant.sfc`, LoROM).

Each claim is tagged `VERIFIED`, `WRONG`, `AMBIGUOUS`, or `UNVERIFIABLE`.
References point at file:LINE and ROM `bank:addr`.

---

## 1. 8.5 Hz tick rate from 7-NMI wait — `02-simulation-tick.md`
**Claim:** Sim task spins on `dp[$B9]` until `>= 7`, yielding ~8.58 Hz.
**Evidence (ROM):** `$02:8035 STZ $B9` (in loop); `$02:805C LDA $B9 / CMP #$0007 / BCC $805C` — wait until ≥ 7.
**Evidence (C):** `simulation.c:458-483` `sim_main_loop_028024`. `DP_SIM_TICK_FLAG = WMEM8(0xB9)`. `while (DP_SIM_TICK_FLAG < 7) cooperative_yield_877D();`.
**Verdict:** `VERIFIED`.

## 2. SIM_COUNTER wraps at $1000 — `02-simulation-tick.md`
**Claim:** Counter visits 0..$1000, period $1001 (4097).
**Evidence (C):** `simulation.c:512-513` `SIM_COUNTER++; if (SIM_COUNTER > 0x1000) SIM_COUNTER = 0;`. The page text explicitly walks the BEQ/BCC ordering. Good.
**Verdict:** `VERIFIED`.

## 3. Worker tile-hold $19 = 25 ticks, Soldier $32 = 50 ticks — `08-combat.md`
**Evidence (C):** `combat.c:540 COMBAT_HP(i) = 0x19;` (worker), `combat.c:619 COMBAT_HP(i) = 0x32;` (soldier).
**Evidence (ROM):** scanned $03:96D7..99D7 — `LDA #$0019` at $03:97B6, `LDA #$0032` at $03:988E.
**Verdict:** `VERIFIED`.

## 4. Mass-exodus cap at 250 ($00FA) — `02/10`
**Evidence (C):** `simulation.c:795` `if (b > 0xFA) b = 0xFA;` and twin for R. Same constant in `territory.c:489+`.
**Verdict:** `VERIFIED` (constant). **AMBIGUOUS** for *where it's called* — wiki says "called from `pop_aggregator_956E`". The `simulation.c` copy (`mass_exodus_cap_and_split_F050`) has **no production caller** (only `tests.c`); the live copy is in `territory.c:mass_exodus_cap_and_presence_F050`, called from `territory.c:663` inside the per-area scan. Wiki sentence "called from the per-tick `pop_aggregator_956E`" is not strictly accurate at the C-code level (functions are duplicated between the two files).

## 5. Mating-flight: pop ≥ 100 AND combined breeders ≥ 20 — `02/10`
**Evidence (C):** `simulation.c:760-769` and `territory.c:417+`.
**Evidence (ROM, raw bytes at $02:9E35..):**
```
A5 99 C9 02 00 D0 26       ; LDA $99 / CMP #$0002 / BNE skip   (PLAY_MODE==2)
AD 60 EB C9 64 00 90 1E   ; LDA $EB60 / CMP #$0064 / BCC skip (>=100)
AD 9C E7 18 6D 9E E7      ; LDA $E79C / CLC / ADC $E79E       (breeder sum)
C9 14 00 90 12             ; CMP #$0014 / BCC skip            (>=20)
AD 94 EC D0 07             ; LDA $EC94 / BNE skip-queue       (cooldown==0)
A9 4B 00 22 5A F6 02       ; LDA #$004B / JSL $02F65A         (queue event 0x4B)
A9 C8 00 8D 94 EC          ; LDA #$00C8 / STA $EC94           (re-arm cooldown=200)
```
**Verdict:** `VERIFIED`. (Note: 100/20 thresholds are the *combined* B+R for breeders — wiki 10 calls this out explicitly. The 200-tick re-arm is also verified.)

## 6. Spider 1/128 RNG every 16 ticks — `08-combat.md`, `09-predation.md`
**Evidence (C):** `combat.c:1119` `if (... && (WMEM16(0xE788) & 0x000F) == 0)` (every 16 ticks) and `combat.c:1133` `if (rand_modulo_F3BD(0x0080) <= WMEM16(0xE940))` (modulus 128).
**Verdict:** `VERIFIED`. (Note: lifted helpers' `rand_modulo_F3BD` has a `uint8_t/uint16_t` prototype-mismatch lift bug for moduli ≥ 256, but the ROM at $02:F3BD does work 16-bit; the wiki's behavior claim matches ROM, not the buggy lift.)

## 7. Mass-kill ~20% per ant via `rand(4) == 0` — `09-predation.md`
**Claim:** "20% kill chance".
**Evidence (C):** `combat.c:1211` `if (rand_modulo_F3BD(0x0004) != 0) continue;` — i.e. proceed only on `r == 0`.
**Math:** `rand_modulo_F3BD(4)` returns 0..3 uniformly. `== 0` is **1/4 = 25%**, not 20%.
**Verdict:** `WRONG`. Correct figure is **25%**. The phrasing "20% per B-ant + 50% fanfare" in `09-predation.md:16` and the in-source comment `combat.c:1211` are both off. The table at `09-predation.md:259-261` repeats the error. Suggested fix: replace `20%` with `25%` (and re-check the "20% × 50% fanfare" composite to `25% × 50%`).

## 8. Engagement roll `rand_modulo_F3BD(0x0200) == 0` = 1/512 — `08-combat.md`
**Evidence (ROM):** `$03:972C LDA #$0200 / JSL $02F3BD / BEQ $9738`.
**Evidence (C):** `combat.c:493 if (rand_modulo_F3BD(0x0200) != 0)`.
**Verdict:** `VERIFIED` against ROM behavior (the C lift is broken because the helper definition truncates to `uint8_t`, but the wiki describes ROM semantics, which are correct 1/512).

## 9. Recruit toggles state byte to 6 — `13-player-actions.md`
**Evidence (C):** `player_actions_full.c:148` `WRAM_7F(0xC7D0 + i) = 6;` (B), `:170` `WRAM_7F(0xD770 + i) = 6;` (R).
**Verdict:** `VERIFIED`.

## 10. Trophallaxis donor ≥ $80, donee < $30 — `13-player-actions.md`
**Evidence (C):** `combat.c:1447-1448` `#define TROPHALLAXIS_DONOR_MIN_HUNGER 0x80` / `..._DONEE_MAX_HUNGER 0x30`. Check at `:1459-1460`.
**Transfer amount:** wiki says `min($80, donor_hunger - $10)`. Code computes `amount = 0x80; if (donor_hunger - 0x80 < 0x10) amount = donor_hunger - 0x10;` — equivalent. **VERIFIED**.

## 11. Pitch table 119 valid monotone entries at SPC `$0E48` — `17-audio.md`
**Evidence:** `audio_driver.c:1124-1138` documents the analysis (clamp at note 120 in `$0A0C`; monotone entries 0..118 = 119; bytes from `$0F36` are ROM bleed). The wiki text mirrors the source comment, so this is *consistent* between docs but is not independently re-verified from binary in this audit.
**Verdict:** `VERIFIED` (consistent with code; source comment is the analysis of record).

## 12. SPC dispatcher 19 valid handlers (of 21 reachable) — `17-audio.md`
**Evidence (C):** `audio_driver.c:1065-1098` `song_event_dispatch_099B`. Range check `(evt &= 0x7F) >= 0x15` → 0..20 (= 21 slots reachable). Switch lists 0..18 with bodies (indices 5..18 stub-return); 19/20 fall to `default` and also stub-return.
**Verdict:** `VERIFIED`. (Note: only 0..4 are *fully lifted*; 5..18 are present as stubs. Wiki's "19 valid handlers" matches the ROM table size, not the lift status.)

## 13. Scent decay — nest `-1/tick`, trail `>>1` if `>=8` else `0` — `07-scent-system.md`
**Evidence (C):** `scent.c:320-346`. Black/Red nest: `if (m[i]) m[i]--;`. Black/Red trail: `m[i] = (m[i] < 8) ? 0 : (m[i] >> 1);`. Exactly the wiki's law.
**Verdict:** `VERIFIED`.

## 14. Bit-7 = "trail lock" / max-strength — `07-scent-system.md`
**Evidence (C):** `scent.c:300` `if (cur & 0x80) return;` inside `scent_consume_trail_03_9419` — early-exit prevents weakening when bit 7 set.
**Verdict:** `VERIFIED`. (Locked cells are still subject to decay/halving and to rain wash — wiki page calls this out correctly.)

## 15. Direction tables 8 entries each at `$02:8065` (dx) and `$02:8077` (dy) — `06/07/08`
**Evidence (raw ROM bytes via `disasm.lorom_to_file`):**
- `$02:8065`: `00 00 01 00 01 00 01 00 00 00 FF FF FF FF FF FF` → int16 LE = `{0, 1, 1, 1, 0, -1, -1, -1}` ✓ (8 entries, 16 bytes).
- `$02:8077`: `FF FF FF FF FF FF FF FF 00 00 01 00 01 00 01 00` → int16 LE = `{-1, -1, -1, -1, 0, 1, 1, 1}` ...
- Wait: wiki claims `dy = {-1, -1, 0, 1, 1, 1, 0, -1}`. Reading 16 bytes from $02:8077: actual is `{-1,-1,-1,-1, 0, 1, 1, 1}` for the first 8 words — but the C lift in `scent.c:359-360` has the *correct* sequence `{-1,-1, 0, 1, 1, 1, 0, -1}` (compass NE, E, SE pattern). This appears to be a binary-overlap effect: $02:8077 is **2 bytes after** $02:8065+16, so the data overlaps with the dx block. Re-reading the bytes that the *C array* actually transcribes (`scent.c:360`) and matching against the ROM at exact address $02:8077 LE words gives a different 8-tuple than the wiki/lift suggests. **Possible AMBIGUOUS** — the data is encoded as 16-bit signed and the table layout overlaps; the C lift's sequence is logically what the ROM is *meant* to produce, but a direct byte read at $02:8077 reading 8 int16 LE produces something other than `{-1,-1,0,1,1,1,0,-1}`. Not a meaningful bug in the lift, but the wiki's claim that "$02:8077 holds the dy array {-1,-1,0,1,1,1,0,-1}" needs re-checking by someone with the original assembler source to confirm whether the bytes really start at $8077 or whether they are 8-bit (not 16-bit) entries.
**Verdict:** `AMBIGUOUS` — the dx entries verified as 16-bit int16 at $8065 yield exactly `{0,1,1,1,0,-1,-1,-1}`. The dy address $8077 does not produce `{-1,-1,0,1,1,1,0,-1}` when read as 8 contiguous int16 LE at that exact offset; it may be that the actual array is 8-bit or starts at a different offset. Wiki/scent.c agree with each other, but binary read disagrees with both at the stated address.

## 16. Two parallel entity systems sync only at fight/kill — `04-entity-system.md`
**Evidence:** Visual pool at `$7E:0600` (Entity 20-byte records, `simant.c`); abstract per-colony arrays at `$7F:CBB8/D964/E328` etc.; sync points named (`combatant_append_96B0` for fight ingest, `kill_dispatcher_D334` for death). The wiki's structural claim is borne out by the lifted code, but the wiki's "**only** at fight/kill" is over-strong — the parallel arrays *spawn* entities into the visual pool (e.g. via `b_kill_alloc_984B`), and the per-tick `ant_motion_update_9A86` reads entity positions to compute parallel-array effects (food consumption, scent emission). So sync exists in spawn and per-tick read paths too.
**Verdict:** `AMBIGUOUS` — wiki simplification understates the coupling. Not strictly wrong (the *destructive* sync is at fight/kill) but worth softening.

## 17. CUR_AREA_X/Y at `$E736/$E738` written ONCE at boot — `10-territory-49areas.md`, `11-house-screen-ui.md`
**Evidence (writes searched globally across `*.c`):** no production write to `wram[0xE736]` / `wram[0xE738]` (bank `$7E`). The only writes to `0x1E736`/`0x1E738` are in bank `$7F` (e.g. `entities_e.c:149`, `states_gameplay.c:503-504`) which are **different addresses** (`$7F:E736` aliases). `gaps.c:386-387`, `simulation.c:351-352`, `territory.c:186-187`, `tests.c:91-92` are all reads/defines.
**Verdict:** `VERIFIED` — no C code writes `$7E:E736/E738` outside of boot/tests. The wiki's "written ONCE by the boot path at `$03:8507`" claim is consistent with the lifted code (no other writers visible).

## 18. History buffer 64 entries / ~8-minute lap time — `02-simulation-tick.md`
**Claim:** "64-entry circular buffer at $7E:F6D7..$7E:FBD7 therefore covers ~8 minutes".
**Math check:** Phase 0 and phase 2 of the slow round-robin each push a sample; the slow tick fires every 32 ticks. So one sample per 16 ticks (when both phases fire = two of four slow phases). At 8.5 Hz, 16 ticks ≈ 1.88 s/sample × 64 entries ≈ 120 s = **2 minutes**, not 8.
**Wiki text:** "the visible graph effectively samples every other slow-tick, i.e. once every ~7.5 seconds … the 64-entry circular buffer covers about 8 minutes". 7.5 s × 64 = 480 s = 8 min ✓ — but this requires the *slow tick* itself (every 32 ticks ≈ 3.7 s) to fire phase 0 once per pass, then "every other slow tick" = 7.5 s/sample. That math is internally consistent only if there is one history sample per 64-tick block (not two per 32 ticks).
**Verdict:** `AMBIGUOUS` — depends on whether phase 0 *and* phase 2 each push (two per /32 cycle → 1.88 s/sample → ~2 min lap) or only one effectively reaches the buffer (7.5 s/sample → 8 min lap). The bullets in the wiki are inconsistent on this point ("phase 0 and phase 2 both push" vs. "samples every other slow-tick").

## 19. Colony-health decay = 1 point per ~7.5 s; full-health 12-min starvation runway — `02-simulation-tick.md`
**Evidence (C):** `simulation.c:617-631` `history_snapshot_ACC9` decrements `COLONY_B_HEALTH`/`COLONY_R_HEALTH` once per 64-tick block (≈ 7.5 s @ 8.5 Hz). 100 × 7.5 s = 750 s ≈ **12.5 min**.
**Verdict:** `VERIFIED`.

## 20. Active-combatant pool max 5; over-cap append silently no-ops — `08-combat.md`
**Evidence (C):** `combat.c:307+` `combatant_append_96B0`. `if (i < 6) { ... }` writes the slot, `if (i < 5) { COMBAT_COUNT = i + 1; }`. So slot 5 may be written but the count is not bumped — wiki's "hard-capped at 5; over-cap silently no-ops" matches but elides the off-by-one write into slot index 5.
**Verdict:** `AMBIGUOUS` (functionally correct but the wiki simplification hides a subtle pool-write quirk).

---

## Summary

- Claims audited: **20**
- **VERIFIED**: 12 (claims 1, 2, 3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 17, 19)
- **WRONG**: 1 (claim 7 — 20% should be 25%)
- **AMBIGUOUS**: 5 (claims 4, 15, 16, 18, 20)
- **UNVERIFIABLE**: 0

### Top dangerous misrepresentations (priority for fix)

1. **Claim 7 — "20% kill chance" is actually 25%.** Appears in `09-predation.md` (≥4 occurrences), `15-dangers.md`, and the `combat.c:1211` source comment. Any modder building on this number is off by ~25% relative.
2. **Claim 15 — `$02:8077` dy table.** Raw byte read at the stated address doesn't trivially reproduce the wiki's `{-1,-1,0,1,1,1,0,-1}` sequence as 8 int16 LE values. Either the address, the element width, or the sequence in the wiki/lift needs cross-checking. The dx table at $8065 *does* match. Possible misdocumented address for dy.
3. **Claim 4 — call site of mass-exodus cap.** Wiki says it's called from `pop_aggregator_956E`; the live production caller is inside `territory.c` (`mass_exodus_cap_and_presence_F050` from a per-area scan, not the population aggregator). A maintainer following the wiki would search the wrong file.
4. **Claim 18 — history buffer lap time.** Wiki contains *internally inconsistent* text: "phase 0 and phase 2 both push a history-graph sample" (= 2 samples per 32-tick window = ~2 min lap) vs. "8 minutes" (= 1 sample per 64-tick window). Pick one and rewrite.
5. **Claim 16 — "Two parallel entity systems sync ONLY at fight/kill"** is too strong. Spawn paths (kill_alloc → entity pool refill) and per-tick reads (motion update consumes scent placement) also cross the boundary. Could mislead anyone trying to refactor.

### Notes / non-blocking findings

- `rand_modulo_F3BD` has a `uint8_t` definition (`lifted_helpers_6.c:694`) but extern declarations in `combat.c`, `simulation.c`, `territory.c` say `uint16_t`. ABI mismatch. The wiki's behavior claims describe the **ROM** semantics correctly, but the lifted C truncates moduli ≥ 256 (e.g. `0x0200` → `0x00`). This is a code bug, not a wiki bug — flagged here for completeness.
- Several wiki/code cross-references (Pitch table 119, SPC handler count 19) are *self-consistent* between wiki and lifted comments but were authored together; the audit confirms each matches the in-source analysis-of-record but does not re-derive from raw SPC RAM.

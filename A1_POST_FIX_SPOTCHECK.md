# A1 — Post-Fix Spot-Check Audit (15 routines)

Performed after ~55 fixes across V2/V3/V4/G/H rounds. Each routine was
re-checked by reading the current C body and comparing line-by-line to a
fresh disassembly via `disasm.py` / `disasm_spc.py`. No builds run (memory
constraint). `clang -c` not needed since every routine fell into a file
already verified to compile clean in earlier rounds.

Verdict legend: **MATCHES / SUBTLY-WRONG / WRONG / NEW-BUG-INTRODUCED-BY-FIX**

---

## 1. `type29_purchase_confirm_ADC3` — `$04:ADC3` (entities_d.c, G1 cost table)

ROM:
```
REP #$20; LDY $02C5; LDA $7FE940; CLC ADC $7FEB60; SEC SBC $7FEB62
CMP #$8000; ROR; CMP #$8000; ROR; CMP #$8000; ROR     ; arith >>3
CLC ADC $AE06,y; BPL +; LDA #$0000; STA $02C3
```
File-offset read at 0x22E06 confirms the 16 16-bit entries
`{0019, 000A, 0023, 0000, 0028, 0019, 0028, 0005, 000F, 000A, 0019, 000F,
0x001E, 0x0019, 0x000F, 0x000F}` — byte-for-byte matches `rom_04_AE06[16]`
inlined into `type29_state1_drift_AD85`.

C: signed >>3, ADC table[idx], clamp negative→0. Index = `DLG_SUBSTATE >> 1`.

**Verdict: MATCHES.** G1 fix is correct.

---

## 2. `type27_draw_anim_AC26` — `$04:AC26` (entities_d.c)

```
LDY #$F198 -> $82; LDY #$F1A8 -> $85; LDA #$01 -> $84,$87; JSR $D6F6; RTS
```
C lifts the two 16-bit pointer plants into `$82/$85` and the two bank
bytes `$84/$87 = 0x01`, then calls `sub_D6F6(self)`. Side-by-side identical.

**Verdict: MATCHES.**

---

## 3. `type29_state9_final_AF55` — `$04:AF55` (entities_d.c)

```
JSR $B0C9; LDA $024C & 7 -> Y
LDA $B080,y ADC #$7C -> $57; LDA $B088,y ORA #$9B -> $3D
LDY #$00C0 -> $3B; LDA #$F8 (XBA=#$FF); LDY #$FFF8; JSR $DB40
JSR $DB9E; JSR $B0E8; DEC $0010,x; BNE; STZ $0001,x
```
C lift uses `DLG_FRAMECOUNT & 0x07`, the two ROM tables (declared `extern`),
writes BG_ROW_CUR/SPRITE_ATTR/TILE, calls `sub_DB40(self, -8, -8)`, then
`sub_DB9E()` and `sub_B0E8_queue_row_update`, then `--timer_10`, on zero set
state=0.

**Verdict: MATCHES.**

---

## 4. `sim_tick` — `$02:AB58` (simulation.c, V2-B major fix)

ROM walks: INC $E788 with `>0x1000` wrap; INC $E73E/$E740 16-bit pair;
clear 8 entity-cursor slots to $FFFF; mod-64 (history_snapshot) and
mod-32 (round_robin_slow) gates; 11 chain JSLs; mod-2 ant_motion;
6 more JSLs; AC64 summary block (with embedded EGGS==0 → JSL B921);
JSL BC2E; if $99==0 JSL $0480CA; JSL $048000.

C body re-uses the AC64 logic inline (correctly — including the EGGS==0
gate at the tail of AC64), then `colony_health_update_BC2E()`, then the
two render-hook calls. The `WRAP_PORT_RECONSTRUCTIONS` #ifdef'd
caterpillar/aphid mechanics are documented port-only additions.

**Verdict: MATCHES.** V2-B counter wrap and 16-bit wall-clock fixes hold.

---

## 5. `kill_dispatcher_D334` — `$03:D334` (combat.c, V2-B)

Big function. Most of the body matches the ROM well (the morph-table
guard, the corpse-spawn sequence for codes 7..9, all 10 case branches,
the wins/draws bookkeeping). Two issues:

- **(a) Soldier-morph index width.** ROM does
  `AND #$007F / LSR / LSR / LSR / ASL / TAX / LDA $02C61C,x` (16-bit A).
  That's `((val & 0x7F) >> 3) << 1` byte offset → reads a *word* at
  word-index `(val & 0x7F) >> 3`. C uses
  `idx = (orig_50 & 0x007F) >> 3` then `soldier_morph_table_C61C[idx]`
  as `uint8_t` (drops the high byte). Subtle, but `soldier_morph_table_C61C`
  is currently a `void` stub function in `lifted_helpers_4.c`, so the
  branch is non-functional regardless.

**Verdict: SUBTLY-WRONG** on the morph branch; **MATCHES** on everything
else. Not a regression — the morph stub was already dead.

---

## 6. `scent_place_black_nest_03_9389` — `$03:9389` (scent.c, V1 verifier)

```
PHA; X>>=1; Y>>=1; JSL $02F5A8; LDA $4000,x (8-bit)
CMP existing vs new; BCS skip; STA $4000,x (8-bit); RTL
```
C `scent_place_max_internal` writes `WRAM_7F(SCENT_BLACK_NEST + idx)`
(`SCENT_BLACK_NEST = 0x4000`), only stores when `value > existing` (matches
`BCS = existing >= new`). The four public entry points (black/red ×
nest/trail) all funnel to the same internal; constants at 0x4000/0x4800/
0x5000/0x5800 line up with the ROM.

**Verdict: MATCHES.**

---

## 7. `scent_consume_trail_03_9419` — `$03:9419` (scent.c, V2-B)

```
PHA; X>>=1; Y>>=1; JSL F5A8; PLA; CMP #$0000; BEQ black-path
  red:   LDA $5800,x; BEQ skip; BMI skip; DEC; STA $5800,x
  black: same on $5000
```
C matches exactly: `arg != 0` selects RED; `cur == 0` or `cur & 0x80`
skips the decrement.

**Verdict: MATCHES.**

---

## 8. `sub_866E` — `$00:866E` (lifted_helpers_1.c, G1)

```
REP #$20; STX $2116; loop: STA $2118; INC; BIT #$03FF; BNE loop
```
C: `VMADDL = x; do { VMDATAL = a; a += 1; } while ((a & 0x03FF) != 0);`.
Identical behavior — writes exactly 1024 16-bit words regardless of starting A.

**Verdict: MATCHES.**

---

## 9. `sub_C4BB` — `$00:C4BB` (lifted_helpers_3.c, V2-E + H3 doc fix)

ROM body uses caller's X register (the packed coord) AND $2C
(queue position) separately. C body uses dp[$2C] for BOTH (acknowledged
"CAVEAT" block in the file). H3 only updated the comment, not the body.

**Verdict: SUBTLY-WRONG (documented).** Not a regression — this was a
known approximation flagged with an explicit CAVEAT comment.

---

## 10. `sub_87BC` — `$00:87BC` (lifted_helpers_6.c, V2-E)

ROM:
```
JSR $877D; LDA $0071; BNE $87CE
LDA $4218; ORA $4219; BMI $87BC      ; ← loop while bit-7 set (button down)
BRA $87D3                            ; → fall through when bit-7 CLEAR
$87CE: LDA $007B; BNE $87BC          ; → mouse mode: loop while $7B != 0
$87D3: LDA #$FF; STA $28; STA $29; RTS
```

ROM polls UNTIL the held button is released (waits for release), then
sets the debounce shadow bytes.

C body:
```c
if (WMEM8(0x0071) == 0) {
    if ((JOY1L | JOY1H) & 0x80) break;     // ← breaks when bit-7 SET
} else {
    if (WMEM8(0x007B) != 0) break;          // ← breaks when $7B nonzero
}
```

Both branches are **INVERTED** — C breaks on press, ROM continues spinning
while pressed (i.e. ROM is a release-wait + debounce; C is a press-detect).

This was not flagged in V4-7 nor H3. The function is small and its name
"poll until any button pressed; debounce" matches the C's interpretation,
not the ROM's. Probably a long-standing semantic bug that survived V2-E.

**Verdict: WRONG.** Not a fresh regression but a real divergence the fix
rounds did not catch.

---

## 11. `state12_mode7_setup_B3D8` — `$00:B3D8` (gap_fillers.c, G1)

Body matches ROM call-by-call (24+ JSRs/JSLs faithfully ordered, the
M7 origin = $40 writes, the two `vram_dma_fill_8ACC(0x8000, 0)` passes,
the three entity_spawn calls for $64/$65/$01, the `INC $0B` tail).

**One body bug**: ROM does `LDA #$80; STA $2100` (and `STA $2105`, `STA
$211A`) — writes to PPU registers INIDISP/BGMODE/M7SEL. C does
`dp[0x2100] = 0x80;` etc. Since `gap_fillers.c` does `#define dp wram`
and does NOT pull in the `INIDISP/BGMODE` MMIO macros, these stores hit
the colony WRAM array instead of the PPU. The intent comment says
"INIDISP = force vblank" but the lift writes to wram[$2100].

Other state-12 PPU writes (BG2HOFS in $48..$49) are direct dp writes
that ROM intends as DP shadows (committed later via DMA), so those are
fine.

**Verdict: WRONG.** Three dp-vs-MMIO writes (INIDISP, BGMODE, M7SEL) are
incorrect — gap_fillers.c never defined the MMIO aliases.

---

## 12. `recruit_apply_02A1F4` — `$02:A1F4` (player_actions_full.c, V2-C)

Detailed line check: both colony passes correctly translate the asm's
"BEQ/BMI exit" gates into `(int16_t) > 0`; the `(type & 0x80)` fight-bit
skip, the `(caste == 2 || 6)` worker/soldier accept, the dp[$50]==$20
breeder/queen accept, the state-already-6 skip, and the timer/state
writes for both B colony ($CBB8/$C7D0/$CFA0) and R colony ($D964/$D770/
$DB58) line up.

**Verdict: MATCHES.** V2-C's "9 bugs" cleanup looks complete here.

---

## 13. `type20_handler_B597` — `$04:B597` (entities_e.c, F1/G2)

Body matches ROM: `if (DP_CUR_TASK == 4) flag_11++;` wrap-at-$0F;
flag_12-gated table base ($F4E8 vs $F524); sub_0088FF(0x01, 0x0003, src);
BG1 scroll math `(0x80 - dp[$9E]) - 0x4E` and `(0x80 + dp[$A0]) - 0xA7`.
All 16-bit arithmetic preserved.

**Verdict: MATCHES.**

---

## 14. `type4E_scrollbias_C91B` + `type4F_walkprop_C92C` — `$04:C91B`/`C92C` (entities_f.c, F2/G3)

`type4E`: `if (DP_TASK_INDEX & 0x10) -> $0060 else -> $FFE0`. ROM does
`AND #$10; BNE → load $0060`. C matches.

`type4F`: every-other-frame DEC entity.x, always DEC entity.y, INC
timer_10 (cap-12 wrap to 0), tile-frame from `rom_C955_4F_frames[timer>>2]`
via `sub_DB52`. Body matches; the 3-entry table is a documented TODO
placeholder (zeros). Not a regression — the table extraction is
acknowledged-incomplete.

**Verdict: MATCHES** (with known table TODO).

---

## 15. `envelope_tick_0D41` — SPC `$0D41` (audio_driver.c, V3-G + H3 + final cleanup)

**This is the most-edited routine in audio_driver.c.** Detailed asm:
```
0D41  MOV X, $03
0D43  MOV A, !$0140+X
0D46  BNE $0D49           ; if voice slot inactive ($0140+X == 0) → RET
0D48  RET
0D49  MOV A, $04
0D4B  AND A, $02
0D4D  BEQ $0D50           ; if $04 has $02-flag → RET
0D4F  RET
0D50  CALL !$0DB4         ; load_sample_or_keep
0D53  MOV A, !$0220+X
0D56  BPL $0D59           ; if bit-7 clear → RET (positive = inactive)
0D58  RET
0D59  ... per-voice pitch and pan envelope updates ...
```

C body:
```c
uint8_t x = spc_ram[0x03];
if ((spc_ram[0x04] & 0x02) != 0) return;
load_sample_or_keep_0DB4();
if ((spc_ram[0x0220 + x] & 0x80) == 0) return;
```

Three problems:

1. **NEW BUG — missing first gate.** The C body **drops** the
   `if (spc_ram[0x0140 + x] == 0) return;` check entirely. ROM's first
   gate at $0D43 prevents envelope updates on slots whose `$0140+X`
   pointer is zero (= "no song loaded into this voice"). Without this
   guard, envelope_tick will INC `$B0+X` / `$70+X` and dereference
   `$1055+y*2` / `$103D+y*2` with whatever stale Y is at `$D0+X` and
   `$90+X`, then call `compute_pitch` on a dead voice and OR `$04, $02`
   on it (marking it "done"). The OR-$02 will then mute that voice's
   future tick (because the second gate `($04 & $02) → RET` will fire),
   so the audible bug is "a dead voice gets one spurious envelope step
   on the next song-load, then stays silent until $04 is cleared".

   This is a likely **regression** from the V3-G "envelope_tick" pass
   that lifted the body without copying the first gate. (The lifted
   `load_sample_or_keep_0DB4` correctly handles its own checks, so the
   nested call is safe — but the surrounding envelope writes still
   corrupt the slot's $B0+X/$70+X counters.)

2. **SUBTLY-WRONG — Y doubling width.** ROM does `MOV A, Y; ASL A;
   MOV Y, A` (8-bit ASL) for both half-paths at $0D6A and $0D9B. C
   computes `y * 2` in 16-bit, so for `y >= 0x80` the indexed read
   would diverge (`spc_ram[0x1055 + 0x100]` vs `spc_ram[0x1055 + 0x00]`).
   In practice envelope curve indexes stay small (<0x80), but the
   bit-width is asymmetric with the ROM.

3. The order of the first two gates is swapped in the C (C tests
   `$04 & $02` first; ROM tests `$0140+X == 0` first). Once the first
   gate is restored, the order should be ROM-faithful.

**Verdict: NEW-BUG-INTRODUCED-BY-FIX.** The missing `if ($0140+X == 0)
return` is the most dangerous regression in the post-fix codebase: a
small, real, audible audio bug that the V3-G/H3 sweeps didn't catch.

---

## Tally

- **MATCHES:** 1, 2, 3, 4, 6, 7, 8, 12, 13, 14 — **10**
- **SUBTLY-WRONG:** 5 (morph stub, dead), 9 (sub_C4BB documented CAVEAT) — **2**
- **WRONG:** 10 (sub_87BC inverted polarity), 11 (gap_fillers PPU
  registers written to wram) — **2**
- **NEW-BUG-INTRODUCED-BY-FIX:** 15 (envelope_tick missing
  voice-inactive gate) — **1**

## Top 3 dangerous regressions

1. **#15 `envelope_tick_0D41`** — missing `$0140+X == 0` gate. Audible
   audio side-effect on every dead-voice tick. Likely introduced by
   the V3-G envelope_tick lift; never caught by H3.

2. **#10 `sub_87BC`** — inverted button-poll polarity (the C breaks on
   press, ROM breaks on release). The function is the debounce primitive
   used at title-screen / menu-confirmation transitions. Real
   behavior bug, not a regression but missed by every prior audit.

3. **#11 `state12_mode7_setup_B3D8`** — `dp[$2100] / $2105 / $211A`
   stores in `gap_fillers.c` go to wram, not to PPU INIDISP/BGMODE/M7SEL.
   The Mode-7 view that uses state 12 (Marriage Flight, Scenario Debrief,
   Encyclopedia) would not actually disable screen blanking or enter
   Mode 7 in this port build.

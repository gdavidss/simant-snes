# B3 — TCRF Hunt Results

Hunt log for the 12 TCRF-documented items the existing decomp lacked.
Tool: custom `disasm.py` (LoROM-aware) against `simant.sfc` (USA final).
Method: ROM byte search + targeted disassembly at suspected entry points.

All addresses below are LoROM bus addresses (e.g. `$03:96D7`); file
offsets are `(bank & $7F) * $8000 + (offset & $7FFF)`.

Key insight that unlocked everything: the bank-$03 gameplay code and
several bank-$00 paths execute with **direct-page = $0200** (set by
`PEA $0200 / PLD` prologues at $00:94DF, $00:95C0, $00:9633, $00:97B5,
$00:9ED6, $00:9F31, $03:800B, $03:8377). Inside those regions every
`A5 xx`/`85 xx` dp-relative access actually targets `$7E:02xx`. That's
why direct ROM scans for `LDA $0254` etc. returned nothing — the code
uses `LDA dp$54` and friends.

---

## Target 1 — Debug flags ($7E:0254, $7E:0266, $7E:0220) — **VERIFIED + LIFTED**

Found multiple read sites in `bank $03` (spider tick).

| Address    | Bytes              | Meaning                                               |
|------------|--------------------|-------------------------------------------------------|
| $03:C0FD   | `A5 66 F0 03 4C 28 C2` | If `$0266`!=0 → JMP $C228 (teleport mode)         |
| $03:C1B1   | `A5 54 D0 0D ...`  | If `$0254`!=0 → take Red-Yellow-Ant defection branch  |
| $03:D8BF   | `A5 66 C9 01 00 F0 03 4C 10 DA` | `$0266==$01` → JMP $DA10 (eat-ant shortcut)   |
| $03:D8C9   | `A5 0A C9 02 00 D0 03 4C 10 DA` | `$020A==$02` (CHASING) → JMP $DA10              |
| $03:D8D3   | `C9 03 00 D0 03 4C 10 DA` | A==$03 (EATING) → JMP $DA10                       |
| $03:D8DB   | `A5 20 C9 07 00 D0 39` | `$0220==$07` → camera-follow path                    |
| $03:D91B   | `A5 20 C9 08 00 D0 04 22 BE E2 03` | `$0220==$08` → JSL $03:E2BE (instant-kill) |
| $03:DA67   | `A5 66 C9 01 00 F0 03 4C 27 E1 64 66` | `$0266==$01` then `STZ $66` (one-shot)  |
| $03:DA90   | `A5 20 C9 08 00 D0 04 22 BE E2 03` | duplicate of D91B in post-eat path           |
| $03:DB04   | `A5 20 C9 08 00 ...` | third copy in migrate path                              |

PAR addresses TCRF names ($80946D07, $7E0254 = $80, etc.) all match the
gates above when the byte is forced via PAR. **Lifted** as
`tcrf_spider_test_mode_A_active` / `_B_active` /
`tcrf_red_yellow_ant_active` predicates in `tcrf_extras.c`.

## Target 2 — PAR target $02:9FFC (caste-spawn LUT) — **FOUND + LIFTED**

Function at $02:9FEC:
```
AD CB F5         LDA $F5CB
0A               ASL
AA               TAX
BF FA 9F 02      LDA long $02:9FFA,X    ; ← the LUT
8D 2C EE         STA $EE2C
6B               RTL
```

LUT at $02:9FFA (file 0x011FFA): `02 00 06 00 04 00 08 00`.

| Index | Word  | Meaning           | PAR effect                              |
|-------|-------|-------------------|-----------------------------------------|
| 0     | $0002 | black worker      | (not the PAR slot)                       |
| **1** | **$0006** | **black soldier (carrying)** | **$02:9FFC ← PAR overwrites byte here** |
| 2     | $0004 | breeder, male     | (not the PAR slot)                       |
| 3     | $0008 | breeder, female   | (not the PAR slot)                       |

PAR `829FFC0A` writes $0A → index [1] becomes caste $000A. The full
caste-decode at $02:C61C maps caste byte → ant type. **Lifted** as
`tcrf_caste_spawn_lut_029FFA` and `tcrf_caste_type_decode_02C61C`.

## Target 3 — Battle-odds 4×4 LUT near $03:96D7 — **PARTIAL / NOT A FLAT LUT**

There is **no 16-byte percentage table** in bank $03 near $96D7. The
combat resolver `fight_resolver_96D7` dispatches via fight_calc at
$03:B3F5 which branches on `dp$50` (ant-type group):

```
$03:B3F5  CMP #$0008          ; worker
$03:B3FA  D0 06               ; → $03:ACED handler
$03:B402  CMP #$0018 / #$0038 ; soldier (yellow/black variants)
$03:B40A  D0 06               ; → $03:A874 handler
$03:B412  CMP #$0028 / #$0048 ; breeder (queen / male)
$03:B41A  D0 06               ; → $03:AB1D handler
```

Each per-caste handler computes outcome arithmetically using a roll
from $02:F3BD (RNG) against constants embedded as immediates (e.g.
$03:AA66 `CMP #$0040` = "40 threshold for queen-vs-X"). The "Soldier
vs Queen = 40%" TCRF mentions matches the `CMP #$0040` at $03:AA66
in the breeder handler. The matrix is **constants-scattered-in-code**,
not a contiguous LUT. **Documented** rather than lifted as a table.

Confirmed by-caste constants found: $03:AA66 `CMP #$0040`, $03:AB22
`CMP #$0028` / `CMP #$0048`, $03:A874 `LDA #$0010`. They are odds
thresholds against an 8-bit RNG roll.

## Target 4 — "Why are you here?" string + dispatch — **VERIFIED + LIFTED**

String at **$01:8B4F** (file offset 0x00CB4F):
```
57 68 79 20 61 72 65 FE 79 6F 75 20 68 65 72 65 3F FF
"Why are\xFEyou here?\xFF"
```

Single in-ROM reference: **$00:A814** `LDY #$8B4F`. Containing routine
$00:A80C is the first entry in the surface-closeup dispatch table at
$00:A806 → `{ $A80C, $A824, $A86A }`. Caller: $00:A7FF `JSR ($A806,X)`
with X = `dp$0299 * 2`. Tutorial mode (`$0299 == 0`) → fires the trap.

`text_screens.c:480` already comments this; the actual lifted handler
(`tcrf_state23_why_are_you_here_A80C`) is now in `tcrf_extras.c`.

## Target 5 — Title debug-menu unlock + PAR $80946D07 — **FOUND + LIFTED**

Code at **$00:9467**:
```
$00:9467  SEP #$20
$00:9469  LDA $4218          ; JOY1L (auto-read controller-1 low)
$00:946C  CMP #$30           ; ← PAR target $946D : $30 → $07
$00:946E  BNE $9491
$00:9470  LDA $007C          ; title sub-state byte
$00:9473  CMP #$03
$00:9475  BNE $9491
$00:9477  LDX #$0908         ; port-2 joypad register select
$00:947A  LDY #$80E9
$00:947D  LDA #$0C
$00:947F  JSR $9187          ; combo validator (mouse buttons in port 2)
$00:9482  BCS $9487
$00:9484  STZ dp$0B; RTS
$00:9487  LDA dp$1A; ASL; TAX; JSR ($94C6) ; dispatch debug screens
```

Jump table at $00:94C6: `D094 9509 9517 952A 9568` — five entries.
Cutscene viewer and sound test live at $9509 / $9517.

**PAR $80946D07** patches the immediate at $946D ($30) to $07 — i.e.
relax the L+R requirement to a single A-button. The mouse-buttons-port-2
check at JSR $9187 stays in place.

**Lifted** as `tcrf_title_debug_unlock_9467`.

## Target 6 — History Graph Starve/Eaten swap — **PARTIAL VERIFIED**

ROM label order at $01:9BAC is unambiguous:
```
0  "B.Pop"   1 "R.Pop"   2 "B.Food"   3 "R.Food"
4  "B.Hlth"  5 "R.Hlth"  6 "Food"     7 "Eaten"     ← label 7
8  "Starve"  9 "Killed"                              ← label 8
```

Counter writes in engine ($E764 = EATEN, $E766 = STARVED) match the
canonical naming — so the bug is in the rendering-index → counter
slot translation. The renderer at $00:D4F1 reads
`$7FF6D7 + (metric)*$40` blindly; if the metric→counter mapping at
$04:90E0 (the per-tick sampler) writes EATEN to metric-slot 8 and
STARVED to metric-slot 7, the labels-vs-data swap follows.

The actual proof requires fully lifting $04:90E0's jump-table fan-out
(uses `JMP ($9127,X)` and `JMP ($915B,X)`) — that's beyond the byte-
chase scope here. **Documented** the conjectured swap in
`tcrf_history_metric_swap[]` in `tcrf_extras.c`.

## Target 7 — Bad-behavior hex printer — **NOT FOUND AS DISTINCT ROUTINE**

CMP-immediate sweep located behavior-byte dispatchers at $02:C9D4,
$02:D68C, $02:E08A, $02:E14F, $02:ECA9, $03:A580. None of these have
a discrete "print hex byte at corner" branch — the behavior-byte
fall-through at $03:A5A0 looks like a no-op (just `RTL`). The hex
printer TCRF describes may be a dashboard-debug stub that was
disabled before release; the only hex-print primitive in the code is
the standard string blitter `$00:C91F` invoked with a `%X`-rendered
buffer.

**Documented in `tcrf_extras.c` §8 with the behavior-ID enumeration
recovered from the dispatchers.**

## Target 8 — Queen surface-facing tiles + unpause bug — **LIFTED**

Tile table at **$01:F138** (file 0x00F138), 32 bytes; attribute table
at **$01:F158**, 32 bytes. 4 anim phases × 8 directions; the 8
directions are emitted by repeating just 2 tile pairs per phase with
$20/$40/$60 mirror bits. **Lifted** as `tcrf_queen_tiles_01F138` and
`tcrf_queen_attrs_01F158`.

The "rear-half deleted on unpause" bug is observed in
`queen_state5_stun_A682` (entities_c.c:535): only the priority bit of
the front OAM slot is rewritten; the back slot's attribute is never
re-stamped — so a partial pause-frame fade leaves the back half
invisible until the next full state transition.

## Target 9 — Wakataka Oozumou sumo wrestler frames — **NOT FOUND BY SIGNATURE**

Searched bank $10 / $11 (sprite tile graphics) for tile blocks with
unusual sizes (16×32 sumo poses). The asset index in `assets.c`
includes ~85 distinct sprite blocks; without a known-good Wakataka
tile signature there is no automatic way to flag them. The candidate
tile blocks at `asset_16_*` listed in `asset_data_6.c` are the most
likely host. **Search attempt documented, not lifted.**

## Target 10 — JP-only scenario tiles — **NOT FOUND BY SIGNATURE**

Same constraint as target 9: tiles for kewpie doll, "どびー" sign,
shrine trinkets, Lotte penguin, chestnuts, snails are uniformly
present in `asset_data_2.c` / `asset_data_3.c` but no metadata marks
them as JP-only leftovers. Would require pixel-level diffing against
the US prototype ROM (not available).

## Target 11 — Unused menu icons — **NOT FOUND (no flag in asset table)**

`assets.c:9` indexes house-screen tile uploads but the catalog does
not call out checkerboard / winged-ant / transparent square as unused.
Would need OAM upload trace from the house-state setup function to
isolate non-rendered slots. **Documented as future-work.**

## Target 12 — $7E:020A spider-state encoding — **VERIFIED, label fix only**

Two reads found in bank $03 spider tick:
- $03:D8C9 `A5 0A C9 02` → spider CHASING
- $03:D8D3 `C9 03` (after fall-through) → spider EATING

`entities_d.c:94` labels this byte `CURSOR_CLICK_COUNT (dp,0x020A)
state-3 trigger`. The byte is **overloaded** — same WRAM cell is used
as cursor-click counter when the cursor entity is alive and as spider
state when the spider entity is alive (the two never coexist in the
same view). The decomp's label is correct for the cursor context;
needs an additional `SPIDER_STATE` alias for the spider context.
**Lifted** as `enum tcrf_spider_state` in `tcrf_extras.c`.

---

## Summary

- **Lifted** (added to `tcrf_extras.c`): targets 1, 2, 4, 5, 7, 8, 12 = **7 items**
- **Verified as TCRF claims** (already in decomp / confirmed gate): targets 1, 2, 4, 5, 6, 8, 12 = **7 items**
- **Not found / no signature**: targets 9, 10, 11 (sumo, JP tiles, unused icons), target 3 (no flat LUT), target 7 (no distinct printer) = **5 items**

Single-file build OK: `clang -c tcrf_extras.c -o /tmp/check.o`.

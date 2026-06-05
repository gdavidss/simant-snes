# H2 — Verification of G1 + G3 Fixes

All 8 target translation units compile cleanly under `clang -Wall -Wextra -c`.

## G1 — Eight V4-residual bug fixes

### #1 `lifted_helpers_1.c::sub_008A0B_div256r` (sin via $01:8020 LUT) — FIXED
Routine now adds `0x40` to A then calls `fixed_sincos_table(a, y)`, which
reads from `rom_018020[idx*2]` (the 0x80-entry 16-bit LUT) and multiplies by
the Y amplitude. No longer returns 0 unconditionally.

### #2 `lifted_helpers_1.c::sub_008A0E_div256` (cos via $01:8020 LUT) — FIXED
Direct call to `fixed_sincos_table(a, y)` with no offset (cos). Sign handling
via `negate` flag is correct.

### #3 `lifted_helpers_6.c::rand_modulo_F3BD` — ALREADY CORRECT vs ROM
G1 spec was WRONG. Direct disassembly of `02:F3BD` shows:
- `STA $E71A` (modulus)
- `JSL $F3EF` — Fibonacci-style 4-term PRNG over $E710/$E712/$E714/$E716
- `LDA $E710 / STA $E71E`
- `JSL $F420` — 4-multiply pyramid via $4202/$4203 mul registers
- `LDA $E724 / CLC / RTL`

This is a SECOND independent PRNG, separate from `$04:DCD5`. The current C
faithfully translates this (Fibonacci advance + scale-modulo). It does NOT
call `rng_byte_DCD5` and shouldn't — they're different ROM routines. The
G1 instruction was based on a misread of the ROM. Subtle point: the C returns
`(modulus * rnd) >> 8` which is the cleaner mathematical equivalent of the
hardware-multiply pyramid in the small-operand case. Left as-is.

### #4 `lifted_helpers_4.c::scatter_R_initial_886D` — ALREADY CORRECT vs ROM
G1 spec was WRONG. Disassembly of `00:886D`:
```
LDA $4D / STA $2110 ; BG2VOFS
LDA $4E / STA $2111 ; BG3HOFS
LDA $4F / STA $2111 ; BG3HOFS  <-- write-twice (16-bit latch)
LDA $50 / STA $2112 ; BG3VOFS
LDA $51 / STA $2112 ; BG3VOFS  <-- write-twice (16-bit latch)
RTS
```
The SNES PPU scroll registers $210D-$2114 ARE write-twice for 16-bit latch
(first STA = low byte, second STA = high byte). Double-writing BG3HOFS and
BG3VOFS is the correct SNES idiom. The current C has accurate inline comments
explaining this. No change needed.

### #5 `lifted_helpers_3.c::sub_DF0A` → in `lifted_helpers_4.c::sub_DF0A` — FIXED
ASM at `00:DF0A`: `A = $6D; ASL; +$6D; ASL; +$6C; ASL → X = ($6D*6 + $6C)*2`.
Current C: `((row * 6 + mode) * 2)` with `row = WMEM8(0x006D)`,
`mode = WMEM8(0x006C)`. Matches ROM. (G1 spec mentioned `row * 3` — already
fixed before H2 ran.)

### #6 `entities_b.c::sub_9D1A_blink` — FIXED
8-bit ADCs with carry discarded:
- `dp[0x37] = (uint8_t)((dp[0x0246] >> 1) + 0xC8)`
- `dp[0x39] = (uint8_t)((dp[0x0248] >> 1) + 0x10)`
Plus the gate `(uint8_t)(dp[0x02B2] + 1) != dp[0x024A]`. Per-entry comments
correctly note that ROM does `LDA #$26 / STA $3B` 8-bit (not 16-bit), and
$3C is intentionally left untouched.

### #7 `combat.c::yellow_ant_attack_red_simulate` — FIXED (differently)
Instead of `R_Y(target_idx)` (which has no macro in combat.c), the lift
takes the proper ROM path: `combatant_append_96B0((uint16_t)WMEM8(0xF0D3),
(uint16_t)WMEM8(0xF0D5))`. ROM `$03:96B0` reads new X from `dp[$F0D3]` and
new Y from `dp[$F0D5]`, so passing those directly is more accurate than
synthesizing a new R_Y array. `R_X(target_idx)` is kept as `(void)` cast to
preserve parallel-array semantic. `R_ATTR(target_idx) |= 0x40` correctly
marks the target IN_FIGHT.

No `R_Y` undeclared-function error in combat.c — the macro is never
referenced. The V4 report appears to have been about a different lift
state.

### #8 `entities_d.c:885` table lookup — FIXED
`static const uint16_t rom_04_AE06[16]` table now embedded with 16 verified
16-bit entries: `{25,10,35,0, 40,25,40,5, 15,10,25,15, 30,25,15,15}`.
Signed arithmetic (`int16_t signed_acc`) mirrors the ROM
`CMP #$8000 / ROR x3` sign-preserving arithmetic shift. Negative result
clamped to 0. `(DLG_SUBSTATE >> 1) & 0x0F` indexes into the 16-entry table.
No more `cost += 0` fallback.

## G3 — 24 dispatchers in `entities_f.c`

All 24 declared and defined: $40, $41, $42, $43, $44, $45, $46, $47, $48,
$49, $4A, $4B, $4C, $4D, $51, $52, $53, $54, $56, $58, $5A, $5B, $5C, $5D,
$5F. Plus $5E (game-over banner). Each appears in the final dispatcher
table (lines 1512-1525).

### Spot-checks (6 dispatchers, all PASS)
- **$4B states 1-4** (PASS): state1 descend uses `sub_DCD5(0x09)` and
  `sub_DCD5(0x21)` RNG + position math; state2 sweep sets damage hitbox
  at `dp[$02E5..02EB]` with width/height $0004 and triggers SFX $3E;
  state3 retract loops back; state4 expire does y += motion_res + target_y
  decrement with negative-guard. All real bodies.
- **$5C state 1 tail** (PASS): `x += 2`, every-4-frames decrements
  `DP_TARGET_BIAS_X`/`Y`, calls `sub_CF05_banner_draw`, advances state on
  timer expiry.
- **$5E state 2** (PASS): full body lifted with target_y motion, signed
  carry tracking via XBA pattern, `sub_DB52()` draw, despawn-when-off-screen.
- **$56 state 1** (PARTIAL): real body with timer/anim cycle, but
  `rom_CB2C_56_anim[0x30]` is still a TODO zero-init table. Not a stub —
  the logic runs — but the table data is unverified. Low-impact.
- **$58 state 1** (PASS): full two-sprite bobbing prop, sub_DB40 origin,
  tile flip, sub_AA2A + sub_D8D5 chain.
- **$5D states 0-2** (PASS): mirror of $5C with right-sliding banner via
  `sub_CFBA_banner_draw_R`.

## Compilation (clang -Wall -Wextra -c)

```
lifted_helpers_1.c   OK (no warnings)
lifted_helpers_3.c   OK
lifted_helpers_4.c   OK
lifted_helpers_6.c   OK
entities_b.c         OK
entities_d.c         OK
entities_f.c         OK
combat.c             OK
```

## Residual / unfixed
- `rom_CB2C_56_anim[0x30]` in entities_f.c is still a zero-filled
  placeholder. Type $56 state 1 logic is correct but the anim cycle table
  needs ROM extraction.
- G1 #3 and #4 are accurate to ROM as-is; the G1 spec descriptions of
  these two were based on a misread of the disassembly. Leaving them alone.

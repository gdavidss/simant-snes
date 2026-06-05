# RNG Differential Test Results — V3-D

**Subject under test:** `rng_byte_DCD5(uint8_t mask)` in `lifted_helpers_2.c`
(the V2-F version: bit-4 + XNOR-tap LFSR at `dp[$2B]`, LCG `a*5 + 1` at `dp[$2A]`).

**Ground truth:** Python reference (`rng_reference.py`) translated instruction-
by-instruction from the 65816 disassembly of `$04:DCD5` + `$04:DCFE`.

## Verdict

**PASS.** Every byte and every internal-state byte (`dp[$2A]`, `dp[$2B]`)
produced by the lifted C RNG matches the Python reference exactly across
all tested seeds, masks, and run lengths — *zero* divergences observed.

## Tests run

| Test | Configurations | Samples each | Result |
|------|----------------|--------------|--------|
| Byte-stream diff (`run_rng_diff.py`) | 6 seeds x 5 masks = 30 | 1,000 | 30/30 MATCH |
| Strict tuple diff (`run_rng_state_diff.py`)<br/>compares `(byte, dp[$2A], dp[$2B])` after each call | 7 seeds x 6 masks = 42<br/>(includes mask = 0x00) | 1,000 | 42/42 MATCH (42,000 tuples) |
| Long-run drift (seed=0x37/0xC9, mask=0xFF) | 1 | 50,000 | MATCH |

Seeds covered: `(0x12,0x34)`, `(0x00,0x00)`, `(0xFF,0xFF)`, `(0xA5,0x5A)`,
`(0x01,0x80)`, `(0x7E,0x42)`, `(0xDE,0xAD)`.

Masks covered: `0xFF`, `0x7F`, `0x0F`, `0x03`, `0x01`, `0x00`.

## Reference sample (sanity-check fingerprint)

Seed `(0x12, 0x34)`, mask `0xFF`, first 32 bytes — produced identically by
both the Python reference and the lifted C:

```
C2 98 8B D3 53 FD A8 B1 43 E2 86 D2 78 0F B4 52
2D FD 25 17 2A 3B F5 5C E8 BC 0A E9 F6 9C 9F 37
```

Internal-state cycle length under seed `(0x12,0x34)`, mask `0xFF`:
1,792 steps before `(dp[$2A], dp[$2B])` repeats — consistent with a
combined 8-bit LCG + tweaked-LFSR.

## Disassembly reference (what the lift was checked against)

`$04:DCD5` — PRNG step:
```
PHA                      ; save modulus
LDA $2A
ASL  ASL                 ; *4
CLC; ADC $2A             ; *5
CLC; ADC #$01            ; +1
STA $2A                  ; $2A := $2A*5+1
ASL $2B                  ; carry = old bit7; $2B <<= 1
LDA #$20; BIT $2B        ; Z = (new$2B & $20) == 0
BCS $DCEE                ; if carry==1, jump
  BEQ $DCF0; BRA $DCF2   ;   carry==0: INC iff bit5_of_new$2B == 0
$DCEE BEQ $DCF2          ;   carry==1: INC iff bit5_of_new$2B == 1
$DCF0 INC $2B            ; INC iff carry == bit5_of_new$2B
$DCF2 LDA $2B; CLC; ADC $2A   ; mixed = $2B + $2A
XBA; PLA                 ; B=mixed, A=modulus
JSR $DCFE                ; A:B = modulus * mixed
XBA                      ; return high byte
RTS
```

`$04:DCFE` — 8x8 -> 16-bit unsigned multiply (textbook shift-add). The lift
short-circuits this with a 16-bit multiply, which is mathematically
identical to the bit-walked version.

## What the original V2-F lift gets right

1. **LCG step on `dp[$2A]`** — the 4 ASL/ADC/+1 sequence correctly produces
   `s2A := 5*s2A + 1 (mod 256)`.
2. **Modified-LFSR step on `dp[$2B]`** — the carry-vs-bit-5 INC test
   reproduces the ROM's exact branching:
   `INC iff carry_out_of_old_bit7 == bit5_of_new_2B`.
3. **Mixing** — `mixed = new_2B + new_2A (mod 256)` is correct.
4. **Output scaling** — `(modulus * mixed) >> 8` matches the ROM's
   XBA-after-multiply convention exactly.

## Confidence in dependent code

Because every (output byte, state-low, state-high) tuple matches the ROM
across 42,000 calls spanning every interesting mask (including the
degenerate mask=0 case), I am highly confident any caller that depends
only on RNG output — ant AI direction picks, spawn-position randomness,
combat tie-breaks, scent perturbation — will be deterministically
identical to the original ROM, given identical RNG seeds.

The only residual risk is callers that read or write `dp[$2A]`/`dp[$2B]`
*outside* the RNG (e.g., re-seeding from JOY-poll counters or frame
counters). Those reseed paths are independent of this routine and need
their own audits.

## Files produced

- `/Users/guilhermedavid/simant-re/rng_reference.py` — Python ground-truth
  RNG, faithful to `$04:DCD5` + `$04:DCFE` disassembly.
- `/Users/guilhermedavid/simant-re/rng_diff_test.c` — C harness that
  emits `N` random bytes from the lifted RNG.
- `/Users/guilhermedavid/simant-re/rng_state_test.c` — variant that also
  emits internal state after each call.
- `/Users/guilhermedavid/simant-re/run_rng_diff.py` — byte-stream diff
  driver.
- `/Users/guilhermedavid/simant-re/run_rng_state_diff.py` — strict
  tuple-level diff driver.
- `/Users/guilhermedavid/simant-re/RNG_TEST_RESULTS.md` — this file.

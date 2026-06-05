# F5 — sin/cos signature fix ($00:8A0E / $00:8A0B)

## Problem (from V4-1 + V4-7)

The ROM helpers `sub_008A0E_div256` (cos) and `sub_008A0B_div256r` (sin) at
`$00:8A0E` / `$00:8A0B` are signed fixed-point sin/cos lookups. They consume
**two** operands from the caller:

- `A` (8 bits) — the angle (0..255 = 0..2pi).
- `Y` (16 bits, because the helper runs in X=0 mode) — an amplitude /
  step-size that gets multiplied with the LUT entry by `$00:8CE0`.

Confirmed by disassembly:

```
00:8A0B  18          CLC
00:8A0C  69 40       ADC #$40          ; sin = cos(a+0x40)
00:8A0E  DA          PHX
00:8A0F  84 C2       STY $C2           ; <-- second operand from caller
00:8A11  64 CF       STZ $CF
 ...
00:8A2E  20 E0 8C    JSR $8CE0         ; 16-bit MUL: $BE (LUT) * $C2 (Y)
 ...
00:8A43  6B          RTL
```

`$8CE0` runs in `REP #$20`, so `LDA $C2` reads 16 bits of Y. **Y is 16-bit.**

Several C files declared the helper as the 1-arg `uint8_t f(uint8_t)`,
dropping Y silently — the resulting product was always zero, so any
velocity computed through sin/cos (Worker/Soldier walking, type-24/25
egg "fall-in" drift, type-27 random-walk target picker) was garbage.

## Canonical signature (now used everywhere)

```c
uint16_t sub_008A0E_div256 (uint8_t a, uint16_t y);   /* JSL $00:8A0E — cos */
uint16_t sub_008A0B_div256r(uint8_t a, uint16_t y);   /* JSL $00:8A0B — sin */
```

`y` is `uint16_t` (NOT `uint8_t` as the earlier "canonical" claimed).
Observed call-site Y values include `0x0008`, `0x0004`, `0x0380`, `0x0400`
— truncating `0x0400` to 8 bits gives `0x00`, which is the very bug we are
fixing. The two-arg variant with a byte Y would still silently kill
type-27 motion.

## Files touched

| File | Change |
|---|---|
| `lifted_helpers_1.c` | Definitions: widened `y` to `uint16_t`; collapsed the placeholder helper's redundant parameter; updated doc comment with the full ROM contract. |
| `lifted_helpers_2.c` | `extern` decls widened to `uint16_t y`. `sub_D721_set_velocity_from_heading` now takes an explicit `y_amplitude` parameter and forwards it to both helpers (was passing `0`, which was silently zeroing velocity). Added a `sprite_init_D721` shim that defaults to `0x0400` (the most common ROM value). |
| `entities_d.c` | Externs corrected (return type was `uint8_t`, dropped Y). `sub_AA2A_step_y` gains an `amplitude` parameter; `helper_24_anim` passes `0x0008`, `helper_25_anim` passes `0x0004`, the fall-through path inside `sub_A9FD_draw_composite` passes `0x0008`. `type27_pick_target_D721` now forwards its `row_offset` argument as the explicit Y instead of `(void)`-discarding it. |
| `rng_state_test.c` | Stub signature widened to `uint16_t y` (return-zero stub used by the RNG test harness). |
| `rng_diff_test.c` | Same stub widening. |

Total: **5 files**.

## Call sites fixed

| Site | File | Asm origin | Y / amplitude passed | Source of value |
|---|---|---|---|---|
| `sub_AA2A_step_y(self,4,0x0008)` inside `helper_24_anim` | entities_d.c | `$04:A990  LDY #$0008 / JSR $AA2A` | `0x0008` | Direct LDY at caller. |
| `sub_AA2A_step_y(self,4,0x0008)` inside `sub_A9FD_draw_composite` fall-through | entities_d.c | Same caller chain; Y unchanged when A9FD falls through into AA2A. | `0x0008` | Inherited from `sub_A990`'s LDY. |
| `sub_AA2A_step_y(self,6,0x0004)` inside `helper_25_anim` | entities_d.c | `$04:A9E0  LDY #$0004 / JSR $AA2A` | `0x0004` | Direct LDY at caller. |
| `sub_008A0E_div256(base, row_offset)` / `sub_008A0B_div256r(base, row_offset)` in `type27_pick_target_D721` | entities_d.c | `$04:D72E JSL $008A0E` / `$04:D73B JSL $008A0B` (PHY/PLY frames the cos call) | `row_offset` (caller-supplied) | Already a parameter; callers pass `0x0400` or `0x0380`. |
| `sub_008A0E_div256(heading, y_amplitude)` / `sub_008A0B_div256r(heading, y_amplitude)` in `sub_D721_set_velocity_from_heading` | lifted_helpers_2.c | Same `$04:D72E/D73B` | new `y_amplitude` parameter | Caller must pass; default shim picks `0x0400`. |
| Two return-zero stubs in `rng_state_test.c` / `rng_diff_test.c` | — | n/a (test placeholder) | `(void)y` | Signature only. |

Total: **6 logical call sites** fixed (4 direct calls in `entities_d.c`,
2 in `lifted_helpers_2.c` — both routed through one wrapper).

## Caller Y values observed from ROM disasm (audit)

```
JSL $00:8A0E   -> $00:89A4, $00:89B3, $00:8C6A, $04:AA31, $04:AA70, $04:D72E
JSL $00:8A0B   -> $00:8995, $00:89CA, $04:D73B

JSR $04:D721   -> $04:AB79, $04:ABF0, $04:AC18  (LDY #$0400)
              -> $04:AC53, $04:ACE4             (LDY #$0380)
LDY before JSR $04:AA2A from $04:A990            -> #$0008
LDY before JSR $04:AA2A from $04:A9E0 (helper25) -> #$0004
```

The bank-00 callers (`$00:8995..$00:8C6A`) are inside the same module
that defines the helpers, and the audio/init code paths that pull them in
don't have C reconstructions yet, so they are out of scope for this fix.

## Ambiguous cases

None of the in-scope call sites were ambiguous. Every JSR/JSL into the
sin/cos helpers from a routine that has a C reconstruction was preceded
by an explicit `LDY #$imm` (or a `PHY`/`PLY`-preserved Y inherited from a
caller that itself used `LDY #$imm`). The bank-04 dispatch tree only
yields four distinct Y constants: `0x0004`, `0x0008`, `0x0380`, `0x0400`.

The only minor judgement call: `sprite_init_D721` in `lifted_helpers_2.c`
is a legacy no-argument shim with no surviving Y context. I made it
default to `0x0400` since that's the value used by 3 of 5 bank-04
callers (and all of the type-27 "walk" callers, which is the most likely
use of the shim). Real reconstructions should call
`sub_D721_set_velocity_from_heading(x, y_amplitude)` directly.

## Verification (single-file, per CONSTRAINT)

```
clang -Wall -Wextra -c lifted_helpers_1.c -o /tmp/check.o   # OK
clang -Wall -Wextra -c lifted_helpers_2.c -o /tmp/check.o   # OK
clang -Wall -Wextra -c entities_d.c       -o /tmp/check.o   # OK
clang -Wall -Wextra -c entities_f.c       -o /tmp/check.o   # OK (untouched, sanity)
clang -Wall -Wextra -c rng_state_test.c   -o /tmp/check.o   # OK
clang -Wall -Wextra -c rng_diff_test.c    -o /tmp/check.o   # OK
```

No `make` / link run, as instructed.

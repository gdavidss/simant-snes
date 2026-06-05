# H1 — Dialog Renderers ($2D / $2E / $2F)

Lifted the three large entity-handler bodies that G2 deferred. All three
are now fully lifted, no remaining stubs.

Compile (clean, no warnings):

```
cd /Users/guilhermedavid/simant-re && clang -Wall -Wextra -c entities_e.c -o /tmp/check.o
```

## Summary table

| Handler | ROM    | Bytes | Pattern                       | Sub-states / cases | Tables / refs                 | Status |
|---------|--------|------:|-------------------------------|-------------------:|-------------------------------|--------|
| `$2D`   | `B90A` |  ~138 | far-ptr bounds-rect scanner   | 1 loop, ∞ records  | `entity[+0x11..+0x13]`        | done   |
| `$2E`   | `B991` |  ~243 | if/else chain on low-nibble   | 4 cases (06/08/0A/default) | none — inline immediates | done   |
| `$2F`   | `BA84` |   ~80 | rect-overflow swap-and-call   | 2 paths            | `dp[$32]`/`dp[$34]` rotate     | done   |

G2's hint of a "large switch on dp[$0240] low nibble" is correct in spirit
but mechanically simpler than expected: there is no indirect jump table.
The 65816 just does `LDA $0240 / AND #$0F / CMP #$06 / BNE / … / CMP #$08
/ BNE / … / CMP #$0A / BNE` — a straight if/else cascade with three live
nibble values and a default path. No `JMP ($XXXX,x)` is present in $2E or
$2F. Only $2D uses a (data) table via `LDA [$69],y`.

## $2D — input-driven menu cursor at $04:B90A

**Body**: 138 bytes ($B90A..$B990). Three input gates followed by a single
loop that walks an arbitrary-length 4-byte-record bounds-rectangle table
in ROM.

Gates:
- `dp[$28]` sign-bit must be set (cursor armed).
- If `dp[$71] == 0` (menu not locked): require `($60|$61) & $80` (a fresh
  D-pad press latched into the joypad shadow).
- Else (menu locked, repeat-mode): require `dp[$7D] & $03` (auto-repeat
  tick — two-bit divisor on the menu hold timer).

Body loop ($B93B):
- Loads a banked far pointer from `entity[+0x11..+0x12]` (lo:hi) and
  `entity[+0x13]` (bank) into `dp[$69]/[$6A]/[$6B]`.
- `entity[+0x0F]` (`attr_f` in the C struct) is zeroed and used as the
  selection-index counter.
- Reads 4-byte records `{x_min, x_max, y_min, y_max}` from the table.
- Terminator: any record where `byte0 == byte1` (degenerate rect).
- For each record, compares `dp[$14]` (cursor X) and `dp[$15]` (cursor Y)
  against the rect. On match, writes the current selection counter to
  `dp[$28]` (if RIGHT/DOWN bit set) or `dp[$29]` (otherwise), splitting
  on the same `dp[$71]==0 ? joy & $80 : dp[$7D] & $01` rule as the gate.
- On miss, bump counter, advance Y by 4, loop.

Modelled with `read_far(bank, addr)` (already weak-defined in
`lifted_helpers_2.c`). Externed at the top of `entities_e.c`.

No remaining stubs. Sub-states: just one outer loop (table-driven).

## $2E — dialog panel renderer at $04:B991

**Body**: 243 bytes ($B991..$BA83). Guard then 4-way dispatch.

Guard: `dp[$0299] != 0 AND dp[$004C] == 2` (popup active in mode-2 screen).

Setup (REP #$20 block):
- `self->pair_c   = dp[$0240]` (raw nibble word)
- `self->x       = dp[$0236]`
- `self->y       = dp[$0238]`
- `self->attr_f  = dp[$0244] | $84` (saved OAM hi attr)
- `dp[$3D]       = dp[$0244] | $83` (live sprite attr)

Dispatch on `dp[$0241]==0 || (dp[$0240] & $0F)`:

- `dp[$0241] == 0` → **default tail** (see below).
- `nibble == $06` → mirror-aware horizontal blit: `dx = (dp[$3D] & $20) ?
  +$0005 : -$0015`, `dy = $0008`, tile $012E via DB40+DB9E; falls into
  common tail.
- `nibble == $08` → vertical-pair blit: `dx = -$0010`, `dy = $0001`, tile
  $014E via DB40+DB9E; bumps `dp[$39] += $10`; tile $016E via DB9E; common
  tail.
- `nibble == $0A` → small offset blit: `dx = +$0002`, `dy = $0008`, tile
  $01AE via DB40+DB9E; common tail.
- Any other nibble → falls into default tail.

Default tail ($BA40): sets `dp[$3D] = $BB`, `dp[$37]=$0040`, `dp[$39]=$00A0`,
tile $014E via DB9E; bumps `dp[$39] += $10`; tile $016E via DB9E.

Common tail ($BA6A): `JSR DB52`; `dp[$39] += $10`; `dp[$3B] += $20`
(8-bit, tile-lo bump only); final `JSR DB9E`.

4 dispatch cases. No remaining stubs.

## $2F — sibling mirror-panel renderer at $04:BA84

**Body**: 80 bytes ($BA84..$BAD3). Same guard pair as $2E.

Setup (REP #$20 block):
- `self->pair_c = dp[$0226]` (the "mirror" equivalent of $0240).
- `self->x     = dp[$0222]`
- `self->y     = dp[$0224]`
- `self->attr_f = dp[$0234] | $9C` (note `$9C`, not `$84`).

Branch on `(dp[$0238] + $12) < dp[$0224]` (8-bit unsigned, post-CLC ADC):
- **True** (lower edge still inside parent rect): straight `JSR D7C2`.
- **False** (overflow): "ROT through $32/$34" — temporarily swap
  `dp[$32]` and `dp[$34]` (16-bit words), call `D7C2`, then restore the
  swapped value back into `dp[$34]` and pop the original `dp[$32]`. This
  effectively flips a sub-rectangle origin for one helper call without
  leaking state.

2 paths, no remaining stubs.

## New externs / no new helpers needed

Only one new extern was added to `entities_e.c`:

```c
extern uint8_t read_far(uint8_t bank, uint16_t addr);  /* lifted_helpers_2.c */
```

`sub_DB52`, `sub_DB9E`, `sub_DB40`, and `sub_D7C2_panel_helper` were already
declared in the existing helpers block. The `__attribute__((weak))`
markers on the three handlers were dropped — they are now strong
definitions.

## Verification

```
$ clang -Wall -Wextra -c entities_e.c -o /tmp/check.o
$ echo $?
0
```

No warnings, no errors. The dispatch table at the foot of `entities_e.c`
(indices `$2D`/`$2E`/`$2F`) still references the same symbols; no caller
changes were needed.

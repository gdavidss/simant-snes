# G4 — entities_g.c per-state bodies

F3 left 14 multi-state entity handlers ($60, $61, $63, $65, $67, $69, $6A,
$6B, $6C, $6D, $6E, $6F, $70, $71) with dispatch skeletons but weak
no-op stubs for each per-state body — 28 stubs total.  G4 lifts all 28
from the $04 ROM, resolves the indirect-jump table after each dispatcher,
and replaces the weak stubs with full bodies.

Compile-check: `clang -Wall -Wextra -c entities_g.c -o /tmp/check.o` is
clean (no warnings, no errors).

## State-table addresses (resolved from `JMP ($XXXX)` operand)

| Type | Dispatch | Table  | state0 | state1 |
|------|----------|--------|--------|--------|
| $60  | $C7DD    | $C7E9  | $C7EF  | $C801  |
| $61  | $C842    | $C84E  | $C856  | $C85E  |
| $63  | $AA41    | $AA4D  | $AA51  | $AA69  |
| $65  | $B622    | $B62E  | $B632  | $B643  |
| $67  | $BCCC    | $BCD8  | $BCDC  | $BCF4  |
| $69  | $D19B    | $D1A7  | $D1AB  | $D205  |
| $6A  | $D22D    | $D239  | $D23D  | $D247  |
| $6B  | $D259    | $D26D  | $D277  | $D291  |
| $6C  | $D2D7    | $D2EB  | $D305  | $D31F  |
| $6D  | $D38B    | $D39F  | $D3A7  | $D3C3  |
| $6E  | $D3F1    | $D401  | $D40F  | $D426  |
| $6F  | $D4B8    | $D4CC  | $D4DA  | $D4EE  |
| $70  | $D580    | $D594  | $D5A8  | $D5CC  |
| $71  | $D62F    | $D650  | $D65A  | $D683  |

## Stats

- **28 / 28 per-state bodies lifted** (no per-state stub remains).
- **2 narrow TODOs** carried forward inside lifted bodies:
  - `type61_state1` — body has `BEQ +1` then falls past the 0x80-byte
    disassembly window; we lift the timer decrement but the
    timer-expired action is left as a no-op.
  - `type69_state0` — references two adjacent ROM tables at $D1D5
    (byte velocity, 16 entries) and $D1E5 (word velocity, 16 entries).
    The tables themselves are outside the 0x80 window; lifted as
    `rom_D1D5_byte_table[16]` / `rom_D1E5_word_table[16]` zero-initialized
    arrays with a TODO comment. Control flow is correct; the actual
    velocity values still need to be extracted from ROM.

## Range-specific patterns

### "INIDISP=$0F + INC state" state1 (six handlers in a row)
Types $6B, $6C, $6D, $6E, $6F, $70 all share the identical state1:

    LDA #$0F ; STA $2100 ; INC $0001,x ; RTS

This is the save-picker / sound-options widget family — the second
state of each unconditionally re-enables full-brightness display and
advances.  It's the "we just finished a DMA upload, turn the screen
back on" idiom.

### DMA-setup state0 family ($6B, $6C, $6D, $70)
All four state0s zero $A2 (or set $80 for $6D), program $A4/$A6/$A8
DMA-context words, then `JSL $00:8B98`.  $6D differs from $6B/$6C only
by `LDA #$80` instead of `STZ` for $A2.  This confirms F3's hypothesis
that $6B/$6C/$6D form a save-slot triplet — they're three rows of the
same widget, distinguished by which force-vblank flag is set during
their VRAM upload.

### Sound-options widget ($71)
State0 programs a $2800-byte DMA from $0000, an unusual $FFC0 tile-bias
(signed -64), and a $40-byte chunk size, then calls $00:8C54 (variant
draw-branch A).  The "row index < 3" branch F3 noted in the dispatcher
is *outside* the per-state bodies — the dispatcher selects between
$00:8C54 and $00:8C41 *before* dispatching, then state0 does the
DMA-setup work.

### Decoration child icon ($67)
State1 is the only entity in this range that does non-trivial draw
work: jitters x and y by `(frame_clock & 7) - 4` (signed -4..+3),
stashes entity[$06] in DP $44 across the camera prologue (so $DB52 sees
a temporary attr override), and self-destructs after $10 frames if
the global clock byte equals 4.  This pairs cleanly with $66's
list-iterator that spawns these as children.

### Audio-tick cousin ($65)
State1 of $65 mirrors $73's audio-blip pattern exactly: derive Y from
`($0011 & 7) + 1`, derive X from a base offset $F5E0 plus the $000C
counter, fire `sub_0088FF(Y, X, 1)`, then walk $000C down by 2 per
frame, wrapping to $40 and incrementing $0011 on underflow.  $65 and
$73 are the two audio-decoration entities in the credits/Mode-7 page.

## Files edited

- `/Users/guilhermedavid/simant-re/entities_g.c` — replaced 28 weak
  stubs with full bodies; added a small block of extra externs (sub_DCD5,
  sub_008A0E_div256, sub_04AAD0), a `ppu_io[]` stand-in for $2100/$2132
  writes, and DP-slot macros for $A2/$A4/$A6/$A8/$AE/$44/$46/$48/$9A/$9C.

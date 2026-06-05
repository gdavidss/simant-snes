#!/usr/bin/env python3
"""
Python reference of the SimAnt SNES RNG ($04:DCD5 + $04:DCFE), translated
directly from the 65816 disassembly. Used as a ground truth for the C lift.

DCD5 (PRNG step):
    48          PHA                   ; save modulus
    A5 2A       LDA $2A
    0A          ASL                   ; *2
    0A          ASL                   ; *4
    18 65 2A    CLC; ADC $2A          ; +$2A -> *5
    18 69 01    CLC; ADC #$01         ; +1
    85 2A       STA $2A               ; $2A := $2A*5 + 1
    06 2B       ASL $2B               ; carry = old bit7; $2B <<= 1
    A9 20       LDA #$20              ; for BIT-test
    24 2B       BIT $2B               ; Z = (new$2B & $20) == 0
    B0 04       BCS $DCEE
    F0 04       BEQ $DCF0             ; carry=0 path: Z=1 -> INC
    80 04       BRA $DCF2             ; carry=0, Z=0 -> skip
$DCEE F0 02     BEQ $DCF2             ; carry=1 path: Z=1 -> skip
$DCF0 E6 2B     INC $2B               ; carry==bit5_of_new_2B -> INC
$DCF2 A5 2B     LDA $2B
       18 65 2A CLC; ADC $2A          ; A = new$2B + new$2A (mixed)
       EB       XBA                   ; B = mixed
       68       PLA                   ; A = saved modulus
       20 FE DC JSR $DCFE             ; A.high|.low = modulus * mixed
       EB       XBA                   ; return high byte
       60       RTS

DCFE (8x8 -> 16-bit unsigned multiply, classic shift-and-add):
    BF := A (modulus)
    C0 := B (mixed) [via XBA]
    BE := 0
    Y  := 8
loop:
    ASL $BE / ROL $BF        ; shift BE:BF left by 1, carry out from bit7 of BF
    BCC skip                 ; if carry clear, don't add
    $BE += $C0 (set carry)
    $BF += 0 + carry
skip:
    DEY; BNE loop
    return (A=BE, B=BF)      ; XBA at caller flips, so return value is $BF
"""

class RngState:
    __slots__ = ("s2A", "s2B")
    def __init__(self, s2A: int, s2B: int):
        self.s2A = s2A & 0xFF
        self.s2B = s2B & 0xFF

def _mul8_correct(a: int, b: int) -> int:
    """Direct port of $04:DCFE, instruction-faithful."""
    BF = a & 0xFF
    C0 = b & 0xFF
    BE = 0
    Y  = 8
    while Y > 0:
        # ASL $BE — shift BE left, capture carry from old bit7
        c1 = (BE >> 7) & 1
        BE = (BE << 1) & 0xFF
        # ROL $BF — rotate BF left through carry
        c2 = (BF >> 7) & 1
        BF = ((BF << 1) | c1) & 0xFF
        # carry-out from ROL is c2 (old bit7 of BF) — tested by BCC
        if c2 == 1:
            # LDA $C0 / CLC / ADC $BE / STA $BE
            sum_lo = BE + C0
            BE = sum_lo & 0xFF
            cy = 1 if sum_lo > 0xFF else 0
            # LDA $BF / ADC #$00 / STA $BF
            sum_hi = BF + 0 + cy
            BF = sum_hi & 0xFF
        Y -= 1
    return (BF << 8) | BE

def rng_step(state: RngState, modulus: int) -> int:
    """One RNG call. Returns the random byte; mutates state."""
    modulus &= 0xFF
    # $2A := $2A*5 + 1 (8-bit, wrapping)
    s = state.s2A
    a = (s << 1) & 0xFF                # ASL
    a = (a << 1) & 0xFF                # ASL  -> a = 4*s
    a = (a + s) & 0xFF                 # CLC; ADC $2A -> a = 5*s
    a = (a + 1) & 0xFF                 # CLC; ADC #$01
    state.s2A = a
    # ASL $2B; capture carry; INC $2B iff carry==bit5(new$2B)
    old_2B = state.s2B
    carry  = (old_2B >> 7) & 1
    new_2B = (old_2B << 1) & 0xFF
    bit5   = (new_2B >> 5) & 1
    if carry == bit5:
        new_2B = (new_2B + 1) & 0xFF
    state.s2B = new_2B
    # mixed = new_2B + new_2A (8-bit wrap)
    mixed = (new_2B + state.s2A) & 0xFF
    # mul: returns 16-bit, take high byte
    prod = _mul8_correct(modulus, mixed)
    return (prod >> 8) & 0xFF

def sequence(s2A: int, s2B: int, mask: int, n: int) -> list[int]:
    st = RngState(s2A, s2B)
    return [rng_step(st, mask) for _ in range(n)]

if __name__ == "__main__":
    # Quick sanity: print 32 bytes for a fixed seed
    bytes_ = sequence(0x12, 0x34, 0xFF, 32)
    print("seed=12,34 mask=FF n=32:", " ".join(f"{b:02X}" for b in bytes_))

    # Verify the multiply against straight integer multiply (mathematically equivalent)
    for a in (0, 1, 2, 5, 0x80, 0xFF, 0x37):
        for b in (0, 1, 0x10, 0x80, 0xFF, 0x37):
            assert _mul8_correct(a, b) == (a * b) & 0xFFFF, (a, b, _mul8_correct(a,b), a*b)
    print("mul8 sanity OK")

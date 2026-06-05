# V4.7 spot-check: 10 random lifted routines

Sampled across 7 different `.c` files (mouse.c, vsync.c, misc_helpers.c,
entities_b.c, simulation.c, audio_driver.c, gaps.c, scent.c, territory.c,
combat.c, entities_d.c, player_actions.c). All disassembly fetched via
`disasm.py` / `disasm_spc.py` — no build/compile.

Verdict legend: **MATCHES** / **SUBTLY-WRONG** / **WRONG** / **UNSURE**.

---

## 1. `mouse_shift_E477` — $00:E477  (mouse.c)

```
00:E477  A5 69       LDA $69
00:E479  F0 16       BEQ $E491
00:E47B  BD 79 00    LDA $0079,x
00:E47E  0A          ASL
00:E47F  90 02       BCC $E483
00:E481  09 80       ORA #$80
00:E483  9D 79 00    STA $0079,x
00:E486  BD 77 00    LDA $0077,x
00:E489  0A          ASL
00:E48A  90 02       BCC $E48E
00:E48C  09 80       ORA #$80
00:E48E  9D 77 00    STA $0077,x
00:E491  C6 69       DEC $69
00:E493  60          RTS
```

C body (mouse.c:182):
```c
if (dp[0x69] != 0) {
    uint8_t y = dp[0x0079 + x];
    dp[0x0079 + x] = (uint8_t)((y << 1) | (y & 0x80));
    uint8_t yh = dp[0x0077 + x];
    dp[0x0077 + x] = (uint8_t)((yh << 1) | (yh & 0x80));
}
dp[0x69]--;
```

The ASM is "ASL; BCC skip; ORA #$80" — i.e. set bit-7 of the result iff
old bit-7 was 1. After ASL, bit 0 of result = 0, so `result = (y<<1) | ((y&0x80)?0x80:0)`.
The C expression `(y<<1) | (y & 0x80)` produces the same byte (the `<<1`
already clears bit 7; OR-ing the *old* bit 7 restores it iff set).
**Verdict: MATCHES.**

---

## 2. `fade_in_85FC` / `fade_out_8616` — $00:85FC / $00:8616  (misc_helpers.c)

```
00:85FC  64 6C / A5 6C / 8D 00 21 / A9 02 / 20 41 88 / E6 6C
        / A5 6C / C9 10 / D0 EE / 60
00:8616  A9 0F / 85 6C / A5 6C / 8D 00 21 / A9 02 / 20 41 88
        / C6 6C / 10 F2 / 60
```

C body (misc_helpers.c:157,175):
```c
void fade_in_85FC(void)  { dp[0x6C]=0; while (dp[0x6C]!=0x10){INIDISP=dp[0x6C];wait_frames_8841(2);dp[0x6C]++;}}
void fade_out_8616(void) { dp[0x6C]=0x0F; do{INIDISP=dp[0x6C];wait_frames_8841(2);dp[0x6C]--;}while((dp[0x6C]&0x80)==0);}
```

Both step INIDISP by 1 every 2 frames; fade_out uses BPL to terminate at
$FF (signed wrap), C uses `(dp[$6C] & 0x80) == 0` — identical exit.
**Verdict: MATCHES.**

---

## 3. `sub_9D1A_blink` — $04:9D1A  (entities_b.c)

```
04:9D1A  AD B2 02 / 1A / CD 4A 02 / F0 01 / 60      ; gate
04:9D24  AD 46 02 / 4A / 18 / 69 C8 / 85 37 / 64 38 ; sprite-X
04:9D2F  AD 48 02 / 4A / 18 / 69 10 / 85 39 / 64 3A ; sprite-Y
04:9D3A  20 71 DC / A9 18 / 85 3D / A9 26 / 85 3B / 20 9E DB / 60
```

C body (entities_b.c:255):
```c
if ((uint8_t)(dp[0x02B2] + 1) != dp[0x024A]) return;
*(uint16_t *)&dp[0x37] = (uint16_t)((dp[0x0246] >> 1) + 0xC8);
dp[0x38] = 0;
*(uint16_t *)&dp[0x39] = (uint16_t)((dp[0x0248] >> 1) + 0x10);
dp[0x3A] = 0;
sub_DC71_apply_camera();
dp[0x3D] = 0x18;
*(uint16_t *)&dp[0x3B] = 0x0026;
sub_DB9E_oam_push();
```

The asm `LDA $0246 / LSR / CLC / ADC #$C8 / STA $37 / STZ $38` is an
**8-bit** add then a separate STZ on $38. If `(dp[$0246]>>1)+$C8` does
NOT fit in 8 bits (e.g. dp[$0246]=$80 → $C8+$40=$108), ASM stores $08 to
$37 and `0` to $38 (carry discarded). The C cast to `uint16_t` and 16-bit
write would store the same low byte to $37 but $01 to $38 — a divergence.
In practice dp[$0246] / dp[$0248] are cursor coords likely well under
$70, so the overflow path may never fire, but it is a divergence.
**Verdict: SUBTLY-WRONG** (overflow handling on the high byte differs;
also the `*(uint16_t *)&dp[0x3B] = 0x0026` writes $26 to $3B and $00 to
$3C, which matches the asm `LDA #$26 / STA $3B` if $3C was already 0 —
the asm does not touch $3C, so divergence depends on prior state).

---

## 4. `colony_health_grade_9E62` — $02:9E62  (simulation.c)

Disassembly (M=0 throughout — 16-bit accumulator):
```
02:9E62 LDA $E776 ; CMP #$000A ; BCS $9E88  (skip H<10 path if H>=10)
02:9E6A LDA $EB60 ; LSR ; CMP $EB62 ; BEQ $9E88 ; BCC $9E88
02:9E75 LDA $EB62 ; BEQ $9E88 ; BMI $9E88
02:9E7C LDA $E766 ; BEQ $9E88 ; BMI $9E88
02:9E83 LDA #0 ; BRA $9EEA          ; H<10 + dominance + R>0 + STARVED>0 -> 0
02:9E88 LDA $E776 ; CMP #$001E ; BCS $9E95 ; LDA #5 ; BRA $9EEA  ; H<30 -> 5
02:9E95 CMP #$0032 ; BCS $9E9F ; LDA #4 ; BRA $9EEA              ; H<50 -> 4
02:9E9F LDA $E746 ; CMP $EB60 ; BCS $9EAC ; LDA #3 ; BRA $9EEA  ; food<pop -> 3
02:9EAC LDA $EB60 ; ASL ; CMP $E746 ; BEQ $9EBC ; BCC $9EBC
        ; LDA #2 ; BRA $9EEA                                    ; pop*2>food -> 2
02:9EBC LDA $EB60 ; CMP #$0064 ; BEQ $9EE7 ; BCC $9EE7   ; pop<=100 -> 1
02:9EC6 LDA $EB62 ; BEQ $9EE7 ; BMI $9EE7
02:9ECD LDA $E766 ; BEQ $9EE7 ; BMI $9EE7
02:9ED4 LDA $EB60 ; JSL $02F5F1 ; CMP $EB62 ; BEQ $9EE7 ; BCC $9EE7
        ; LDA #0 ; BRA $9EEA                              ; pop/3>R -> 0 (Thriving!)
02:9EE7 LDA #1 ; RTL
```

C body (simulation.c:861) checks `H<10`, `b_pop/2 > r_pop`, `r_pop > 0`,
`(int16_t)r_pop >= 0`, `starved > 0`, `(int16_t)starved >= 0`, returns 0;
then ranges 30/50/food/pop2 cases for 5/4/3/2; "Thriving" path checks
`b_pop > 100` AND R>0/STARVED>0 AND `b_pop / 3 > r_pop` → return 0.
The Thriving branch returning 0 (instead of, say, "6") is the lift's
honest mapping of the asm. **Verdict: MATCHES.**

---

## 5. `set_master_volume_074C` / `start_fade_0758` — SPC $074C / $0758  (audio_driver.c)

```
074C  MOV !$F2 ; DSPADDR ,#$0C    ; select DSP $0C (MVOL_L)
074F  MOV !$F3 ; DSPDATA ,A
0751  MOV !$F2 ,#$1C              ; select DSP $1C (MVOL_R)
0754  MOV !$F3 ,A
0756  SETC ; 0757 RET
0758  LSR A ; INC A ; MOV X,A     ; X = (a>>1)+1, 1..8
075B  MOV $1B,A
075D  MOV A,#0 ; MOV $1A,A         ; $1A:1B = X*256
0761..0768  LSR $1B ; ROR $1A  ; LSR $1B ; ROR $1A   ; >>2 -> X*64
0769  MOV $1E,#$FF ; MOV $1F,#$7F
       (continuing) MOV A,$1F ; MOV Y,#0 ; DIV YA,X ; MOV $1D,A
                  MOV A,$1E ; DIV YA,X ; MOV $1C,A
       6× (LSR $1D ; ROR $1C)
```

C body (audio_driver.c:626, 646):
```c
dsp_set(DSP_MVOL_L, a); dsp_set(DSP_MVOL_R, a);
...
uint8_t x = (a >> 1) + 1;
uint16_t total_ticks = x * 64;
spc_ram[0x1A] = lo;  spc_ram[0x1B] = hi;
spc_ram[0x1E] = 0xFF; spc_ram[0x1F] = 0x7F;
uint16_t step = (0x7FFF / x) >> 6;
spc_ram[0x1C] = lo;  spc_ram[0x1D] = hi;
```

DSP addrs $0C / $1C are MVOL_L / MVOL_R. The DIV YA,X pair builds
$7FFF/X across two passes (high byte first, remainder kept in Y by the
SPC700 DIV op), then >>6. Matches the explicit C formula.
**Verdict: MATCHES.**

---

## 6. `rng_byte_DCD5` — $04:DCD5  (gaps.c)

```
04:DCD5 PHA / LDA $2A / ASL / ASL / CLC / ADC $2A / CLC / ADC #1 / STA $2A
        ASL $2B / LDA #$20 / BIT $2B
04:DCE8 BCS $DCEE / BEQ $DCF0 / BRA $DCF2
04:DCEE BEQ $DCF2
04:DCF0 INC $2B
04:DCF2 LDA $2B / CLC / ADC $2A / XBA / PLA / JSR $DCFE (mul) / XBA / RTS
```

C body (gaps.c:100):
```c
uint8_t a = (uint8_t)(dp[0x2A] * 5 + 1);  dp[0x2A] = a;
uint8_t b = dp[0x2B];
uint8_t bit7 = (b>>7)&1, bit4 = (b>>4)&1;
b <<= 1;
if (bit7 == bit4) b |= 1;  /* XNOR feedback */
dp[0x2B] = b;
return ((uint16_t)(b + a) * mask) >> 8;
```

LCG: `(a<<2)+a+1 = a*5+1` ✓. LFSR feedback: ASL captures old bit 7 into
carry; `LDA #$20 / BIT $2B` tests bit 5 of the NEW $2B (which is old
bit 4). The 4-way branch implements: INC iff old-bit-7 == old-bit-4 — i.e.
XNOR. ✓. Final byte = (b + a) then multiplied by mask, high byte returned.
**Verdict: MATCHES.**

---

## 7. `map_set_current_area_00968F` — $00:968F  (gaps.c)

```
00:968F  EB           XBA
00:9690  A9 00        LDA #$00
00:9692  EB           XBA               ; A = $00:A_orig — zero-extend A8
00:9693  C2 20        REP #$20
00:9695  48           PHA
00:9696  AF 38 E7 7F  LDA $7FE738       ; cur_y (16-bit)
00:969A  0A 0A 0A     ASL ; ASL ; ASL   ; cur_y << 3
00:969D  18 6F 36 E7 7F  CLC ; ADC $7FE736 ; +cur_x
00:96A2  0A AA        ASL ; TAX         ; X = offset << 1
00:96A4  68           PLA
00:96A5  9F 46 EA 7F  STA $7FEA46,x     ; (b_pop word)
00:96A9  98 9F C6 EA 7F TYA ; STA $7FEAC6,x  ; (r_pop word from Y)
00:96AE  E2 20 / 60   SEP #$20 / RTS
```

C body (gaps.c:372):
```c
uint16_t off = ((W16(0xE738) << 3) + W16(0xE736)) << 1;
SW16(0xEA46 + off, (uint16_t)b_pop);
SW16(0xEAC6 + off, r_pop);
```

The XBA/LDA#0/XBA pattern is a standard 8-bit-A → 16-bit zero-extend
in M=1 (saves and restores A_lo through the B register). Both b_pop and
r_pop end up written as 16-bit. The shift+add+shift index formula
matches.
**Verdict: MATCHES.**

---

## 8. `scent_consume_trail_03_9419` — $03:9419  (scent.c)

```
03:9419 PHA / TXA ; LSR ; TAX / TYA ; LSR ; TAY     ; halve pixel coords
03:9420 JSL $02F5A8                                  ; X = cell offset
03:9424 PLA / CMP #$0000 / BEQ $943B                 ; arg==0 -> BLACK path
03:942A SEP #$20 / LDA $5800,x / BEQ skip / BMI skip ; RED trail
03:9433 DEC ; STA $5800,x
03:9437 REP #$20 ; BRA $944A
03:943B SEP #$20 / LDA $5000,x / BEQ skip / BMI skip ; BLACK trail
03:9444 DEC ; STA $5000,x
03:9448 REP #$20 / RTL
```

C body (scent.c:282):
```c
uint16_t idx = scent_index_F5A8(x>>1, y>>1);
uint16_t base = (arg != 0) ? SCENT_RED_TRAIL : SCENT_BLACK_TRAIL;
uint8_t cur = WRAM_7F(base + idx);
if (cur == 0)    return;
if (cur & 0x80)  return;
WRAM_7F(base + idx) = (uint8_t)(cur - 1);
```

SCENT_RED_TRAIL = 0x5800, SCENT_BLACK_TRAIL = 0x5000 (verified at
scent.c:158). arg==0 → black; arg!=0 → red. ✓.
**Verdict: MATCHES.**

---

## 9. `neighbour_balance_F2D9` — $03:F2D9  (territory.c)

```
03:F2D9 PHX/PHY/PHA/PHA           ; reserve 2 16-bit stack slots: i, bal
03:F2DD STZ bal ; STZ i
03:F2E7 loop: LDA i ; CMP #6 ; BEQ exit
        ASL ; TAX ; LDA $F34C,x (dy) ; ADC $05,s (cy) ; TAY
        LDA $F340,x (dx)            ; ADC $07,s (cx) ; TAX
        CPX #0 / BMI skip ; CPY #0 / BMI skip
        CPX #7 / BCS skip ; CPY #7 / BCS skip
        JSL $02F5B2                 ; X = area cell offset
        LDA $EA46,x / BEQ a ; bal += 3 a:
        LDA $EAC6,x / BEQ skip ; bal -= 3 skip:
        INC i ; BRA loop
03:F339 exit: LDA bal ; PLY x4 ; RTL
```

Tables $03:F340 = `{0,+1,0,-1,0,0}` (dx), $03:F34C = `{-1,0,+1,0,0,0}` (dy)
(I verified by reading the raw words.)

C body (territory.c:506):
```c
static const int8_t dx[6]={0,+1,0,-1,0,0};
static const int8_t dy[6]={-1,0,+1,0,0,0};
int16_t bal = 0;
for (int i = 0; i < 6; ++i) {
    int nx = cx + dx[i], ny = cy + dy[i];
    if (nx<0 || ny<0 || nx>=7 || ny>=7) continue;
    if (AREA_B_POP(nx,ny) != 0) bal += 3;
    if (AREA_R_POP(nx,ny) != 0) bal -= 3;
}
return bal;
```

**Verdict: MATCHES.**

---

## 10. `combat_mark_tile_99A0` — $03:99A0  (combat.c)

```
03:99A0 ASL ; TAX                              ; idx*=2
03:99A2 LDA $E882,x ; STA $F015                ; ARG_TILE_X = COMBAT_X(i)
03:99A8 LDA $E88E,x ; STA $F017                ; ARG_TILE_Y = COMBAT_Y(i)
03:99AE LDA $E8B2,x ; CLC ; ADC #$0038 ; STA $F019  ; ARG_TILE_VALUE = FRAME+0x38
03:99B8 LDA #$0001 ; STA $F013                 ; ARG_TILE_KIND = 1
03:99BE JSL $03A689 ; RTL
```

C body (combat.c:397):
```c
unsigned i = combatant_idx;
ARG_TILE_X     = COMBAT_X(i);
ARG_TILE_Y     = COMBAT_Y(i);
ARG_TILE_VALUE = COMBAT_FRAME(i) + 0x38;
ARG_TILE_KIND  = 1;
tilemap_write_A689();
```

All five base addresses (E882, E88E, E8B2 for the entity table;
F015/F017/F019/F013 for the tilemap-write argument block) line up with
the macros defined in combat.c (verified inline).
**Verdict: MATCHES.**

---

## 11. `sub_AA2A_step_y` — $04:AA2A  (entities_d.c)  *(bonus — flagged bug)*

```
04:AA2A CLC ; ADC $0013,x ; STA $0013,x          ; cursor13 += step
04:AA31 JSL $008A0E                              ; sin*Y helper
04:AA35 REP #$20 ; CLC ; ADC $0009,x ; STA $0004,x ; y = target_y + sin
04:AA3E SEP #$20 / RTS
```

C body (entities_d.c:252):
```c
self->anim_cursor_13 += step;
uint16_t sin_v = (uint16_t)sub_008A0E_div256(self->anim_cursor_13);
self->y = (uint16_t)(self->target_y + (int16_t)sin_v);
```

Two issues at the helper boundary:

1. **Signature mismatch.** entities_d.c (line 116) declares
   `extern uint8_t sub_008A0E_div256(uint8_t a);`
   but the definition in lifted_helpers_1.c:216 is
   `uint16_t sub_008A0E_div256(uint8_t a, uint8_t y);`
   Wrong return type AND missing argument — undefined behaviour at the
   call site.
2. **Missing Y arg.** The ROM helper at $00:8A0E reads the caller's Y
   register (`STY $C2`) as the second multiplicand for a 16x16→32 then
   `>>16`. The lift never passes it, so the result depends on whatever
   was left in `Y_param`. The ROM caller at $04:AA31 relied on the
   previous routine to set Y; the C lift silently loses that.

ASM also stores the 16-bit return into a 16-bit add — but the C extern
declaration narrows to `uint8_t`, then later code does
`(uint16_t)(...) << 8` (line 495) treating the byte as if it were the
high half. Net: drift the moment any sine call wants a magnitude > 1
pixel.

**Verdict: WRONG.** (Flagged for fix; do NOT auto-fix in this pass —
multiple call sites need the same correction.)

---

# Bonus context — also reviewed (single-RTS routines)

- `fade_in_85FC` / `fade_out_8616`: MATCHES (item 2)
- `sub_A3D6` (vsync.c): MATCHES — straight 6-call sequence with two
  DP byte-copies.
- `sub_DEEE` (vsync.c): MATCHES — `!(B1==0 && B4!=0)` gate matches the
  asm `LDA B1 / BNE run / LDA B4 / BEQ run / RTS`.

# Tally

- Files sampled: mouse.c, vsync.c, misc_helpers.c, entities_b.c,
  simulation.c, audio_driver.c (SPC), gaps.c, scent.c, territory.c,
  combat.c, entities_d.c, player_actions.c.
- Routines spot-checked (primary): 10 (+ 1 bonus AA2A bug)
- MATCHES: 8 (mouse_shift, fades, colony_health, set_master_volume +
  start_fade, rng_byte, map_set_current_area, scent_consume_trail,
  neighbour_balance, combat_mark_tile)
- SUBTLY-WRONG: 1 (sub_9D1A — 8-bit ADC overflow vs 16-bit store)
- WRONG: 1 (sub_AA2A_step_y — wrong extern decl + missing Y arg for
  sub_008A0E_div256)

# V4-1: SimAnt Decomp Comment-vs-Body Audit

Audit pass over /Users/guilhermedavid/simant-re/ — flagging divergences
between docstrings/inline comments and the C body. PURE READ pass; no
edits applied.

**Severity legend**
- **cosmetic**: harmless typo / wording slip
- **misleading**: doc claims something the body does not match, could
  mislead a reader but does not silently corrupt
- **dangerous**: doc claims behavior that ROM also implements, but body
  silently does something different (wrong write, wrong call, wrong
  arithmetic) — would cause behavioral divergence if executed.

Files scanned: 44 .c files (skipping the 5 large asset_data_*.c blobs
which are purely data tables).

---

## stubs.c
- L4–7 vs L23: header docstring claims "Provides `dp` as a linker alias
  of `wram`" but the inline comment at line 23 says
  "`dp` is provided as `#define dp wram` in each .c file". Doc
  contradicts itself. **cosmetic**.

## stubs_for_test.c, stubs_test_extras.c, rng_state_test.c
- No findings.

## rng_diff_test.c
- L6 docstring says output is `"byte $XX (or just XX hex)"`; body at L41
  emits only `"%02X\n"`. **cosmetic**.

## vsync.c
- **L116–117 (sub_E527, inner loop):** comment says
  "*(only even iters bump $B6)*" but body increments `dp[$B6]` only in
  the `else` branch (s == 1 or s == 3, i.e. ODD iterations).
  Comment-vs-body inverted. **dangerous**.
- L141 `sub_DEEE` body just early-returns; L149 TODO acknowledges
  "body at $00:DEF9 not yet fully lifted". **misleading** (function
  comment narrates behavior the body never runs).
- L176 `sub_DF79` doc narrates the SEC-path body, but the C body never
  implements it. **misleading**.
- L190 `sub_A3D6` doc describes work; body just calls helpers without
  the `dp[$0026]` zero-on-SELECT pattern claimed. **misleading**.

## mouse.c
- L168–179 `mouse_shift_E477` doc: "DEC-even-when-zero behavior is
  faithful to the ROM". Body L184–193 indeed DECs unconditionally —
  matches. (Cross-reference for sanity.)
- L211–213 docstring at L207–209 mentions "On failure after 256 retries:
  dp[$0075+x] = $80" but the early "no mouse" branch ALSO writes $80
  (L215) — not mentioned in the abstract. **cosmetic**.

## lifted_helpers_1.c
- **L173–207 `fixed_sincos_table` / `sub_008A0B_div256r`:** docstring
  claims sine lookup at `$01:8020` via mult-by-Y, but body returns 0
  unconditionally (L206 "Return 0 placeholder"). Function is a silent
  stub even though the doc treats it as faithful. **dangerous**.
- L66 `extern const uint8_t rom_018020[]` declared but never used in
  this TU. **cosmetic**.
- L297–317 `sub_866E` doc says "VRAM fill 0x400 entries (16-bit), source
  = caller's 16-bit A"; body's loop count is governed by
  `(a & 0x03FF) != 0`, which is 1024 writes regardless of starting A —
  matches the doc's "1024 entries" but contradicts the literal "0x400
  entries" header line. **cosmetic**.
- L322 `sub_867F` doc says "A.high(EBA) = high, A.low = low" but the C
  signature is `void sub_867F(uint8_t a, uint16_t x)` (only 8-bit a).
  **misleading**.
- L367 `extern void vram_dma_from_scratch_8ACC` declared but no body in
  this TU (strong def in lifted_helpers_6.c). **cosmetic**.

## lifted_helpers_2.c
- L40–43 `sub_DC84` doc says "edge bits dp[$007B] & $03"; body L53
  matches. OK.
- L75–85 `sub_DCD5` PRNG narrative matches body — the BIT/INC logic
  collapse to "INC iff carry==bit_set" is explicit and consistent.
- L132 `sub_D6F6` doc: `dp[$3D] = [$85],y | (X+$0F byte)` — body L150
  uses `WMEM8(0x0044 …) | WMEM8(x + 0x0F)`. Matches.
- L262 `sub_DB40`: doc reads "A,Y = an extra (x,y) to add to the
  entity's coords"; body matches.

## lifted_helpers_3.c
- **L353 `sub_DF0A`:** doc says
  `x = ($6D * 6 + $6C) * 2`; body L364 computes
  `x = ((row * 3 + mode) * 2)` — `row*3` not `row*6`. Index will be off
  by `row*6`. **dangerous** (changes table offset).
- **L240–274 `sub_C4BB`:** comment explicitly states that `$2C` is the
  queue position and that C4BB "does NOT write back to $2C". Body
  L265–273 USES `$2C` as both the packed coordinate AND the queue
  position, and writes back `pos + 2` to `$2C`. Body contradicts its
  own comment. **dangerous**.
- L158 `sub_CFB9_panel_xform` is `__attribute__((weak))` with empty
  body, but the doc above says it computes `(input * scaler) / 100`.
  Body comment at L159 acknowledges. **misleading**.
- L356 `sub_8F08` doc says `Y' = ROM table[$8F27/$8F33/$8F3F]` then body
  reads from weak placeholder arrays initialised to 0. Real LUT not
  linked. **misleading**.

## lifted_helpers_4.c
- L20–22 doc lists 8941 as `STY $2E` "falls into 8943=RTS" — body
  L237 `sty_to_2E_then_rts_8941(uint8_t y)` writes `dp[$2E] = y`.
  Consistent.
- **L350-353 vs L405–411 `scatter_R_initial_886D`:** body writes
  `BG3HOFS` twice in a row (L407, L408) — both should likely be
  distinct registers (BG2HOFS then BG3HOFS), with `BG2VOFS` already
  taken at L406. The repeated `BG3HOFS` two lines apart looks like a
  copy-paste error. Doc claims "BG-VOFS-shadow loader". **dangerous**.
- L186 `sub_DD24`'s C body uses a local `uint16_t x = 0` annotated
  "set by caller, opaque" — caller-side input is never propagated.
  Function effectively does nothing useful. **misleading**.
- L575 `sub_0490D2` doc says "$04:90D2 pushes DBR to dp[$D2]"; body
  writes literal `0x04` (the bank constant) to `dp[$D2]`, not the
  current DBR value. **misleading**.

## lifted_helpers_5.c
- L92 doc "if ($15 < $08)" vs body L99 `if (low_limit >= coord)` —
  equivalent at the boundary, but L94's "if ($14 > $X) { $14 = $X-1 }"
  vs body L104 `if (high_limit <= coord) { coord = high_limit-1 }` may
  be off-by-one (`>=` vs `>`). **cosmetic**.
- **L269–273 `sub_9D48`:** doc says "yield, then if dp[$28] >= 6, RTS"
  — body just calls `sub_877D()` with no check, no RTS path. The
  conditional/return is dropped. **misleading**.

## lifted_helpers_6.c
- L259–264 `sub_8B0C` doc claims "zero $7E:2000..$7E:9FFF then stamp
  16-byte counter"; body zeros 0x8000 bytes and acknowledges
  "We don't have to be exact". **misleading**.
- L527–545 `sub_EB58` doc says "decompress 64x128 tilemap to $7F:6000";
  body just writes zeros. **misleading**.
- **L626–630 `rand_modulo_F3BD`:** doc says "the LCG" reference at
  `$02:F3BD`; body uses a brand-new internal `lcg_state` initialized to
  `0xACE1` with a `1103/12345` constants — NOT the ROM's LCG. Output
  will differ from ROM. **dangerous**.
- L575 `sub_0490D2` writes literal 0x04 to dp[$D2] same issue as
  helpers_4.

## misc_helpers.c
- L52–77 PPU init table comment annotates entries by index
  (`[0]`, `[10]`, `[48]` etc.) but the actual table layout no longer
  matches: e.g. TM=$17 is at index 20, not 48 as the doc states.
  **cosmetic**.
- L130 `apu_play_sfx_8EA3` body checks `dp[0x0036]` for SFX-enable; the
  lifted_helpers_1.c twin at L121 checks `dp[$0033+y]`-style for Y=3
  ($36). They agree numerically. OK.
- L320–339 `scroll_surface_view_A106` uses literal `$09` for snap-back;
  doc's `low_limit + 1` is consistent at low_limit=$08. OK.

## assets.c
- Mostly data tables; no obvious body-vs-comment mismatches in the
  asset_table[] payload. (Cross-checking the per-view dispatch tables
  for byte-exact ROM fidelity is V4-7's job.)

## audio_intro.c
- **L74:** `extern void sub_8F08(void)` declared no-args, but
  lifted_helpers_3.c defines `sub_8F08(uint8_t a)`. Site at L490 calls
  with `sub_8F08()` — undefined behavior. **dangerous**.
- **L68:** `extern void caption_screen_BACA(uint8_t, uint16_t)` declared
  with 2 args; lifted_helpers_4.c defines as `(uint8_t)` 1 arg. Multiple
  call sites in this file pass 2 args — signature mismatch. **dangerous**.
- L582 doc says `TM = BG1+BG3` while writing `$11` to `$212C`; the bits
  are `0=BG1, 4=OBJ` — so the byte enables BG1+OBJ, not BG1+BG3.
  **misleading**.
- L583 doc says `TS = BG2+BG3` while writing `$0C` (bits 2+3 = BG3+BG4).
  **misleading**.
- L586–587 writes to MMIO `$2188` commented "(CGADSUB tweak)" but
  CGADSUB is at `$2131`; `$2188` is undefined in PPU/MMIO. **misleading**.
- L411 vs L411 numeric mismatch: doc text mentions "the Maxis Ant
  Heads at $01:9850" but credits_pages[22]=0x981E and [23]=0x9871.
  **misleading**.

## audio_driver.c
- L242–245 forward decls for `song_event_dispatch_099B(uint8_t *yp)` and
  per-event handlers `event_*_0Axx(uint8_t *yp)` use pointer args, but
  the bodies (L1014, L1196+) define them as `(void)`. **dangerous**
  (signature mismatch; though all calls within the file use the
  parameter-less form, an extern caller using the declared signature
  would be wrong).
- L1187 doc says "$0A9D — event_keypress: read 'transpose' byte →
  $0028+X" but body L1220 writes to `0x0028 + spc_ram[$00]` (= port,
  not the X=voice). **misleading**.
- L1142 `event_note_0A46`: doc claims `wram[$50+X] = duration` (where X
  is the voice slot); body uses `spc_ram[0x07]` as a Y stash that the
  caller does not maintain consistently. **misleading**.

## combat.c
- **L913–924 `yellow_ant_attack_red_simulate`:** body calls
  `combatant_append_96B0(R_X(target_idx), target_idx)` — passes
  `target_idx` (an index, 0..N) as the Y coordinate. The proper call
  would pass `R_Y(target_idx)` (the target's Y coord), if such a macro
  existed. **dangerous** — wrong argument to a kill-pool insert.
- L66, L507, L590, L668, L764, L1049, L1096, L1123, L1199 all contain
  self-acknowledged "earlier draft / V2 lift / inverted" notes. These
  are repaired in current bodies, but indicate active drift.
- L805 (case 0): doc says "NOT silent"; the silence vs SFX distinction
  is encoded only through the lack of `apu_play_sfx_008EA3()` in the
  case 0 body. Comment is fine but a reader could miss that the SFX
  call really is absent. **cosmetic**.

## scent.c
- L521–524 self-acknowledged "An earlier draft of this file treated it
  as a 64×32 grid of 16-bit cells — that's wrong". Body now treats it
  as 1-byte cells. **cosmetic** (already fixed).

## simant.c (skeleton + stubs)
- L1116–1148: ~25 functions declared static with empty bodies and a
  `/* TODO */` marker, but the surrounding narrative in the file body
  (gs_full_game, gs_sound, etc.) calls them as if functional. The
  whole file is effectively a sketch — every call into these stubs is
  a comment-vs-body divergence. **misleading** (consistently labelled).
- L557 acknowledges "previous lift was wrong — verified at $00:8937
  the body walks". **cosmetic** (already fixed).

## player_actions.c, player_actions_full.c
- player_actions.c L123 macro comment "$A7 - cursor Y (Y inverted)" —
  but the `DP_NEST_CURY_A0` is defined as `0xA0` (not `0xA7`). The
  comment refers to the formula `$A7 - dp[$15]` used to compute the
  value stored AT `$A0`. The macro name vs comment formula could
  confuse. **cosmetic**.
- player_actions_full.c L688 has a self-noted "lift had this inverted:
  BEQ branches when AND result is 0…". **cosmetic** (already fixed).

## simulation.c, territory.c, gap_fillers.c, gaps.c
- Multiple self-acknowledged "earlier draft used WMEM8 which made these
  8-bit accidentally" notes. **cosmetic** if already fixed.
- gap_fillers.c L349 defines a literally-named `rng_seed_XXXX(void)` —
  the symbol name still contains the meta-placeholder `XXXX`. Doc at
  L1001 says "Prompt requested: void rng_seed_XXXX(void) — provided
  above." **misleading** (intentional? Indicates an unfilled
  placeholder).

## states_menu.c, states_gameplay.c, control_panels.c, save_options.c,
## text_screens.c, text_content.c, scenarios.c, ui_menus.c,
## entities_a/b/c/d.c, render_helpers.c
- Skimmed; no further egregious docstring-vs-body mismatches beyond
  the self-acknowledged "earlier draft" notes already counted. Long
  state handlers are mostly faithful narration of the ROM body.

---

## Summary

- Files reviewed: **40 functional .c files** (44 minus 4 large
  asset_data_*.c data blobs that contain only arrays).
- Findings: **~38** distinct comment-vs-body divergences flagged.
- Self-acknowledged in-source notes ("earlier draft", "V2 lift",
  "inverted", "off by") found in 11 files — most are already fixed in
  current bodies but indicate active drift hot-spots.

### Top 5 most dangerous comment-body divergences

1. **lifted_helpers_1.c L173–207 — `sub_008A0B_div256r` / `sub_008A0E_div256`
   (sine/cosine)** are documented as fixed-point sin/cos using the ROM
   LUT at `$01:8020`, but the body returns `0` unconditionally. Every
   caller that expects a real value gets zero — entity heading-to-velocity
   conversion (`sub_D721_set_velocity_from_heading`) is broken.

2. **lifted_helpers_6.c L626 — `rand_modulo_F3BD`** is documented as
   the ROM's LCG at `$02:F3BD`; body uses a brand-new internal seed and
   multiplier (`0xACE1`, `1103+12345`). Combat probabilities, scatter
   spawn positions, and RNG-diff tests will not match the ROM.

3. **combat.c L919 — `yellow_ant_attack_red_simulate`** calls
   `combatant_append_96B0(R_X(target_idx), target_idx)` passing the
   array index as the Y coordinate of a new combat-pool entry. The
   correct value should be the target's Y, not the index. This
   misplaces every Yellow-Ant-attacks-Red engagement in the combat
   pool's geometry.

4. **lifted_helpers_3.c L353/L364 — `sub_DF0A`** doc says
   `x = ($6D * 6 + $6C) * 2` to index `$7F:E796`, but body computes
   `x = (row * 3 + mode) * 2`. Off by `row * 6` bytes — wrong scroll
   commit table entry will be read each tick.

5. **lifted_helpers_4.c L405 — `scatter_R_initial_886D`** writes
   `BG3HOFS` twice consecutively (lines 407–408). Likely a copy-paste
   that should have written `BG2HOFS` or `BG3VOFS`. R-scenario BG
   scroll initialisation has a wrong register write each call.

Honourable mention: **audio_intro.c L74/L68 signature mismatches**
between this file's `extern` declarations and lifted_helpers_3/4.c
definitions — relies on C99 unspecified-args coercion to link, but
arguments are silently ignored at the call sites.

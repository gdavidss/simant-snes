/*
 * lifted_helpers_4.c — Batch 4: micro-helpers and JSL-thunks.
 *
 *   $00:E260  STZ $21      — clear cursor state byte
 *   $00:E201  if dp[$0002] == 4: call $E263 (run-mode entry)
 *   $00:E66A  emit byte to BG3 tile buffer via [B1],y under mask BD
 *   $00:E680  if dp[$02B1] non-zero: jump to $E6BB (return-path)
 *   $00:E7C6  call $E7D6, then dp[$88]=9
 *   $00:E7CE  call $E844, then dp[$88]=$0A
 *   $00:E939  call $E95F + $E9CD, then dp[$88]=$0B
 *   $00:E944  call $EAA9, then dp[$88]=$0C
 *   $00:E94C  call $E96B + $EA14, then dp[$88]=$0B
 *   $00:E957  call $EAFE, then dp[$88]=$0C
 *   $00:E79B  pack dp[$93:$94] for VRAM index (BG2)
 *   $00:E7A8  pack dp[$93:$94] for VRAM index (BG3, /4)
 *   $00:E7B7  pack dp[$93:$94] for VRAM index (BG4, alt)
 *   $00:E259  unpack dp[$25:$26] into A,X
 *   $00:8E06  search SRAM ($7F:C3E8/$7F:CBB8) for area match
 *   $00:8941  STY $2E (single-instruction "store Y to dp[$2E]"; falls into 8943=RTS)
 *   $00:8943  RTS  (the actual BG-VOFS-shadow loader is $00:886D — see scatter_R_initial_886D)
 *   $00:876E  clamp dp[$11] negative -> zero
 *   $00:88FF  palette upload to BG-data scratch ($0F20)
 *   $00:8D7E  asset decompress to scratch ($02D1/$02CF/$02D4/$02D6, JSL $028010)
 *   $00:D4B5  per-frame cleanup walk over dp[$45..]
 *   $00:8E88  alt entry (lifted earlier — re-aliased)
 *   $00:DC84  (lifted earlier in batch 2)
 *   $00:BACA  caption blit wrapper: PHA + JSR $BAD3 + PLA + JSR $BA9E
 *   $00:BA9E  wait until system second (dp[$02]) advances by A
 *   $00:8841  delay frames (lifted in batch 1)
 *   $00:E260  STZ $21 (cursor)
 *   $00:DD24  per-frame "16-tile column" loader
 *
 * Source: SimAnt (USA) SNES ROM.
 */

#include <stdint.h>

extern uint8_t           wram[];
extern volatile uint8_t  mmio[];
#define dp wram
#define WMEM8(off)   (*(uint8_t  *)&wram[(off)])
#define WMEM16(off)  (*(uint16_t *)&wram[(off)])
#define MMIO8(addr)  (*(volatile uint8_t  *)&mmio[(addr) & 0xFFFF])

#define BG2HOFS    MMIO8(0x210F)
#define BG2VOFS    MMIO8(0x2110)
#define BG3HOFS    MMIO8(0x2111)
#define BG3VOFS    MMIO8(0x2112)

extern void sub_877D(void);
extern void sub_8841(uint8_t a);
extern void sub_BAD3(void);            /* tile clear helper */
extern void sub_BA9E_wait_sec(uint8_t a); /* forward decl */

extern void sub_E7D6(void);
extern void sub_E844(void);
extern void sub_E95F(void);
extern void sub_E9CD(void);
extern void sub_EA14(void);
extern void sub_EAA9(void);
extern void sub_EAFE(void);
extern void sub_E96B(void);
extern void sub_E263(void);
extern void sub_E6BB(void);
extern void sub_DCD5_b0(void);  /* opaque */
extern void sub_E494(void);

/* External jsl_E79B etc. — fallback */
__attribute__((weak)) void sub_E7D6 (void) {}
__attribute__((weak)) void sub_E844 (void) {}
__attribute__((weak)) void sub_E95F (void) {}
__attribute__((weak)) void sub_E9CD (void) {}
__attribute__((weak)) void sub_EA14 (void) {}
__attribute__((weak)) void sub_EAA9 (void) {}
__attribute__((weak)) void sub_EAFE (void) {}
__attribute__((weak)) void sub_E96B (void) {}
__attribute__((weak)) void sub_E263 (void) {}
__attribute__((weak)) void sub_E6BB (void) {}
__attribute__((weak)) void sub_E494 (void) {}
__attribute__((weak)) void sub_BAD3 (void) {}

/* -------------------------------------------------------------------------
 * $00:E260 — STZ $21 (clear cursor-state byte)
 * ------------------------------------------------------------------------- */
void sub_E260(void) {
    WMEM8(0x0021) = 0;
}

/* -------------------------------------------------------------------------
 * $00:E201 — if state-machine byte == 4, call run-mode entry
 * ------------------------------------------------------------------------- */
void sub_E201(void) {
    if (WMEM8(0x0002) == 0x04) sub_E263();
}

/* -------------------------------------------------------------------------
 * $00:E66A — paint two pixels via [B1] indirect with mask BD
 *   Pre-condition: A held in $92, Y is the pixel index, dp[$B1..$B3] long
 *   For each of 2 pixels: if LSR(BD) carries, OR-store via [B1],y
 * ------------------------------------------------------------------------- */
void sub_E66A(uint16_t y) {
    /* The ROM uses 24-bit indirect [B1]. We resolve to wram[]. */
    uint16_t base = WMEM16(0x00B1);
    uint8_t  bd   = WMEM8(0x00BD);
    uint8_t  mask = WMEM8(0x0092);
    /* shift BD right twice, each step doing the conditional OR */
    for (int step = 0; step < 2; step++) {
        uint8_t old = bd;
        bd = (uint8_t)(bd >> 1);
        WMEM8(0x00BD) = bd;
        if (old & 0x01) {
            uint16_t off = (uint16_t)(base + y);
            wram[off & 0x1FFFF] |= mask;
        }
        ++y;
    }
}

/* -------------------------------------------------------------------------
 * $00:E680 — if dp[$02B1] != 0: tail-call $E6BB
 * ------------------------------------------------------------------------- */
void sub_E680(void) {
    if (WMEM8(0x02B1) != 0) sub_E6BB();
}

/* -------------------------------------------------------------------------
 * Substate-transition micro-helpers. Each one calls one or two engine
 * functions and writes the next substate code to dp[$88].
 * ------------------------------------------------------------------------- */
void sub_E7C6(void) { sub_E7D6(); WMEM8(0x0088) = 0x09; }
void sub_E7CE(void) { sub_E844(); WMEM8(0x0088) = 0x0A; }
void sub_E939(void) { sub_E95F(); sub_E9CD(); WMEM8(0x0088) = 0x0B; }
void sub_E944(void) { sub_EAA9();              WMEM8(0x0088) = 0x0C; }
void sub_E94C(void) { sub_E96B(); sub_EA14();  WMEM8(0x0088) = 0x0B; }
void sub_E957(void) { sub_EAFE();              WMEM8(0x0088) = 0x0C; }

/* -------------------------------------------------------------------------
 * $00:E79B — pack dp[$93:$94] into X (BG2 VRAM index)
 *   X = ($94 << 8 | $93*2) >> 1
 * ------------------------------------------------------------------------- */
uint16_t jsl_E79B(void) {
    uint16_t packed = (uint16_t)((WMEM8(0x0094) << 8) | (WMEM8(0x0093) << 1));
    return (uint16_t)(packed >> 1);
}

/* -------------------------------------------------------------------------
 * $00:E7A8 — same but >>2 (BG3 stride)
 *   X = ($94 << 8 | $93*4) >> 2
 * ------------------------------------------------------------------------- */
uint16_t jsl_E7A8(void) {
    uint16_t packed = (uint16_t)((WMEM8(0x0094) << 8) | (WMEM8(0x0093) << 2));
    return (uint16_t)(packed >> 2);
}

/* -------------------------------------------------------------------------
 * $00:E7B7 — alt with LSR(B) before XBA
 *   B = $94 >> 1, low = $93 * 2
 *   X = (B<<8 | low) >> 2
 * ------------------------------------------------------------------------- */
uint16_t jsl_E7B7(void) {
    uint16_t packed = (uint16_t)(((WMEM8(0x0094) >> 1) << 8) | (WMEM8(0x0093) << 1));
    return (uint16_t)(packed >> 2);
}

/* -------------------------------------------------------------------------
 * $00:E259 — unpack dp[$25:$26] into A:X
 *   A = $26 (high in EBA), X = $25
 *   We don't have multiple return values in C; expose via out params.
 * ------------------------------------------------------------------------- */
void caption_at_default_pos_E259(uint8_t *out_high, uint16_t *out_x) {
    if (out_high) *out_high = WMEM8(0x0026);
    if (out_x)    *out_x    = WMEM8(0x0025);
}

/* -------------------------------------------------------------------------
 * $00:DD24 — per-frame "16-tile column" loader.   *** PARTIAL PORT ***
 *   Walks dp[$6E] iterations, alternating between two BG-modes (call
 *   $0490E2 with (A&1)+1) and dispatching tile rows via $049617.
 *
 *   NOTE: the ROM body receives X from the caller (a 16-bit column index
 *   that drives the $049617 dispatch). The C model below initializes
 *   `x = 0` locally — i.e. the per-column index is NOT propagated from
 *   the caller. Until the call sites are lifted with their X value, the
 *   helper exercises the loop shape but not the per-column placement.
 * ------------------------------------------------------------------------- */
extern void sub_0490E2(uint8_t a);
extern void sub_049617(uint8_t a, uint16_t x);
__attribute__((weak)) void sub_0490E2(uint8_t a) { (void)a; }
__attribute__((weak)) void sub_049617(uint8_t a, uint16_t x) { (void)a; (void)x; }
void sub_DD24(void) {
    uint16_t x = 0;  /* set by caller, opaque */
    while ((int8_t)WMEM8(0x006E) >= 0) {
        uint8_t a = (uint8_t)((WMEM8(0x006E) & 0x01) + 1);
        sub_0490E2(a);
        --x;
        uint8_t a2 = (uint8_t)((uint8_t)x + 0x17);
        sub_049617(a2, x);
        --WMEM8(0x006E);
    }
}

/* -------------------------------------------------------------------------
 * $00:DFCD — message dispatch by upper-bits of A.
 *   Computes X = (A >> 3) & 0xFE, then JMP indirect ($DFDC,X).
 *   The dispatch table at $DFDC has ~16 entries (16-byte stride).
 *   We can't lift the indirect jumps without the table — provide a
 *   parameterized stub.
 * ------------------------------------------------------------------------- */
void sub_DFCD(uint8_t a) {
    WMEM8(0x0072) = a;
    /* The dispatch table at $DFDC is in ROM; would normally jump to one
     * of 16 entry routines. Caller usually passes a known SFX code. */
    (void)a;
}
void sub_DFCD_play_popup_sfx(uint8_t a) { sub_DFCD(a); }
void msg_dispatch_DFCD(uint8_t a)        { sub_DFCD(a); }

/* -------------------------------------------------------------------------
 * $00:8E06 — search SRAM area-id arrays ($7F:C3E8 / $7F:CBB8) for the
 * pair that matches dp[$0264] / dp[$025E]. On match, store X into $025C.
 * Walking X = caller-provided down to 0.
 * ------------------------------------------------------------------------- */
void sub_8E06(int16_t x_in) {
    int16_t x = x_in;
    while (x >= 0) {
        /* SRAM accessed via wram[] aliasing for bank $7F:$Cxxx */
        if (wram[0xC3E8 + x] == WMEM8(0x0264) &&
            wram[0xCBB8 + x] == WMEM8(0x025E)) {
            break;
        }
        --x;
    }
    WMEM16(0x025C) = (uint16_t)x;
}
void danger_init_8E06(int16_t x) { sub_8E06(x); }
void slow_8E06(int16_t x)        { sub_8E06(x); }

/* -------------------------------------------------------------------------
 * $00:8943 — RTS (no-op).  $00:8941 above it is `STY $2E` then falls
 * through to this RTS, so callers of 8941 just stash Y into dp[$2E].
 * ------------------------------------------------------------------------- */
void large_R_swarm_8943(void) {}
void sty_to_2E_then_rts_8941(uint8_t y) { WMEM8(0x002E) = y; }

/* -------------------------------------------------------------------------
 * $00:876E — if dp[$11] is negative, zero it
 * ------------------------------------------------------------------------- */
void scatter_B_initial_876E(void) {
    if ((int8_t)WMEM8(0x0011) < 0) WMEM8(0x0011) = 0;
}

/* -------------------------------------------------------------------------
 * $00:88FF — palette upload to BG scratch.
 *   in: X = pointer (24-bit), A = color base, Y = palette index
 *   Walks 16 entries of (idx, [X], [X+1]) writing into $0F20.
 * ------------------------------------------------------------------------- */
void palette_upload_0088FF(uint16_t X_long, uint8_t A_base, uint8_t Y_idx) {
    (void)A_base;
    /* STX $7F in X=16-bit mode stores X.low to $7F and X.high to $80;
     * the ROM later uses [$7F] as a far indirect, so the bank byte at $81
     * is set by the caller before invocation. */
    WMEM16(0x007F) = X_long;
    uint8_t x = WMEM8(0x002E);
    uint8_t y_shifted = (uint8_t)(Y_idx << 4);
    WMEM8(0x0070) = y_shifted;
    WMEM8(0x006F) = 0x0F;
    do {
        WMEM8(0x0070) = (uint8_t)(WMEM8(0x0070) + 1);
        wram[0x0F20 + x] = WMEM8(0x0070); ++x;
        uint16_t addr = WMEM16(0x007F);
        wram[0x0F20 + x] = wram[addr & 0x1FFFF]; ++x; ++addr;
        WMEM16(0x007F) = addr;
        wram[0x0F20 + x] = wram[addr & 0x1FFFF]; ++x; ++addr;
        WMEM16(0x007F) = addr;
        --WMEM8(0x006F);
    } while ((int8_t)WMEM8(0x006F) >= 0);
    WMEM8(0x002E) = x;
}

/* -------------------------------------------------------------------------
 * $00:8D7E — asset decompress to scratch buffer at $7E:2000.
 *   in: A=src bank, Y=src offset
 *   sets dp[$02CF] = src offset, $02D1 = src bank,
 *        dp[$02D4] = $2000 (dst), $02D6 = $7E (dst bank),
 *   then JSL $02:8010 (the LZSS decoder).
 * ------------------------------------------------------------------------- */
extern void sub_028010_lzss(void);
__attribute__((weak)) void sub_028010_lzss(void) {}
void sub_8D7E(uint8_t a, uint16_t y) {
    WMEM8(0x02D1)  = a;
    WMEM16(0x02CF) = y;
    WMEM16(0x02D4) = 0x2000;
    WMEM8(0x02D6)  = 0x7E;
    sub_028010_lzss();
}
void asset_decompress_to_scratch_8D7E(uint8_t a, uint16_t y) { sub_8D7E(a, y); }
void asset_decompress_028010(void) { sub_028010_lzss(); }
void sub_028005(void) {}    /* JSL $038507 — write-back/finalize, opaque */

/* -------------------------------------------------------------------------
 * $00:D4B5 — cleanup walk over dp[$45..]: while dp[$6C] >= 0, if
 * dp[$45+$6C] is positive-byte, copy to dp[$72] and call $D4F1 via
 * sub_0490E0.
 * ------------------------------------------------------------------------- */
extern void sub_0490E0(uint8_t a);
extern void sub_D4F1_plot_one(void);
__attribute__((weak)) void sub_0490E0(uint8_t a) { (void)a; }
__attribute__((weak)) void sub_D4F1_plot_one(void) {}
void sub_D4B5_kill_cleanup(void) {
    while ((int8_t)WMEM8(0x006C) >= 0) {
        uint8_t v = wram[0x0045 + WMEM8(0x006C)];
        if ((int8_t)v >= 0) {
            WMEM8(0x0072) = v;
            sub_0490E0((uint8_t)(WMEM8(0x006C) + 1));
            sub_D4F1_plot_one();
        }
        --WMEM8(0x006C);
    }
    WMEM8(0x0088) = 0x0E;
}
void kill_cleanup_D4B5(void) { sub_D4B5_kill_cleanup(); }

/* -------------------------------------------------------------------------
 * $00:BA9E — wait until system second counter ($02) advances by A.
 *   in: A = number of system seconds to wait
 *   The ROM caches (A + $02) mod 60, then yields until $02 matches.
 * ------------------------------------------------------------------------- */
void sub_BA9E_wait_sec(uint8_t a) {
    uint8_t target = (uint8_t)((a + WMEM8(0x0002)) % 0x3C);
    while (WMEM8(0x0002) != target) {
        sub_877D();
    }
}
void wait_until_second_BA9E(uint8_t a) { sub_BA9E_wait_sec(a); }

/* -------------------------------------------------------------------------
 * $00:BACA — caption-blit wrapper. PHA + JSR $BAD3 + PLA + JSR $BA9E.
 *   Signature mirrors the ROM body: A = hold-seconds, X = caption pointer.
 *   (Earlier draft was 1-arg; updated to match the 2-arg form used by
 *   the audio_intro.c / states_menu.c call sites.)
 * ------------------------------------------------------------------------- */
void caption_screen_BACA(uint8_t a, uint16_t x) {
    (void)x;              /* caption pointer — unused in this stub */
    sub_BAD3();           /* tile clear */
    sub_BA9E_wait_sec(a); /* wait that many system seconds */
}

/* -------------------------------------------------------------------------
 * Save/load thunks (bank $03 — JSLs into the SRAM-driver). The ROM code
 * past their entry points is bytes (the disassembly hit garbage). Each
 * just dispatches to a JSL or pushes state. Leave as no-op for the link.
 * ------------------------------------------------------------------------- */
void save_full_game_03_F988(void)  {}
void save_scenario_03_F9B9(void)   {}
void load_game_03_FA74(void)       {}

/* -------------------------------------------------------------------------
 * $00:DF0A — per-view "scroll one step" (lifted partially)
 *   in: dp[$6C] = view-mode index, dp[$6D] = "row" (0=B colony, 1=R)
 *   ROM index calc ($DF0D-$DF18):
 *     A = $6D; ASL; +$6D (=3*$6D); ASL (=6*$6D); +$6C; ASL  ->  X
 *     i.e. X = (6*$6D + $6C) * 2  — picks 16-bit entry at $7F:E796,X.
 *   When $6C == 3, sums the NEXT 16-bit entry too.
 *   The X-coord is then computed and a "$C5CF" cell-render is issued.
 *   We capture the address calculation and the storage of dp[$73]/[$74].
 * ------------------------------------------------------------------------- */
extern void sub_C5CF_cell_draw(uint8_t a, uint8_t x);
__attribute__((weak)) void sub_C5CF_cell_draw(uint8_t a, uint8_t x) {
    (void)a; (void)x;
}
void sub_DF0A(void) {
    uint8_t mode = WMEM8(0x006C);
    uint8_t row  = WMEM8(0x006D);
    /* X = (6*row + mode) * 2  — 16-bit-entry table at $7F:E796 */
    uint16_t x   = (uint16_t)(((row * 6 + mode) * 2));
    uint16_t y;
    /* SRAM $7F:E796,X */
    uint16_t base = 0xE796;
    if (mode == 3) {
        y = (uint16_t)(WMEM16(base + x) + WMEM16(base + x + 2));
    } else {
        y = WMEM16(base + x);
    }
    /* dp[$73] = (row*3*4) + 8; dp[$74] = mode + 0x16 */
    WMEM8(0x0074) = (uint8_t)(mode + 0x16);
    WMEM8(0x0073) = (uint8_t)((row * 3 * 4) + 8);
    WMEM8(0x008C) = WMEM8(0x002E);
    sub_C5CF_cell_draw(0, WMEM8(0x0073));
    if (row != 0) WMEM8(0x008C) = WMEM8(0x002F);
    WMEM8(0x0073) = (uint8_t)(WMEM8(0x0073) + 3);
    WMEM8(0x0027) = 0x40;
    (void)y;  /* fed to BCD-printer downstream */
}

/* -------------------------------------------------------------------------
 * $00:BC2E — colony health tick (chains into $E494 + $BC53).
 *   The disassembly head is "TRB $8F00" — really data bytes. The real
 *   entry seems to be at $BC3D. Provide a wrapper that just calls the
 *   real entry's effect.
 * ------------------------------------------------------------------------- */
extern void sub_BC53_health_pump(void);
__attribute__((weak)) void sub_BC53_health_pump(void) {}
void colony_health_update_BC2E(void) {
    WMEM8(0x0073) = 0x02;
    sub_E494();
    sub_BC53_health_pump();
}

/* -------------------------------------------------------------------------
 * Bank $04 micro-helpers: most are pure-RTS dispatch sinks.
 * ------------------------------------------------------------------------- */
void corpse_spawn_C3E3(void) {}
void soldier_morph_table_C61C(void) {}
void ant_lion_tick_C0FD(void) {}
void large_B_swarm_8873(void) {}
void scatter_R_initial_886D(void) {
    /* Faithful: $00:886D writes BG2VOFS (once — high byte; low byte was
     * written by the fall-through from $00:8868 in the caller), then both
     * bytes of BG3HOFS ($2111), then both bytes of BG3VOFS ($2112).
     *
     * The "BG3HOFS written twice consecutively" pattern is INTENTIONAL —
     * SNES PPU scroll registers ($210D-$2114) are write-twice for 16-bit
     * latch: the first STA writes the LOW byte, the second writes the HIGH
     * byte to the same address. Same for BG3VOFS below. */
    BG2VOFS = WMEM8(0x004D);              /* $4D -> BG2VOFS high (latch finish) */
    BG3HOFS = WMEM8(0x004E);              /* $4E -> BG3HOFS low  */
    BG3HOFS = WMEM8(0x004F);              /* $4F -> BG3HOFS high (write-twice) */
    BG3VOFS = WMEM8(0x0050);              /* $50 -> BG3VOFS low  */
    BG3VOFS = WMEM8(0x0051);              /* $51 -> BG3VOFS high (write-twice) */
}

/* -------------------------------------------------------------------------
 * Aliases / no-op stubs for the ones we won't implement here.
 * ------------------------------------------------------------------------- */
void per_area_food_tick_E4DB(void) {}
void per_area_visit_tick_9D96(void) {}
void predator_despawn_9D6D(void) {}
void area_event_tick_ACF9(void) {}
void breeder_movement_C6A9(void) {}
void ant_motion_update_9A86(void) {}
void ant_at_position_2991(void) {}
void danger_event_tick_DD5F(void) {}
void cooldown_dec_AC41(void) {}
void cooperative_yield_877D(void) {}  /* alias for $877D — sub_877D is the strong */
void hist_post_9419(void) {}
void build_population_histogram_923B(void) {}
void build_sprite_tables_F8C5(void) {}
void caption_blit_00C4D8(uint16_t xy, uint16_t yp);  /* lifted in batch 3 */
void common_screen_setup_BB38(void) {}  /* large — leave for now */
void compute_screen_DB40(void) {}
void corpse_spawn_B198(void) {}
void egg_hatch_sfx_B921(void) {}
void entity_class_CBB8(void) {}
void entity_class_D964(void) {}
void entity_class_E328(void) {}
void entity_table_reset_0499BB(void) {}
void entity_spawn_0499C1(uint16_t x, uint16_t y, uint8_t a) {
    (void)x; (void)y; (void)a;
}
void handle_button_A_A36B(void) {}
void is_scenario_mode_dp97(void) {}
void lz_compress_03_8000(void) {}
void lz_decompress_03_8467(void) {}
void pop_aggregator_956E(void) {}
void pop_summary_923B(void) {}
void queue_event_F65A(void) {}
void r_kill_alloc_989C(void) {}
void r_kill_book_ED7D(void) {}
void slotmap_select_a_F59F(void) {}
void simulate_eat_food_for_yellow(void) {}
void simulate_pickup_food_for_yellow(void) {}
void slow_subsys_80BD(void) {}
void slow_subsys_812F(void) {}
void slow_subsys_81A1(void) {}
void slow_subsys_9269(void) {}
void slow_subsys_92C2(void) {}
void slow_subsys_931B(void) {}
void slow_subsys_9333(void) {}
void slow_subsys_934B(void) {}
void slow_subsys_936A(void) {}
void slow_subsys_F927(void) {}
void sram(void) {}
void tile_clear_89ED(void) {}
void tile_commit_8518(void) {}
void tile_commit_855B(void) {}
void tile_is_blocked_9F6A(void) {}
void tile_is_combatant_A547(void) {}
void tile_is_walkable_A534(void) {}
void tilemap_addr_A5BB(void) {}
void tilemap_read_A626(void) {}
void tilemap_write_A689(void) {}
void world_tile_is_dirt(void) {}
void yellow_ant_assign_to(void) {}
void yellow_ant_get_self(void) {}
void yellow_ant_is_worker(void) {}
void text_render_00C91F(void) {}
void text_render_049000(void) {}
void bg1_text_upload_049000(void) {}
void sub_028005_alt(void) {}
void scent_tilemap_7F6000(void) {}
void sub_088003(void) {}
void asset_chain_088003(void) {}
void sub_8D41_alt(void) {}
void b_kill_alloc_984B(void) {}
void b_kill_book_D760(void) {}

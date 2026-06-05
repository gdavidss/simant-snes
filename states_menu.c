/*
 * states_menu.c — Lifted bodies of the 10 menu/transition game-state
 * handlers (the entries dispatched from $00:9369 indexed by dp[$0B]).
 *
 * Each handler RUNS ONCE per visit, builds the screen it represents
 * (palette/tilemap/sprites/audio), then INCs dp[$0B] so the dispatcher
 * picks up the next state on the following frame.
 *
 * Source: /Users/guilhermedavid/simant-re/simant.sfc (1 MB LoROM ROM).
 * Disassembled with disasm.py; cross-referenced with simant.c (scaffold).
 *
 * Conventions for the 65816 → C translation:
 *   - M=1, X=0 (8-bit accumulator, 16-bit index) at handler entry.
 *   - DBR=$01 (UI text/tables); D=$0000 (dp[i] == wram[i]).
 *   - dp[]    aliases wram[0..0xFF].
 *   - MMIO8/MMIO16 wrap the volatile SNES register window.
 *   - `extern` decls below pull in helpers already in simant.c. New
 *     helpers needed by the handlers (sub_BB38, sub_BA9E, sub_BACA,
 *     sub_BAF2, sub_85FC, sub_8616, sub_8841, sub_8976, sub_8AED,
 *     sub_867F, sub_877D, sub_B1D2, sub_BAD3) are fully defined here.
 *
 * Asset-loader contract for $00:8D7E (asset_decompress_to_scratch):
 *     A  = source ROM bank
 *     Y  = source ROM offset within that bank
 *   → decompresses to scratch buffer at $7E:2000.
 * Asset-loader contract for $00:8ACC (vram_dma_from_scratch):
 *     X  = length in bytes
 *     Y  = VRAM destination word address
 *   → DMA channel 0 from $7E:2000 to $2118 (VMDATAL).
 * Asset-loader contract for $00:8AED (cgram_dma):
 *     A  = source ROM bank
 *     Y  = source ROM offset
 *   → 0x200 byte DMA to $2122 (CGDATA) — palette upload.
 *
 * Entity-spawn contract for $04:99C1 (`JSL $0499C1`):
 *     X  = screen X (16-bit)
 *     Y  = screen Y (16-bit)
 *     A  = entity type byte
 *   → allocates the next free 20-byte slot at $04:0600 onward.
 * Entity-spawn contract for $04:99BB:
 *     Resets the entity-table "next free" cursor (dp[$30]) to $0600.
 *
 * The handlers fall into three flavors:
 *   1. "FULL setup" — silence APU, wait 4 frames, force blank, call the
 *      common screen template (sub_BB38), upload N {decompressed asset →
 *      VRAM} pairs, install palette via $8AED, re-enable NMI, fade in
 *      (sub_85FC), INC $0B.        (GS_FULL_GAME, GS_SAVED_GAME, GS_ANT_*)
 *   2. "Template + caption" — call sub_BA9E (countdown to specific frame
 *      slot) then sub_BACA (template + text-screen overlay), fade out
 *      (sub_8616), INC $0B.        (GS_TUTORIAL, GS_MARRIAGE_FLIGHT,
 *      GS_SOUND, GS_SCENARIO_END)
 *   3. "Wait for task spawn" — spawn N sub-tasks (sub_877D), wait until
 *      task count reaches the expected value, fade out.  (GS_SCENARIO_GAME)
 *
 * The screen-template helper sub_BB38 ($00:BB38) is invoked by FOUR of
 * the handlers (FULL_GAME, SAVED_GAME, FULL_END, and via B1D2 by
 * ANT_INFORMATION and GAME_OVER). It bring-up sets BGMODE=9 (Mode 1 +
 * BG3 priority), configures BG tile bases, allocates VRAM, and clears
 * an $800-tile block to value 0 at $7800. We lift it once below.
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Shared aliases (must match simant.c). We re-declare the strict minimum
 * to keep this file self-compileable in isolation.
 * ------------------------------------------------------------------------ */
static uint8_t  wram[0x20000];
static uint8_t  sram[0x08000];
#define dp wram

static volatile uint8_t  mmio[0x10000];
#define MMIO8(addr)   (*(volatile uint8_t  *)&mmio[(addr) & 0xFFFF])
#define MMIO16(addr)  (*(volatile uint16_t *)&mmio[(addr) & 0xFFFF])

/* SNES MMIO names we touch (only the ones used here). */
#define INIDISP   MMIO8 (0x2100)   /* force blank + master brightness     */
#define OBSEL     MMIO8 (0x2101)   /* OBJ size + name table addr          */
#define BGMODE    MMIO8 (0x2105)
#define BG12NBA   MMIO8 (0x210B)
#define BG34NBA   MMIO8 (0x210C)
#define VMADDL    MMIO16(0x2116)
#define VMDATAL   MMIO8 (0x2118)
#define CGADD     MMIO8 (0x2121)
#define TM        MMIO8 (0x212C)   /* main-screen layer enable            */
#define TS        MMIO8 (0x212D)   /* sub-screen  layer enable            */
#define CGWSEL    MMIO8 (0x2130)
#define CGADSUB   MMIO8 (0x2131)
#define APUIO0    MMIO8 (0x2140)
#define APUIO1    MMIO8 (0x2141)
#define APUIO2    MMIO8 (0x2142)
#define APUIO3    MMIO8 (0x2143)
#define NMITIMEN  MMIO8 (0x4200)
#define MDMAEN    MMIO8 (0x420B)
#define HVBJOY    MMIO8 (0x4212)
#define JOY1L     MMIO8 (0x4218)
#define DMA0_PARAM MMIO8 (0x4300)
#define DMA0_DEST  MMIO8 (0x4301)
#define DMA0_SRC   MMIO16(0x4302)
#define DMA0_BANK  MMIO8 (0x4304)
#define DMA0_LEN   MMIO16(0x4305)

/* Cooperative-scheduler view of task count ($02 in dp; see simant.c). */
#define TASK_LIMIT  dp[0x02]

/* ------------------------------------------------------------------------
 * extern hooks satisfied by simant.c (or the rest of the decomp).
 * ------------------------------------------------------------------------ */
extern void asset_decompress_to_scratch_8D7E(uint8_t src_bank, uint16_t src_ofs);
extern void vram_dma_from_scratch_8ACC      (uint16_t length, uint16_t vram_dst);

/* Bank-$04 spawner & dispatcher (lifted in simant.c stubs). */
extern void entity_spawn_0499C1(uint16_t x, uint16_t y, uint8_t type);
extern void entity_table_reset_0499BB(void);

/* Bank-$04 BG3 helpers used by sub_BB38 / sub_BAD3 (text-layer rebuild). */
extern void sub_0490D2(void);
extern void sub_04911B(void);
extern void sub_0490DB(void);
extern void sub_049000(uint8_t a, uint16_t x);   /* (A=$C0, X=$A420) — text install */

/* Bank-$08 multi-asset loader entry (JSL $08:8003). The X register
 * selects which packed asset chain to run (e.g. $0A00 for the encyclopedia
 * payload used by GS_ANT_INFORMATION). */
extern void asset_chain_088003(uint16_t selector_x);

/* Bank-$00 sound-/SPC700 IPC helper at $008E88. Caller passes A=command,
 *   STA $0037
 *   if (dp[$0033]) APUIO0 = dp[$0037]
 *   RTL
 * Used by GS_ANT_INFORMATION to push command $02 (encyclopedia BGM). */
extern void apu_send_if_enabled_008E88(uint8_t cmd);

/* Direct-page bring-up helpers from sub_BB8D's pipeline. */
extern void sub_C318(void);
extern void sub_C398(void);
extern void sub_8F08(void);

/* ------------------------------------------------------------------------
 * COMMON HELPERS — defined here because every menu state needs them and
 * simant.c only has stubs.
 * ------------------------------------------------------------------------ */

/* $00:8967 — busy-wait until HVBJOY bit 7 = 0 (NMI ack done). */
static void vblank_ack_8967(void)
{
    while (HVBJOY & 0x80) { /* spin */ }
}

/* $00:896D — wait for vblank-ack, then unmask NMI (re-enables NMI +
 * joypad-auto-read by writing the saved $81 from dp[$0A]). */
static void enable_nmi_896D(void)
{
    vblank_ack_8967();
    NMITIMEN = dp[0x0A];
}

/* $00:8976 — `JSR $877D` + STZ $4200. Atomically yields to the scheduler
 * once and then masks NMI off. The yield ensures the in-flight VBlank
 * completes BEFORE NMI is disabled, so we don't strand the renderer mid
 * frame. */
static void sub_877D(void);
static void mask_nmi_after_yield_8976(void)
{
    sub_877D();
    NMITIMEN = 0;
}

/* $00:8101 etc. — full sub_877D body. From the disassembly:
 *   LDA $00       ; current task ID
 *   CMP $00       ; wait until that byte changes
 *   BEQ -3        ; (busy-spins; CUR_TASK is bumped by the NMI scheduler)
 *   PHX / PHY
 *   JSR $8887     ; latch joypad shadow + edge-detect
 *   JSR $E3FD     ; busy-wait for joypad-auto-read complete + sample
 *   JSR $87DA     ; first-half of pause logic (lockout-aware)
 *   PLY / PLX / RTS
 *
 * Effect: "cooperatively yield one frame, then update the joypad
 * shadows used by the pause/cursor handlers". */
static void joypad_edge_latch_8887(void);
static void joypad_auto_read_wait_E3FD(void);
static void pause_lockout_check_87DA(void);
static void sub_877D(void)
{
    /* Spin until the scheduler has switched to a different task slot. The
     * original reads dp[$00] (CUR_TASK) and compares against itself in a
     * tight loop — the NMI handler increments CUR_TASK on every entry. */
    uint8_t t0 = dp[0x00];
    while (dp[0x00] == t0) { /* spin */ }
    joypad_edge_latch_8887();
    joypad_auto_read_wait_E3FD();
    pause_lockout_check_87DA();
}

/* $00:8887 — "edge-detect" the joypad. For 4 bytes ($60..$63 vs $5C..$5F)
 * compute (~prev & current) and stash as "just pressed this frame", then
 * copy current into prev. The original is 16-bit-A, loops X=0,2 to do
 * both halves. */
static void joypad_edge_latch_8887(void)
{
    /* Read current joypad ($4218 == JOY1L, sequence $4218..$421B). */
    uint16_t cur_lo = JOY1L | (MMIO8(0x4219) << 8);
    uint16_t cur_hi = MMIO8(0x421A) | (MMIO8(0x421B) << 8);
    uint16_t prev_lo = *(uint16_t *)&dp[0x5C];
    uint16_t prev_hi = *(uint16_t *)&dp[0x5E];
    *(uint16_t *)&dp[0x60] = (uint16_t)(~prev_lo) & cur_lo;
    *(uint16_t *)&dp[0x62] = (uint16_t)(~prev_hi) & cur_hi;
    *(uint16_t *)&dp[0x5C] = cur_lo;
    *(uint16_t *)&dp[0x5E] = cur_hi;
}

/* $00:E3FD — wait for joypad auto-read to finish (HVBJOY bit 0 = 0), then
 * mirror $421A → dp[$5C+2] high, $4218 → dp[$5C] low. The original passes
 * via JSR $E415 which is the actual byte-copy; for our purposes the read
 * loop alone is the side-effect. */
static void joypad_auto_read_wait_E3FD(void)
{
    while (HVBJOY & 0x01) { /* wait */ }
    /* (the actual $4218/$421A snapshot is already done by 8887) */
}

/* $00:87DA — first byte: `LDA $0071; BEQ +1; RTS`. The full body is
 * larger; this prologue alone matches the bail-on-pause-locked pattern. */
static void pause_lockout_check_87DA(void)
{
    if (dp[0x0071]) return;
    /* TODO: the rest of $87DA (further menu / lockout state). */
}

/* $00:8841 — "wait A frames" by yielding via sub_877D, A times. */
static void wait_frames_8841(uint8_t n)
{
    while (n--) sub_877D();
}

/* $00:85FC — fade IN: write $00..$0F to INIDISP, 2-frame pause each step.
 * Counter at dp[$6C], compares against #$10 (16 levels). */
static void fade_in_85FC(void)
{
    dp[0x6C] = 0;
    do {
        INIDISP = dp[0x6C];
        wait_frames_8841(2);
        dp[0x6C]++;
    } while (dp[0x6C] != 0x10);
}

/* $00:8616 — fade OUT: write $0F..$00 (BPL — stops when DEC underflows). */
static void fade_out_8616(void)
{
    dp[0x6C] = 0x0F;
    do {
        INIDISP = dp[0x6C];
        wait_frames_8841(2);
        --dp[0x6C];
    } while ((int8_t)dp[0x6C] >= 0);
}

/* $00:867F — clear a 0x400-word VRAM block starting at X (VRAM word
 * address), filling with 16-bit A. The original handles A via XBA pre/post
 * (the LDA #$00 / XBA pair effectively zeroes the high byte going in). */
static void vram_clear_block_867F(uint8_t fill_lo, uint16_t vram_addr)
{
    VMADDL = vram_addr;
    uint16_t fill = (uint16_t)fill_lo;
    for (unsigned i = 0; i < 0x400; ++i) {
        VMDATAL       = (uint8_t)fill;
        MMIO8(0x2119) = (uint8_t)(fill >> 8);
    }
}

/* $00:866E — same idea, but called only from sub_BB38: takes 16-bit fill
 * in A and VRAM dest in X; fills until $03FF address bits all hit (0x400
 * words). */
static void vram_fill_866E(uint16_t fill, uint16_t vram_addr)
{
    VMADDL = vram_addr;
    do {
        MMIO16(0x2118) = fill;
        ++fill;
    } while ((fill & 0x03FF) != 0);
}

/* $00:8AED — CGRAM upload. Length is fixed at 0x200, dest is CGRAM offset
 * 0 (full 256-color palette). A = source bank, Y = source offset. */
static void cgram_dma_8AED(uint8_t src_bank, uint16_t src_ofs)
{
    CGADD      = 0;
    DMA0_BANK  = src_bank;
    DMA0_SRC   = src_ofs;
    DMA0_PARAM = 0x00;          /* 1-register write             */
    DMA0_DEST  = 0x22;          /* $2122 = CGDATA               */
    DMA0_LEN   = 0x0200;
    MDMAEN     = 0x01;
}

/* $00:BB38 — common screen-template setup. Called by FOUR screens
 * directly (FULL_GAME, SAVED_GAME, FULL_END) and transitively from
 * B1D2 (which serves GAME_OVER + ANT_INFORMATION). It:
 *   - resets clock seconds/minutes ($01, $02)
 *   - selects BGMODE 9 (Mode 1, BG3 priority high)
 *   - configures BG tile-base registers (BG12NBA=$60, BG34NBA=$02)
 *   - sets OBSEL to $A2 (32x32 large sprite, name base $A000-region)
 *   - initialises the VRAM-update-queue head (dp[$2B/$2C/$2D])
 *   - delegates BG3/text-layer rebuild via three JSLs into bank $04
 *   - DMAs 0x4000 bytes of {scratch → VRAM $2000}
 *   - clears 0x800 words at VRAM $7800 to value 0 (tilemap blank tiles). */
static void common_screen_setup_BB38(void)
{
    sub_C318();                      /* JSR $C318 */
    dp[0x02] = 0;                    /* STZ $02   (clock seconds) */
    dp[0x01] = 0;                    /* STZ $01   (clock frames)  */
    BGMODE  = 0x09;                  /* Mode 1, BG3 priority high */
    sub_C398();                      /* JSR $C398 */
    BG12NBA = 0x60;
    BG34NBA = 0x02;
    OBSEL   = 0xA2;                  /* 16x16 + 32x32, name base  */
    sub_8F08();                      /* JSR $8F08 */
    dp[0x98] = 0x00;
    dp[0x002B] = 0x03;
    dp[0x002C] = 0x02;
    dp[0x002D] = 0x01;
    sub_0490D2();                    /* JSL $04:90D2 */
    sub_04911B();                    /* JSL $04:911B */
    sub_0490DB();                    /* JSL $04:90DB */
    vram_dma_from_scratch_8ACC(0x4000, 0x2000);
    /* sub_BB38 sets up 16-bit A = $2000 via LDA #$20 / XBA / LDA #$00 then
     * calls $866E with REP #$20 — fill is the running tile index starting at
     * $2000 (not zero); the loop runs 0x400 words writing $2000..$23FF. */
    vram_fill_866E(0x2000, 0x7800);
}

/* $00:BAF2 — "load FULL_END secondary screen". Decompresses two asset
 * chunks: a $18:FF8A → VRAM $6000 patch and a $07:D5A6 → VRAM $7400 tilemap.
 * Called inline by GS_FULL_END (between the main asset chain and the
 * celebration spawns). */
static void load_end_secondary_BAF2(void)
{
    asset_decompress_to_scratch_8D7E(0x18, 0xFF8A);
    vram_dma_from_scratch_8ACC      (0x0100, 0x6000);
    asset_decompress_to_scratch_8D7E(0x07, 0xD5A6);
    vram_dma_from_scratch_8ACC      (0x0800, 0x7400);
}

/* $00:BA9E — "advance until clock-second hits A+now mod 60". Used by the
 * caption-only states (TUTORIAL, MARRIAGE_FLIGHT, SOUND, SCENARIO_END)
 * to time how long a screen sticks before the fade-out. Loop:
 *
 *     A = (A + dp[$02]) mod 60          ; target second
 *     do { sub_877D(); } while (A != dp[$02])
 *
 * Effect: "wait A seconds (modulo 60) at the current frame rate". */
static void wait_until_second_BA9E(uint8_t add_seconds)
{
    uint16_t target = (uint16_t)add_seconds + dp[0x02];
    while (target >= 60) target -= 60;
    do {
        sub_877D();
    } while ((uint8_t)target != dp[0x02]);
}

/* $00:BAD3 — "rebuild BG3 from a text record, install $A420 caption". The
 * caller passes Y (the source caption pointer) on the stack via PHY/PLY.
 * After the sub_877D yield + three BG3 JSLs, it calls `JSL $04:9000` with
 * A=$C0 and X=$A420 to push the caption strip into VRAM. Always pins
 * dp[$88] to $1A on exit (the VRAM streamer expects this slot to be free
 * for the caption block next vblank). */
static void rebuild_bg3_with_caption_BAD3(uint16_t caption_ptr)
{
    (void)caption_ptr;               /* the original pushes Y but BAD3
                                      * never consumes it directly here;
                                      * the address is plumbed via dp by
                                      * the upstream BACA call below. */
    sub_877D();
    sub_0490D2();
    sub_04911B();
    sub_0490DB();
    sub_049000(/*A=*/0xC0, /*X=*/0xA420);
    dp[0x88] = 0x1A;
}

/* $00:BACA — "render caption-overlay screen and pause N seconds".
 *
 *   PHA            ; save caller's seconds count
 *   JSR $BAD3      ; rebuild BG3 + install caption (Y = caption ptr)
 *   PLA
 *   JSR $BA9E      ; wait A seconds
 *   RTS                                                             */
static void caption_screen_BACA(uint8_t hold_seconds, uint16_t caption_ptr)
{
    rebuild_bg3_with_caption_BAD3(caption_ptr);
    wait_until_second_BA9E(hold_seconds);
}

/* $00:B1D2 — "set up the standard half-screen + caption template used
 * by ANT_INFORMATION and GAME_OVER". Six asset/VRAM pairs; ends by
 * spawning entity type $01 (cursor/input). */
static void screen_template_B1D2(void)
{
    common_screen_setup_BB38();
    asset_decompress_to_scratch_8D7E(0x1A, 0x9F31);
    vram_dma_from_scratch_8ACC      (0x4000, 0x0000);
    asset_decompress_to_scratch_8D7E(0x07, 0xE6E9);
    vram_dma_from_scratch_8ACC      (0x0800, 0x7000);
    load_end_secondary_BAF2();
    asset_decompress_to_scratch_8D7E(0x1A, 0x8662);
    vram_dma_from_scratch_8ACC      (0x2000, 0x4000);
    cgram_dma_8AED                  (0x07,   0xA380);
    entity_spawn_0499C1(0, 0, 0x01);
}

/* APU silence — write 0 to all four IPC registers. */
static void apu_silence_all(void)
{
    APUIO0 = 0; APUIO1 = 0; APUIO2 = 0; APUIO3 = 0;
}

/* ========================================================================
 * GAME-STATE HANDLERS
 * ======================================================================== */

/* $00:ACF3 — GS_FULL_GAME.
 *
 * Screen reached when the player picks "FULL GAME" from the main menu.
 * Silences the APU, waits 4 frames, force-blanks, runs the common screen
 * setup, then performs FIVE asset loads:
 *
 *   $16:9D63 → VRAM $0000  ($2000 bytes — BG1 tile graphics)
 *   $1E:FA24 → VRAM $7000  ($0800 bytes — BG3/text tile graphics)
 *   $10:8000 → VRAM $6000  ($2000 bytes — sprite tile graphics)
 *   (in-place) VRAM $7400 cleared to value 0 ($0400 words)
 *   $07:8400 → CGRAM 0     ($0200 bytes — 256-colour palette)
 *
 * Then enables NMI, fades in, INCs $0B.                              */
static void gs_full_game_ACF3(void)
{
    apu_silence_all();
    wait_frames_8841(0x04);
    mask_nmi_after_yield_8976();
    INIDISP = 0x80;                                /* force blank */
    common_screen_setup_BB38();

    asset_decompress_to_scratch_8D7E(0x16, 0x9D63);
    vram_dma_from_scratch_8ACC      (0x2000, 0x0000);

    asset_decompress_to_scratch_8D7E(0x1E, 0xFA24);
    vram_dma_from_scratch_8ACC      (0x0800, 0x7000);

    asset_decompress_to_scratch_8D7E(0x10, 0x8000);
    vram_dma_from_scratch_8ACC      (0x2000, 0x6000);

    vram_clear_block_867F           (0x00,   0x7400);
    cgram_dma_8AED                  (0x07,   0x8400);

    enable_nmi_896D();
    fade_in_85FC();
    dp[0x0B]++;
}

/* $00:AD5B — GS_SCENARIO_GAME.
 *
 * The smallest "do something complex" handler. Spawns sub-tasks until the
 * task limit hits 4 (i.e. 4 tasks alive), then fades OUT and advances.
 * The actual scenario-loading work happens in those spawned tasks. */
static void gs_scenario_game_AD5B(void)
{
    do {
        sub_877D();
    } while (TASK_LIMIT != 0x04);
    fade_out_8616();
    dp[0x0B]++;
}

/* $00:AC63 — GS_SAVED_GAME.
 *
 * Screen for "SAVED GAME" main-menu pick. Sets up the same common
 * template, but with the saved-game asset chain plus a color-math twist:
 *
 *   $18:FF9E → VRAM $0000  ($4000 bytes — saved-game BG tiles)
 *   $07:D79E → VRAM $7000  ($0800 bytes — caption font/tilemap)
 *   $19:A9C9 → VRAM $2000  ($2000 bytes — additional BG tiles)
 *   $07:E070 → VRAM $7800  ($0800 bytes — extra tilemap)
 *   $16:CBF3 → VRAM $4000  ($2000 bytes — BG2 tilemap/graphics)
 *   $07:9F80 → CGRAM 0     ($0200 bytes — 256-colour palette)
 *
 * Then installs a half-intensity color-math overlay: CGWSEL=$02,
 * CGADSUB=$61 (BG2 sub-screen, half), TM=$15 (BG1+BG3+OBJ on main),
 * TS=$00 (no sub-screen drawing). Enables NMI, fades in, INCs $0B.  */
static void gs_saved_game_AC63(void)
{
    mask_nmi_after_yield_8976();
    INIDISP = 0x80;
    common_screen_setup_BB38();

    asset_decompress_to_scratch_8D7E(0x18, 0xFF9E);
    vram_dma_from_scratch_8ACC      (0x4000, 0x0000);

    asset_decompress_to_scratch_8D7E(0x07, 0xD79E);
    vram_dma_from_scratch_8ACC      (0x0800, 0x7000);

    asset_decompress_to_scratch_8D7E(0x19, 0xA9C9);
    vram_dma_from_scratch_8ACC      (0x2000, 0x2000);

    asset_decompress_to_scratch_8D7E(0x07, 0xE070);
    vram_dma_from_scratch_8ACC      (0x0800, 0x7800);

    asset_decompress_to_scratch_8D7E(0x16, 0xCBF3);
    vram_dma_from_scratch_8ACC      (0x2000, 0x4000);

    cgram_dma_8AED                  (0x07,   0x9F80);

    CGWSEL  = 0x02;                                /* dual-window      */
    CGADSUB = 0x61;                                /* BG1+OBJ half-sub */
    TM      = 0x15;                                /* BG1+BG3+OBJ      */
    TS      = 0x00;                                /* sub-screen off   */

    enable_nmi_896D();
    fade_in_85FC();
    dp[0x0B]++;
}

/* $00:ACE8 — GS_TUTORIAL.
 *
 * Trivial handler: waits 4 in-game seconds on the current screen (no new
 * tilemap upload — the tutorial caption was set up by whoever transitioned
 * here), fades out, and advances. The 4-second hold lets the caption read
 * before transition. */
static void gs_tutorial_ACE8(void)
{
    wait_until_second_BA9E(0x04);
    fade_out_8616();
    dp[0x0B]++;
}

/* $00:B155 — GS_ANT_INFORMATION.
 *
 * The "ant encyclopedia" screen. Silences APU, waits 4 frames, sets up
 * the common ant-info layout via screen_template_B1D2 (which itself
 * calls common_screen_setup_BB38, 3 asset-pair loads, BAF2's two extra
 * loads, an extra asset pair, palette upload, then spawns the type-$01
 * cursor entity). The selector word here is unique:
 *
 *   LDX #$0A00 ; JSL $08:8003 — runs the encyclopedia content chain
 *   JSR $B1D2  — the shared "info template" sub above
 *   LDA #$02 ; JSL $00:8E88 — send APU command $02 (encyclopedia BGM)
 *
 * Then re-enable NMI, fade-in, INC $0B. */
static void gs_ant_information_B155(void)
{
    apu_silence_all();
    wait_frames_8841(0x04);
    mask_nmi_after_yield_8976();
    INIDISP = 0x80;

    dp[0x0032] = 0x00;                             /* OAM hi-priority cursor reset */
    asset_chain_088003(0x0A00);                    /* encyclopedia payload */
    screen_template_B1D2();
    apu_send_if_enabled_008E88(0x02);              /* play info-screen BGM */

    enable_nmi_896D();
    fade_in_85FC();
    dp[0x0B]++;
}

/* $00:B18C — GS_MARRIAGE_FLIGHT.  [ROM string misspells as "MARRIGE"]
 *
 * Triggered when the colony's breeders are ready to launch. Holds the
 * caption (caption text pointer $90:3C) for 3 in-game seconds + 5 hold,
 * then fades out and advances. */
static void gs_marriage_flight_B18C(void)
{
    wait_until_second_BA9E(0x03);
    caption_screen_BACA   (0x05, 0x903C);
    fade_out_8616();
    dp[0x0B]++;
}

/* $00:B07B — GS_FULL_END.
 *
 * FULL Game victory screen. Same common-setup template, then SIX asset
 * loads + a BAF2 secondary set + a palette upload, finally spawns three
 * celebration entities:
 *
 *   $19:FC44 → VRAM $0000  ($2000 — main BG)
 *   $07:E339 → VRAM $7000  ($0800 — caption/text)
 *   <BAF2: $18:FF8A → $6000 ($0100), $07:D5A6 → $7400 ($0800)>
 *   $1A:8662 → VRAM $4000  ($2000 — extra BG)
 *   $1A:9091 → VRAM $5000  ($2000 — another BG)
 *   $07:A180 → CGRAM 0     ($0200 — palette)
 *
 *   spawn type $5A at (0x0020, 0x0040)   — left celebration prop
 *   spawn type $5B at (0x00E0, 0x0040)   — right celebration prop
 *   spawn type $01 (cursor)              — input listener
 *
 * Re-enable NMI, fade in, INC. */
static void gs_full_end_B07B(void)
{
    mask_nmi_after_yield_8976();
    INIDISP = 0x80;
    common_screen_setup_BB38();

    asset_decompress_to_scratch_8D7E(0x19, 0xFC44);
    vram_dma_from_scratch_8ACC      (0x2000, 0x0000);

    asset_decompress_to_scratch_8D7E(0x07, 0xE339);
    vram_dma_from_scratch_8ACC      (0x0800, 0x7000);

    load_end_secondary_BAF2();

    asset_decompress_to_scratch_8D7E(0x1A, 0x8662);
    vram_dma_from_scratch_8ACC      (0x2000, 0x4000);

    asset_decompress_to_scratch_8D7E(0x1A, 0x9091);
    vram_dma_from_scratch_8ACC      (0x2000, 0x5000);

    cgram_dma_8AED                  (0x07,   0xA180);

    entity_spawn_0499C1(0x0020, 0x0040, 0x5A);
    entity_spawn_0499C1(0x00E0, 0x0040, 0x5B);
    entity_spawn_0499C1(0x0000, 0x0000, 0x01);     /* cursor */

    enable_nmi_896D();
    fade_in_85FC();
    dp[0x0B]++;
}

/* $00:B0FC — GS_SCENARIO_END.
 *
 * Per-scenario victory screen — actually THREE stacked sub-screens. Each
 * stage:
 *
 *   1. waits N seconds with sub_BA9E(A=4|2|2)
 *   2. installs a caption strip (sub_BACA(A=3..4, Y=caption ptr))
 *   3. resets the entity table via JSL $04:99BB
 *   4. spawns a banner sprite (type $5C, $5D, ...) and the type-$01 cursor
 *
 * The caption pointers are $8F:51, $8F:B5, $90:0F (three separate strings)
 * — three "Congratulations on completing scenario X!" pages.
 *
 * Stage 1 — caption $8F:51 + sprite type $5C at (0xFFC0, 0x005F)
 * Stage 2 — caption $8F:B5 + sprite type $5D at (0x0140, 0x005F)
 * Stage 3 — caption $90:0F (no extra sprite — fade-out follows)
 *
 * Final: fade_out_8616, INC. */
static void gs_scenario_end_B0FC(void)
{
    /* ---- stage 1 ---- */
    wait_until_second_BA9E(0x04);
    caption_screen_BACA   (0x03, 0x8F51);
    entity_table_reset_0499BB();
    entity_spawn_0499C1   (0xFFC0, 0x005F, 0x5C);
    entity_spawn_0499C1   (0x0000, 0x0000, 0x01);

    /* ---- stage 2 ---- */
    wait_until_second_BA9E(0x02);
    caption_screen_BACA   (0x04, 0x8FB5);
    entity_table_reset_0499BB();
    entity_spawn_0499C1   (0x0140, 0x005F, 0x5D);
    entity_spawn_0499C1   (0x0000, 0x0000, 0x01);

    /* ---- stage 3 ---- */
    wait_until_second_BA9E(0x02);
    caption_screen_BACA   (0x04, 0x900F);

    fade_out_8616();
    dp[0x0B]++;
}

/* $00:B19F — GS_GAME_OVER.
 *
 * Quick handler: mask NMI, force blank, install the shared template
 * (B1D2 — same six asset pairs as ANT_INFORMATION + its cursor), then
 * spawn the "GAME OVER" banner entity (type $5E) at screen-center
 * (0x0088, 0x0098). Re-enable NMI, fade IN (not out — game over is the
 * destination), INC.
 *
 * The reason it doesn't silence the APU here is that the game-over BGM
 * cue was already issued by the gameplay state that triggered the
 * transition. */
static void gs_game_over_B19F(void)
{
    mask_nmi_after_yield_8976();
    INIDISP = 0x80;
    screen_template_B1D2();
    entity_spawn_0499C1(0x0088, 0x0098, 0x5E);
    enable_nmi_896D();
    fade_in_85FC();
    dp[0x0B]++;
}

/* $00:B1BF — GS_SOUND.
 *
 * Sound-options screen (the "music / sound / both / off" toggle page).
 * Same caption-only pattern as MARRIAGE_FLIGHT and TUTORIAL: hold for 3
 * seconds, paint caption from $90:8D and hold 5 more, fade out. */
static void gs_sound_B1BF(void)
{
    wait_until_second_BA9E(0x03);
    caption_screen_BACA   (0x05, 0x908D);
    fade_out_8616();
    dp[0x0B]++;
}

/* ========================================================================
 * Dispatch table at $00:9369 (the entries the state machine indexes via
 * dp[$0B]). Kept here so the file's complete (the calling convention from
 * simant.c's main_9340 loop expects this exact 10-entry order).
 * ======================================================================== */
typedef void (*GameStateFn)(void);
static const GameStateFn gs_dispatch_9369[10] = {
    [0] = gs_full_game_ACF3,            /* GS_FULL_GAME        */
    [1] = gs_scenario_game_AD5B,        /* GS_SCENARIO_GAME    */
    [2] = gs_saved_game_AC63,           /* GS_SAVED_GAME       */
    [3] = gs_tutorial_ACE8,             /* GS_TUTORIAL         */
    [4] = gs_ant_information_B155,      /* GS_ANT_INFORMATION  */
    [5] = gs_marriage_flight_B18C,      /* GS_MARRIAGE_FLIGHT  */
    [6] = gs_full_end_B07B,             /* GS_FULL_END         */
    [7] = gs_scenario_end_B0FC,         /* GS_SCENARIO_END     */
    [8] = gs_game_over_B19F,            /* GS_GAME_OVER        */
    [9] = gs_sound_B1BF,                /* GS_SOUND            */
};

/* Silence "unused" warnings for top-level decls/tables we want as docs. */
__attribute__((used))
static void const * const _menu_doc_refs[] = {
    (void const *)gs_dispatch_9369,
    (void const *)gs_full_game_ACF3,
    (void const *)gs_scenario_game_AD5B,
    (void const *)gs_saved_game_AC63,
    (void const *)gs_tutorial_ACE8,
    (void const *)gs_ant_information_B155,
    (void const *)gs_marriage_flight_B18C,
    (void const *)gs_full_end_B07B,
    (void const *)gs_scenario_end_B0FC,
    (void const *)gs_game_over_B19F,
    (void const *)gs_sound_B1BF,
    (void const *)screen_template_B1D2,
    (void const *)load_end_secondary_BAF2,
    (void const *)common_screen_setup_BB38,
    (void const *)caption_screen_BACA,
    (void const *)rebuild_bg3_with_caption_BAD3,
    (void const *)wait_until_second_BA9E,
    (void const *)fade_in_85FC,
    (void const *)fade_out_8616,
    (void const *)wait_frames_8841,
    (void const *)vram_clear_block_867F,
    (void const *)cgram_dma_8AED,
    (void const *)mask_nmi_after_yield_8976,
    (void const *)enable_nmi_896D,
    (void const *)vblank_ack_8967,
    (void const *)apu_silence_all,
    (void const *)sub_877D,
    (void const *)joypad_edge_latch_8887,
    (void const *)joypad_auto_read_wait_E3FD,
    (void const *)pause_lockout_check_87DA,
    (void const *)&sram[0],
};

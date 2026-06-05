/*
 * audio_intro.c — SimAnt SNES (Maxis / Imagineer / Tomcat Systems, 1993)
 *
 * This module is documentation-as-source. It captures:
 *
 *   PART 1.  The SPC700 sound driver binary as embedded in bank $08
 *            ($08:0A00 .. end-of-bank), including its upload procedure,
 *            on-CPU IPC entry points, and the observed music / SFX
 *            command-code table sent via APUIO0 / APUIO3.
 *
 *            We DO NOT disassemble the SPC700 code itself here. The
 *            project's disasm.py only handles 65816, and the SPC700
 *            machine code lives at FILE OFFSETS 0x40A00..0x47FFF
 *            (30,208 bytes).  Reverse-engineering it requires a
 *            separate SPC700 disassembler — the goal of this file is
 *            to mark the boundaries and the host-side IPC surface.
 *
 *   PART 2.  The intro + credits flow lifted from bank $00.
 *            State $40 ($B875) sets up the credits screen + plays
 *            APU music command $02; state $41 ($B8B9) cycles through
 *            the 24 credit pages by calling the per-page renderer at
 *            $00:B94C with each page-text pointer in turn.  The
 *            "Will Wright" string lives in the credits page whose
 *            text starts at $01:93AF (page 1 of 24); "the Maxis" is
 *            in the page at $01:9850 (page 22 of 24).
 *
 *   PART 3.  The boot-time game-state dispatch table.  At reset, dp[$0B]
 *            is zero, so the dispatcher at $00:935C jumps via
 *            ($9369 + 2*0) into state 0 (= $ACF3 = GS_FULL_GAME
 *            setup) and the state machine walks forward until the
 *            title state ($16 = $93F3) is reached.  There IS NO
 *            attract-mode self-play — the only auto-running visuals
 *            are the boot animations + the post-victory credits.
 *
 * Compile-check:
 *     cd /Users/guilhermedavid/simant-re && \
 *       clang -Wall -Wextra -c audio_intro.c -o /tmp/ai.o
 */

#include <stdint.h>

extern uint8_t           wram[0x20000];
extern volatile uint8_t  mmio[0x10000];
#define dp wram
#define MMIO8(addr)   (*(volatile uint8_t  *)&mmio[(addr) & 0xFFFF])
#define MMIO16(addr)  (*(volatile uint16_t *)&mmio[(addr) & 0xFFFF])

/* SNES MMIO registers used by the SPC700 IPC + screen setup. */
#define INIDISP       MMIO8 (0x2100)
#define APUIO0        MMIO8 (0x2140)    /* music command          */
#define APUIO1        MMIO8 (0x2141)    /* (mute-channel-1 mark)  */
#define APUIO2        MMIO8 (0x2142)
#define APUIO3        MMIO8 (0x2143)    /* SFX command (channel 3)*/
#define HVBJOY        MMIO8 (0x4212)

/* Externs satisfied by other .c modules (see simant.c, states_menu.c, etc.). */
extern void enable_nmi_896D(void);
extern void wait_frames_8841(uint8_t n);
extern void fade_in_85FC(void);
extern void fade_out_8616(void);
extern void apu_send_music_8E88(uint8_t cmd);
extern void apu_play_sfx_8EA3 (uint8_t cmd);
extern void mask_nmi_after_yield_8976(void);
extern void asset_decompress_to_scratch_8D7E(uint8_t src_bank, uint16_t src_ofs);
extern void vram_dma_from_scratch_8ACC      (uint16_t length, uint16_t vram_dst);
extern void cgram_dma_8AED                  (uint8_t src_bank, uint16_t src_ofs);
extern void entity_spawn_0499C1             (uint16_t x, uint16_t y, uint8_t type);
extern void caption_screen_BACA             (uint8_t hold_seconds, uint16_t caption_ptr);
extern void wait_until_second_BA9E          (uint8_t add_seconds);
extern void common_screen_setup_BB38        (void);
extern void sub_877D                        (void);
extern void sub_C318                        (void);
extern void sub_C398                        (void);
extern void sub_8F08                        (uint8_t a);
extern void sub_8B0C                        (void);     /* per-page text upload prep */
extern void sub_87B1                        (void);     /* small "post-spawn sync"   */

/* ========================================================================
 * PART 1 — SPC700 SOUND DRIVER (host-side surface)
 * ========================================================================
 *
 * UPLOAD PROCEDURE
 * ----------------
 * Entry: JSL $08:8000 with X = source offset within bank $08 (always
 * $0A00 at boot).  The entry at $08:8000 is `JMP $8006`; sister entry
 * $08:8003 is `JMP $8085` (a different chain: the multi-asset loader
 * used by the encyclopedia screen).
 *
 * The procedure at $08:8006 (`spc700_upload_driver_088006` in simant.c):
 *
 *   1. Stash the 24-bit source pointer in dp[$E7..$E9]
 *      (E7=lo, E8=mid, E9=bank).
 *   2. Read 4 bytes from that pointer, pack into A/X/Y, JSR $817A
 *      (single-IPL-frame "send 4 bytes via APUIO0..APUIO3" helper).
 *   3. Advance the 24-bit pointer by 4, loop until the stream marker
 *      hits end (a sentinel pair inside the binary).
 *   4. End-of-stream: write 0 to APUIO0 + APUIO2 (long-form `STA $2142`
 *      from program-bank $08), then poll APUIO0 until SPC ack flips
 *      bit 7. JSL/RTL returns to caller (sub_BB8D during boot, or
 *      state $16's "music settled" re-upload path).
 *
 * RE-UPLOAD AT TITLE SCREEN
 * -------------------------
 * State $16 at $00:93F3 (`state_16_title_input_93F3` in states_gameplay.c)
 * checks dp[$0032] — when non-zero, the renderer just left a screen
 * that mutated APU state; the title state silences APUIO0..3, waits
 * 4 frames, then re-runs the asset chain at `$08:0A00` via JSL $088003.
 * This is the only post-boot SPC re-upload site.
 *
 * BINARY LAYOUT (FILE OFFSET 0x40A00 .. 0x47FFF, 30,208 bytes)
 * ------------------------------------------------------------
 * Without an SPC700 disassembler we can only delimit byte regions by
 * inspection:
 *
 *   file 0x40A00 .. ~0x42FFF (~9.5 KB)
 *       Mostly-uniform entropy ~4.5-5.2; characteristic of mixed
 *       SPC700 code + small lookup tables.  First 4 bytes are
 *       `58 1B  56 06` — the IPL handshake reads these as (A=$58,
 *       X=$1B56, Y=$06??) at JSR $817A entry.  Likely a header
 *       (start-PC = $1B58, channel-mask = $06) followed by the
 *       driver's reset+dispatch core.
 *
 *   file ~0x43000 .. ~0x46DFF (~16 KB)
 *       Music sequence data + sample directory.  Many 0x03/0x06/0x0C
 *       byte motifs which match typical Maxis-era song-data opcodes
 *       (note duration + pitch).  Repeating 0x53/0x56/0x60/0x67 byte
 *       patterns at e.g. $42500 look like channel-N pitch tables.
 *
 *   file ~0x46E00 .. ~0x47000 (~512 bytes)
 *       Lower-entropy transition zone — likely the BRR sample
 *       directory (consecutive 4-byte "start+loop" addresses into
 *       SPC ARAM, plus envelope/ADSR scratch).
 *
 *   file 0x47000 .. 0x47FFF (~4 KB)
 *       BRR-encoded sample data.  Entropy drops to ~2.9-3.7, with
 *       dominant `aa`/`a8`/`88`/`2a` bytes — those are the 4-bit
 *       BRR nibble pairs (8 nibbles = 16 samples per 9-byte block).
 *       The last 128 bytes (file 0x47F80..0x47FFF) are all sample
 *       nibbles, confirming the binary ends at end-of-bank.
 *
 * Total uploaded payload ≈ 30,208 bytes (well under the 64 KB SPC
 * ARAM ceiling — the BIOS-resident IPL leaves $0002..$00EF and
 * $FFC0..$FFFF reserved).
 *
 * Disassembling the SPC700 instructions requires a separate tool
 * (e.g. spc700dis, snes9x's built-in debugger, or bsnes-plus's APU
 * trace logger).  The file region to feed it is:
 *     simant.sfc[0x40A00:0x48000]   (30,208 bytes)
 *
 * NOTE ON BANK-$08 MAPPING.  Address $08:0A00 is in the LOWER half of
 * bank $08, which in vanilla LoROM is unmapped (lower halves of
 * banks $00-$3F are WRAM/PPU mirrors).  SimAnt's cartridge wires the
 * bank-$08 lower half through to ROM by using the FastROM-mirror
 * convention ($88:0A00 ≡ file 0x40A00); the uploader explicitly uses
 * long-mode loads through dp[$E7..$E9] so the mirror works.
 * ======================================================================== */

/* Boundaries for the SPC700 binary inside the ROM image. */
#define SPC700_BIN_FILE_OFS         0x40A00u
#define SPC700_BIN_FILE_END         0x48000u
#define SPC700_BIN_SIZE_BYTES       (SPC700_BIN_FILE_END - SPC700_BIN_FILE_OFS) /* 0x7600 */
#define SPC700_BIN_LOROM_ADDR_BASE  0x080A00u    /* what the uploader passes as X */

/* The IPC handlers exposed to game code — already lifted in misc_helpers.c.
 * Reproducing the comment headers here for cross-reference: */

/* $00:8E88 — apu_send_music_8E88(cmd).
 *   - dp[$0037] := cmd
 *   - if (dp[$0033]) APUIO0 := dp[$0037]    ; music-enable gate
 *   - RTL                                                                  */

/* $00:8EA3 — apu_play_sfx_8EA3(cmd).
 *   - dp[$003A] := cmd
 *   - if (dp[$0036]) {                       ; SFX-enable gate
 *       dp[$003E] := (dp[$003E] + 1) & 0xFF; ; channel-3 retrigger counter
 *       APUIO3   := (dp[$003E] & 1) | cmd;   ; "different value every shot"
 *     }
 *   - RTL                                                                  */

/* sub_8611 / play_sfx_and_fade_8611: special inlined path that
 * unconditionally writes $C4 to APUIO0 (NOT going through the gate) and
 * then runs the standard fade-out loop.  Used only by the view-switch
 * tail in $00:8611, and that single $C4 write is the "view-change beep". */

/* ------------------------------------------------------------------------
 * SOUND COMMAND TABLE
 *
 * Every observed APU command byte that the 65816 side sends to the SPC700.
 * Music codes go to APUIO0 (via $00:8E88), SFX codes go to APUIO3 (via
 * $00:8EA3, OR'd with the channel-3 retrigger counter so the SPC700 always
 * sees a value change even on a same-SFX retrigger).
 *
 * The "Track" column lists where the call originates in the lifted decomp;
 * "$00:XXXX" addresses are the ROM call-site, file names link to the
 * lifted handler bodies in this repository.
 * ------------------------------------------------------------------------ */

typedef enum {
    SC_KIND_MUSIC = 0,   /* sent via APUIO0 (8E88) — gated by dp[$33] */
    SC_KIND_SFX   = 1,   /* sent via APUIO3 (8EA3) — gated by dp[$36] */
    SC_KIND_RAW   = 2,   /* direct APUIO0/3 write, ungated            */
    SC_KIND_SILENCE = 3, /* writes 0 to all four APUIO regs           */
} SoundCmdKind;

typedef struct {
    uint8_t           cmd;        /* the byte sent to APUIO0/APUIO3   */
    SoundCmdKind      kind;
    const char       *role;       /* one-line description             */
    const char       *call_site;  /* "$00:XXXX in <file>"             */
} SoundCommand;

const SoundCommand sound_commands[] = {
    /* ---------------- MUSIC (APUIO0 via $00:8E88) ---------------------- */
    { 0x00, SC_KIND_SILENCE, "silence (write 0 to APUIO0..3)",
            "many; e.g. $00:ACF3 (GS_FULL_GAME setup), $00:B155 (info)" },
    { 0x02, SC_KIND_MUSIC,   "encyclopedia / ant-info BGM",
            "$00:B17D in states_menu.c (gs_ant_information_B155)"     },
    { 0x02, SC_KIND_MUSIC,   "credits BGM (same track as encyclopedia)",
            "$00:B8B0 in audio_intro.c (state_40_credits_setup_B875)" },
    { 0x04, SC_KIND_MUSIC,   "B-nest overview BGM",
            "$00:BFE5 in states_gameplay.c (state $1F setup)"         },
    { 0x06, SC_KIND_MUSIC,   "R-nest overview BGM",
            "$00:C037 in states_gameplay.c (state $21 setup)"         },
    { 0x08, SC_KIND_MUSIC,   "main-menu / title BGM",
            "$00:9468 in states_gameplay.c (state $16 title input)"   },
    { 0x0C, SC_KIND_MUSIC,   "B-nest close-up interior BGM",
            "$00:CCBE in states_gameplay.c (state $24 nest close-up)" },
    { 0x0E, SC_KIND_MUSIC,   "R-nest close-up interior BGM",
            "$00:CCC2 in states_gameplay.c (state $26 nest close-up)" },
    { 0x16, SC_KIND_MUSIC,   "surface-overview BGM (track #2)",
            "$00:BDCA in states_gameplay.c (state $1D setup tail)"    },
    { 0x30, SC_KIND_MUSIC,   "pause-overlay music (sub_A0D2)",
            "$00:A0D2 (pause toggle one-shot; not yet lifted in full)" },

    /* ---------------- SFX (APUIO3 via $00:8EA3) ----------------------- */
    { 0x2B, SC_KIND_SFX,     "queen \"lay egg\" SFX",
            "$00:?? in player_actions.c (lay_egg)"                    },
    { 0x2C, SC_KIND_SFX,     "control-panel \"click\" SFX",
            "$00:?? in control_panels.c (4× cp_slider_step_*)"        },
    { 0x2C, SC_KIND_SFX,     "queen \"dig start\" SFX (same code)",
            "$00:?? in player_actions.c (start_dig_new_nest)"         },
    { 0x2E, SC_KIND_SFX,     "menu-open / icon-popup SFX",
            "$00:?? in player_actions.c (popup_open)"                 },
    { 0x44, SC_KIND_SFX,     "\"pickup\" SFX (yellow ant grabs item)",
            "$00:?? in player_actions.c (yellow_pickup)"              },
    { 0x4E, SC_KIND_SFX,     "\"ouch / munch\" SFX (collision, eating)",
            "entities_c.c (Queen hit ×2) + player_actions.c (eat ×2)" },
    { 0x4F, SC_KIND_SFX,     "trophallaxis \"feed-from-nestmate\" SFX",
            "$00:?? in player_actions.c (feed_from_nestmate)"         },

    /* ---------------- RAW WRITES (bypass the gate) -------------------- */
    { 0xC4, SC_KIND_RAW,     "view-switch confirmation beep "
                             "(direct STA $2140)",
            "$00:8611 in simant.c (play_sfx_and_fade_8611)"           },
    { 0xC8, SC_KIND_SFX,     "\"dig new nest\" confirmation SFX",
            "$00:D754 (Queen's dig-new-nest commit; not lifted yet)"  },
};

const unsigned sound_commands_count =
    sizeof(sound_commands) / sizeof(sound_commands[0]);


/* ========================================================================
 * PART 2 — INTRO + CREDITS
 * ========================================================================
 *
 * The intro flow is woven into the same game-state machine that runs
 * gameplay.  The dispatch table at $00:9369 actually has $44 entries
 * (states 0x00 .. 0x43); the existing lifts in states_menu.c +
 * states_gameplay.c cover up to state $2F.  Beyond that, states
 * $30..$43 are post-game / scenario-ending / credits states.  This file
 * lifts the credits-specific ones.
 *
 * BOOT-TIME STATE FLOW
 * --------------------
 *   RESET ($00:8009)  -> JMP $9340 (main_9340)
 *     - WRAM cleared at $801D-$8033 (also zeroes dp[$0B])
 *     - SP = $03FF, IRQ disabled, CLD, native mode
 *
 *   main_9340 ($00:9340)
 *     - TASK_LIMIT := 2   (one slot for task 0, one for the dispatcher)
 *     - spawn_task(pc=$935C, a=0)   -- the dispatcher task
 *     - JSR $BB8D (boot_init_BB8D — DP defaults, SRAM shadow seed,
 *                  SPC700 upload via JSL $088000 with X=$0A00)
 *     - JSR $896D (enable NMI)
 *     - infinite spin (BRA -2)
 *
 *   game_state_dispatch_935C (task 1, runs forever)
 *     loop:
 *       LDA dp[$0B]  -> XBA / LDA #$00 -> ASL -> TAX
 *       JSR ($9369,x)        ; indirect-X call through state table
 *       BRA loop
 *
 * Initial dp[$0B] = 0 (WRAM was cleared at reset), so the FIRST handler
 * to fire is state 0 = $ACF3 = `gs_full_game_ACF3`.  This is the
 * "FULL GAME asset blast" — it loads main-game tilemaps and graphics,
 * fades in, then INC dp[$0B].  Subsequent states ($01..$15) walk
 * through the various screen-setup templates one after another, each
 * finishing with `INC $0B`.
 *
 * State $15 ($B4DA) is the BUG-CUTIN caption — it STZ $0B (writes 0
 * back) to RESTART the cycle.  But before $15 is reached, state $16
 * ($93F3) is also part of the parade — state $16 is the TITLE INPUT
 * state and it spawns the cursor + plays APU command $08 (main-menu
 * BGM).  Title-input either advances to $17 (save-picker) on L+R held
 * or to state 0 (= GS_FULL_GAME) when the player picks FULL GAME.
 *
 * There is NO attract-mode self-play in SimAnt.  The "intro" is just
 * the boot-time parade of screen templates, ending at $16 where the
 * player picks a mode.
 *
 * CREDITS FLOW
 * ------------
 * Triggered after winning the FULL game.  The post-victory path is:
 *
 *   state $06 = $B07B   GS_FULL_END
 *                        Loads $19:FC44 (celebration BG), spawns the
 *                        "GAME WON" sprites $5A and $5B, INC $0B.
 *
 *   state $07 = $B0FC   GS_SCENARIO_END   (also reused as 3-stage page)
 *
 *   state $08 = $B19F   GS_GAME_OVER (LOSING path — different entry)
 *
 *   state $0A = $B21A   "credits-continue" (Agent F label correct).
 *                        Force-blank, CGADSUB=$31 (BG1 half-add color
 *                        math for the "shimmer"), loads $19:FC44,
 *                        $07:E339, BAF2, $1A:B7E3, palette $07:A180.
 *                        Spawns entity type $01 (cursor input), fade in,
 *                        INC $0B.
 *
 *   state $0B = $B281   "scenario-end celebration" (Agent F correct).
 *                        Hold 1 sec, caption from $90:B6 hold 2 sec,
 *                        spawn type $5F at (0, $A0) — confetti — wait,
 *                        spawn type $6A — banner — wait, fade out,
 *                        INC $0B.
 *
 *   state $3F = $B833   AUTUMN -> WINTER post-victory narrative.
 *                        Caption $93:2D ("Autumn has ended..."),
 *                        caption $93:41 ("...and Winter has arrived"),
 *                        128-frame "fade" loop tweaking dp[$48] /
 *                        dp[$07] (BG2 V-scroll + page counter), then
 *                        caption $93:5B ("...their long sleep."),
 *                        fade out, INC.
 *
 *   state $40 = $B875   CREDITS SETUP.
 *                        force-blank, JSR $C318, TM=$01 (BG1 only),
 *                        JSR $C398, OBSEL=$A2, JSR $8F08, BGMODE=$07
 *                        (mode 7), CGRAM upload $07:B180 (palette),
 *                        mode-7 rotation centre = (0x0040, 0x0040),
 *                        STZ dp[$72] (page counter),
 *                        enable NMI, APU music cmd $02 (BGM), INC $0B.
 *
 *   state $41 = $B8B9   CREDITS PAGE LOOP — 24 calls to the per-page
 *                        renderer at $00:B94C, each with a different
 *                        bank-$01 text pointer.  See `credits_pages[]`
 *                        below.  INC $0B when finished.
 *
 *   state $42 = $B996   CREDITS WRAP-UP — common screen setup
 *                        ($00:BB38 + asset loads $18:FF9E, $07:D79E,
 *                        $19:??, $07:??), prepares the post-credits
 *                        "Thanks for playing!" tilemap.
 *
 *   state $43 = $BA4D   FINAL POST-CREDITS — wait 5 sec, color-math
 *                        TM=$11, run two B4BA (caption + sprite) cycles
 *                        with a 3-sec pause between, spawn type $72 at
 *                        ($0098, $00C0), then infinite spin (BRA -2).
 *                        Player must reset console to return to title.
 *
 * WILL WRIGHT and the MAXIS:
 *   - "Will Wright" is in the credits text at $01:93E0, which appears
 *     within page 1 of the credits.  Page 1's caption pointer is
 *     $01:93AF — see `credits_pages[1]` below.  The page-1 text reads
 *     "ORIGINAL\nGAME DESIGN\n\nWill Wright\nJustin McCormick" and is
 *     uploaded as a screen-wide BG1 text tilemap via JSL $04:9000.
 *   - "the Maxis" is in the credits text at $01:985A, which appears
 *     within page 22 of the credits (caption pointer $01:9850).  The
 *     page-22 text reads "and all the rest of\n   the Maxis\n
 *     Ant Heads".
 *
 * Both names are uploaded as part of larger text blocks; there is NO
 * dedicated `LDY #$93E0` / `LDY #$985A` instruction that loads just
 * those bytes.
 * ======================================================================== */

/* Credit-page caption pointers (bank $01), in render order.
 * Lifted verbatim from the LDY-immediates at $00:B8B9..$00:B946. */
const uint16_t credits_pages[] = {
    /*  0 */ 0x93AF,   /* "ORIGINAL GAME DESIGN: Will Wright / Justin McCormick" */
    /*  1 */ 0x93C1,   /* "PRODUCER: Don Walters"                                */
    /*  2 */ 0x93FD,   /* "PRODUCT MANAGER: Beckie O'Brien"                      */
    /*  3 */ 0x941C,   /* "LEAD TESTER: Roger Johnsen"                           */
    /*  4 */ 0x9446,   /* "TESTING SUPERVISOR: Alan Barton"                      */
    /*  5 */ 0x946A,   /* "ADDITIONAL TESTING: Manny Granillo / Chris Weiss"     */
    /*  6 */ 0x9496,   /* "ART DIRECTOR: Jenny Martin"                           */
    /*  7 */ 0x94D3,   /* "ARTWORK: Susie Greene"                                */
    /*  8 */ 0x94F9,   /* "DOCUMENTATION: Michael Bremer"                        */
    /*  9 */ 0x9519,   /* "DOCUMENTATION DESIGN & LAYOUT: Vera Jaye"             */
    /* 10 */ 0x953A,   /* "CONTRIBUTIONS TO THE MANUAL: Tom Bentley / Kris..."   */
    /* 11 */ 0x956B,   /* (manual contributors, page 2)                          */
    /* 12 */ 0x95D1,   /* (etc. — these later pages get progressively longer)   */
    /* 13 */ 0x9606,
    /* 14 */ 0x9627,
    /* 15 */ 0x9656,
    /* 16 */ 0x967D,
    /* 17 */ 0x969E,
    /* 18 */ 0x96EC,
    /* 19 */ 0x9732,
    /* 20 */ 0x978F,
    /* 21 */ 0x97BF,
    /* 22 */ 0x981E,   /* (still credits — likely "Special Thanks" or similar)   */
    /* 23 */ 0x9871,   /* final page — the "Maxis Ant Heads" intermediate string
                          falls between entries 22 ($981E) and 23 ($9871).        */
};
const unsigned credits_pages_count =
    sizeof(credits_pages) / sizeof(credits_pages[0]);

/* Convenience table mapping page index -> decorative sprite type.
 * Reproduced from the byte table at $00:B98F..$00:B995 (7 entries,
 * indexed by `dp[$72] mod 7` after INC).  These are the small ant-head
 * mascots that flash up next to each credit page. */
const uint8_t credits_page_sprites[7] = {
    0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71,
};

/* ========================================================================
 * Lifted state handlers.
 *
 * These slot into the same `gameplay_states[]` table that
 * states_gameplay.c builds, but live in this file because they're part
 * of the credits/intro flow.  Inclusion order matches the dispatch
 * table at $00:9369.
 * ======================================================================== */

/* $00:B83A — INTERNAL "winter ending" routine.  NOT in the state table
 * directly; called from state $3F's prologue (which sits at $B833).
 * Plays the 3-caption "Autumn has ended / Winter has arrived / their
 * long sleep" sequence with a slow scroll between the 2nd and 3rd
 * caption, then fade out + INC. */
static void winter_ending_inner_B83A(void)
{
    /* LDA #$01 / JSR $BA9E    ; (the first byte at $B833) */
    wait_until_second_BA9E(0x01);

    /* LDA #$02 / LDY #$932D / JSR $BACA  — caption "Autumn has ended..." */
    caption_screen_BACA(0x02, 0x932D);

    /* LDA #$02 / LDY #$9341 / JSR $BACA — caption "...and Winter has arrived" */
    caption_screen_BACA(0x02, 0x9341);

    /* STZ $6C ; counter = 0
     * loop:  JSR $877D (×3), INC dp[$48-$49] (BG2 H-scroll), INC dp[$07-$08],
     *        INC $6C; CMP #$80; BNE loop                                 */
    dp[0x6C] = 0;
    do {
        sub_877D(); sub_877D(); sub_877D();
        if (++dp[0x48] == 0) dp[0x49]++;
        if (++dp[0x07] == 0) dp[0x08]++;
    } while (++dp[0x6C] != 0x80);

    /* LDA #$05 / LDY #$935B / JSR $BACA  — "...their long sleep." */
    caption_screen_BACA(0x05, 0x935B);

    /* JSR $8611  — view-switch beep + fade-out (the $C4 cue ALSO ends scenes). */
    APUIO0 = 0xC4;                                      /* direct write */
    fade_out_8616();
    dp[0x0B]++;
}

/* Wrapper used by the state-3F slot. */
static void state_3F_winter_ending_B833(void)
{
    winter_ending_inner_B83A();
}

/* ========================================================================
 * STATE $40   $00:B875   CREDITS SETUP — mode-7 background + APU $02
 * ========================================================================
 * Just before the per-page credit loop, this state switches the PPU
 * into mode 7 with the "Ant Heads" palette, recenters the mode-7
 * rotation origin, and sends APU music command $02 (the encyclopedia
 * track is reused for credits).
 */
static void state_40_credits_setup_B875(void)
{
    mask_nmi_after_yield_8976();                       /* JSR $8976       */
    INIDISP = 0x80;                                    /* force-blank     */
    sub_C318();                                        /* DP scratch init */
    MMIO8(0x212C) = 0x01;                              /* TM = BG1 only   */
    sub_C398();                                        /* OAM scratch     */
    MMIO8(0x2101) = 0xA2;                              /* OBSEL = $A2     */
    sub_8F08(0x00);                                    /* OAM-DMA prep    */
    MMIO8(0x2105) = 0x07;                              /* BGMODE = 7      */
    dp[0x98]      = 0x03;                              /* mode-7 flag set */
    /* Palette upload: CGRAM dest 0, source $07:B180 (256 entries). */
    cgram_dma_8AED(0x07, 0xB180);
    /* Mode-7 rotation centre (16-bit at dp[$9E], dp[$A0]). */
    *(uint16_t *)&dp[0x9E] = 0x0040;
    *(uint16_t *)&dp[0xA0] = 0x0040;
    dp[0x72] = 0x00;                                   /* page counter    */
    enable_nmi_896D();
    apu_send_music_8E88(0x02);                         /* credits BGM     */
    dp[0x0B]++;
}

/* $00:B94C — render-one-credits-page helper.
 *
 *   PHY                       ; save the caption ptr passed by the caller
 *   JSR $8976                 ; mask NMI after yield
 *   LDA #$80 / STA $2100      ; force-blank
 *   LDA dp[$72] / INC A       ; bump page counter
 *   STA $002D                 ; ??? — possibly mode-7 SC base index
 *   JSR $8B0C                 ; per-page CG/BG setup
 *   LDX #$0000 / PLY          ; X=0, Y=caption ptr
 *   LDA #$7F / JSL $049000    ; bank-$04 text-tile upload (A=tile attr, X/Y=src)
 *   LDX #$8000 / LDY #$0000   ; DMA len $8000 bytes to VRAM $0000
 *   JSR $8ACC                 ; (the credits text fills the entire BG1)
 *   LDA #$00 / XBA / LDA dp[$72] / TAY
 *   LDA $B98F,y               ; sprite table lookup
 *   JSL $0499C1               ; spawn that sprite (X=Y=0 from earlier)
 *   JSR $896D                 ; enable NMI
 *   JSR $87B1                 ; small "post-spawn sync"
 *   INC dp[$72]               ; bump counter
 *   LDA dp[$72] / CMP #$07 / BCC +2 / STZ dp[$72]
 *   RTS
 */
static void render_credits_page_B94C(uint16_t caption_ptr)
{
    extern void asset_chain_088003(uint16_t selector_x);  /* placeholder */
    extern void bg1_text_upload_049000(uint8_t attr, uint16_t src_ofs);

    mask_nmi_after_yield_8976();
    INIDISP = 0x80;
    dp[0x002D] = (uint8_t)(dp[0x72] + 1);
    sub_8B0C();
    /* The original passes Y from a PLY; we pass caption_ptr explicitly. */
    bg1_text_upload_049000(/*A=*/0x7F, /*X=*/caption_ptr);
    vram_dma_from_scratch_8ACC(0x8000, 0x0000);
    /* Mascot sprite: indexed table at $00:B98F, modulo 7 by post-INC. */
    unsigned slot = dp[0x72] % 7;
    entity_spawn_0499C1(0, 0, credits_page_sprites[slot]);
    enable_nmi_896D();
    sub_87B1();
    if (++dp[0x72] >= 0x07) dp[0x72] = 0;
}

/* $00:B8B9 — CREDITS PAGE-LOOP state.  Iterates all 24 entries of
 * `credits_pages[]` via the renderer, then INC $0B. */
static void state_41_credits_pageloop_B8B9(void)
{
    for (unsigned i = 0; i < credits_pages_count; ++i)
        render_credits_page_B94C(credits_pages[i]);
    dp[0x0B]++;
}

/* $00:B996 — CREDITS WRAP-UP state ($42).  Re-runs common-screen
 * setup and loads the "Thanks for playing!" tilemap. */
static void state_42_credits_wrapup_B996(void)
{
    mask_nmi_after_yield_8976();
    INIDISP = 0x80;
    common_screen_setup_BB38();
    /* BG2 V scroll write-twice = $06; OBSEL = $72 (sprite tile base). */
    MMIO8(0x210C) = 0x06;
    MMIO8(0x2107) = 0x72;
    asset_decompress_to_scratch_8D7E(0x18, 0xFF9E);
    vram_dma_from_scratch_8ACC      (0x4000, 0x0000);
    asset_decompress_to_scratch_8D7E(0x07, 0xD79E);
    vram_dma_from_scratch_8ACC      (0x0800, 0x7000);
    asset_decompress_to_scratch_8D7E(0x19, 0xB02F);
    vram_dma_from_scratch_8ACC      (0x4000, 0x2000);
    asset_decompress_to_scratch_8D7E(0x07, 0xDBED);
    /* (Tail continues past $B9CA; truncated for the lift.) */
    dp[0x0B]++;
}

/* $00:BA4D — FINAL POST-CREDITS state ($43).  Caption-only "fin"
 * screen; the player must reset the console after this to return
 * to the title state. */
static void state_43_post_credits_BA4D(void)
{
    wait_until_second_BA9E(0x05);
    MMIO8(0x212C) = 0x11;                              /* TM = BG1+OBJ   */
    MMIO8(0x212D) = 0x0C;                              /* TS = BG3+BG4   */
    caption_screen_BACA(0x1E, 0xB4BA);                 /* 30-sec caption */
    MMIO8(0x212D) = 0x00;
    caption_screen_BACA(0x15, 0xB4BA);                 /* 21-sec caption */
    /* $2188 is WMDATA (WRAM data port) — not CGADSUB ($2131). The two
     * writes below stage two bytes through the WRAM-write window, used
     * elsewhere in the credits/fin path. */
    MMIO8(0x2188) = 0x7F;
    MMIO8(0x2188) = 0x1E;
    wait_until_second_BA9E(0x03);
    entity_spawn_0499C1(0x0098, 0x00C0, 0x72);         /* "fin" sprite   */
    /* Infinite spin — the original here is "BRA -2 forever".  We model
     * that as a never-incremented dp[$0B].  In the real ROM, the only
     * exit is a hardware reset. */
    for (;;) sub_877D();
}

/* ========================================================================
 * Dispatch slots — these would slot into the same gameplay_states[]
 * array that states_gameplay.c builds, indices $3F..$43.  We expose
 * the symbols so that states_gameplay.c's table can be extended later.
 * ======================================================================== */
typedef void (*credits_state_fn)(void);

const credits_state_fn credits_state_table[5] = {
    [0] = state_3F_winter_ending_B833,        /* state $3F */
    [1] = state_40_credits_setup_B875,        /* state $40 */
    [2] = state_41_credits_pageloop_B8B9,     /* state $41 */
    [3] = state_42_credits_wrapup_B996,       /* state $42 */
    [4] = state_43_post_credits_BA4D,         /* state $43 */
};


/* ========================================================================
 * NOTES FOR A FUTURE SPC700 LIFT
 * ========================================================================
 *
 * To disassemble the SPC700 driver itself:
 *
 *   1. Extract the binary:
 *        dd if=simant.sfc of=spc.bin bs=1 skip=265216 count=30208
 *      (265216 = 0x40A00, 30208 = 0x7600)
 *
 *   2. The first 4 bytes are not SPC700 code — they're the 65816-side
 *      uploader's framing.  The actual SPC700 reset/entry PC is read
 *      out of those 4 bytes by JSR $00:817A.  Hypothesis: entry = $1B58
 *      (the LE-pair `58 1B`), with the leading `56 06` being the IPL
 *      command word (0x06 = "go to address in A:X").
 *
 *   3. Disassemble with any SPC700 tool (e.g. `spc700dis` or hand-
 *      writing a parser — the 65816 disasm.py here would need a separate
 *      tool because the ISAs share no opcodes).
 *
 *   4. Cross-reference the command bytes in `sound_commands[]` against
 *      the SPC700 driver's main loop dispatch: the driver polls APUIO0
 *      every tick and branches on the value.  The dispatch will be a
 *      table indexed by the command code; expected layout is
 *      `bra .seq00 / bra .seq02 / bra .seq04 / ...` at the start of the
 *      driver code, with each `.seqNN` pointing at a music-sequence
 *      header in ARAM.
 *
 *   5. The BRR sample directory is the easiest landmark — search for
 *      consecutive 4-byte (start, loop) ARAM pointers in the region
 *      around file 0x46E00..0x47000.  Sample data immediately follows.
 *
 * ======================================================================== */

/* Tag every public symbol so -Wunused-variable stays quiet without
 * actually changing observable behaviour. */
__attribute__((used))
static const void * const _audio_intro_refs[] = {
    (void const *)sound_commands,
    (void const *)&sound_commands_count,
    (void const *)credits_pages,
    (void const *)&credits_pages_count,
    (void const *)credits_page_sprites,
    (void const *)credits_state_table,
    (void const *)state_3F_winter_ending_B833,
    (void const *)state_40_credits_setup_B875,
    (void const *)state_41_credits_pageloop_B8B9,
    (void const *)state_42_credits_wrapup_B996,
    (void const *)state_43_post_credits_BA4D,
    (void const *)render_credits_page_B94C,
    (void const *)winter_ending_inner_B83A,
};

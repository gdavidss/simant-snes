/*
 * SimAnt (SNES) — Save/Load + Sound/Speed Options chains.
 *
 * This file is a structural reconstruction of three intertwined subsystems
 * lifted directly from the 65816 disassembly. None of it runs as-is — it
 * documents the byte layout, control flow, and storage locations so the
 * Flipper-Zero port (or any reimplementation) has a faithful blueprint.
 *
 *   I.   SAVE  — write WRAM to SRAM, LZSS-compressed, with checksum + signature
 *   II.  LOAD  — read SRAM back to WRAM, validate signature + checksum
 *   III. SOUND — Music/SFX on-off toggles (dp[$0033..$0036], dp[$0044])
 *   IV.  SPEED — Fast/Normal/Slow/Pause (dp[$0016], dp[$001E])
 *   V.   ERASE — clear individual save slots
 *
 * The save subsystem is the most complex — there are SEVEN cooperating
 * routines spanning banks $00, $02, and $03. The chain is:
 *
 *     $00:9EA0 save_full_game_ui  (UI + status messages)
 *        |
 *        v                      via the bank-2 trampoline:
 *     $02:8015 save_full_entry  -> $03:F988 save_full_game (real serializer)
 *     $02:801A save_scn_entry   -> $03:F9B9 save_scenario  ("Summarize"-style)
 *     $02:801F load_entry       -> $03:FA74 load_game      (the LOAD path)
 *
 * Caller convention for the bank-3 entries:
 *   - DBR = $7F (PHB done, then LDA #$7F PHA PLB)
 *   - D   = $0200 (PEA #$0200 PLD)
 *   - M16, X16
 *   - Return value in A: 0 = success, $FFFF = error
 *
 * Layout of SRAM ($70:0000-$70:7FFF, 32 KB):
 *
 *   $70:0000-0007  bulk-save header (8 bytes; high addresses NOT counted in
 *                  checksum)
 *      $70:0000   "save type" byte (0=none, 1=full, 2=scenario after summarize)
 *      $70:0001-0002  serializer state (init context)
 *      $70:0003-0004  reserved / version
 *      $70:0005-0006  16-bit checksum of $70:0008..$70:7F9F
 *      $70:0007   slot index or scenario level
 *   $70:0008-7F9F  compressed save body (LZSS, max ~32 KB; see DOBBY format)
 *                  Body begins with 5-byte signature ("DOBBY" or "DURRY"),
 *                  then 2-byte uncompressed length, then 2-byte reserved,
 *                  then 2-byte compressed length, then LZSS stream.
 *   $70:7FA0-7FA2  3-byte save signature (game-state marker, written by
 *                  save_signature_write_AA2B at $00:AA2B-$AA3E):
 *                    $70:7FA0  = dp[$02ED]  (game-state code; STA after
 *                                            the LDA $02ED at $AA2B)
 *                    $70:7FA1  = dp[$0004]  (clock hours)
 *                    $70:7FA2  = dp[$0003]  (clock minutes)
 *   $70:7FB0-7FBF  16-byte developer signature ("TOMCAT SYSTEM   ")
 *                  Validated by sub_BC53 at boot. If corrupted, restored
 *                  from ROM ($01:9893).
 *   $70:7FC0-7FFF  4 scenario-save slots, 16 bytes each:
 *                    $70:7FC0+i*16 ... $70:7FC0+i*16+15  for i=0..3
 *                  Slot 0 byte 0 nonzero => slot is occupied.
 *                  Slot ASCII labels are read by save_slot_label_A9D4.
 *
 * The "DOBBY"/"DURRY" signatures live at:
 *     $03:F97E  "DOBBY" — full-game save (compressed via $03:8000)
 *     $03:F983  "DURRY" — scenario "summarized" save (smaller, fits when
 *                       full save returns "data too big")
 *
 * See wiki/18-save-load.md §2 for the DOBBY/DURRY developer-in-joke
 * commentary (both signatures, plus the "TOMCAT SYSTEM" 16-byte
 * developer signature at $70:7FB0, look like project fingerprints
 * worth cataloguing on TCRF).
 *
 * Two-level save retry: if save_full returns $FFFF, the UI prompts
 * "Save data is too big. Summarize? Yes/No". On Yes, save_scenario is
 * tried (uses a leaner compression / different signature).
 *
 * ---------------------------------------------------------------------------
 * Direct-page variables introduced or used by this subsystem:
 *
 *   dp[$0002]  during gameplay this is BOTH the wall-clock-seconds counter
 *              AND a flag the icon code uses to distinguish paused ($04)
 *              from running ($06).
 *   dp[$0003]  clock minutes (1-byte)
 *   dp[$0004]  clock hours (1-byte)
 *   dp[$0016]  SPEED setting: 1=Fast, 2=Normal, 3=Slow.  (Pause uses
 *              dp[$002A] instead — see below.)
 *   dp[$001E]  per-state speed shadow (copied from dp[$0016] every state
 *              loop iteration). Read by entity AI bodies in bank $04.
 *   dp[$0026]  per-state frame counter (INCs every loop iteration)
 *   dp[$002A]  PAUSE flag (set by START button in sub_8101; cleared by
 *              the Speed option's resume path)
 *   dp[$002E]  text color base (state tile color for HUD)
 *   dp[$0033]  MUSIC ON/OFF and master sound gate. JSL $00:8E88 reads
 *              dp[$0033..$0036] (4 entries) to decide whether to write
 *              the SFX index to APUIO0.
 *   dp[$0034]  Sound channel 1 enable (SFX)
 *   dp[$0035]  Sound channel 2 enable (SFX)
 *   dp[$0036]  Sound channel 3 enable (SFX)
 *   dp[$0037]  current SFX command byte (sent to APUIO0 when SFX-enabled)
 *   dp[$003B-3E]  per-channel SFX repeat counters (used by $00:8EA3)
 *   dp[$0044]  Music icon-display flag (toggled by $00:CE04; controls the
 *              gameplay HUD "music note" sprite, NOT the actual audio gate)
 *   dp[$0045-48]  4-slot "selected" tracker for the Scent Display options
 *              screen (state $2A/$2B). Each slot holds a row-index
 *              (0..11) or $FF for unset.  Boot values: $01, $00, $FF, $FF.
 *   dp[$0055]  "save-complete" flag (set to 1 at end of save_game_959D)
 *   dp[$0056]  scenario level for Main-Menu choice (0=Full, 6..1E=scenario)
 *   dp[$0072]  "save type" parameter at $00:9555 / used to dispatch save vs
 *              load vs erase in the save menu
 *   dp[$007C-7E]  scratch indirect pointer used by erase code ($00:A9A0)
 *   dp[$0081]  Mouse speed selection
 *   dp[$008C]  text color, used by C91F draw routine
 *   dp[$0299]  popup gate (set/cleared by various UI states)
 * ------------------------------------------------------------------------ */

#include <stdint.h>
#include <string.h>

/* ============================================================
 * Memory model — same conventions as simant.c.
 * ============================================================ */
extern uint8_t  wram[0x20000];        /* $7E:0000 .. $7F:FFFF                */
#define dp wram                       /* direct page = wram[0..0xFF]         */
extern uint8_t  sram[0x08000];        /* $70:0000 .. $70:7FFF                */
extern volatile uint8_t mmio[0x10000];

#define APUIO0       (*(volatile uint8_t *)&mmio[0x2140])

/* WRAM index helper. The 65816 sees WRAM as two banks ($7E + $7F = 128 KB).
 * Our wram[] is just a 128 KB linear buffer. The original code uses
 * full 24-bit addresses like $7E:6000 — which in our buffer is offset
 * 0x06000 (the $7E base). For $7F:xxxx we add 0x10000. We provide an
 * accessor so the source reads naturally. */
__attribute__((unused))
static inline uint8_t *wram_at(uint32_t addr24)
{
    uint8_t bank = (addr24 >> 16) & 0xFF;
    uint16_t off = addr24 & 0xFFFF;
    if (bank == 0x7E) return &wram[off];
    if (bank == 0x7F) return &wram[0x10000 + off];
    /* Banks $00-$3F low addresses mirror $7E:0000-$7E:1FFF */
    if (off < 0x2000) return &wram[off];
    /* fall back: treat as $7E mirror */
    return &wram[off & 0xFFFF];
}

/* Forward declarations (so the file flows top-down). */
static void save_signature_write_AA2E_inline(void);
static uint16_t save_checksum_03_FC3A(void);

/* ============================================================
 * Bank-3 entry trampolines (the $02:80xx pad).
 *
 * Caller convention: see file header. These are exposed via a 16-entry
 * pad at $02:8000 so any bank can JSL into them without knowing the
 * actual long address in $03.
 * ============================================================ */
extern int16_t save_full_game_03_F988(void);     /* $03:F988 */
extern int16_t save_scenario_03_F9B9(void);      /* $03:F9B9 */
extern int16_t load_game_03_FA74(void);          /* $03:FA74 */
extern void    lz_decompress_03_8467(void);      /* $03:8467 */
extern void    lz_compress_03_8000(void);        /* $03:8000 */
extern void    post_load_init_03_8507(void);     /* $03:8507 */
extern void    sram_io_03_836A(void);            /* $03:836A — actually the
                                                    LZ-style codec used by
                                                    both compress and decompress */

__attribute__((unused))
static inline int16_t bank3_call_save_full(void)     { return save_full_game_03_F988(); }
__attribute__((unused))
static inline int16_t bank3_call_save_scenario(void) { return save_scenario_03_F9B9(); }
static inline int16_t bank3_call_load(void)          { return load_game_03_FA74(); }
__attribute__((unused))
static inline void    bank3_call_post_load(void)     { post_load_init_03_8507(); }

/* ============================================================
 * The two 5-byte save-format signatures, lifted byte-for-byte from
 * $03:F97E / $03:F983. These identify which compressor was used and
 * disambiguate from random SRAM.
 * ============================================================ */
static const uint8_t SAVE_SIG_DOBBY[5] = { 'D', 'O', 'B', 'B', 'Y' };  /* full */
static const uint8_t SAVE_SIG_DURRY[5] = { 'D', 'U', 'R', 'R', 'Y' };  /* summarized scenario */

/* Developer signature (validated at boot by sub_BC53). 16 bytes at $70:7FB0. */
static const uint8_t DEV_SIG[16] = "TOMCAT SYSTEM   ";   /* trailing spaces, 16 bytes total */

/* ============================================================
 * SRAM layout constants.
 * ============================================================ */
#define SRAM_BULK_BASE         0x0000  /* bulk save data (compressed)       */
#define SRAM_BULK_END          0x7F9E  /* exclusive end of checksum range   */
#define SRAM_CKSUM_OFF         0x0005  /* 1-byte checksum                   */
#define SRAM_GAMESTATE_SIG     0x7FA0  /* 3-byte signature (state + clock)  */
#define SRAM_DEV_SIG_OFF       0x7FB0  /* 16-byte "TOMCAT SYSTEM"           */
#define SRAM_SCN_SLOTS_OFF     0x7FC0  /* 4 × 16-byte scenario slots        */
#define SRAM_SLOT_SIZE         16
#define SRAM_NUM_SCN_SLOTS     4
#define SRAM_WRAM_STAGE        0x6000  /* compressed blob stages here in WRAM */

/* ============================================================
 * I. SAVE — full chain, from the icon click down to SRAM.
 * ============================================================
 *
 * The trigger comes from the "Save / Exit" icon in the gameplay HUD
 * (entity type $43, spawned by state $28 at $00:D7CE). When clicked,
 * the icon spawns popup state $2D (handler at $00:D24C), which calls
 * the view-switch helper that brings up the Save/Main-Menu submenu
 * via $00:9E36.
 *
 * UI flow (lifted from $00:9E36-$9F7F):
 *
 *   $00:9E36   prompt "Save Game / Main Menu" (string list $01:8836)
 *              dp[$1A] = 0 (Save) or 1 (Main Menu)
 *   $00:9E80   dispatch:
 *                  dp[$02A7] == 0 -> JMP $9E9C   "save flow"
 *                  dp[$02A7] != 0 -> show "Save data corrupt", jump to exit
 *   $00:9E9C   push some context (dp[$0037]), advance to save UI
 *   $00:9EA0   "Save Game" full-game save flow:
 *                  - draw "Saving. Please wait." string ($01:8843)
 *                  - JSL $02:8015 (-> save_full_game_03_F988)
 *                  - if A != $FF: success, write signature, return
 *                  - if A == $FF: "Save data is too big. Summarize? Y/N"
 *                                  prompt at $9F08 ($01:8868)
 *                  - on Yes: JSL $02:801A (-> save_scenario_03_F9B9)
 *                  - finally JSR $00:AA2B writes the 3-byte SRAM signature
 *
 * The full-game save body at $03:F988:
 *
 *     LDA #$7FB0 / STA dp[$D7]      ; ?
 *     JSL $03:F9EA                  ; copy WRAM $0200..$02AA (dp+stack) -> $EEDA
 *     JSL $03:FA16                  ; src=$5000, set up compressor pointers:
 *                                     dp[$CD] = (EF85 - 5000) = $9F85 = max length
 *                                     dp[$CF] = $5000 = compressor source addr
 *                                     dp[$D4] = $600B = stage destination
 *                                     dp[$D1] = $7F (src bank)
 *                                     dp[$D6] = $7E (dst bank)
 *                                     JSL $03:8000  (the LZ compressor)
 *     BCS $F9B5                     ; compress failed (too big) -> return $FFFF
 *     JSL $03:FA44                  ; write "DOBBY" signature to $7E:6000
 *     PHB
 *     LDY #$0000 / LDX #$6000 / LDA #$7FAF / MVN $70,$7E
 *                                   ; copy WRAM $7E:6000..$DFAF -> SRAM $70:0000..$7F9F
 *     PLB
 *     JSL $03:FC3A                  ; compute checksum of SRAM $70:0008..$7F9D
 *     STA $70:0005                  ; store checksum
 *     LDA #$0000 / RTL              ; success
 *
 * The "summarized" save at $03:F9B9 is identical except:
 *     - it calls $03:FA01 instead of $03:FA16: src=$6000 (smaller area)
 *     - it writes "DURRY" signature via $03:FA5C
 *     - it accepts a smaller dataset
 *
 * The compressor at $03:8000 is a custom LZ-style codec (related to but
 * distinct from the LZSS decompressor at $03:8467 used for VRAM assets).
 * It writes its output starting at dp[$D4] and dp[$D2] (output cursor),
 * bumps the cursor as it emits bytes. If the cursor exceeds dp[$CD]
 * (max length), it sets C and returns — that's the "too big" condition.
 *
 * ------------------------------------------------------------ */

/* Stage 1: the UI dispatcher. Called from the Save/Exit popup.
 *
 * Returns:
 *    0  success (slot saved, signature written)
 *   -1  user cancelled at submenu
 *   -2  save corrupted (dp[$02A7] != 0 at entry)
 *   -3  data too big AND summarize declined
 */
/* See wiki/18-save-load.md §5 for the three-bank dispatch chain
 * ($00 → $02:80xx pad → $03:Fxxx codec) and §2 for the DOBBY-then-DURRY
 * two-tier retry that this routine implements. */
int save_ui_dispatch_9E36(void)
{
    /* Step 1: prompt Save Game / Main Menu */
    /* extern uint8_t ui_prompt(uint16_t style, uint16_t string_table); */
    /* uint8_t choice = ui_prompt(0x1005, 0x8836); */
    /* dp[0x1A] = choice; */

    /* Step 2: branch */
    /* if (dp[0x1A] != 0) return 0;  // user picked "Main Menu" -> handled by caller */

    /* Step 3: save-data-corrupted shortcut */
    if (dp[0x02A7] != 0) {
        /* show "Save data corrupt" string ($01:8BC1), exit save flow */
        return -2;
    }

    /* Step 4: full-save attempt */
    int16_t rc = save_full_game_03_F988();
    if (rc != -1) {
        /* success — write the 3-byte SRAM state signature */
        save_signature_write_AA2E_inline();
        return 0;
    }

    /* Step 5: too big — prompt "Summarize?" */
    /* if (ui_prompt(0x1005, 0x8868) != 0) return -3; */

    /* Step 6: scenario-summary save */
    rc = save_scenario_03_F9B9();
    if (rc != -1) {
        save_signature_write_AA2E_inline();
        return 0;
    }
    return -3;
}

/* save_signature_write_AA2E — already lifted in simant.c but reproduced here
 * for completeness of the chain. */
static void save_signature_write_AA2E_inline(void)
{
    sram[SRAM_GAMESTATE_SIG    ] = dp[0x02ED];  /* state code */
    sram[SRAM_GAMESTATE_SIG + 1] = dp[0x0004];  /* hours      */
    sram[SRAM_GAMESTATE_SIG + 2] = dp[0x0003];  /* minutes    */
}

/* Stage 2: the actual serializer at $03:F988.
 *
 *   - "Source" WRAM region depends on entry: full = $5000, scenario = $6000.
 *   - Output staging buffer = WRAM $7E:6000-$DFAF (32 KB, the SRAM mirror).
 *   - Sets max-length so compressor knows when to fail.
 *   - On success, MVN copies $7E:6000-DFAF -> $70:0000-7F9F
 *
 * Return: 0 = success, -1 = compressor exceeded max length.
 */
int16_t save_full_game_03_F988_impl(void)
{
    /* Capture dp/stack to scratch ($03:F9EA copy WRAM $0200..$02AA -> $EEDA) */
    for (unsigned i = 0; i < 0xAB; ++i)
        wram[0xEEDA + i] = wram[0x0200 + i];

    /* Configure compressor pointers ($03:FA16). For full game: src=$5000. */
    *(uint16_t *)&dp[0xCD] = 0xEF85 - 0x5000;   /* = $9F85: max output length */
    *(uint16_t *)&dp[0xCF] = 0x5000;            /* compressor source         */
    *(uint16_t *)&dp[0xD4] = 0x600B;            /* compressor dest cursor    */
    dp[0xD1] = 0x7F;                            /* src bank                  */
    dp[0xD6] = 0x7E;                            /* dst bank                  */

    *(uint16_t *)&wram[0x6007] = *(uint16_t *)&dp[0xCD];  /* store length at $7E:6007 */

    /* Run the compressor. Returns C set if output exceeded dp[$CD]. */
    lz_compress_03_8000();
    /* if (carry_set) return -1; */

    /* Get final compressed length from dp[$D2], stamp it into header */
    wram[0x6009] = dp[0xD2];   /* $7E:6009 — bank $7E starts at offset 0 */

    /* Write "DOBBY" signature into the staging buffer head ($03:FA44) */
    for (unsigned i = 0; i < 5; ++i)
        wram[0x6000 + i] = SAVE_SIG_DOBBY[i];   /* $7E:6000+i */

    /* Bulk-copy WRAM staging -> SRAM (MVN $70,$7E with length $7FAF) */
    memcpy(&sram[0x0000], &wram[0x6000], 0x7FB0);  /* $7E:6000 -> $70:0000 */

    /* Compute and store checksum */
    sram[SRAM_CKSUM_OFF] = save_checksum_03_FC3A();

    return 0;
}

/* Stage 2b: the scenario "summarize" variant at $03:F9B9.
 * Identical structure but src=$6000 (smaller staging) and "DURRY" sig. */
int16_t save_scenario_03_F9B9_impl(void)
{
    for (unsigned i = 0; i < 0xAB; ++i)
        wram[0xEEDA + i] = wram[0x0200 + i];

    *(uint16_t *)&dp[0xCD] = 0xEF85 - 0x6000;   /* = $8F85: max output length */
    *(uint16_t *)&dp[0xCF] = 0x6000;            /* compressor source         */
    *(uint16_t *)&dp[0xD4] = 0x600B;
    dp[0xD1] = 0x7F;
    dp[0xD6] = 0x7E;

    *(uint16_t *)&wram[0x6007] = *(uint16_t *)&dp[0xCD];

    lz_compress_03_8000();
    /* if (carry_set) return -1; */
    wram[0x6009] = dp[0xD2];   /* $7E:6009 */

    /* "DURRY" signature ($03:FA5C) */
    for (unsigned i = 0; i < 5; ++i)
        wram[0x6000 + i] = SAVE_SIG_DURRY[i];   /* $7E:6000+i */

    memcpy(&sram[0x0000], &wram[0x6000], 0x7FB0);  /* $7E:6000 -> $70:0000 */
    sram[SRAM_CKSUM_OFF] = save_checksum_03_FC3A();
    return 0;
}

/* The save checksum at $03:FC3A — 16-bit running sum of 16-bit WORDS at
 * SRAM $70:0008..$70:7F9E (exclusive end), little-endian. The ROM body:
 *     LDA #$0000 / LDX #$0008
 *   loop:
 *     CPX #$7F9E / BEQ done
 *     CLC / ADC $700000,x   ; 16-bit add (M=0)
 *     INX / INX             ; advance by 2 (word stride)
 *     BRA loop
 *   done:
 *     RTL                   ; sum returned in A
 *
 * Although the result is 16 bits, only the low byte is stored at
 * $70:0005 (1-byte slot in the SRAM header). */
/* See wiki/18-save-load.md §4: 16-bit running WORD sum (M=0 in the
 * ROM), low byte stored at $70:0005. The earlier byte-sum interpretation
 * was a V2-C bug; this lift matches the ROM body at $03:FC3A. */
uint16_t save_checksum_03_FC3A(void)
{
    uint16_t sum = 0;
    for (unsigned i = 0x0008; i < 0x7F9E; i += 2) {
        /* 16-bit word read (LE): low byte + high byte at i+1. */
        sum = (uint16_t)(sum + (sram[i] | (sram[i + 1] << 8)));
    }
    return sum;
}

/* ============================================================
 * II. LOAD — the SRAM-to-WRAM path.
 *
 * Trigger: from the SAVED GAME state (state $02 = $00:AC63), which shows
 * the saved-game palette/map, then state $19 ($00:96B1) handles the
 * menu prompt, and state $1A ($00:96DF) is the "after-load init".
 *
 * The 5-entry menu at state $1A consists of:
 *    [0]  Full Game slot (the one written by save_full)
 *    [1]  Scenario slot 1
 *    [2]  Scenario slot 2
 *    [3]  Scenario slot 3
 *    [4]  Scenario slot 4
 *
 * The menu builder at $00:9517:
 *   - JSR $877D            ; spawn sub-task
 *   - LDX #$0000           ; clear menu buffer
 *   - LDA #$05 / STA $0B00,x ; menu count = 5
 *   - LDY #$0B0B / JSR $961B  ; emit "FULL GAME" label
 *   - LDY #$0B21 / JSR $961B  ; emit "SCENARIO 1" label
 *   - LDY #$0B37 / JSR $961B  ; emit "SCENARIO 2" label
 *   - LDY #$0B4D / JSR $961B  ; emit "SCENARIO 3" label
 *   - LDY #$0B63 / JSR $961B  ; emit "SCENARIO 4" label
 *   - JSR $AA05            ; probe SRAM for valid signature (full slot)
 *   - JSR $A9AD            ; probe 4 scenario slots; populate labels
 *   - LDX #$0903 / LDY #$0B00 / LDA #$15 / JSR $9187
 *                          ; prompt user; dp[$1A] = choice (0..4) or B = cancel
 *   - if BCC (cancel) -> retry
 *   - dp[$72] = dp[$1A]   ; remember choice
 *   - LDX #$1014 / LDY #$8122 / LDA #$07 / JSR $9187
 *                          ; "Yes/No" confirm
 *   - if BCC -> retry
 *   - if dp[$1A] == 0 (Yes) -> LOAD path
 *
 * LOAD path ($00:9571 onward):
 *   - if dp[$72] != 0 (scenario slot): jump to $95DD (per-slot load)
 *   - JSR $AA4C   ; signature probe
 *   - if BCS (no signature) -> show "Save Game empty" + restart
 *   - JSR $8976 / REP #$20 / JSL $02:801F (= load_game_03_FA74)
 *   - LDA #$01 / STA dp[$0055]    ; "load complete" flag
 *   - LDA #$1A / STA dp[$0B]      ; transition to state $1A
 *   - RTS
 *
 * The load body at $03:FA74:
 *
 *     JSL $03:FB07          ; clear WRAM ($4000+, $0000+, $2000+, $3000+)
 *     PHB
 *     LDY #$6000 / LDX #$0000 / LDA #$6FFF / MVN $7E,$70
 *                           ; bulk-copy SRAM $70:0000..$6FFF -> WRAM $7E:6000..$CFFF
 *     PLB
 *     JSL $03:FACB          ; signature check at $7E:6000:
 *                           ;   = "DOBBY" -> A=0 (full save)
 *                           ;   = "DURRY" -> A=1 (scenario save)
 *                           ;   = anything else -> A=$FF
 *     CMP #$0000 / BEQ $FA9E    ; full save: configure decompressor for $6000
 *     CMP #$0001 / BEQ $FA98    ; scenario:  configure decompressor for $5000
 *     LDA #$FFFF / RTL          ; corrupt: return error
 *     JSL $03:FB41              ; sets dp[$D4]=$6000 + decompressor cursor
 *     JSL $03:FB48              ; alternate for $5000 (scenario)
 *                               ; both end with JSL $03:836A (decompressor)
 *     JSL $03:FB63              ; copy decompressed $7E:EEDA..$EEDA+AA -> $0200
 *                               ; (restore the dp/stack snapshot)
 *     JSL $03:FB7A              ; restore game-object table 1 (CBB8/C3E8/C000)
 *     JSL $03:FBBA              ; restore game-object table 2 (D964/D57C/D388)
 *     JSL $03:FBFA              ; restore game-object table 3 (E328/DF40/DD4C)
 *     STZ $F6D5 / STZ $F6D3     ; clear sound/scent ticks
 *     LDA $EB5E / STA dp[$ED]   ; restore ant-count pointer
 *     LDA $EB5C / STA dp[$EF]
 *     LDA #$0000 / RTL          ; success
 *
 * Three FB7A/FBBA/FBFA helpers each rebuild a parallel-arrays object table.
 * Pattern (e.g. FB7A):
 *     LDA #$0000 / STA 01,s     ; loop index
 *     loop:
 *        CMP $E77E              ; compare to count limit
 *        BEQ done
 *        TAX
 *        SEP #$20
 *        LDA $CBB8,x            ; "type" field — if zero, skip
 *        REP #$20
 *        BEQ next
 *        STA 03,s               ; remember
 *        SEP #$20
 *        LDA $C3E8,x / TAY
 *        LDA $C000,x / TAX
 *        REP #$20
 *        JSL $02:F59F           ; allocate new slot for this object
 *        LDA 03,s
 *        SEP #$20
 *        STA $0000,x            ; write type back at allocated slot
 *        REP #$20
 *     next:
 *        INC 01,s
 *        BRA loop
 *
 * After load_game_03_FA74 returns, state $1A's body ($00:96DF) does:
 *   - clear menu state (dp[$0049,$004B,$0053,$004C,$02B1..$02B4 etc)
 *   - re-spawn HUD entities
 *   - JSL $02:8005 (= post_load_init_03_8507)
 *      which re-initializes WRAM tables ($8000+, $9000+, $A000+, $B000+,
 *      etc) for the loaded game world
 *   - STZ $0055
 *   - dispatch through dp[$0299]: state $16 (main menu), or back to state $29
 *     (full gameplay) etc.
 *
 * ------------------------------------------------------------ */

/* Returns 0 = full game, 1 = scenario, -1 = corrupt / no save.
 * See wiki/18-save-load.md §6: this is the three-way DOBBY / DURRY /
 * corrupt arbiter that gates the load path's decompressor branch. */
int16_t load_signature_check_03_FACB(void)
{
    /* Try "DOBBY" */
    int match = 1;
    for (unsigned i = 0; i < 5; ++i) {
        if (wram[0x6000 + i] != SAVE_SIG_DOBBY[i]) { match = 0; break; }
    }
    if (match) return 0;

    /* Try "DURRY" */
    match = 1;
    for (unsigned i = 0; i < 5; ++i) {
        if (wram[0x6000 + i] != SAVE_SIG_DURRY[i]) { match = 0; break; }
    }
    if (match) return 1;

    return -1;
}

int load_game_03_FA74_impl(void)
{
    /* Step 1: clear WRAM scratch regions (FB07). */
    memset(&wram[0x4000], 0, 0x0800);   /* $7E:4000..47FF */
    memset(&wram[0x4800], 0, 0x0800);   /* $7E:4800..4FFF */
    memset(&wram[0x5000], 0, 0x0800);   /* $7E:5000..57FF */
    memset(&wram[0x5800], 0, 0x0800);   /* $7E:5800..5FFF */
    memset(&wram[0x0000], 0, 0x2000);   /* $7E:0000..1FFF */
    memset(&wram[0x2000], 0, 0x1000);   /* $7E:2000..2FFF */
    memset(&wram[0x3000], 0, 0x1000);   /* $7E:3000..3FFF */

    /* Step 2: bulk-copy SRAM -> WRAM staging.
     * MVN $7E,$70: src=$70:0000, dst=$7E:6000, length=$7000. */
    memcpy(&wram[0x6000], &sram[0x0000], 0x7000);  /* $7E:6000 base */

    /* Step 3: validate signature. */
    int16_t sig_type = load_signature_check_03_FACB();
    if (sig_type < 0) return -1;     /* corrupt */

    /* Step 4: decompressor pointer setup.
     * Both branches call JSL $03:836A (LZ codec) with these settings:
     *   src bank  = $7E
     *   dst bank  = $7F  (final WRAM destination)
     *   src addr  = $6007 (length header location)
     *   dst addr  = (varies)  */
    if (sig_type == 0) {
        /* Full save: $03:FB48 — src=$5000 staging area */
        *(uint16_t *)&dp[0xD4] = 0x5000;
    } else {
        /* Scenario save: $03:FB41 — src=$6000 staging area */
        *(uint16_t *)&dp[0xD4] = 0x6000;
    }

    *(uint16_t *)&dp[0xCF] = 0x6007;    /* source offset (data starts here)  */
    dp[0xD1] = 0x7E;                    /* source bank                       */
    dp[0xD6] = 0x7F;                    /* destination bank                  */

    lz_decompress_03_8467();            /* via JSL $03:836A actually          */

    /* Step 5: restore the dp/stack snapshot ($03:FB63).
     * The decompressor reconstructed $7E:EEDA..$EEDA+AA which is then
     * copied to WRAM $0200..$02AA. */
    for (unsigned i = 0; i < 0xAB; ++i)
        wram[0x0200 + i] = wram[0xEEDA + i];  /* both in $7E mirror */

    /* Step 6: restore parallel-arrays object tables ($03:FB7A/FBBA/FBFA).
     * Each table is "object count" entries at:
     *    Table 1: type at $CBB8, attr at $C3E8, x at $C000     (count: $E77E)
     *    Table 2: type at $D964, attr at $D57C, x at $D388     (count: $E780)
     *    Table 3: type at $E328, attr at $DF40, x at $DD4C     (count: $E782)
     *
     * For each entry with non-zero type:
     *   - load attr (Y) and x (X)
     *   - JSL $02:F59F or $02:F5A8 (allocation routine — slot manager)
     *   - write the original type into the newly allocated slot
     *
     * Final destinations:
     *   Table 1 -> wram[$0000..$0FFF]   (the "active" entity-1 table)
     *   Table 2 -> wram[$2000..$2FFF]
     *   Table 3 -> wram[$3000..$3FFF]
     */
    /* (Pseudocode — actual allocation logic lives in $02:F59F and is not
     *  fully lifted here.) */

    /* Step 7: clear history-graph sample-count + write-cursor.
     * ROM: STZ $F6D5 / STZ $F6D3 at $03:FAB7/$03:FABA, executed with
     * M=0 (16-bit accumulator) — these are 16-bit STZ. The earlier lift
     * dropped the high byte. */
    *(uint16_t *)&wram[0xF6D5] = 0;
    *(uint16_t *)&wram[0xF6D3] = 0;

    /* Step 8: restore ant-count pointer */
    dp[0xED] = wram[0xEB5E];
    dp[0xEF] = wram[0xEB5C];

    return 0;
}

/* The full-save LOAD-UI dispatcher at $00:9517:
 *
 *   - shows the 5-slot menu
 *   - validates checksum before LOAD
 *
 * Returns 0 on success.
 */
int load_ui_dispatch_9517(void)
{
    /* Step 1: build menu */
    /* JSR $877D — spawn sub-task */
    /* Menu has 5 slots: Full, Scenario 1..4 */

    /* Step 2: prompt 'Are you sure?' */
    /* ... */

    /* Step 3: validate checksum */
    uint8_t want = sram[SRAM_CKSUM_OFF];
    uint16_t got = save_checksum_03_FC3A();
    if ((uint8_t)got != want) {
        /* corrupted! erase slot, display "Saved data corrupted" */
        sram[SRAM_BULK_BASE] = 0;
        /* show_text($1014, $813A, $08); */
        return -1;
    }

    /* Step 4: run load (skipping the DBR/D setup that the assembly does) */
    bank3_call_load();

    /* Step 5: dispatch to state $1A (after-save / after-load) */
    dp[0x0055] = 1;
    dp[0x000B] = 0x1A;
    return 0;
}

/* ============================================================
 * Save slot scanner — populates the 5-entry menu with slot labels.
 *
 * SRAM $70:7FA0-7FA2 holds the full-game signature (state + clock).
 * If non-zero, the full-game slot is "occupied" and labelled with the
 * game state + clock time.
 *
 * SRAM $70:7FC0-7FFF holds 4 × 16-byte scenario slots. Slot N's first
 * 4 bytes are checked for non-zero by save_slot_probe_A9D4.
 *
 * From $00:A9AD:
 *
 *     dp[$7E] = $70                  ; bank
 *     dp[$7C] = $7FC0                ; ptr
 *     dp[$6C] = 0                    ; slot counter
 *     loop:
 *       JSR $A9D4                    ; probe slot
 *       dp[$7C] += $10                ; next slot
 *       dp[$6C]++
 *       if dp[$6C] < 4: loop
 *
 * Each probe at $A9D4:
 *     LDY #$0000
 *     LDA [$7C],y     / INY    ; byte 0
 *     ORA [$7C],y     / INY    ; byte 1
 *     ORA [$7C],y     / INY    ; byte 2
 *     ORA [$7C],y     / INY    ; byte 3
 *     CMP #$00
 *     BEQ $A9EC      -> slot empty: show "empty" string
 *     else:
 *       JSR $C82C    (draw string at $8B77 prefix)
 *       LDA [$7C],y / DEC / CLC / ADC #$31 / STA $0B00,x / INX
 *       LDA #$FF / STA $0B00,x / INX
 * ------------------------------------------------------------ */

int save_slot_is_occupied(unsigned slot_index)
{
    if (slot_index >= SRAM_NUM_SCN_SLOTS) return 0;
    unsigned base = SRAM_SCN_SLOTS_OFF + slot_index * SRAM_SLOT_SIZE;
    return (sram[base] | sram[base+1] | sram[base+2] | sram[base+3]) != 0;
}

int full_save_is_occupied(void)
{
    /* From $00:AA4C: prompt $8BB8 (DOBBY-encoded ascii?) compare against $7FA0 */
    /* Implementation: if all 3 signature bytes are 0, no save. */
    return (sram[SRAM_GAMESTATE_SIG    ] |
            sram[SRAM_GAMESTATE_SIG + 1] |
            sram[SRAM_GAMESTATE_SIG + 2]) != 0;
}

/* ============================================================
 * III. ERASE — clear a save slot.
 *
 * The "Erase" string lives at $01:812C. The erase flow has two paths:
 *
 *   Full-save erase ($00:9608):
 *     LDA #$00 / STA $700000        ; clear the first byte (save-type)
 *     JMP $9517                      ; rebuild menu
 *
 *   Scenario-slot erase ($00:A986):
 *     XBA / LDA #$00 / REP #$20      ; A = slot index (passed in B)
 *     ASL ASL ASL ASL                 ; multiply by 16
 *     CLC / ADC #$7FC0 / STA dp[$7C] ; slot pointer
 *     SEP #$20 / LDA #$70 / STA dp[$7E]
 *     LDY #$0000
 *     STA [$7C],y / INY  (4 times)   ; zero the first 4 bytes
 *
 * This writes 4 zeros to the slot's header bytes ($70:7FC0+i*16). The slot
 * is now flagged as empty by save_slot_is_occupied.
 *
 * Note: the body data (bytes 4..15 of the slot) is NOT cleared; only the
 * 4-byte header. The original probably re-uses the body data on next save
 * to preserve continuity.
 * ------------------------------------------------------------ */

void erase_full_save_00_9608(void)
{
    sram[0x0000] = 0;
    /* Note: the 3-byte signature at $70:7FA0 is also cleared explicitly
     * in some paths via STA $707FA0 etc. */
    sram[SRAM_GAMESTATE_SIG    ] = 0;
    sram[SRAM_GAMESTATE_SIG + 1] = 0;
    sram[SRAM_GAMESTATE_SIG + 2] = 0;
}

void erase_scenario_slot_00_A986(uint8_t slot_index)
{
    if (slot_index >= SRAM_NUM_SCN_SLOTS) return;
    unsigned base = SRAM_SCN_SLOTS_OFF + slot_index * SRAM_SLOT_SIZE;
    for (unsigned i = 0; i < 4; ++i) sram[base + i] = 0;
}

/* ============================================================
 * IV. SOUND OPTIONS — Music On/Off, SFX On/Off.
 *
 * Storage:
 *   dp[$0033]  Music ON/OFF (1 = on, 0 = off).  Read by SFX gate $00:8E88:
 *                  STA $0037           ; SFX index goes into dp[$37]
 *                  LDA dp[$0033]       ; check gate
 *                  BEQ $8E96 ; if 0, skip APU write
 *                  LDA dp[$0037]
 *                  STA APUIO0
 *              So Music actually gates ALL audio (master enable).
 *
 *   dp[$0034..$0036]  SFX channel enables (channels 1, 2, 3).
 *                  Written by the SFX submenu at $00:A04B-$A063.
 *                  Read by the per-channel SFX walker at $00:8EA3:
 *                      LDY #$03 ; channels 0..3
 *                      LDA $0033,y          ; check enable
 *                      BEQ skip
 *                      INC $003B,y          ; per-channel counter
 *                      AND #$01
 *                      ORA $0037,y          ; SFX index from per-channel table
 *                      STA $2140,y          ; APUIO[0..3]
 *
 *   dp[$0044]  Music HUD icon flag (NOT the audio gate). Toggled by
 *              $00:CE04 in response to the music-icon click in the HUD.
 *              Read by $00:CF09 to decide which icon tile to draw.
 *
 * UI flow ($00:A000 — the "Options" submenu trigger):
 *
 *   $00:A000  LDA #$05 / JSR $9187    ; prompt: "Sound / Mouse / Speed"
 *                                       string list $01:8933
 *                                       result in dp[$1A] (0=Sound, 1=Mouse, 2=Speed)
 *   $00:A00E  ASL / TAX / JMP ($A012) ; jump table:
 *                                       [0] $A018  Sound submenu
 *                                       [1] $A064  Mouse submenu
 *                                       [2] $A078  Speed submenu
 *
 * Sound submenu ($00:A018):
 *
 *   LDX #$1005 / LDY #$894A / LDA #$05 / JSR $9187
 *                                       ; prompt "Music / SFX" ($01:894A)
 *                                       ; result in dp[$1A] (0=Music, 1=SFX)
 *   BCS $A026   ; if cancelled, retry
 *   LDA dp[$1A]
 *   BNE $A04B   ; -> if 1 (SFX), branch to SFX flow
 *
 *   Music flow ($A02A):
 *     LDX #$1005 / LDY #$8954 / LDA #$05 / JSR $9187
 *                                       ; prompt "OFF / ON " ($01:8954)
 *                                       ; result in dp[$1A] (0=OFF, 1=ON)
 *     BCC $A018   ; retry
 *     LDA dp[$1A] / STA dp[$0033]      ; commit Music ON/OFF
 *     BNE $A043
 *     STZ APUIO0                         ; on OFF: silence immediately
 *     RTS
 *     LDA dp[$0037] / JSL $00:8E88     ; on ON: play test SFX
 *     RTS
 *
 *   SFX flow ($A04B):
 *     LDX #$1005 / LDY #$8954 / LDA #$05 / JSR $9187
 *                                       ; same "OFF / ON" prompt
 *     BCC $A018   ; retry
 *     LDA dp[$1A] / STA dp[$0036] / STA dp[$0035] / STA dp[$0034]
 *                                       ; commit SFX to all 3 SFX channels
 *     RTS
 *
 * Boot defaults (from $00:BBD7-$BBE2):
 *     dp[$0033] = 1    (Music ON)
 *     dp[$0034] = 1    (SFX ch1 ON)
 *     dp[$0035] = 1    (SFX ch2 ON)
 *     dp[$0036] = 1    (SFX ch3 ON)
 *     dp[$0037..3A] = 0 (per-channel SFX index slots)
 *
 * Persistence: the sound settings are NOT persisted to SRAM — they reset
 * to defaults at every boot. (Unlike the developer signature, which IS
 * persisted at $70:7FB0.)
 *
 * ------------------------------------------------------------ */

/* Music submenu handler at $00:A02A — the actual ON/OFF flip. */
void sound_music_set_A02A(uint8_t enable)
{
    dp[0x0033] = enable;
    if (!enable) {
        APUIO0 = 0;  /* immediate silence */
    } else {
        /* play test SFX */
        dp[0x0037] = 0x2C;     /* test-sound index */
        /* JSL $00:8E88 — but only if dp[$0033] != 0, which it is now */
        APUIO0 = dp[0x0037];
    }
}

/* SFX submenu handler at $00:A04B — toggle 3 SFX channels at once. */
void sound_sfx_set_A04B(uint8_t enable)
{
    dp[0x0034] = enable;
    dp[0x0035] = enable;
    dp[0x0036] = enable;
}

/* Mouse submenu handler at $00:A064 — stores choice (0=Slow, 1=Normal, 2=Fast)
 * to dp[$0081], which is then read by mouse_set_speed_E494 (in mouse.c).
 * The mouse strings are at $01:8966 ("Slow ", "Normal", "Fast "). */
void mouse_speed_set_A064(uint8_t choice)
{
    dp[0x0081] = choice;
}

/* The SFX trigger gate at $00:8E88 — exposed here so simant.c can refer
 * back to it. NOTE: ONLY dp[$0033] is checked; channels 1-3 use the
 * walker at $00:8EA3 instead. */
void sfx_play_8E88(uint8_t sfx_index)
{
    dp[0x0037] = sfx_index;
    if (dp[0x0033] == 0) return;     /* music/sound master OFF */
    APUIO0 = sfx_index;
}

/* The per-channel SFX walker at $00:8EA3. Called once per frame with
 * Y=$03 (process channels 0..3 in descending order). */
void sfx_channel_walker_8EA3(void)
{
    for (unsigned y = 3; y < 4; --y) {  /* 3,2,1,0 then wraps */
        if (dp[0x0033 + y] == 0) continue;
        dp[0x003B + y]++;                            /* per-channel counter */
        uint8_t v = (dp[0x003B + y] & 1) | dp[0x0037 + y];
        /* STA $2140,y — write to one of APUIO[0..3] */
        ((volatile uint8_t *)&mmio[0x2140])[y] = v;
        if (y == 0) break;
    }
}

/* Music HUD icon click handler at $00:CE04. Toggles dp[$0044] (visual only),
 * plays a click SFX. NOTE: this is the ICON DISPLAY toggle, NOT the actual
 * audio gate. The audio gate is dp[$0033]. */
void music_icon_toggle_CE04(void)
{
    dp[0x0044] = 1 - dp[0x0044];   /* toggle 0<->1 */
    /* play UI click via $00:8EA3 with command $2C */
    /* JSL $00:8EA3 */
    dp[0x28] = 0xFF;               /* deselect cursor */
}

/* ============================================================
 * V. SPEED OPTION — Fast / Normal / Slow / Pause.
 *
 * Storage:
 *   dp[$0016]  current SPEED setting (1=Fast, 2=Normal, 3=Slow)
 *              (Pause is handled separately via dp[$002A].)
 *   dp[$001E]  per-frame speed shadow. EVERY game-state handler at the top
 *              does:
 *                  LDA $0016 / STA $001E / INC $0026
 *              dp[$001E] is read by entity AI in bank $04 (e.g. $04:A544
 *              in the walking-ant handler) to decide whether to skip frames.
 *   dp[$002A]  PAUSE flag. Set to 1 by START button in sub_8101. Also set
 *              by the Speed submenu when "Pause" is chosen.
 *
 * UI flow at $00:A078 (Speed submenu):
 *
 *   JSR $877D                              ; spawn sub-task
 *   ...display current speed...
 *   LDA dp[$0002] / CMP #$04 / BNE $A08E   ; check if currently paused
 *   LDA #$03                                ; if paused, display "Slow"
 *   BRA $A092
 *   LDA dp[$0016] / DEC                     ; otherwise show current speed - 1
 *   ASL / TAX
 *   REP #$20
 *   LDA $0189A1,x                           ; lookup string-list pointer table
 *                                              ($01:89A1) for displayed value
 *   TAY
 *   SEP #$20
 *   LDX #$1005 / LDA #$07 / JSR $C91F       ; draw "Speed = ___" header
 *   LDX #$1005 / LDY #$897D / LDA #$07 / JSR $9187
 *                                          ; prompt "Fast/Normal/Slow/Pause"
 *                                              strings at $01:8984 ($89A2)
 *                                              result in dp[$1A] (0..3)
 *   BCS $A0B3   ; if confirmed
 *   LDA dp[$1A] / INC                       ; A = (choice + 1)
 *   CMP #$04 / BNE $A0C5
 *
 *   Pause flow ($A0BA):
 *     JSR $E260                             ; pause-helper (clears dp[$0021])
 *     LDY #$0004 / STY dp[$0002]           ; "paused" state code = 4
 *     RTS
 *
 *   Speed-change flow ($A0C5):
 *     STA dp[$0016] / STA dp[$001E]        ; commit new speed (1, 2, or 3)
 *     LDY #$0006 / STY dp[$0002]           ; "running" state code = 6
 *     RTS
 *
 * GATING MECHANISM:
 *
 * The SimAnt main loop at state $29 ($00:D943) is:
 *
 *     LDA $0016                 ; load speed
 *     STA $001E                 ; shadow into per-frame slot
 *     INC $0026                 ; tick this state's frame counter
 *     JSR $877D                 ; YIELD until next NMI
 *     LDA dp[$002A]
 *     BEQ $D957                 ; not paused -> run sim
 *     JSR $A0D2                 ; paused -> handle pause UI
 *     STZ $02E3
 *     JSR $DDD7                 ; per-state work
 *     BCS $D943                 ; loop back if not done
 *     JSR $DB46
 *     BCS $D943
 *     JSR $DF79
 *     BCC $D978                 ; if valid input received, dispatch
 *     ; else continue
 *
 * The pause flag dp[$002A] short-circuits the sim. Speed-1 (Fast) keeps
 * dp[$001E] at 1, which means entity walkers in $04 will process every
 * frame. Speed-2 (Normal) keeps dp[$001E] at 2, meaning entities skip
 * every other frame. Speed-3 (Slow) keeps dp[$001E] at 3, meaning
 * entities process every third frame.
 *
 * The actual skip check lives in each entity handler (e.g. walking ant
 * at $04:A544 LDA $1E). When dp[$1E] doesn't match the entity's per-state
 * counter modulo, the AI skips and returns.
 *
 * Boot default:  dp[$0016] is NOT explicitly initialized at boot, so
 * the very first state body that runs reads whatever WRAM had. In
 * practice, dp[$0016] gets set by the first state transition (states 0..15
 * all write dp[$001E] but only the SPEED option writes dp[$0016]).
 *
 * ------------------------------------------------------------ */

enum SpeedChoice {
    SPEED_FAST   = 1,
    SPEED_NORMAL = 2,
    SPEED_SLOW   = 3,
};

/* The speed setter at $00:A0C5. */
void speed_set_A0C5(uint8_t choice_plus_one)
{
    dp[0x0016] = choice_plus_one;       /* speed setting */
    dp[0x001E] = choice_plus_one;       /* immediately propagate */
    *(uint16_t *)&dp[0x0002] = 0x0006;  /* "running" state code */
}

/* The pause setter at $00:A0BA. */
void speed_set_pause_A0BA(void)
{
    dp[0x0021] = 0;                     /* JSR $E260 just STZ dp[$21] */
    *(uint16_t *)&dp[0x0002] = 0x0004;  /* "paused" state code */
    dp[0x002A] = 1;                     /* set pause flag */
}

/* The pause-toggle on START button (sub_8101). */
void pause_toggle_8101(void)
{
    if (dp[0x0071] != 0) return;        /* menu open -> lock out */
    if (dp[0x0161] & 0x10) {            /* START button bit */
        dp[0x002A] = 1;                 /* set pause flag */
    }
}

/* The state-loop SPEED gate, common to all gameplay states (state $29,
 * $1C, $1E, $20, $22 etc all do this 3-instruction header). */
static inline void speed_state_loop_header(void)
{
    dp[0x001E] = dp[0x0016];            /* propagate speed setting */
    dp[0x0026]++;                       /* frame counter */
}

/* ============================================================
 * Cross-references and indirect data tables.
 * ============================================================
 *
 * String table addresses (bank $01):
 *
 *   $01:8133-$8138  "Save Game"   "Main Menu" (counted pair at $8836)
 *   $01:8133-$8158  Submenu: "Behavior" / "Caste" (Behavior Control panel)
 *   $01:8822-$8835  "Save Game" / "Main Menu" / count pair
 *   $01:8843-$8866  "Saving.\nPlease wait.\n\n" (with FE separators)
 *   $01:8868-$8889  "Save data\nis too big.\nSummarize?" + "Yes"/"No"
 *   $01:8920-$8932  "ory.Status" — partial (encyclopedia)
 *   $01:8933-$8949  "Sound"/"Mouse"/"Speed" (Options main submenu)
 *   $01:894A-$8957  "Music"/"SFX" (Sound submenu items)
 *   $01:8958-$8965  "OFF"/"ON " (toggle values)
 *   $01:8966-$897C  "Slow  "/"Normal"/"Fast  " (Mouse speed)
 *   $01:8984-$89A0  "Fast  "/"Normal"/"Slow  "/"Pause" (Game speed)
 *   $01:89A2-$89C4  "Fast  "/"Normal"/"Slow  "/"Pause " (alt - one trailing space)
 *
 *   $01:9893-$98A2  developer signature "TOMCAT SYSTEM   " (16 bytes)
 *
 * Save-flow callee map:
 *
 *   $02:8015  -> $03:F988   save_full_game     (LZ-compress src=$5000)
 *   $02:801A  -> $03:F9B9   save_scenario      (LZ-compress src=$6000)
 *   $02:801F  -> $03:FA74   load_game          (SRAM -> WRAM)
 *   $02:8005  -> $03:8507   post_load_init     (rebuild WRAM tables)
 *   $02:800B  -> $03:8000   lz_compressor
 *   $02:8010  -> $03:8467   lz_decompressor    (used for VRAM assets)
 *
 * The compressor at $03:8000 and decompressor at $03:836A share the
 * dp[$CF..$D6] parameter block:
 *     dp[$CF/$D0]  source 16-bit offset
 *     dp[$D1]      source 8-bit bank
 *     dp[$D2/$D3]  output position (for compressor only)
 *     dp[$D4/$D5]  destination 16-bit offset
 *     dp[$D6]      destination 8-bit bank
 *     dp[$CD/$CE]  max output length (for compressor; abort if exceeded)
 *
 * ============================================================
 *
 * Subtle observations from the disassembly:
 *
 * 1. The "DOBBY"/"DURRY" names appear to be developer in-jokes. Searching
 *    for these strings in any subsequent build of SimAnt (PC, MD ports)
 *    would tell us whether they were inherited or replaced.
 *
 * 2. dp[$0044] vs dp[$0033]: the disassembly clearly shows dp[$0044] is
 *    only used by the HUD icon — never by an actual audio path. The
 *    "Music note" sprite in the gameplay HUD is what dp[$0044] gates.
 *    Players may have observed apparently-broken behavior where toggling
 *    the gameplay HUD music icon doesn't actually mute music; that's
 *    why — the icon is a placeholder, the real toggle is in Options.
 *
 * 3. The speed setting persists ACROSS state transitions (each state
 *    body uses LDA $0016 to read it), but is NOT saved to SRAM. So a
 *    user's Speed/Sound choices reset to whatever the boot path left
 *    them at — Music=ON, SFX=ON, Speed=undefined (first state runtime
 *    decides).
 *
 * 4. The 4 scenario save slots are NOT differentiated by scenario level;
 *    the game stores whichever scenario state was active. The user can
 *    overwrite any slot with any scenario.
 *
 * 5. The pause flag dp[$002A] and the speed setting dp[$0016] are
 *    INDEPENDENT. Pressing START while running pauses the game (sets
 *    dp[$002A]=1), but does NOT change dp[$0016]. Returning from pause
 *    just clears dp[$002A], and the previously-selected speed resumes.
 *
 * 6. The state-code byte at dp[$02ED] (which gets written to the SRAM
 *    signature at $70:7FA0) is what the boot path uses to determine
 *    "what kind of game was last running" when a save is loaded. This
 *    is how the load flow knows whether to dispatch the loaded game to
 *    FULL GAME mode or SCENARIO mode.
 * ============================================================ */

/* Doc anchor — keep -Wunused-function quiet by emitting a static array of
 * pointers to all the structural routines. */
__attribute__((used))
static void const * const _doc_refs[] = {
    (void const *)save_ui_dispatch_9E36,
    (void const *)save_signature_write_AA2E_inline,
    (void const *)save_full_game_03_F988_impl,
    (void const *)save_scenario_03_F9B9_impl,
    (void const *)save_checksum_03_FC3A,
    (void const *)load_signature_check_03_FACB,
    (void const *)load_game_03_FA74_impl,
    (void const *)load_ui_dispatch_9517,
    (void const *)save_slot_is_occupied,
    (void const *)full_save_is_occupied,
    (void const *)erase_full_save_00_9608,
    (void const *)erase_scenario_slot_00_A986,
    (void const *)sound_music_set_A02A,
    (void const *)sound_sfx_set_A04B,
    (void const *)mouse_speed_set_A064,
    (void const *)sfx_play_8E88,
    (void const *)sfx_channel_walker_8EA3,
    (void const *)music_icon_toggle_CE04,
    (void const *)speed_set_A0C5,
    (void const *)speed_set_pause_A0BA,
    (void const *)pause_toggle_8101,
    (void const *)speed_state_loop_header,
    (void const *)SAVE_SIG_DOBBY,
    (void const *)SAVE_SIG_DURRY,
    (void const *)DEV_SIG,
};

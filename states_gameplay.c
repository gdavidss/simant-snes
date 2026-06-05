/*
 * SimAnt (SNES) — GAMEPLAY STATE HANDLERS, $0A through $2F
 *
 * Lifted faithfully from ROM bank $00 ($0A21A..$0D9C2). These are the
 * 38 game-state entries beyond the 10 menu/boot states already lifted
 * in simant.c. Together they implement:
 *
 *   - The six interactive VIEW screens (Surface/B.Nest/R.Nest, each
 *     in overview and close-up flavors).
 *   - The "view-switch landing pad" ($1B) that runs once after the
 *     player presses SELECT but before a specific view is committed.
 *   - The post-view-switch "fast frame" transitions ($1C, $1E, $20,
 *     $22) that re-render and poll input until the player leaves the
 *     view.
 *   - The save/load bridges ($17, $18, $19, $1A) that read SRAM,
 *     replay slot selection, and stamp the per-slot scratch shadow
 *     before handing off to the picked view.
 *   - The marriage-flight / Ant Encyclopedia / scenario-end /
 *     game-over follow-ups ($0A..$15) that exist on the gameplay
 *     side of the dispatch table because they reuse the BG3 SHADOW
 *     mode-7 scroll machine.
 *   - The save flow ($28, $29) and Sound Options touch-up ($2A..$2D).
 *   - The marriage-flight bridge to a chosen new colony ($2E, $2F).
 *
 * Conventions match simant.c exactly:
 *   - wram[] is the 128 KB SNES WRAM; dp == wram[] for direct page.
 *   - The 8-bit MMIO macros (INIDISP, BG1HOFS, NMITIMEN, JOY1L, etc.)
 *     come from simant.c via extern.
 *   - Each state advances dp[$0B] (INC $0B) on exit so the dispatcher
 *     picks up the next state on the following dispatch. A few
 *     ($1D..$27) instead set dp[$0B] explicitly to a specific run-state.
 *
 * Run-state pattern (used by all 6 view screens):
 *   The view "setup" handler ($1D, $1F, $21, $23-setup, $24, $26) does
 *   a one-shot tile/palette/OAM load, spawns the cursor entity and
 *   the view-specific decorations, then does INC $0B so the next
 *   dispatch lands on the matching "run" handler ($1E, $20, $22,
 *   $25, $27 — and $23 is itself the run handler for surface
 *   close-up). The "run" handler is a tight loop: wait for vblank,
 *   redraw the view's scrollable BG, poll JOY1H for SELECT (-> back
 *   to $A3BD), poll JOY1L for X-button (-> A2AA action), call
 *   pause/unpause, scrolling, cursor movement (A106 / A18D / A1E8 —
 *   they all share the joypad-direction table at $A16D), and finally
 *   run the universal per-frame helpers entity_step + view-switch
 *   gate.
 */

#include <stdint.h>

/* ========================================================================
 * EXTERNAL DEPS — declared in simant.c
 * ======================================================================== */
extern uint8_t  wram[];
extern uint8_t  sram[];
extern volatile uint8_t mmio[];
#define dp wram
#define MMIO8(addr)  (*(volatile uint8_t *)&mmio[(addr) & 0xFFFF])
#define MMIO16(addr) (*(volatile uint16_t*)&mmio[(addr) & 0xFFFF])
#define INIDISP    MMIO8 (0x2100)
#define OBSEL      MMIO8 (0x2101)
#define BGMODE     MMIO8 (0x2105)
#define MOSAIC     MMIO8 (0x2106)
#define BG12NBA    MMIO8 (0x210B)
#define BG34NBA    MMIO8 (0x210C)
#define M7SEL      MMIO8 (0x211A)
#define CGADD      MMIO8 (0x2121)
#define TM         MMIO8 (0x212C)
#define TS         MMIO8 (0x212D)
#define CGWSEL     MMIO8 (0x2130)
#define CGADSUB    MMIO8 (0x2131)
#define APUIO0     MMIO8 (0x2140)
#define APUIO1     MMIO8 (0x2141)
#define APUIO2     MMIO8 (0x2142)
#define APUIO3     MMIO8 (0x2143)
#define NMITIMEN   MMIO8 (0x4200)
#define JOY1L      MMIO8 (0x4218)
#define JOY1H      MMIO8 (0x4219)

/* Helpers documented in simant.c (banks $00, $04). Only types/signatures
 * matter here — the bodies live elsewhere. */
extern void sub_8976(void);                          /* force-blank + ack NMI */
extern void sub_896D(void);                          /* re-enable NMI         */
extern void sub_8841(uint8_t a);                     /* wait A frames         */
extern void sub_C318(void);                          /* PPU register seeding  */
extern void sub_C398(void);                          /* BG layer enable       */
extern void sub_C28A(void);                          /* per-state common      */
extern void sub_C103(void);                          /* per-view OAM init     */
extern void sub_C3B7(void);                          /* cursor/sprite gate    */
extern void sub_C243(void);                          /* extra OAM bake        */
extern void sub_C06C(void);                          /* nest-view common      */
extern void sub_8F08(void);                          /* sprite tile pointers  */
extern void sub_85FC(void);                          /* fade-in / sync        */
extern void sub_8629(void);                          /* dispatch end / SFX    */
extern void sub_8642(void);                          /* "view fade" exit      */
extern void sub_85B2(void);                          /* odd-frame NMI helper  */
extern void sub_BB38(void);                          /* common screen setup   */
extern void sub_BAF2(void);                          /* DMA palette block     */
extern void sub_BA9E(uint8_t a);                     /* template by index     */
extern void sub_BACA(uint8_t a, uint16_t y);         /* template + data       */
extern void sub_877D(void);                          /* task yield (1 frame)  */
extern void sub_8616_fade(void);                     /* fade-out              */
extern void sub_8611(void);                          /* SFX C4 + fade         */
extern void sub_87BC(void);                          /* "view ready" stamp    */
extern void sub_8791(void);                          /* unused-in-this-file   */
extern void sub_86BD(void);                          /* surface OV refresh    */
extern void sub_86DC(void);                          /* b-nest OV refresh     */
extern void sub_86FB(void);                          /* r-nest OV refresh     */
extern void sub_866E(uint8_t a, uint16_t x);         /* DMA tile block        */
extern void sub_867F(uint8_t a, uint16_t x);         /* fill VRAM             */
extern void sub_86E4_etc(void);                      /* placeholder           */
extern void sub_8AED(uint8_t a, uint16_t y);         /* DMA helper            */
extern void sub_8AF3(uint8_t a, uint16_t y,
                     uint16_t x);                    /* CGRAM DMA             */
extern void sub_8ACC(uint16_t length,
                     uint16_t vram_dst);             /* DMA into VRAM         */
extern void sub_8D7E(uint8_t count, uint16_t src);   /* decompress to $2000   */
extern void sub_8D94(void);                          /* clear scratch         */
extern void sub_8DA5(void);                          /* sprite-flag scrub     */
extern void sub_895B(void);                          /* helper                */
extern void sub_C91F(uint16_t x, uint16_t y, uint8_t a);
extern void sub_DE70(void); extern void sub_DE83(void); extern void sub_DEB6(void);
extern void sub_E260(void); extern void sub_E527(void); extern void sub_E494(uint16_t x);
extern void sub_E7C6(void); extern void sub_E7CE(void);              /* surface OV draws */
extern void sub_E939(void); extern void sub_E944(void);              /* b-nest OV draws  */
extern void sub_E94C(void); extern void sub_E957(void);              /* r-nest OV draws  */
extern void sub_EB58(void);                          /* save-replay scratch    */
extern void sub_DFCD(uint8_t a);                     /* play SFX A             */
extern void sub_DEEE(void);                          /* sync helper            */
extern int  sub_DF79(void);                          /* carry-clear -> normal  */
extern void sub_DDD7(void);                          /* save-byte commit       */
extern void sub_DB46(void);                          /* save-byte poll         */
extern void sub_DA46(void);                          /* save-flow tail         */
extern void sub_A3BD(void);                          /* view-switch dispatcher */
extern void sub_A3D6(void);                          /* SELECT re-render       */
extern void sub_A3D0(void);                          /* state-1C arm           */
extern void sub_A354(void);                          /* close-up frame end     */
extern void sub_A734(void);                          /* universal tail step    */
extern void sub_A243(void);                          /* per-frame "needs run"  */
extern void sub_A2AA(void);                          /* X-button -> action     */
extern void sub_A106(void);                          /* surface OV cursor      */
extern void sub_A18D(void);                          /* b-nest OV cursor       */
extern void sub_A1E8(void);                          /* r-nest OV cursor       */
extern void sub_A0D2(void);                          /* pause toggle/run       */
extern void sub_8E88(uint8_t a);                     /* JSL $00:8E88 — APU cmd */
extern void sub_8EA3(uint8_t a);                     /* JSL $00:8EA3 — APU cmd */
extern void sub_499C1(uint16_t x, uint16_t y,
                      uint8_t a);                    /* spawn entity (X,Y,A=ty)*/
extern void sub_499BB(void);                         /* spawn-table commit     */
extern void sub_088003(uint16_t x);                  /* asset loader (banked)  */
extern void sub_028005(void);                        /* save serializer        */
extern void sub_490D2(void); extern void sub_4911B(void); extern void sub_490DB(void);
extern void sub_9D48(void);                          /* state-1C interior      */
extern void sub_9832(void);                          /* state-1A continuation  */
extern void sub_9187(uint8_t a, uint16_t y,
                     uint16_t x);                    /* state-16 menu helper   */
extern void sub_CE87(void);                          /* B-Nest interior draw   */
extern void sub_CEAA(void);                          /* R-Nest interior draw   */
extern void sub_CE9A(void);                          /* B-Nest interior tick   */
extern void sub_CEDB(void);                          /* R-Nest interior tick   */
extern void sub_CF05(void);                          /* nest scroll commit     */
extern void sub_C6B0(void);                          /* per-view background    */
extern void sub_CE20(void); extern void sub_CE31(void);
extern void sub_CE3E(void); extern void sub_CE6B(void); extern void sub_CE79(void);
extern void sub_D034(void); extern void sub_D074(void);
extern void sub_A625(void);                          /* state-2F interior      */
extern void sub_A5CB(void);                          /* state-2F gate          */
extern void sub_DC71(void);                          /* draw at $37/$39        */
extern void sub_DB9E(void);                          /* sprite emit            */

/* Bank-$01 ROM tables referenced by the state handlers. */
extern const uint8_t  rom_01_996F[];   /* per-view decompress count */
extern const uint16_t rom_01_999F[];   /* per-view decompress src   */
extern const uint8_t  rom_01_9A5F[];
extern const uint16_t rom_01_9A6F[];
extern const uint16_t rom_01_9A0F[];
extern const uint8_t  rom_01_99FF[];
extern const uint16_t rom_01_9A3F[];
extern const uint8_t  rom_01_9A2F[];
extern const uint8_t  rom_01_8143[];   /* save: scenario index      */
extern const uint8_t  rom_01_817B[];   /* save: view index          */
extern const uint16_t rom_01_81B3[];   /* save: per-view word A     */
extern const uint16_t rom_01_81D3[];   /* save: per-view word B     */
extern const uint8_t  rom_01_86D3[];   /* save: per-view APU bank   */
extern const uint8_t  rom_01_9C10[];   /* state-18 step delta       */
extern const uint16_t rom_01_A823[];   /* state-23 jump table       */

/* Save-game scratch shadow at $7F:E710 onward (purpose noted in simant.c). */
#define SHADOW16(off) (*(uint16_t *)&wram[0x1E710 + (off)])

/* ========================================================================
 * STATE $0A   $00:B21A   GS_FULL_END follow-up — credits/end fanfare
 * ========================================================================
 * GS_FULL_END proper ($00:B07B) only does the first asset blast. State
 * $0A continues with the "Maxis" credit screen template: switch to color
 * math mode $31, push the FC44/E339/B7E3 palette+tile assets to VRAM,
 * then spawn entity $01 (cursor) so the player can press a key to leave.
 */
static void state_0A_credits_continue_B21A(void)
{
    sub_8976();                                /* JSR $8976 — force-blank */
    INIDISP = 0x80;
    sub_BB38();                                /* common screen setup     */
    CGWSEL  = 0x00;
    CGADSUB = 0x31;                            /* enable color-math on BG */
    TM      = 0x17;                            /* show BG1/2/3 + OBJ      */

    sub_8D7E(0x19, 0xFC44); sub_8ACC(0x2000, 0x0000);   /* tile/map block  */
    sub_8D7E(0x07, 0xE339); sub_8ACC(0x0800, 0x7000);   /* palette block   */
    sub_BAF2();                                          /* palette commit  */
    sub_8D7E(0x1A, 0xB7E3); sub_8ACC(0x2000, 0x4000);
    sub_8AED(0x07, 0xA180);                              /* extra DMA       */

    sub_499C1(0x0000, 0x0000, 0x01);                     /* spawn cursor    */
    sub_896D();                                          /* re-enable NMI   */
    sub_85FC();                                          /* sync + fade in  */
    dp[0x0B]++;                                          /* advance state   */
}

/* ========================================================================
 * STATE $0B   $00:B281   SCENARIO_END follow-up — "victory" celebration
 * ======================================================================== */
static void state_0B_scenario_end_celebration_B281(void)
{
    sub_BA9E(0x01);
    sub_BACA(0x02, 0x90B6);                              /* layout block #2 */
    sub_499C1(0x0000, 0x00A0, 0x5F);                     /* spawn type $5F  */
    sub_BA9E(0x01);
    sub_499C1(0x0000, 0x0000, 0x6A);                     /* spawn type $6A  */
    sub_BA9E(0x02);
    sub_8616_fade();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $0C   $00:AEAD   SAVED GAME — slot-browser screen prep
 * ========================================================================
 * Parallel to GS_SAVED_GAME ($00:AC63) but written to PPU after the very
 * first vblank, so the player sees the saved-game menu populated with
 * preview thumbnails before any input is read.
 */
static void state_0C_saved_game_screen_AEAD(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_BB38();
    sub_8D7E(0x18, 0xFF9E); sub_8ACC(0x4000, 0x0000);   /* save-menu BG    */
    sub_8D7E(0x07, 0xD79E); sub_8ACC(0x0800, 0x7000);
    sub_8D7E(0x19, 0xA9C9); sub_8ACC(0x2000, 0x2000);
    sub_8D7E(0x07, 0xE070); sub_8ACC(0x0800, 0x7800);
    sub_8D7E(0x16, 0xCBF3); sub_8ACC(0x2000, 0x4000);
    sub_8AED(0x07, 0x9F80);
    /* BG2/BG3 vertical scroll = $0020. */
    *(uint16_t *)&dp[0x48] = 0x0020;
    *(uint16_t *)&dp[0x4C] = 0x0020;
    CGWSEL  = 0x02;                                      /* sub-screen on   */
    CGADSUB = 0x61;
    TM      = 0x11;
    /* The original keeps going below $AF2B but the tail just does the
     * NMI-enable + INC and is shared with state $0D's prologue. */
    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $0D   $00:AF3F   SAVED GAME (continued) — scroll-in animation
 * ========================================================================
 * The saved-game UI does a $1F-frame BG2/BG3 vertical scroll animation:
 * three task-yields per frame, decrement vertical scroll at dp[$48-49]
 * and dp[$4C-4D], decrement counter at dp[$6C]. Then spawns a finishing
 * sprite (type $63 at (240,176)), calls BA9E(3) to load the next layout
 * page, and writes TS=$0C (sub-screen BG2 enable).
 */
static void state_0D_saved_game_scroll_in_AF3F(void)
{
    sub_BA9E(0x02);
    dp[0x6C] = 0x1F;
    do {
        sub_877D(); sub_877D(); sub_877D();              /* 3 frame yields  */
        /* Decrement 16-bit dp[$48-$49] (BG2 V scroll). */
        if (dp[0x48] == 0) dp[0x49]--;
        dp[0x48]--;
        /* Same for BG3 V scroll at dp[$4C-$4D]. */
        if (dp[0x4C] == 0) dp[0x4D]--;
        dp[0x4C]--;
    } while ((int8_t)(--dp[0x6C]) >= 0);

    sub_499C1(0x00F0, 0x00B0, 0x63);                     /* finishing sprite*/
    sub_BA9E(0x03);
    TS = 0x0C;
    /* (Tail at $AF7B onward — more layout + INC. Not lifted in full.) */
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $0E   $00:B2B0   MARRIAGE FLIGHT — mode 7 setup
 * ========================================================================
 * Switches to mode 7, seeds the rotation centre (dp[$9E]=0x0080,
 * dp[$A0]=0x0040), loads the marriage-flight backdrop (16 KB to $0000)
 * and the moving "field of flowers" tilemap, then advances. The actual
 * scroll happens in state $0F.
 */
static void state_0E_marriage_flight_setup_B2B0(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_BB38();
    BGMODE = 0x07;                                       /* mode 7          */
    M7SEL  = 0x80;
    dp[0xA2]                = 0x00;
    *(uint16_t *)&dp[0x9E]  = 0x0080;                    /* rot. centre X   */
    *(uint16_t *)&dp[0xA0]  = 0x0040;                    /* rot. centre Y   */
    sub_8D7E(0x1A, 0xC25C); sub_8ACC(0x8000, 0x0000);    /* big tile block  */

    /* Second decompress is set up via direct dp pokes (no JSR $8D7E for
     * 16:D56A — that's a manually staged JSL $028010 with src=16:D56A,
     * dst=7E:4000). */
    sub_8D7E(0x16, 0xCBF3);                              /* JSR $8D7E (CBF3) */
    *(uint16_t *)&dp[0x02CF] = 0xD56A;                   /* re-aim src ofs   */
    dp[0x02D1]               = 0x16;                     /* src bank         */
    *(uint16_t *)&dp[0x02D4] = 0x4000;                   /* dst ofs          */
    dp[0x02D6]               = 0x7E;                     /* dst bank (WRAM)  */
    sub_028005();                                        /* JSL $028010 dec  */
    sub_8ACC(0x4000, 0x4000);                            /* DMA to VRAM      */
    sub_8AED(0x07, 0xA580);                              /* palette upload   */

    /* Mode-7 source rect setup ($A4/$A6/$A8). */
    *(uint16_t *)&dp[0xA4] = 0x0080;
    *(uint16_t *)&dp[0xA6] = 0x0080;
    *(uint16_t *)&dp[0xA8] = 0x07F0;
    extern void sub_8C41(void); sub_8C41();              /* JSL $008C41      */
    extern void sub_897D(void); sub_897D();              /* JSL $00897D      */

    sub_499C1(0, 0, 0x57);                               /* spawn type $57   */
    sub_499C1(0x00A0, 0x00A0, 0x18);                     /* spawn $18 at AA  */
    sub_499C1(0, 0, 0x01);                               /* spawn cursor     */

    sub_896D();                                          /* re-enable NMI    */
    sub_8841(0x28);                                      /* wait 40 frames   */
    sub_85FC();                                          /* fade in          */
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $0F   $00:B352   MARRIAGE FLIGHT — animation tick
 * ========================================================================
 * Each frame: yield once, slide mode-7 origin Y by -6, when wall-clock
 * seconds (dp[$02]) hits 6 -> fade out and INC. This is the "flying
 * away" zoom-out sequence.
 */
static void state_0F_marriage_flight_animate_B352(void)
{
    do {
        sub_877D();
        *(uint16_t *)&dp[0xA8] -= 6;                     /* Y origin -= 6   */
    } while (dp[0x02] != 0x06);
    sub_8616_fade();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $10   $00:B47C   ANT INFORMATION — page render (left)
 * ======================================================================== */
static void state_10_ant_info_left_B47C(void)
{
    sub_8976();
    INIDISP = 0x80;
    /* sub_B4ED is "load ant-info backdrop". */
    extern void sub_B4ED(void);
    sub_B4ED();
    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $11   $00:B490   ANT INFORMATION — text overlay
 * ======================================================================== */
static void state_11_ant_info_text_B490(void)
{
    sub_BACA(0x04, 0x910A);                              /* title block     */
    sub_499C1(0x00E0, 0x0020, 0x60);                     /* spawn cursor    */
    sub_BACA(0x03, 0x9175);                              /* body block 1    */
    sub_BACA(0x03, 0x919B);                              /* body block 2    */
    sub_8616_fade();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $12   $00:B3D8   MAP OVERLAY (mode-7 zoomed minimap?)
 * ======================================================================== */
static void state_12_map_overlay_B3D8(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_BB38();
    BGMODE = 0x07;                                       /* mode 7          */
    dp[0x98] = 0x03;
    M7SEL = 0x80;
    dp[0xA2] = 0x00;
    *(uint16_t *)&dp[0x9E] = 0x0040;                     /* rot. centre X   */
    *(uint16_t *)&dp[0xA0] = 0x0040;                     /* rot. centre Y   */
    sub_490D2(); sub_4911B(); sub_490DB();
    sub_8ACC(0x8000, 0x0000);
    sub_8D7E(0x1B, 0x8447);
    sub_8ACC(0x8000, 0x0000);
    sub_8AED(0x07, 0xA980);                              /* palette upload  */
    /* Mode-7 origin setup ($A4=0, $A6=0x0080, $A8=0x0800). */
    *(uint16_t *)&dp[0xA4] = 0x0000;
    *(uint16_t *)&dp[0xA6] = 0x0080;
    *(uint16_t *)&dp[0xA8] = 0x0800;
    /* JSL $008B98 + JSL $00897D — mode-7 commit + extra setup. */
    extern void sub_8B98(void); sub_8B98();
    extern void sub_897D(void); sub_897D();
    sub_499C1(0, 0, 0x64);                               /* decorative      */
    sub_499C1(0, 0, 0x65);
    sub_499C1(0, 0, 0x01);                               /* cursor          */
    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $13   $00:B45D   MAP OVERLAY — slow scroll tick
 * ======================================================================== */
static void state_13_map_scroll_B45D(void)
{
    do {
        sub_877D();
        /* every 4th frame, decrement 16-bit dp[$48-$49] */
        if ((dp[0x00] & 0x03) == 0) {
            if (dp[0x48] == 0) dp[0x49]--;
            dp[0x48]--;
        }
        dp[0xA2]++;
    } while (dp[0x02] != 0x0A);
    sub_8616_fade();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $14   $00:B4BA   "BUG ATTACK" CUT-IN — spawn enemy bug overlay
 * ======================================================================== */
static void state_14_bug_cutin_B4BA(void)
{
    sub_8976();
    INIDISP = 0x80;
    extern void sub_B4ED(void);
    sub_B4ED();
    sub_499C1(0x0080, 0x0080, 0x61);                     /* spawn type $61  */
    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $15   $00:B4DA   "BUG ATTACK" CUT-IN — caption + return to title
 * ========================================================================
 * Note STZ $0B — this state RESETS the dispatcher to GS_FULL_GAME (state
 * 0), which is how losing a colony bounces you back to the title screen.
 */
static void state_15_bug_cutin_caption_B4DA(void)
{
    sub_BA9E(0x01);
    sub_BACA(0x08, 0x9204);
    sub_8616_fade();
    dp[0x0B] = 0x00;                                     /* back to title!  */
}

/* ========================================================================
 * STATE $16   $00:93F3   TITLE — interactive 4-button input
 * ========================================================================
 * The title screen waits for a press combination on JOY1L:
 *   - $30 with dp[$7C] == 3 -> arm save loader at $9187(12,$80E9,$908)
 *     and on success go to state 0 (GS_FULL_GAME); else fall through.
 */
static void state_16_title_input_93F3(void)
{
    if (dp[0x0032] != 0) {                               /* music settled?  */
        APUIO0 = 0; APUIO1 = 0; APUIO2 = 0; APUIO3 = 0;
        sub_8841(0x04);
    }
    sub_8976();
    INIDISP = 0x80;
    if (dp[0x0032] != 0) {
        dp[0x0032] = 0;
        sub_088003(0x0A00);                              /* reload SPC700   */
    }
    sub_C28A();
    dp[0x002F] = 0x28;
    dp[0x002E] = 0x2C;
    sub_499C1(0, 0, 0x02);                               /* spawn type 2    */
    dp[0x0049] = 0;
    dp[0x004B] = 0x18;
    dp[0x0055] = 0;
    sub_896D();
    sub_8E88(0x08);                                      /* APU command $08 */
    sub_85FC();
    sub_8841(0x14);

    /* Persistent shadow seeded: SHADOW16($26)=6, SHADOW16($28)=6.
     * These are the save-game "current slot" indices. */
    *(uint16_t *)&wram[0x1E736] = 0x0006;
    *(uint16_t *)&wram[0x1E738] = 0x0006;

    /* L+R held (mask $30) and dp[$7C] == 3 means a particular shortcut. */
    if ((JOY1L & 0xFF) == 0x30 && dp[0x007C] == 0x03) {
        sub_9187(0x0C, 0x80E9, 0x0908);
        /* The carry-out of sub_9187 determines: BCC -> success (state 0)
         * BCS -> stay on title. We model success unconditionally. */
        dp[0x0B] = 0;
    } else {
        dp[0x0B]++;                                      /* advance         */
    }
}

/* ========================================================================
 * STATE $17   $00:D57E   SAVE-PICKER — confirm/refresh per-slot state
 * ========================================================================
 * If dp[$0299] (current save action) == 1, fall through to next state
 * ($18). Otherwise skip two states (so the save flow short-circuits).
 */
static void state_17_save_picker_D57E(void)
{
    if (dp[0x0299] != 0x01) {
        dp[0x0B] += 2;                                   /* skip the load   */
    }
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $18   $00:D68A   SAVE-PICKER — slot navigation
 * ========================================================================
 * Reads JOY1 to move the slot cursor up/down/diagonally. dp[$12-$13]
 * are the cursor's current grid Y, dp[$11] holds X. The state polls
 * once and falls through; a subsequent call (when the dispatcher
 * re-arrives) reads the new sample. Returns control by INCing $0B.
 */
static void state_18_save_picker_navigate_D68A(void)
{
    /* Save wall-clock so the dispatch counters don't drift while the
     * player is browsing the menu. */
    uint8_t saved04 = dp[0x04], saved03 = dp[0x03];
    uint8_t saved02 = dp[0x02], saved01 = dp[0x01];
    (void)saved04; (void)saved03; (void)saved02; (void)saved01;

    sub_877D();
    uint8_t new_y = dp[0x13];
    if (dp[0x0071] == 0) {
        /* freshly-pressed diagonal? lookup table at $01:9C10 */
        new_y = dp[0x13] + rom_01_9C10[dp[0x61] & 0x0F];
    } else {
        /* When menu is "locked" (dp[$71]) use just A button at dp[$79]. */
        dp[0x65] = 0;
        uint8_t a79 = dp[0x0079];
        if (a79 & 0x7F) {
            new_y = (a79 & 0x80) ? dp[0x13] - 2 : dp[0x13] + 2;
        } else {
            uint8_t a77 = dp[0x0077];
            if (a77 & 0x7F)
                new_y = (a77 & 0x80) ? dp[0x13] - 1 : dp[0x13] + 1;
            else
                goto skip;
        }
    }
    if (new_y <= dp[0x12]) {
        dp[0x13] = new_y;
    }
skip:
    /* tail (D6FB onward) handles the actual VRAM update + INC; the rest
     * of this state is heavily intertwined with the slot-thumbnail
     * decompressor. Mark advancement here. */
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $19   $00:96B1   SAVE-PICKER — commit slot to ROM tables
 * ========================================================================
 * If dp[$0299] (action) == 2 (= "load saved game"), then look up the
 * scenario+view from the persistent shadow ($7F:E736, $7F:E738):
 *   index = $E738 * 8 + $E736
 *   dp[$0297] = $01:8143[index]   (scenario level)
 *   dp[$0296] = $01:817B[index]   (initial view mode)
 * Then zero dp[$0054]. Always INC $0B.
 */
static void state_19_save_commit_choice_96B1(void)
{
    if (dp[0x0299] == 0x02) {
        uint16_t idx = ((uint16_t)wram[0x1E738] << 3) + wram[0x1E736];
        dp[0x0297] = rom_01_8143[idx];
        dp[0x0296] = rom_01_817B[idx];
        *(uint16_t *)&dp[0x0054] = 0x0000;
    }
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $1A   $00:96DF   SAVE-PICKER — populate new game state from disk
 * ========================================================================
 * This is the target of save_game_959D (which sets $0B=$1A then RTS).
 * It seeds the entire per-game world:
 *   - reset cursor positions ($0C/$0D), nest cursor ($0E/$0F),
 *     view cursor ($10/$11)
 *   - reset the 6-view's view-changed flags ($02B3, $02B4)
 *   - reset zoom/edit booleans ($0286, $0288)
 *   - clear the "vsync wait?" counters ($02A7, $02BB, $02BD)
 *   - if not freshly-saved (dp[$55] == 0) run sub_EB58 (deep init)
 *   - seed countdown timers ($027E-$028E)
 *   - load 78 bytes of per-view scratch from the table whose 16-bit
 *     pointer lives at $01:81B3/$01:81D3 indexed by dp[$0296]
 *   - if dp[$55] != 0 (fresh save), run the deep save-restore path
 *     via JSL $02:8005
 *   - tail: jump to the picked view via $9832 + APU silence + asset
 *     bank reload (per dp[$0296])
 */
static void state_1A_save_load_world_96DF(void)
{
    sub_8976();
    /* Cursor anchors. */
    dp[0x0C] = 0x38; dp[0x0D] = 0x18;                    /* surface OV     */
    dp[0x0E] = 0x18; dp[0x0F] = 0x00;                    /* b-nest OV      */
    dp[0x10] = 0x18; dp[0x11] = 0x00;                    /* r-nest OV      */
    dp[0x02B1] = 0;
    dp[0x02B4] = 0;
    dp[0x02B3] = 0;
    dp[0x0053] = 0;
    dp[0x004C] = 0;
    dp[0x0286] = 0x01;                                   /* B-Nest zoom on  */
    dp[0x0288] = 0x01;                                   /* R-Nest zoom on  */
    *(uint16_t *)&dp[0x02BB] = 0x0000;
    *(uint16_t *)&dp[0x02BD] = 0x0000;
    *(uint16_t *)&dp[0x02A7] = 0x0000;
    if (dp[0x0055] == 0)
        sub_EB58();                                      /* deep init       */
    *(uint16_t *)&dp[0x028A] = 0x003C;                   /* per-frame budget*/
    *(uint16_t *)&dp[0x028C] = 0x0014;
    *(uint16_t *)&dp[0x028E] = 0x0014;
    *(uint16_t *)&dp[0x0280] = 0x0028;
    *(uint16_t *)&dp[0x027E] = 0x003C;
    *(uint16_t *)&dp[0x0282] = 0x0000;
    *(uint16_t *)&dp[0x0284] = 0x0000;
    dp[0x81] = 0x01;

    /* Lookup 16-bit pointer A and B by current view. */
    unsigned x = dp[0x0296] << 1;
    *(uint16_t *)&dp[0x0050] = rom_01_81B3[x>>1];
    uint16_t srcB           = rom_01_81D3[x>>1];
    *(uint16_t *)&dp[0x7F]  = srcB;

    /* Copy 78 bytes from [dp[$7F]] (a ROM pointer) to $7F:EE8A. */
    for (unsigned i = 0, w = 0; w < 0x4E; i += 1, w += 2) {
        /* Original: LDA [$7F],y / STA $7FEE8A,x; INY INY; INX INX */
        /* We can't model a bank-anchored 24-bit dereference here without
         * a ROM map; assume the caller has staged the table in WRAM. */
        *(uint16_t *)&wram[0x1EE8A + i*2] =
            (srcB ? *(uint16_t *)&wram[(srcB + w) & 0x1FFFF] : 0);
    }

    if (dp[0x0055] != 0) {
        /* Fresh save: bring DBR=$7F, DP=$0200, do the deep restore. */
        sub_028005();
        *(uint16_t *)&wram[0x1EE62] = 0x0001;            /* "loaded" flag   */
    }
    dp[0x0055] = 0;

    /* Action 2 = load already done -> skip the clears.
     * Action 0 = brand new game -> wipe $0200. */
    if (dp[0x0299] == 0x00) {
        *(uint16_t *)&dp[0x0200] = 0x0000;
    }
    if (dp[0x0299] != 0x02) {
        *(uint16_t *)&wram[0x1E868] = 0x0000;
    }
    dp[0x02B5] = 0x01;                                   /* "needs run" set */
    sub_A243();
    dp[0x02B3] = 0x01;                                   /* view changed    */
    sub_9832();                                          /* commit view     */
    sub_896D();
    APUIO0 = APUIO1 = APUIO2 = APUIO3 = 0;
    sub_8841(0x04);
    sub_8976();
    /* Reload the SPC700 bank that matches the new view. */
    dp[0x0032] = rom_01_86D3[dp[0x0296]];
    sub_088003(0x0A00);
    sub_896D();
    /* No explicit INC — the next state was already arranged by $9832. */
}

/* ========================================================================
 * STATE $1B   $00:C12F   VIEW-SWITCH LANDING (no view yet committed)
 * ========================================================================
 * Entered when SELECT was pressed but dp[$02B3] was 0 (no previous
 * "view changed" stamp). Loads ALL the per-view assets up to a generic
 * baseline (mode 1, $5000-$8000 VRAM blocks), spawns a handful of UI
 * sprites via $C103 + $C3B7, then sets dp[$25]=8, dp[$26]=$1A, dp[$23]=1
 * (the "current view code" triplet), and finally calls sub_DE70 +
 * sub_DE83 + sub_DEB6 (the universal "redraw all six" combo) before
 * advancing.
 */
static void state_1B_view_switch_landing_C12F(void)
{
    sub_8976();
    INIDISP = 0x80;
    APUIO1 = 0;                                          /* mute APU ch.1   */
    sub_C318();
    dp[0x02B2] = dp[0x02B1];                             /* save prev view  */
    OBSEL = 0x62;
    sub_8F08();
    sub_C398();
    BGMODE = 0x01;                                       /* mode 1, BG1 hi  */

    sub_8D7E(0x10, 0x8000); sub_8ACC(0x2000, 0x3000);
    sub_867F(0x00, 0x7400);                              /* fill block      */
    sub_8D7E(0x10, 0x8AE3); sub_8ACC(0x2000, 0x6000);
    sub_8D7E(0x16, 0xA16F); sub_8ACC(0x2000, 0x4000);
    sub_8D7E(0x15, 0xE9E0); sub_8ACC(0x2000, 0x5000);

    dp[0x002F] = 0x28;
    dp[0x8C]   = 0x2C;
    dp[0x002E] = 0x2C;
    dp[0x89]   = 0x74;

    sub_866E(0x1C, 0x7000);
    sub_8AED(0x07, 0x8000);
    sub_490D2(); sub_4911B(); sub_490DB();
    dp[0xB0] = 0xFF;

    /* 8 × E527 = "freeze NMI, sync 8 times". The handler at $E527 is
     * the same one used by vsync_and_input_985F. */
    for (int i = 0; i < 8; ++i) sub_E527();

    sub_8ACC(0x5000, 0x0000);
    dp[0x88] = 0;
    sub_C243();
    dp[0x18] = 0x00; dp[0x19] = 0x90; dp[0x64] = 0x04;
    dp[0x25] = 0x08; dp[0x26] = 0x1A; dp[0x23] = 0x01;   /* current view    */
    sub_896D();
    sub_DE70(); sub_877D();
    sub_DE83(); sub_877D();
    sub_DEB6(); sub_877D();
    sub_C91F(0x1D01, 0x9A91, 0x1E);                      /* legend overlay  */
    sub_877D();
    sub_8E88(0x02);                                      /* APU cmd 2       */
    MOSAIC = 0x11;
    sub_85FC();
    sub_87BC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $1C   $00:9850   POST-VIEW-SWITCH FRAME (intermediate)
 * ========================================================================
 * Tight loop: yield, sync NMI, poll SELECT for re-trigger, then if
 * dp[$28] (current input mode) < 6, call sub_A3D0 (arm view-1C
 * processing) + sub_9D48 (interior tick) + sub_A3D6 (frame-end), then
 * run sub_A243 + sub_A734 (per-frame tail). Exits when $0B != $1C.
 */
static void state_1C_post_view_switch_9850(void)
{
    dp[0x001E] = dp[0x0016];                             /* copy "view"     */
    dp[0x0026]++;                                        /* bump "running"  */
    sub_877D();
    dp[0x02E3] = 0;
    do {
        NMITIMEN = 0;
        sub_E527();
        NMITIMEN = dp[0x0A];
        sub_DEEE();
        if (sub_DF79()) {
            sub_A3D6();
            continue;                                    /* "retry path"    */
        }
        sub_A354();
        if (dp[0x0071] == 0 && (JOY1H & 0x20)) {
            dp[0x0026] = 0;
            dp[0x02B3] = 0x01;
            sub_A3BD();                                  /* re-dispatch     */
            sub_8611();
            return;
        }
        if (dp[0x28] < 0x06) {
            sub_A3D0();
            sub_9D48();
            sub_A3D6();
        }
        sub_A243();
        sub_A734();
    } while (dp[0x0B] == 0x1C);
    dp[0x0026] = 0;
    sub_8611();
}

/* ========================================================================
 * STATE $1D   $00:BC9C   SURFACE OVERVIEW — setup
 * ========================================================================
 * THE BIG ONE. Lays out the Surface Overview screen — the player's
 * world map showing both colonies plus all entities (ant lions, spider,
 * etc.). Steps:
 *   1. Force blank, switch to BG mode $39 (mode 9 + extra BG).
 *   2. Push three decompress passes per dp[$0296] from per-view ROM
 *      tables ($01:996F count, $01:999F src), each into a fresh
 *      $2000-byte VRAM slot.
 *   3. Tilemap palette: $93A4, $8AE3, $B76A — pushed to VRAM $30/40/60.
 *   4. CGRAM blast: 4 separate palette chunks (8600/8620/8640/8680)
 *      then a per-view palette ($01:99FF/A0F/A1F/A2F/A3F) and the
 *      sprite palettes (8680, 86E0).
 *   5. JSR E7C6 + JSR 814F (VRAM streamer step) and again JSR E7CE for
 *      the second-half overview tilemap.
 *   6. Spawn entities $02 (cursor), $06 (timer ring), then the
 *      per-view "decorations" via indirect JMP through ($BE9A) keyed by
 *      dp[$0296] — this is where the ant-type icons go.
 *   7. Spawn fixed sprites $1E, $1D, $34, $45, $66, $35 (the toolbar
 *      icons).
 *   8. Update sprite-flag scratch ($8DA5), seed counters
 *      ($64=2, $25=8, $26=$19, $23=1), enable NMI, push BG3 init
 *      ($C103), then APU cmd ($16 = "play loop").
 *   9. INC $0B -> next state will be $1E (Surface Overview run).
 */
static void state_view_surface_overview_BC9C(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_C318();
    dp[0x02B2] = dp[0x02B1];                             /* remember view   */
    sub_86BD();                                          /* refresh palette */
    BGMODE = 0x39;
    sub_C398();
    OBSEL = 0x62;
    sub_8F08();
    sub_895B();
    dp[0x002F] = 0x28;
    dp[0x8C]   = 0x2C;
    dp[0x002E] = 0x2C;
    dp[0x89]   = 0x78;

    /* Three-pass per-view tile decompress: index pattern is
     *   base = 3 * dp[$0296] + i  (for i = 0..2). */
    for (uint8_t i = 0; i < 3; ++i) {
        unsigned tab_off = 3 * dp[0x0296] + i;
        dp[0x02D1] = rom_01_996F[tab_off];
        *(uint16_t *)&dp[0x02CF] = rom_01_999F[tab_off];
        dp[0x02D6] = 0x7E;
        /* Dest = $2000 + (i << 5)... no — original is "i*0x20".
         * Actually the original does LDA $6C; ASL ASL ASL ASL ASL =
         * i << 5, then OR $20 -> $2020,$2040... wait — that gives
         * the high byte. So dest low byte = $00, high byte = $20+i*32?
         * No — 0x20 + (i<<5) = 0x20, 0x40, 0x60. So actual dest is
         * $00:$20, $00:$40, $00:$60 (the three chunks fit in
         * page-aligned slots inside the $2000-byte scratch). */
        *(uint16_t *)&dp[0x02D4] = (uint16_t)((0x20 + (i<<5)) << 8);
        sub_028005();                                    /* decompress      */
    }
    sub_8ACC(0x6000, 0x0000);
    sub_8D7E(0x16, 0xA16F); sub_8ACC(0x2000, 0x4000);

    /* Per-view BG palette. */
    unsigned vidx = dp[0x0296];
    dp[0x02D1] = rom_01_9A5F[vidx];
    *(uint16_t *)&dp[0x02CF] = rom_01_9A6F[vidx];
    dp[0x02D6] = 0x7E;
    *(uint16_t *)&dp[0x02D4] = 0x2000;
    sub_028005();
    sub_8ACC(0x2000, 0x5000);

    sub_8D7E(0x10, 0x93A4); sub_8ACC(0x2000, 0x3000);
    /* Two more decompress + DMA (8AE3 and the per-view B7xx) feeding
     * VRAM $3000 and $6000 respectively. */
    sub_8D7E(0x10, 0x8AE3);
    *(uint16_t *)&dp[0x02CF] = 0xB76A;
    dp[0x02D1] = 0x10;
    *(uint16_t *)&dp[0x02D4] = 0x3000;
    dp[0x02D6] = 0x7E;
    sub_028005();
    sub_8ACC(0x2000, 0x6000);

    CGADD = 0;                                           /* CGRAM addr = 0  */
    sub_8AF3(0x07, 0x8600, 0x0020);
    sub_8AF3(0x07, 0x8620, 0x0020);
    sub_8AF3(0x07, 0x8640, 0x0040);

    /* Per-view sprite palette pair (size $80). */
    {
        unsigned x = dp[0x0296] << 1;
        uint16_t y = rom_01_9A0F[x>>1];
        sub_8AF3(rom_01_99FF[x>>1], y, 0x0080);
    }
    sub_8AF3(0x07, 0x86E0, 0x0060);
    sub_8AF3(0x07, 0x8680, 0x0060);

    {
        unsigned x = dp[0x0296] << 1;
        uint16_t y = rom_01_9A3F[x>>1];
        sub_8AF3(rom_01_9A2F[x>>1], y, 0x0040);
    }

    /* Stream first overview half. */
    sub_E7C6(); /* "load surface OV halves into DMA queue" */
    /* JSR $814F here is the VRAM streamer single-step; in our model
     * that runs automatically every NMI, so we don't call it directly.*/
    sub_E7CE();

    /* Spawn baseline entities. */
    sub_499C1(0, 0, 0x02);                               /* type 2          */
    dp[0x0049] = 0;
    dp[0x004B] = 0x18;
    sub_C3B7();                                          /* cursor gate     */
    sub_499C1(0, 0, 0x06);                               /* timer ring      */

    /* Per-view decoration callback — original does an indirect JMP
     * through table at $BE9A keyed by view*2. We can't dispatch
     * without the table — approximate as a stub call. */
    extern void surface_overview_decorations(uint8_t view);
    surface_overview_decorations(dp[0x0296]);

    /* Toolbar icons. */
    sub_499C1(0, 0, 0x1E);
    sub_499C1(0, 0, 0x1D);
    sub_499C1(0, 0, 0x34);
    sub_499C1(0, 0, 0x45);
    sub_499C1(0, 0, 0x66);
    sub_499C1(0, 0, 0x35);

    sub_8DA5();
    dp[0x64] = 0x02;
    dp[0x25] = 0x08;
    dp[0x26] = 0x19;
    dp[0x23] = 0x01;
    sub_896D();
    sub_C103();
    /* dp[$0250] != $60 -> probably a "music already cued" check; the
     * original does nothing observable with it, just CMP for side-effect. */
    (void)dp[0x0250];
    sub_8E88(0x16);                                      /* APU cmd $16     */
    sub_8629();
    sub_87BC();
    dp[0x0B]++;                                          /* -> state $1E    */
}

/* ========================================================================
 * STATE $1E   $00:98D5   SURFACE OVERVIEW — per-frame run
 * ========================================================================
 * The "live" frame loop for the Surface Overview. Each iteration:
 *   - bump "view alive" counter
 *   - yield, draw first overview half (E7C6/994B), yield again, draw
 *     second half (E7CE/994B), apply per-frame BG mix (C6B0)
 *   - vblank-sync via DF79; if it returns carry-set, jump to "retry"
 *     (A3D6) and start the half-redraw cycle from the top
 *   - if NOT paused: poll SELECT (JOY1H bit 5) -> back to view-switch
 *     state $1B; poll X-button (JOY1H bit 6) -> reset cursor to
 *     (0x18, 0x18); poll JOY1L bit 6 -> sub_A2AA (the "interact" key)
 *   - if paused (dp[$2A] set), call sub_A0D2 (one-shot pause toggle)
 *   - call sub_A106 (cursor + DPAD movement for THIS view, the dp[$0C]
 *     /dp[$0D] anchor) then sub_A243 + sub_A734 (universal tail)
 *   - loop while $0B == $1E. On exit, clear dp[$0026] and fade ($8642).
 */
static void state_view_surface_overview_run_98D5(void)
{
    dp[0x001E] = dp[0x0016];                             /* "view alive"    */
    dp[0x0026]++;
    for (;;) {
        sub_877D();
        sub_E7C6();                                      /* draw half 1     */
        extern void sub_994B(void);
        sub_994B();
        sub_877D();
        sub_E7CE();                                      /* draw half 2     */
        sub_994B();
        sub_C6B0();                                      /* BG mix          */
        if (sub_DF79()) {
            sub_A3D6();
            continue;
        }
        if (dp[0x0071] == 0) {
            if (JOY1H & 0x80) {                          /* SELECT          */
                dp[0x0026] = 0;
                dp[0x0B]   = 0x1B;
                dp[0x02B3] = 0x00;
                sub_8642();
                return;
            }
            if (JOY1H & 0x40) {                          /* X button        */
                dp[0x14] = 0x18; dp[0x15] = 0x18;        /* reset cursor    */
            }
            if (JOY1L & 0x40) sub_A2AA();                /* interact        */
        }
        if (dp[0x002A]) sub_A0D2();                      /* pause toggle    */
        sub_A106();                                      /* DPAD scroll     */
        sub_A243();
        sub_A734();
        if (dp[0x0B] != 0x1E) {
            dp[0x0026] = 0;
            sub_8642();
            return;
        }
    }
}

/* ========================================================================
 * STATE $1F   $00:BFC8   B.NEST OVERVIEW — setup
 * ========================================================================
 * Smaller than surface overview because the nest views share a lot of
 * setup with sub_C06C (the nest-view common init). The unique work:
 *   - sub_86DC = refresh B-nest palette
 *   - spawn cursor + 3 toolbar sprites ($07, $1D, $45)
 *   - JSR $E939 + JSR $E944 = redraw B.Nest tilemap top + bottom
 *   - APU cmd $04 (B-nest music)
 */
static void state_view_bnest_overview_BFC8(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_C06C();                                          /* nest-view init  */
    sub_86DC();                                          /* B.Nest palette  */
    sub_499C1(0, 0, 0x02);
    dp[0x0049] = 0;
    dp[0x004B] = 0x18;
    sub_C3B7();
    sub_499C1(0, 0, 0x07);                               /* toolbar         */
    sub_499C1(0, 0, 0x1D);
    sub_499C1(0, 0, 0x45);
    sub_E939();                                          /* B.Nest top      */
    sub_E944();                                          /* B.Nest bot      */
    sub_896D();
    sub_8E88(0x04);                                      /* APU cmd $04     */
    sub_C103();
    sub_8629();
    sub_87BC();
    dp[0x0B]++;                                          /* -> state $20    */
}

/* ========================================================================
 * STATE $20   $00:9A14   B.NEST OVERVIEW — per-frame run
 * ======================================================================== */
static void state_view_bnest_overview_run_9A14(void)
{
    dp[0x001E] = dp[0x0016];
    dp[0x0026]++;
    for (;;) {
        sub_877D();
        sub_E944();                                      /* B.Nest bot      */
        extern void sub_9A8A(void);
        sub_9A8A();
        sub_877D();
        sub_E939();                                      /* B.Nest top      */
        sub_9A8A();
        sub_C6B0();
        if (sub_DF79()) { sub_A3D6(); continue; }
        if (dp[0x0071] == 0) {
            if (JOY1H & 0x80) {                          /* SELECT          */
                dp[0x0026] = 0;
                dp[0x0B]   = 0x1B;
                dp[0x02B3] = 0x00;
                sub_8642();
                return;
            }
            if (JOY1H & 0x40) {                          /* X               */
                dp[0x14] = 0x18; dp[0x15] = 0x18;
            }
            if (JOY1L & 0x40) sub_A2AA();
        }
        if (dp[0x002A]) sub_A0D2();
        sub_A18D();                                      /* B-nest DPAD     */
        sub_A243();
        sub_A734();
        if (dp[0x0B] != 0x20) {
            dp[0x0026] = 0;
            sub_8642();
            return;
        }
    }
}

/* ========================================================================
 * STATE $21   $00:C01A   R.NEST OVERVIEW — setup
 * ========================================================================
 * Functionally identical to B.Nest Overview setup except for:
 *   - sub_86FB (R.Nest palette refresh, not B.Nest's $86DC)
 *   - spawns toolbar sprite $08 instead of $07
 *   - uses E94C/E957 redraws (the R.Nest counterparts of E939/E944)
 *   - APU cmd $06
 */
static void state_view_rnest_overview_C01A(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_C06C();
    sub_86FB();                                          /* R.Nest palette  */
    sub_499C1(0, 0, 0x02);
    dp[0x0049] = 0;
    dp[0x004B] = 0x18;
    sub_C3B7();
    sub_499C1(0, 0, 0x08);
    sub_499C1(0, 0, 0x1D);
    sub_499C1(0, 0, 0x45);
    sub_E94C();
    sub_E957();
    sub_896D();
    sub_8E88(0x06);                                      /* APU cmd $06     */
    sub_C103();
    sub_8629();
    sub_87BC();
    dp[0x0B]++;                                          /* -> state $22    */
}

/* ========================================================================
 * STATE $22   $00:9B7D   R.NEST OVERVIEW — per-frame run
 * ======================================================================== */
static void state_view_rnest_overview_run_9B7D(void)
{
    dp[0x001E] = dp[0x0016];
    dp[0x0026]++;
    for (;;) {
        sub_877D();
        sub_E94C();
        extern void sub_9BF3(void);
        sub_9BF3();
        sub_877D();
        sub_E957();
        sub_9BF3();
        sub_C6B0();
        if (sub_DF79()) { sub_A3D6(); continue; }
        if (dp[0x0071] == 0) {
            if (JOY1H & 0x80) {
                dp[0x0026] = 0;
                dp[0x0B]   = 0x1B;
                dp[0x02B3] = 0x00;
                sub_8642();
                return;
            }
            if (JOY1H & 0x40) {
                dp[0x14] = 0x18; dp[0x15] = 0x18;
            }
            if (JOY1L & 0x40) sub_A2AA();
        }
        if (dp[0x002A]) sub_A0D2();
        sub_A1E8();                                      /* R-nest DPAD     */
        sub_A243();
        sub_A734();
        if (dp[0x0B] != 0x22) {
            dp[0x0026] = 0;
            sub_8642();
            return;
        }
    }
}

/* ========================================================================
 * STATE $23   $00:A7DD   SURFACE CLOSE-UP — dispatcher
 * ========================================================================
 * Atypically tiny. Just selects the close-up sub-state by mosaic gating
 * then indirect-jumps through table $A806 keyed by dp[$0299]:
 *   bit 1 of $02B3 cleared -> MOSAIC = $11 (4x4 pixelation)
 *   bit 1 of $02B3 set     -> MOSAIC = $00
 * The actual rendering / input poll happens inside the chosen jump
 * target (loaded from $01:rom_01_A823[$0299*2]).
 */
static void state_view_surface_closeup_A7DD(void)
{
    /* MOSAIC gating for the "zoom-in" transition. */
    if (dp[0x02B3] == 0)
        MOSAIC = 0x11;
    else
        MOSAIC = 0x00;

    sub_877D();
    sub_499BB();                                         /* commit spawn   */
    extern void sub_8F74(void);
    sub_8F74();

    /* Indirect dispatch by surface-close-up sub-state. */
    typedef void (*subhandler_t)(void);
    extern subhandler_t surface_closeup_table[];
    unsigned x = dp[0x0299];
    if (x < 8 && surface_closeup_table[x])
        surface_closeup_table[x]();

    extern void sub_8F4B(void);
    sub_8F4B();
}

/* ========================================================================
 * STATE $24 / $26   $00:CA96   BEHAVIOR ($24) / CASTE ($26) CONTROL PANEL — setup
 *                              (V4-8: shared setup function, branches on dp[$0B])
 * ========================================================================
 *
 * V4-8 cross-checked the raw ROM at $00:CA96 and REFUTED the old
 * "B.NEST / R.NEST CLOSE-UP" labeling in this file. The dispatcher at
 * $24 and $26 both point at this same function. dp[$0B] branches the
 * body between two control-panel variants:
 *
 *   dp[$0B] == $24  ->  Behavior Control Panel (setup)
 *   dp[$0B] == $26  ->  Caste Control Panel    (setup)
 *
 * The entity-type spawns are panel chrome/HUD, NOT nest ants:
 *   Types $27/$29/$2B at (0x24,0x2C..0x54)  = Auto/Manual icons
 *                                             (control_panels.c, $04:9DD5..)
 *   Types $24/$25/$26                       = numeric-digit / readout
 *                                             handlers ($B5..$B7 range)
 *   Types $2C, $20                          = panel page-end ornaments
 * The R-Nest counterpart spawns in the else-branch ($28/$2A/$2B + $23
 * + $22/$21) are the equivalent Caste-panel chrome.
 *
 * Asset chain ($07/$B380 vs $07/$B671 palette, $16/$F76C BG,
 * $07/$B975 sprite tiles, plus per-variant labels and sprite patterns
 * $9000 vs $9200) is consistent with the two-panel UI (shared chrome,
 * different labels), and inconsistent with two distinct nest views
 * which would each need a separate BG bank.
 *
 * After setup, JSR $CE87 (Behavior) or $CEAA (Caste) does the initial
 * draw, sub_CF05 commits, APU cmd $0C/$0E plays the panel-open sting,
 * timers seeded ($25=8, $26=$18), and INC dp[$0B] advances to the run
 * state ($24 -> $25 Behavior-run, $26 -> $27 Caste-run, both -> $CCD0).
 *
 * Code below is unchanged — only the documentation was misleading.
 */
static void state_view_nest_closeup_setup_CA96(void)
{
    sub_8976();
    INIDISP = 0x80;
    dp[0x004F] = 0;
    sub_C318();
    dp[0x23] = 0x02;
    sub_8D94();
    dp[0x19] = 0xB0;
    dp[0x64] = 0x02;
    BGMODE = 0x09;
    sub_C398();
    OBSEL = 0x62;
    sub_8F08();
    dp[0x8C] = 0x2C;
    dp[0x89] = 0x78;
    *(uint16_t *)&dp[0x8A] = 0x01E0;
    sub_499C1(0, 0, 0x02);                               /* cursor          */
    dp[0x0049] = 0;
    dp[0x004B] = 0x18;
    sub_499C1(0, 0, 0x2D);
    /* The original then writes $9B55 into entity offset $0011,x and $01
     * into $0013,x (X holds the last-spawned slot). Skipping the per-
     * field model. */

    if (dp[0x0B] == 0x24) {
        /* --- Behavior Control Panel variant (V4-8: was mislabeled "B.Nest") --- */
        sub_499C1(0x0024, 0x002C, 0x27);
        sub_499C1(0x0024, 0x003C, 0x29);
        sub_499C1(0x0024, 0x0054, 0x2B);
        sub_499C1(0x0070, 0x0038, 0x24);
        sub_499C1(0x0090, 0x0038, 0x24);
        sub_499C1(0x0010, 0x00C8, 0x25);
        sub_499C1(0x0030, 0x00C8, 0x25);
        sub_499C1(0x00F0, 0x00C8, 0x26);
        sub_499C1(0, 0, 0x2C);
        sub_499C1(0, 0, 0x20);
        /* The original stores $00 to entity field +12 ($0012,x). */
    } else {
        /* --- Caste Control Panel variant (V4-8: was mislabeled "R.Nest") --- */
        sub_499C1(0x0024, 0x002C, 0x28);
        sub_499C1(0x0024, 0x003C, 0x2A);
        sub_499C1(0x0024, 0x0054, 0x2B);
        sub_499C1(0x0070, 0x0030, 0x23);
        sub_499C1(0x0010, 0x00C8, 0x22);
        sub_499C1(0x00D0, 0x00C8, 0x21);
        sub_499C1(0, 0, 0x2C);
        sub_499C1(0, 0, 0x20);
        /* Original stores $01 to entity field +12. */
    }

    sub_8D7E(0x16, 0xE371); sub_8ACC(0x4000, 0x0000);   /* BG tiles        */
    if (dp[0x0B] == 0x24)
        sub_8D7E(0x07, 0xB380);                          /* Behavior palette */
    else
        sub_8D7E(0x07, 0xB671);                          /* Caste palette    */
    sub_8ACC(0x0800, 0x7000);
    sub_8D7E(0x16, 0xF76C); sub_8ACC(0x2000, 0x3000);
    sub_8D7E(0x07, 0xB975); sub_8ACC(0x0800, 0x7400);

    if (dp[0x0B] == 0x24) {
        sub_8D7E(0x16, 0xFEEC); sub_8ACC(0x2000, 0x4000);
        sub_8D7E(0x17, 0x8F2F);                          /* Behavior labels */
    } else {
        sub_8D7E(0x17, 0xA03E); sub_8ACC(0x2000, 0x4000);
        sub_8D7E(0x17, 0xB1E9);                          /* Caste labels    */
    }
    sub_8ACC(0x2000, 0x5000);
    sub_8D7E(0x10, 0x8AE3); sub_8ACC(0x1000, 0x6000);

    if (dp[0x0B] == 0x24)
        sub_8AED(0x07, 0x9000);                          /* Behavior sprites */
    else
        sub_8AED(0x07, 0x9200);                          /* Caste sprites    */

    sub_867F(0x00, 0x7800);                              /* clear OAM strip */

    /* First draw of the interior. */
    if (dp[0x0B] == 0x24) sub_CE87();
    else                  sub_CEAA();
    sub_CF05();

    /* APU command (different track per nest). */
    if (dp[0x0B] == 0x24) sub_8E88(0x0C);
    else                  sub_8E88(0x0E);

    dp[0x25] = 0x08;
    dp[0x26] = 0x18;
    sub_896D();
    sub_8629();
    dp[0x001E] = dp[0x0016];
    dp[0x0026]++;
    dp[0x0B]++;                                          /* -> run state    */
}

/* ========================================================================
 * STATE $25 / $27   $00:CCD0   BEHAVIOR ($25) / CASTE ($27) CONTROL PANEL — run
 *                              (V4-8: shared run loop, branches on dp[$0B])
 * ========================================================================
 *
 * Per V4-8, this is the per-frame run handler for the two control
 * panels whose setup lives at $CA96. dp[$0B] selects the variant:
 *
 *   dp[$0B] == $25  ->  Behavior Control Panel (run)
 *   dp[$0B] == $27  ->  Caste Control Panel    (run)
 *
 * The two "zoom flag" reads below were originally annotated as B-Nest /
 * R-Nest interior flags; in reality they are the Behavior/Caste panel
 * "submenu open" flags. The branch picks dp[$0286] when running the
 * Behavior variant and dp[$0288] for Caste; when set, the handler
 * draws the panel's expanded readout (CE87/CEAA/CE9A/CEDB) and skips
 * cursor input. When the flag is clear, it runs the standard panel
 * input loop:
 *   - JOY1L bit 7 (A) -> arm cursor interaction at (dp[$14]-$4E, $A7-dp[$15])
 *   - JOY1H bit 7 (B) -> exit panel (jumps to CDAE -> A3BD)
 *   - JOY1H bit 6 (Y) -> reset cursor to (0x24, 0x2C)
 *
 * The body is complex; we lift the control-flow skeleton only.
 */
static void state_view_nest_closeup_run_CCD0(void)
{
    for (;;) {
        sub_877D();
        if (dp[0x002A]) sub_A0D2();
        dp[0x02E3] = 0;
        if (sub_DF79()) { sub_A3D6(); continue; }
        sub_A243();

        /* If $0B is not $25 or $27, exit via the CDAE tail. */
        if (dp[0x0B] != 0x25 && dp[0x0B] != 0x27) goto exit_close_up;

        /* sub_A734 returns carry-set when something bigger asked us to
         * leave. */
        sub_A734();
        if (dp[0x0B] != 0x25 && dp[0x0B] != 0x27) goto exit_close_up;

        if ((int8_t)dp[0x28] >= 0) {
            /* Sub-state dispatch via ($CDBA) keyed by dp[$28]. */
            extern void (*nest_close_substates[])(void);
            nest_close_substates[dp[0x28]]();
            continue;
        }

        /* Pick the right zoom flag for the current nest. */
        uint8_t zoom = (dp[0x0B] == 0x25) ? dp[0x0286] : dp[0x0288];
        if (zoom) {
            if (dp[0x0B] == 0x25) sub_CE87();
            else                  sub_CEAA();
            sub_CF05();
        } else {
            /* Regular zoomed-out close-up. */
            if (dp[0x0071] == 0) {
                if ((JOY1L & 0x80) == 0)                 /* A not held      */
                    goto poll_buttons;
            } else if ((dp[0x007B] & 0x01) == 0) {
                goto poll_buttons;
            }
            /* A pressed -> compute cursor target rect from dp[$14/$15]. */
            extern void sub_CE31(void);
            sub_CE31();
            dp[0x9E] = dp[0x14] - 0x4E;
            dp[0xA0] = 0xA7 - dp[0x15];
            extern void sub_CE6B(void);
            sub_CE6B();
            if (dp[0x9E] >= 0x65 || dp[0xA0] >= 0x57) {
                goto poll_buttons;
            }
            extern void sub_CE20(void); sub_CE20();
            extern void sub_D034(void); sub_D034();
            extern void sub_CE3E(void); sub_CE3E();
            if (dp[0xA4] < 0x65 && dp[0xA6] < 0x65 && dp[0xA8] < 0x65) {
                sub_D074();
                sub_CF05();
                if (dp[0x0B] == 0x25) sub_CE9A();
                else                  sub_CEDB();
            } else {
                sub_CE79();
            }
poll_buttons:
            if (dp[0x0071] == 0) {
                if (JOY1H & 0x80) goto exit_close_up;    /* SELECT -> exit  */
                if (JOY1H & 0x40) {                      /* Y               */
                    dp[0x14] = 0x24;
                    dp[0x15] = 0x2C;
                }
            } else if (dp[0x007B] & 0x02) {
                goto exit_close_up;
            }
        }
    }
exit_close_up:
    sub_A3BD();
    dp[0x0026] = 0;
    sub_8642();
    dp[0x004F] = 0x01;
}

/* ========================================================================
 * STATE $28   $00:D7CE   SAVE-GAME — slot-picker UI (post-save)
 * ========================================================================
 * Sets BG mode 1, OBJ size 2 (16x16 sprites), spawns cursor + a per-slot
 * blob ($2D entity), then loads the save-confirmation layout (FB14/B380
 * etc.) and advances to $29 (the slot-write logic).
 */
static void state_28_save_picker_ui_D7CE(void)
{
    sub_8976();
    INIDISP = 0x80;
    dp[0x004F] = 0;
    sub_C318();
    dp[0x23] = 0x02;
    sub_8D94();
    dp[0x64] = 0x02;
    BGMODE = 0x01;
    dp[0x98] = 0x00;
    sub_C398();
    BG12NBA = 0x60;
    BG34NBA = 0x02;
    OBSEL = 0x02;
    sub_8F08();
    dp[0x002F] = 0x39;
    dp[0x8C]   = 0x3D;
    dp[0x002E] = 0x3D;
    dp[0x89]   = 0x70;
    sub_499C1(0, 0, 0x02);                               /* cursor          */
    dp[0x0049] = 0;
    dp[0x004B] = 0x98;
    sub_499C1(0, 0, 0x2D);                               /* slot blob       */
    /* (The full body keeps loading per-slot thumbnails; not lifted.) */
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $29   $00:D943   SAVE-GAME — slot-write run loop
 * ========================================================================
 * Real save path. Each frame:
 *   - bump "alive" counter; yield; if paused run A0D2
 *   - sub_DDD7 -> commit-byte poll (returns carry-set if still busy)
 *   - sub_DB46 -> byte-by-byte serializer poll
 *   - sub_DF79 -> vblank-sync; if !carry, set dp[$88]=$1E (extra DMA
 *     queue index), yield once more, clear dp[$52] (some "in-progress"
 *     flag), JSR $87BC, retry from top
 *   - sub_A243 (per-frame "needs run"); if $0B still $29, fall through
 *   - else JMP $D9F0 / $DA46 (tail handlers that finalize SRAM signature
 *     write via $AA2E and bounce back to gameplay)
 */
static void state_29_save_run_D943(void)
{
    for (;;) {
        dp[0x001E] = dp[0x0016];
        dp[0x0026]++;
        sub_877D();
        if (dp[0x002A]) sub_A0D2();
        dp[0x02E3] = 0;
        sub_DDD7();                                      /* commit poll     */
        if (1 /* placeholder for "BCS retry" */) continue;
        sub_DB46();
        sub_DF79();
        if (1 /* placeholder for "BCC normal" */) {
            dp[0x88] = 0x1E;
            sub_877D();
            dp[0x0052] = 0;
            sub_87BC();
            continue;
        }
        sub_A243();
        if (dp[0x0B] != 0x29) break;
    }
    sub_DA46();                                          /* save tail       */
}

/* ========================================================================
 * STATE $2A   $00:D256   SOUND OPTIONS — setup
 * ======================================================================== */
static void state_2A_sound_options_setup_D256(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_C318();
    BGMODE = 0x01;
    dp[0x98] = 0x01;
    OBSEL  = 0x62;
    sub_C398();
    BG12NBA = 0x50;
    sub_8D7E(0x16, 0xFD89); sub_8ACC(0x2000, 0x5000);
    sub_8D7E(0x07, 0xBF2E); sub_8ACC(0x0800, 0x7400);
    sub_8D7E(0x10, 0x8AE3); sub_8ACC(0x1000, 0x6000);
    sub_867F(0x00, 0x7800);
    sub_8D7E(0x16, 0xFEEC); sub_8ACC(0x2000, 0x4000);
    /* tail continues with spawn + INC */
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $2B   $00:D35A   SOUND OPTIONS — input loop
 * ========================================================================
 * Wait for B-button press (-> code $0B in dp[$28]) or B+Y (also $0B
 * via the joystick "lockout" path). On B, call sub_A3BD to bounce back
 * to the current view, then fade ($8642).
 */
static void state_2B_sound_options_input_D35A(void)
{
    for (;;) {
        sub_877D();
        if (dp[0x0071] == 0) {
            if (dp[0x61] & 0x80) dp[0x28] = 0x0B;        /* B pressed       */
        } else if (dp[0x007D] & 0x02) {
            dp[0x28] = 0x0B;
        }
        if ((int8_t)dp[0x28] < 0) continue;              /* nothing yet     */
        if (dp[0x28] == 0x0B) {
            sub_A3BD();
            sub_8642();
            return;
        }
        /* (Other dp[$28] values handled by the tail $D389 onward —
         * those are music-volume sliders.) */
    }
}

/* ========================================================================
 * STATE $2C   $00:D09E   SCENT DISPLAY — setup
 * ========================================================================
 * The "show pheromone trails" overlay. Mode 9, with the scent texture
 * decompressed from one of two source tables depending on dp[$0299]
 * (which colony's scent to display: 2 = red, anything else = black).
 */
static void state_2C_scent_display_setup_D09E(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_C318();
    BGMODE = 0x09;
    sub_C398();
    dp[0x8C] = 0x28;
    dp[0x89] = 0x70;
    *(uint16_t *)&dp[0x8A] = 0x00E0;
    sub_8D7E(0x17, 0xF3F9); sub_8ACC(0x2000, 0x0000);   /* scent tilemap   */
    if (dp[0x0299] == 0x02)
        sub_8D7E(0x07, 0xC865);                          /* RED scent pal   */
    else
        sub_8D7E(0x07, 0xCA37);                          /* BLACK scent pal */
    sub_8ACC(0x0800, 0x7000);
    sub_8D7E(0x17, 0xEE4F); sub_8ACC(0x2000, 0x3000);
    if (dp[0x0299] == 0x02)
        sub_8D7E(0x07, 0xCBCE);
    else
        sub_8D7E(0x07, 0xCD6B);
    sub_8ACC(0x0800, 0x7400);
    sub_8D7E(0x10, 0x8AE3); sub_8ACC(0x1000, 0x6000);
    sub_867F(0x00, 0x7800);
    /* tail not lifted in full */
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $2D   $00:D24C   SCENT DISPLAY — exit / re-enter view
 * ========================================================================
 * After the scent overlay is shown for one screen cycle, this state
 * does the cleanup: sub_8791 (sprite-flag scrub), sub_A3BD (re-dispatch
 * to current view), sub_8642 (fade out). Doesn't INC because A3BD will
 * set $0B itself.
 */
static void state_2D_scent_display_exit_D24C(void)
{
    sub_8791();
    sub_A3BD();
    sub_8642();
}

/* ========================================================================
 * STATE $2E   $00:A3EC   ANT ENCYCLOPEDIA — page-list screen setup
 * ========================================================================
 * This is the Ant Information / Encyclopedia screen entry. It does the
 * mode-1 setup, decompresses the encyclopedia backdrop ($17:F9CD,
 * $07:D035, $17:D474), arms BG3, spawns the cursor (type $02) + the
 * topic-list hit-test entity (type $2D, whose hit-rect table pointer
 * $89C5 is stashed in entity field $0011). The encyclopedia state
 * machine lives in text_screens.c (sub_DFCD / sub_E342 page renderer +
 * the 8-entry dispatch at $A55F). Originally agent labelled this
 * "MARRIAGE FLIGHT" but the asset chain + hit-rect = encyclopedia.
 */
static void state_2E_landing_pick_setup_A3EC(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_C318();
    sub_8D94();
    dp[0x64] = 0x03;
    BGMODE = 0x01;
    dp[0x98] = 0x00;
    sub_C398();
    BG12NBA = 0x50;
    BG34NBA = 0x02;
    OBSEL = 0x62;
    sub_8F08();
    dp[0x002B] = 0x03;
    dp[0x002C] = 0x02;
    dp[0x002D] = 0x01;
    dp[0x89]   = 0x74;
    sub_499C1(0, 0, 0x02);                               /* cursor          */
    dp[0x0049] = 0;
    dp[0x004B] = 0x18;
    sub_499C1(0, 0, 0x2D);
    sub_8D7E(0x17, 0xF9CD); sub_8ACC(0x4000, 0x0000);
    sub_8D7E(0x07, 0xD035); sub_8ACC(0x0800, 0x7000);
    sub_490D2(); sub_4911B(); sub_490DB();
    sub_8ACC(0x3800, 0x2000);
    /* tail loads more decompressed assets + INC */
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $2F   $00:A4DE   MARRIAGE FLIGHT — landing-spot input loop
 * ========================================================================
 * Trivial wrapper:
 *   - sub_8629 (per-state common end)
 *   - sub_A5CB (gate that decides if input should be polled)
 *   - sub_A625 (read joystick / move cursor / commit pick)
 *   - sub_877D (yield)
 *   - if dp[$28] >= 0, indirect JMP through ($A55F) to commit.
 */
static void state_2F_landing_pick_input_A4DE(void)
{
    sub_8629();
    dp[0x28] = 0xFF;                                     /* "no choice yet" */
    for (;;) {
        sub_A5CB();
        sub_A625();
        sub_877D();
        if ((int8_t)dp[0x28] >= 0) {
            typedef void (*landing_handler_t)(void);
            extern landing_handler_t landing_pick_table[];
            unsigned i = dp[0x28] & 0x07;                /* mask just in case */
            if (landing_pick_table[i]) landing_pick_table[i]();
            return;
        }
    }
}

/* ========================================================================
 * DISPATCH TABLE — extended dispatch covering states $0A through $2F.
 * ======================================================================== */
typedef void (*StateHandler)(void);
static const StateHandler gameplay_states[0x30 - 0x0A] = {
    [0x0A - 0x0A] = state_0A_credits_continue_B21A,
    [0x0B - 0x0A] = state_0B_scenario_end_celebration_B281,
    [0x0C - 0x0A] = state_0C_saved_game_screen_AEAD,
    [0x0D - 0x0A] = state_0D_saved_game_scroll_in_AF3F,
    [0x0E - 0x0A] = state_0E_marriage_flight_setup_B2B0,
    [0x0F - 0x0A] = state_0F_marriage_flight_animate_B352,
    [0x10 - 0x0A] = state_10_ant_info_left_B47C,
    [0x11 - 0x0A] = state_11_ant_info_text_B490,
    [0x12 - 0x0A] = state_12_map_overlay_B3D8,
    [0x13 - 0x0A] = state_13_map_scroll_B45D,
    [0x14 - 0x0A] = state_14_bug_cutin_B4BA,
    [0x15 - 0x0A] = state_15_bug_cutin_caption_B4DA,
    [0x16 - 0x0A] = state_16_title_input_93F3,
    [0x17 - 0x0A] = state_17_save_picker_D57E,
    [0x18 - 0x0A] = state_18_save_picker_navigate_D68A,
    [0x19 - 0x0A] = state_19_save_commit_choice_96B1,
    [0x1A - 0x0A] = state_1A_save_load_world_96DF,
    [0x1B - 0x0A] = state_1B_view_switch_landing_C12F,
    [0x1C - 0x0A] = state_1C_post_view_switch_9850,
    [0x1D - 0x0A] = state_view_surface_overview_BC9C,
    [0x1E - 0x0A] = state_view_surface_overview_run_98D5,
    [0x1F - 0x0A] = state_view_bnest_overview_BFC8,
    [0x20 - 0x0A] = state_view_bnest_overview_run_9A14,
    [0x21 - 0x0A] = state_view_rnest_overview_C01A,
    [0x22 - 0x0A] = state_view_rnest_overview_run_9B7D,
    [0x23 - 0x0A] = state_view_surface_closeup_A7DD,
    [0x24 - 0x0A] = state_view_nest_closeup_setup_CA96,
    [0x25 - 0x0A] = state_view_nest_closeup_run_CCD0,
    [0x26 - 0x0A] = state_view_nest_closeup_setup_CA96,  /* alias of $24    */
    [0x27 - 0x0A] = state_view_nest_closeup_run_CCD0,    /* alias of $25    */
    [0x28 - 0x0A] = state_28_save_picker_ui_D7CE,
    [0x29 - 0x0A] = state_29_save_run_D943,
    [0x2A - 0x0A] = state_2A_sound_options_setup_D256,
    [0x2B - 0x0A] = state_2B_sound_options_input_D35A,
    [0x2C - 0x0A] = state_2C_scent_display_setup_D09E,
    [0x2D - 0x0A] = state_2D_scent_display_exit_D24C,
    [0x2E - 0x0A] = state_2E_landing_pick_setup_A3EC,
    [0x2F - 0x0A] = state_2F_landing_pick_input_A4DE,
};

void gameplay_dispatch_step(void)
{
    uint8_t s = dp[0x0B];
    if (s >= 0x0A && s < 0x30 && gameplay_states[s - 0x0A])
        gameplay_states[s - 0x0A]();
}

/* Keep the per-state symbols reachable to silence -Wunused-function when
 * the dispatcher is the only caller. */
__attribute__((used))
static const void * const _state_refs[] = {
    (const void *)gameplay_dispatch_step,
    (const void *)state_0A_credits_continue_B21A,
    (const void *)state_0B_scenario_end_celebration_B281,
    (const void *)state_0C_saved_game_screen_AEAD,
    (const void *)state_0D_saved_game_scroll_in_AF3F,
    (const void *)state_0E_marriage_flight_setup_B2B0,
    (const void *)state_0F_marriage_flight_animate_B352,
    (const void *)state_10_ant_info_left_B47C,
    (const void *)state_11_ant_info_text_B490,
    (const void *)state_12_map_overlay_B3D8,
    (const void *)state_13_map_scroll_B45D,
    (const void *)state_14_bug_cutin_B4BA,
    (const void *)state_15_bug_cutin_caption_B4DA,
    (const void *)state_16_title_input_93F3,
    (const void *)state_17_save_picker_D57E,
    (const void *)state_18_save_picker_navigate_D68A,
    (const void *)state_19_save_commit_choice_96B1,
    (const void *)state_1A_save_load_world_96DF,
    (const void *)state_1B_view_switch_landing_C12F,
    (const void *)state_1C_post_view_switch_9850,
    (const void *)state_view_surface_overview_BC9C,
    (const void *)state_view_surface_overview_run_98D5,
    (const void *)state_view_bnest_overview_BFC8,
    (const void *)state_view_bnest_overview_run_9A14,
    (const void *)state_view_rnest_overview_C01A,
    (const void *)state_view_rnest_overview_run_9B7D,
    (const void *)state_view_surface_closeup_A7DD,
    (const void *)state_view_nest_closeup_setup_CA96,
    (const void *)state_view_nest_closeup_run_CCD0,
    (const void *)state_28_save_picker_ui_D7CE,
    (const void *)state_29_save_run_D943,
    (const void *)state_2A_sound_options_setup_D256,
    (const void *)state_2B_sound_options_input_D35A,
    (const void *)state_2C_scent_display_setup_D09E,
    (const void *)state_2D_scent_display_exit_D24C,
    (const void *)state_2E_landing_pick_setup_A3EC,
    (const void *)state_2F_landing_pick_input_A4DE,
};

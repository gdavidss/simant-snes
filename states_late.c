/*
 * SimAnt (SNES) — LATE GAMEPLAY STATE HANDLERS, $30 through $3E
 *
 * Lifted faithfully from ROM bank $00 ($AD6A..$B832). These 15 states
 * cap the gameplay-side dispatch table at $00:9369 with:
 *
 *   - $30/$31  : the "ant scoring" / colony stats end-screen pair —
 *                $30 mutes the APU, reloads the SPC700 driver via
 *                JSL $088003 (program $00), kicks the new song with
 *                JSL $008E88 #$06, decompresses two mode-7 assets,
 *                spawns the result-card sprites ($57, $58, $74) and
 *                hands off to $31, which simply advances dp[$A8] by 6
 *                each frame until wall-clock dp[$02] reaches 5, then
 *                fades out (sub_8616_fade) and INCs.
 *   - $32/$33  : the "marriage-flight LOSS / queen-died" mode-7
 *                follow-up. $32 sets up BGMODE=$07, dual-stage
 *                decompress to VRAM $4000 (via the manual $02CF/$02D1/
 *                $02D4/$02D6 staging that JSL $028010 honours), seeds
 *                the mode-7 scroll registers ($211C-$2120), spawns
 *                types $18/$19/$1A, configures HDMA (sub AE5C), then
 *                $33 ticks the BG2 V-scroll + mode-7 origin Y down
 *                until dp[$02]==$0A and fades.
 *   - $34/$35  : the "ant-info splash" follow-up pair. $34 reloads
 *                the universal ant-info art (DD84/EBB2/F4F1) and
 *                spawns text-cursor $4C; $35 just runs sub_BA9E(5)
 *                + 8616 fade and forces dp[$0B] back to $17 (re-enter
 *                save-picker UI loop).
 *   - $36/$37  : "credits-roll" prologue. $36 mutes APU, reloads
 *                song $0B via the SPC reload chain, decompresses three
 *                blocks (C487 -> VRAM 0, F27A -> 7000 pal, E73B/F319
 *                -> 4000/5000), and spawns the five "credit ticker"
 *                sprites ($51..$55). $37 advances the layout via
 *                BA9E(1) + two BACA blocks at $9257/$9265 and fades.
 *   - $38/$39  : an alternative credits page — $38 enables sub-screen
 *                color-math (CGADSUB=$41) + TM=$07 / TS=$10 (BG1+OBJ
 *                on main, BG3 on sub) and spawns ticker $4D; $39 then
 *                blits two more layout blocks at $9283/$928F via BA9E(7)
 *                + BACA before fading.
 *   - $3A/$3B  : the helper-driven credit-card pair. $3A runs the
 *                shared "small credit card" loader at $00:B743 (which
 *                decompresses A856/FA58/E2D6/EFCC and spawns three
 *                cursor sprites $4E/$4F/$50), then sub_8629 dispatches
 *                end-of-state SFX. $3B advances via BA9E(4) + two
 *                BACA writes, then sub_8642 fades and JUMPS the state
 *                machine to $40 (next state-pack — the demo loop).
 *   - $3C      : second "small credit card" — same as $3A but with
 *                an APU reload (program $00), spawns the cursor entity
 *                $01 first, then the credit card.
 *   - $3D      : a layout-only continuation that BA9E(5)/BACA(8,92EF)
 *                writes, sub_8642 fades, and rewinds dp[$0B] to $16
 *                (back into the save-picker preamble).
 *   - $3E      : the BIG credits sprite-sheet loader — full BGMODE
 *                reset via BB38, OBSEL=$62, decompress F834 (16 KB)
 *                to VRAM $0000, F400 pal to $7000, 9F47 tilemap to
 *                $4000; spawn ticker $68 and seed BG2 V-scroll
 *                ($48) and dp[$07] to $FFE0 so the next state ($3F,
 *                already lifted in audio_intro.c) finds the credits
 *                page ready to scroll up.
 *
 * Conventions match states_gameplay.c exactly — same extern set, same
 * #define table, same INC $0B convention on exit. Where a state
 * explicitly stamps dp[$0B] (not INC) the comment calls it out.
 */

#include <stdint.h>

/* ========================================================================
 * EXTERNAL DEPS — declared in simant.c / states_gameplay.c
 * ======================================================================== */
extern uint8_t  wram[];
extern volatile uint8_t mmio[];
#define dp wram
#define MMIO8(addr)  (*(volatile uint8_t *)&mmio[(addr) & 0xFFFF])
#define INIDISP    MMIO8 (0x2100)
#define OBSEL      MMIO8 (0x2101)
#define BGMODE     MMIO8 (0x2105)
#define M7SEL      MMIO8 (0x211A)
#define M7X        MMIO8 (0x211F)
#define M7Y        MMIO8 (0x2120)
#define BG2HOFS    MMIO8 (0x211C)
#define BG2VOFS    MMIO8 (0x211D)
#define TM         MMIO8 (0x212C)
#define TS         MMIO8 (0x212D)
#define CGWSEL     MMIO8 (0x2130)
#define CGADSUB    MMIO8 (0x2131)
#define APUIO0     MMIO8 (0x2140)
#define APUIO1     MMIO8 (0x2141)
#define APUIO2     MMIO8 (0x2142)
#define APUIO3     MMIO8 (0x2143)

/* Helpers — bodies live in simant.c / lifted_helpers_*.c / states_*.c. */
extern void sub_8841(uint8_t a);                     /* wait A frames        */
extern void sub_8976(void);                          /* force-blank + ack NMI*/
extern void sub_896D(void);                          /* re-enable NMI        */
extern void sub_85FC(void);                          /* fade-in / sync       */
extern void sub_8616_fade(void);                     /* fade-out             */
extern void sub_8629(void);                          /* dispatch end / SFX   */
extern void sub_8642(void);                          /* "view fade" exit     */
extern void sub_877D(void);                          /* task yield (1 frame) */
extern void sub_BB38(void);                          /* common screen setup  */
extern void sub_BAF2(void);                          /* DMA palette block    */
extern void sub_BA9E(uint8_t a);                     /* template by index    */
extern void sub_BACA(uint8_t a, uint16_t y);         /* template + data ptr  */
extern void sub_8AED(uint8_t a, uint16_t y);         /* DMA helper           */
extern void sub_8ACC(uint16_t length,
                     uint16_t vram_dst);             /* DMA into VRAM        */
extern void sub_8D7E(uint8_t count, uint16_t src);   /* decompress           */
extern void sub_8C41(void);                          /* JSL $008C41          */
extern void sub_897D(void);                          /* JSL $00897D          */
extern void sub_8B41(void);                          /* JSR $8B41 helper     */
extern void sub_BB15(void);                          /* JSR $BB15 helper     */
extern void sub_028005(void);                        /* JSL $028010 dec      */
extern void sub_088003(uint16_t x);                  /* SPC reload (banked)  */
extern void sub_499C1(uint16_t x, uint16_t y,
                      uint8_t a);                    /* spawn entity         */
extern void sub_8E88(uint8_t a);                     /* JSL $00:8E88 APU cmd */

/* Local forward — $32 calls the small HDMA seeder at $00:AE5C. */
static void hdma_seed_AE5C(void);

/* ========================================================================
 * STATE $30   $00:AF9A   COLONY-STATS / RESULTS SCREEN — setup
 * ========================================================================
 * Mute the APU IO ports, wait 4 frames, force-blank, reload the SPC700
 * driver (program $0A00 = bank-08 stub, song "scoring" $00), kick the
 * scoring tune via JSL $008E88 #$06, switch to BGMODE 7 with M7SEL=$80
 * (V-flip mode-7 tilemap), seed the rotation centre ($9E=0x80, $A0=0x40),
 * decompress two big assets (C25C -> VRAM $0000, E91E manual-staged to
 * $7E:$4000 via $028010 and then DMA'd to VRAM $4000), upload sprite
 * palette (07 block at A580), seed the mode-7 source rect ($A4=0x80,
 * $A6=0x80, $A8=0x100), prime sub_8C41 / sub_897D, then spawn the three
 * result-card sprites ($57, $58 at (80,C0), $74 at (10,FFF8)).
 */
static void state_30_results_screen_setup_AF9A(void)
{
    APUIO0 = 0; APUIO1 = 0; APUIO2 = 0; APUIO3 = 0;
    sub_8841(0x04);
    sub_8976();
    INIDISP = 0x80;
    dp[0x32] = 0x00;                                     /* song index 0      */
    sub_088003(0x0A00);                                  /* reload SPC driver */
    sub_BB38();
    sub_8E88(0x06);                                      /* play scoring tune */

    BGMODE = 0x07;
    M7SEL  = 0x80;
    dp[0xA2] = 0x00;
    *(uint16_t *)&dp[0x9E] = 0x0080;
    *(uint16_t *)&dp[0xA0] = 0x0040;

    sub_8D7E(0x1A, 0xC25C); sub_8ACC(0x8000, 0x0000);    /* tile block */

    /* Second decompress is the same manual-staged $028010 idiom used
     * in state $0E ($00:B2B0) — seed $02CF/$02D1/$02D4/$02D6 then
     * call the dispatcher. */
    sub_8D7E(0x19, 0xE91E);
    *(uint16_t *)&dp[0x02CF] = 0xF495;
    dp[0x02D1]               = 0x19;
    *(uint16_t *)&dp[0x02D4] = 0x4000;
    dp[0x02D6]               = 0x7E;
    sub_028005();                                        /* JSL $028010 */
    sub_8ACC(0x4000, 0x4000);

    sub_8AED(0x07, 0xA580);                              /* palette upload */

    /* Mode-7 source rect. */
    *(uint16_t *)&dp[0xA4] = 0x0080;
    *(uint16_t *)&dp[0xA6] = 0x0080;
    *(uint16_t *)&dp[0xA8] = 0x0100;

    sub_8C41();
    sub_897D();

    sub_499C1(0x0000, 0x0000, 0x57);                     /* spawn result $57 */
    sub_499C1(0x0080, 0x00C0, 0x58);                     /* spawn result $58 */
    sub_499C1(0x0010, 0xFFF8, 0x74);                     /* spawn result $74 */

    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $31   $00:B060   RESULTS SCREEN — mode-7 zoom-in tick
 * ========================================================================
 * One frame yield, then advance dp[$A8] (16-bit mode-7 source width) by
 * +6 each iteration until wall-clock dp[$02] hits 5; then fade out and
 * INC. This is the "Maxis-logo-style zoom" that brings the result-card
 * art on screen as the music starts.
 */
static void state_31_results_screen_zoom_B060(void)
{
    do {
        sub_877D();
        *(uint16_t *)&dp[0xA8] += 6;
    } while (dp[0x02] != 0x05);
    sub_8616_fade();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $32   $00:AD6A   QUEEN-DIED / SCENARIO-FAIL — mode-7 setup
 * ========================================================================
 * The "your queen has died" mode-7 backdrop. Identical structure to
 * state $0E (marriage-flight) but with different asset offsets and an
 * extra sub_8B41 pre-DMA + the AE5C HDMA seeder for the gradient.
 *   - decompress CBF3 + manual-stage D56A -> 7E:$4000 -> VRAM $4000
 *   - decompress AE3B -> VRAM 0 (after sub_8B41 BG seeding)
 *   - palette 07@8E00, seed mode-7 origin to (0x40,0x40)
 *   - reset M7X/M7Y/BG2 H/V scroll
 *   - BG2 V-scroll dp[$48] = 0xFF90 (off-screen up), BG1 V dp[$46]=0x20
 *   - spawn types $18/$19/$1A (three "queen-died" sprites)
 *   - CGWSEL=0, CGADSUB=0x7F (full color-math), hdma_seed_AE5C().
 */
static void state_32_queen_died_setup_AD6A(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_BB38();
    BGMODE = 0x07;
    M7SEL  = 0x00;

    sub_8D7E(0x16, 0xCBF3);
    /* Manual-staged decompress (no JSR $8D7E for D56A — staged direct). */
    *(uint16_t *)&dp[0x02CF] = 0xD56A;
    dp[0x02D1]               = 0x16;
    *(uint16_t *)&dp[0x02D4] = 0x4000;
    dp[0x02D6]               = 0x7E;
    sub_028005();                                        /* JSL $028010 */
    sub_8ACC(0x4000, 0x4000);

    sub_8D7E(0x16, 0xAE3B);
    sub_8B41();                                          /* BG3 seed */
    sub_8ACC(0x8000, 0x0000);
    sub_8AED(0x07, 0x8E00);                              /* palette */

    dp[0xA2] = 0x00;
    *(uint16_t *)&dp[0x9E] = 0x0040;
    *(uint16_t *)&dp[0xA0] = 0x0040;

    /* Mode-7 X / Y register double-writes (high then low). */
    M7X = 0x40; M7X = 0x00;
    M7Y = 0x40; M7Y = 0x00;
    BG2HOFS = 0; BG2HOFS = 0;
    BG2VOFS = 0; BG2VOFS = 0;

    *(uint16_t *)&dp[0x48] = 0xFF90;                     /* BG2 V scroll */
    *(uint16_t *)&dp[0x46] = 0x0020;                     /* BG1 V scroll */

    sub_499C1(0x00A0, 0x00A0, 0x18);
    sub_499C1(0x00E0, 0x00A0, 0x19);
    sub_499C1(0x0000, 0x0000, 0x1A);

    CGWSEL  = 0x00;
    CGADSUB = 0x7F;
    hdma_seed_AE5C();

    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $33   $00:AE33   QUEEN-DIED — slide-in tick
 * ========================================================================
 * Per-frame: yield once, decrement 16-bit dp[$48-$49] (BG2 V-scroll
 * slides DOWN -> picture moves UP onto screen), decrement 16-bit
 * dp[$A0-$A1] (mode-7 Y origin), then write the low/high bytes of
 * $A0/$A1 to $2120 (M7Y double-write). When wall-clock dp[$02] reaches
 * 0x0A, fade out and INC.
 */
static void state_33_queen_died_slide_in_AE33(void)
{
    do {
        sub_877D();
        if (dp[0x48] == 0) dp[0x49]--;
        dp[0x48]--;
        if (dp[0xA0] == 0) dp[0xA1]--;
        dp[0xA0]--;
        M7Y = dp[0xA0];
        M7Y = dp[0xA1];
    } while (dp[0x02] < 0x0A);
    sub_8616_fade();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $34   $00:B36D   ANT-INFO SPLASH — setup
 * ========================================================================
 * The "ant info card" full-screen. Straight asset blast — no mode-7,
 * no APU touch. Decompress 1A@DD84 -> VRAM 0, palette 07@EBB2 -> 7000,
 * palette-commit (BAF2), decompress 1A@F4F1 -> VRAM 4000, sprite
 * palette 07@A780, spawn ticker $4C at (0xF8, 0xFFC0).
 */
static void state_34_ant_info_setup_B36D(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_BB38();
    sub_8D7E(0x1A, 0xDD84); sub_8ACC(0x2000, 0x0000);
    sub_8D7E(0x07, 0xEBB2); sub_8ACC(0x0800, 0x7000);
    sub_BAF2();
    sub_8D7E(0x1A, 0xF4F1); sub_8ACC(0x2000, 0x4000);
    sub_8AED(0x07, 0xA780);
    sub_499C1(0x00F8, 0xFFC0, 0x4C);                     /* spawn ticker $4C */
    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $35   $00:B3CB   ANT-INFO SPLASH — exit
 * ========================================================================
 * BA9E(5) layout, fade out, then JUMP back into save-picker UI by
 * stamping dp[$0B] = $17 (NOT INC).
 */
static void state_35_ant_info_exit_B3CB(void)
{
    sub_BA9E(0x05);
    sub_8616_fade();
    dp[0x0B] = 0x17;
}

/* ========================================================================
 * STATE $36   $00:B535   CREDITS ROLL — setup
 * ========================================================================
 * Identical opening to state $30 (APU mute, SPC reload, sub_BB38,
 * sub_8E88(6)) but with song index $0B (dp[$32]) and a different asset
 * chain:
 *   - C487 (1B) -> VRAM $0000      (credits tile sheet)
 *   - F27A (07) -> VRAM $7000      (palette)
 *   - sub_BB15 (BG seed)
 *   - E73B (1B) -> VRAM $4000      (tilemap A)
 *   - F319 (1B) -> VRAM $5000      (tilemap B)
 *   - 07@AD80 sprite palette
 *   - spawn 5 ticker sprites $55..$51 at (0x30/0x10, 0x90) overlapping
 *     to make the "credit line scroller" stack.
 */
static void state_36_credits_setup_B535(void)
{
    APUIO0 = 0; APUIO1 = 0; APUIO2 = 0; APUIO3 = 0;
    sub_8841(0x04);
    sub_8976();
    INIDISP = 0x80;
    dp[0x32] = 0x0B;                                     /* song index $0B   */
    sub_088003(0x0A00);
    sub_BB38();
    sub_8E88(0x06);

    sub_8D7E(0x1B, 0xC487); sub_8ACC(0x4000, 0x0000);
    sub_8D7E(0x07, 0xF27A); sub_8ACC(0x0800, 0x7000);
    sub_BB15();
    sub_8D7E(0x1B, 0xE73B); sub_8ACC(0x2000, 0x4000);
    sub_8D7E(0x1B, 0xF319); sub_8ACC(0x2000, 0x5000);
    sub_8AED(0x07, 0xAD80);

    sub_499C1(0x0030, 0x0090, 0x55);
    sub_499C1(0x0010, 0x0090, 0x54);
    sub_499C1(0x0010, 0x0090, 0x53);
    sub_499C1(0x0010, 0x0090, 0x52);
    sub_499C1(0x0010, 0x0090, 0x51);

    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $37   $00:B5F7   CREDITS — layout page #1
 * ========================================================================
 * Single layout-block step: BA9E(1) + two BACA writes (01@$9257,
 * 04@$9265) + fade. Used between credit pages.
 */
static void state_37_credits_page1_B5F7(void)
{
    sub_BA9E(0x01);
    sub_BACA(0x01, 0x9257);
    sub_BACA(0x04, 0x9265);
    sub_8616_fade();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $38   $00:B612   CREDITS — page #2 setup
 * ========================================================================
 * Sub-screen color-math enabled (CGWSEL=$02, CGADSUB=$41), TM=$07
 * (BG1+BG2+BG3), TS=$10 (OBJ on sub). Heavier asset load:
 *   - FA97 (1B) -> VRAM $0000  (12 KB)
 *   - F66A (07) -> VRAM $7000  (palette)
 *   - sub_BB15
 *   - 9901 (1C) -> VRAM $4000
 *   - 9F44 (1C) -> VRAM $5000
 *   - 07@AF80 sprite palette
 *   - spawn ticker $4D at (0x78, 0x80)
 * Note: INC $0B happens BEFORE the 896D/85FC tail (uncommon but matches
 * the ROM — the dispatcher checks dp[$0B] before the fade kicks in).
 */
static void state_38_credits_page2_setup_B612(void)
{
    sub_8976();
    INIDISP = 0x80;
    sub_BB38();
    CGWSEL  = 0x02;
    CGADSUB = 0x41;
    TM      = 0x07;
    TS      = 0x10;

    sub_8D7E(0x1B, 0xFA97); sub_8ACC(0x3000, 0x0000);
    sub_8D7E(0x07, 0xF66A); sub_8ACC(0x0800, 0x7000);
    sub_BB15();
    sub_8D7E(0x1C, 0x9901); sub_8ACC(0x2000, 0x4000);
    sub_8D7E(0x1C, 0x9F44); sub_8ACC(0x2000, 0x5000);
    sub_8AED(0x07, 0xAF80);

    sub_499C1(0x0078, 0x0080, 0x4D);

    dp[0x0B]++;                                          /* INC before tail */
    sub_896D();
    sub_85FC();
}

/* ========================================================================
 * STATE $39   $00:B695   CREDITS — page #2 layout
 * ========================================================================
 * BA9E(7) + two BACA writes (01@$9283, 04@$928F) + fade.
 */
static void state_39_credits_page2_layout_B695(void)
{
    sub_BA9E(0x07);
    sub_BACA(0x01, 0x9283);
    sub_BACA(0x04, 0x928F);
    sub_8616_fade();
    dp[0x0B]++;
}

/* ========================================================================
 * Shared "small credit card" loader at $00:B743
 * ========================================================================
 * Used by states $3A and $3C. Loads a tile/palette/tilemap chain into
 * VRAM and spawns three cursor-style sprites ($4E/$4F/$50):
 *   - A856 (1C) -> VRAM $0000   (16 KB tile)
 *   - FA58 (07) -> VRAM $7000   (palette)
 *   - palette commit (BAF2)
 *   - E2D6 (1C) -> VRAM $4000   (tilemap A)
 *   - EFCC (1C) -> VRAM $5000   (tilemap B)
 *   - spawn $4E at (0,0), $4F at (0xC0,0x40), $50 at (0x40,0xC0).
 */
static void load_small_credit_card_B743(void)
{
    sub_BB38();
    sub_8D7E(0x1C, 0xA856); sub_8ACC(0x4000, 0x0000);
    sub_8D7E(0x07, 0xFA58); sub_8ACC(0x0800, 0x7000);
    sub_BAF2();
    sub_8D7E(0x1C, 0xE2D6); sub_8ACC(0x2000, 0x4000);
    sub_8D7E(0x1C, 0xEFCC); sub_8ACC(0x2000, 0x5000);
    sub_499C1(0x0000, 0x0000, 0x4E);
    sub_499C1(0x00C0, 0x0040, 0x4F);
    sub_499C1(0x0040, 0x00C0, 0x50);
}

/* ========================================================================
 * STATE $3A   $00:B6B0   SMALL CREDIT CARD — setup (no APU reload)
 * ========================================================================
 * Force-blank, call the shared $B743 loader, sprite palette 07@F200,
 * sub_8629 (end-of-state SFX) + INC.
 */
static void state_3A_credit_card_setup_B6B0(void)
{
    sub_8976();
    INIDISP = 0x80;
    load_small_credit_card_B743();
    sub_8AED(0x1E, 0xF200);                              /* sprite palette  */
    sub_896D();
    sub_8629();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $3B   $00:B6CC   SMALL CREDIT CARD — exit + JUMP to $40
 * ========================================================================
 * BA9E(4) + two BACA writes (02@$92AF, 04@$92C7) + sub_8642 fade.
 * Stamps dp[$0B] = $40 (NOT INC) — hands off to the next state pack
 * (likely the boot-loop / attract demo, lifted elsewhere).
 */
static void state_3B_credit_card_exit_to_40_B6CC(void)
{
    sub_BA9E(0x04);
    sub_BACA(0x02, 0x92AF);
    sub_BACA(0x04, 0x92C7);
    sub_8642();
    dp[0x0B] = 0x40;
}

/* ========================================================================
 * STATE $3C   $00:B6E9   SMALL CREDIT CARD — setup (with APU reload)
 * ========================================================================
 * APU mute, 4-frame wait, force-blank, reload SPC driver (program $00 —
 * silent / boot song), spawn cursor entity $01, kick song $04 via
 * sub_8E88, run the shared $B743 loader, sprite palette 07@F000,
 * sub_8629 + INC. This is the credits-card variant that fires when the
 * sequence is entered cold (from the title screen path).
 */
static void state_3C_credit_card_with_apu_B6E9(void)
{
    APUIO0 = 0; APUIO1 = 0; APUIO2 = 0; APUIO3 = 0;
    sub_8841(0x04);
    sub_8976();
    INIDISP = 0x80;
    dp[0x32] = 0x00;
    sub_088003(0x0A00);
    load_small_credit_card_B743();
    sub_499C1(0x0000, 0x0000, 0x01);                     /* spawn cursor */
    sub_8E88(0x04);                                      /* play song $04 */
    sub_8AED(0x1E, 0xF000);
    sub_896D();
    sub_8629();
    dp[0x0B]++;
}

/* ========================================================================
 * STATE $3D   $00:B72E   CREDIT CARD — layout + JUMP back to $16
 * ========================================================================
 * BA9E(5) + BACA(8,$92EF) + sub_8642 fade. Forces dp[$0B] = $16 — re-
 * enters the title-screen input loop (state $16 is the title-input
 * handler per states_gameplay.c line 1714).
 */
static void state_3D_credit_card_exit_to_16_B72E(void)
{
    sub_BA9E(0x05);
    sub_BACA(0x08, 0x92EF);
    sub_8642();
    dp[0x0B] = 0x16;
}

/* ========================================================================
 * STATE $3E   $00:B7AC   BIG CREDITS PAGE — setup (precedes $3F)
 * ========================================================================
 * The "scroll-up credits" prologue. APU reset, 4-frame wait, force-
 * blank, reload SPC driver with song index $0B (dp[$32]), sub_BB38,
 * kick the song via sub_8E88(4), OBSEL=$62 (sprite tile base $4000,
 * 8x8/16x16 mix), then:
 *   - F834 (1C) -> VRAM $0000   (16 KB credits sprite/tile sheet)
 *   - F400 (1E) -> VRAM $7000   (palette)
 *   - palette commit (BAF2)
 *   - 9F47 (1D) -> VRAM $4000   (tilemap)
 *   - 07@B180 sprite palette
 *   - dp[$48] = dp[$07] = $FFE0 (BG2 V-scroll starts off-screen)
 *   - spawn ticker $68 at (0,0)
 * Then NMI on, fade in, INC. State $3F (in audio_intro.c) takes over
 * to actually scroll the credits up.
 */
static void state_3E_big_credits_setup_B7AC(void)
{
    APUIO0 = 0; APUIO1 = 0; APUIO2 = 0; APUIO3 = 0;
    sub_8841(0x04);
    sub_8976();
    INIDISP = 0x80;
    dp[0x32] = 0x0B;
    sub_088003(0x0A00);
    sub_BB38();
    sub_8E88(0x04);

    OBSEL = 0x62;
    sub_8D7E(0x1C, 0xF834); sub_8ACC(0x4000, 0x0000);
    sub_8D7E(0x1E, 0xF400); sub_8ACC(0x0800, 0x7000);
    sub_BAF2();
    sub_8D7E(0x1D, 0x9F47); sub_8ACC(0x2000, 0x4000);
    sub_8AED(0x07, 0xB180);

    *(uint16_t *)&dp[0x48] = 0xFFE0;
    *(uint16_t *)&dp[0x07] = 0xFFE0;

    sub_499C1(0x0000, 0x0000, 0x68);                     /* spawn ticker $68 */

    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * HDMA seeder used by state $32 ($00:AE5C)
 * ========================================================================
 * Programs two HDMA channels (1 and 2) to drive BG3 H-scroll (reg $1B)
 * from a table at $00:8C1D, and a 2-register pair starting at $1E from
 * $00:8D64. Channel 3 is reset to $00 / $32, table at $00:8EAB. This
 * makes the dirt-gradient and parallax bands of the "queen-died" mode-7
 * screen. The body terminates with the third channel-base store; the
 * tail isn't reached from state $32's call site.
 */
static void hdma_seed_AE5C(void)
{
    MMIO8(0x420C) = 0x00;                                /* HDMAEN off       */
    /* Channel 1: 2-reg HDMA into $2118-$2119 (VRAM data?) — */
    /* actually the original writes $4310=$02 mode, $4311=$1B (BG3HOFS), */
    /* table @ $00:8C1D, ind addr-bank $01 */
    MMIO8(0x4310) = 0x02;
    MMIO8(0x4311) = 0x1B;
    MMIO8(0x4312) = 0x1D;
    MMIO8(0x4313) = 0x8C;
    MMIO8(0x4314) = 0x01;
    MMIO8(0x4317) = 0x01;
    /* Channel 2: mode $02 into $211E, table @ $00:8D64. */
    MMIO8(0x4320) = 0x02;
    MMIO8(0x4321) = 0x1E;
    MMIO8(0x4322) = 0x64;
    MMIO8(0x4323) = 0x8D;
    MMIO8(0x4324) = 0x01;
    MMIO8(0x4327) = 0x01;
    /* Channel 3: mode $00 into $2132, table @ $00:8EAB. */
    MMIO8(0x4330) = 0x00;
    MMIO8(0x4331) = 0x32;
    MMIO8(0x4332) = 0xAB;
    MMIO8(0x4333) = 0x8E;
    /* (The ROM tail continues past this point; not reached on the $32
     * call path because the cold-state setup re-enables HDMA via sub_896D
     * before this routine's INC-equivalent tail runs.) */
}

/* ========================================================================
 * Anchor — references all lifted handlers so -Wunused-function stays clean.
 * ======================================================================== */
__attribute__((used))
static const void * const _state_refs_late[] = {
    (const void *)state_30_results_screen_setup_AF9A,
    (const void *)state_31_results_screen_zoom_B060,
    (const void *)state_32_queen_died_setup_AD6A,
    (const void *)state_33_queen_died_slide_in_AE33,
    (const void *)state_34_ant_info_setup_B36D,
    (const void *)state_35_ant_info_exit_B3CB,
    (const void *)state_36_credits_setup_B535,
    (const void *)state_37_credits_page1_B5F7,
    (const void *)state_38_credits_page2_setup_B612,
    (const void *)state_39_credits_page2_layout_B695,
    (const void *)state_3A_credit_card_setup_B6B0,
    (const void *)state_3B_credit_card_exit_to_40_B6CC,
    (const void *)state_3C_credit_card_with_apu_B6E9,
    (const void *)state_3D_credit_card_exit_to_16_B72E,
    (const void *)state_3E_big_credits_setup_B7AC,
    (const void *)load_small_credit_card_B743,
    (const void *)hdma_seed_AE5C,
};

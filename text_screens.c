/*
 * text_screens.c — SimAnt (SNES) ANT ENCYCLOPEDIA + TUTORIAL subsystems.
 *
 * Two text-driven UI screens lifted from the ROM:
 *
 *   1. ANT ENCYCLOPEDIA (manual p.5) — accessed via Main Menu → Ant
 *      Information.  Title-screen pick #4 calls $00:9668 which sets
 *      dp[$0B] = $2E.  That game-state (`state_2E_landing_pick_setup_A3EC`
 *      in states_gameplay.c) does the screen template and spawns the
 *      topic-list selector entity (type $2D).  Then state $2F
 *      (`state_2F_landing_pick_input_A4DE`) runs the polled input loop.
 *
 *      Each frame: read joypad-decoded $28 pick (filled by entity type
 *      $2D when the cursor enters one of 8 hit-rects); jump via the
 *      8-entry table at $00:A55F.  The 8 picks are
 *        [0..4] = Introduction / Ant Life / Ants at Home / Ants&Relatives /
 *                 SimAnt Strategy (set page-index dp[$56] = 0, 6, 17, 20, 26)
 *        [5]    = EXIT  (fade-out + dp[$0B]=$16, back to title)
 *        [6]    = NEXT  (INC dp[$56] unless we just hit the next topic's
 *                 start; the boundary list is {6, $11, $14, $1A, $1E})
 *        [7]    = PREV  (decrement dp[$90] scroll; if underflow, render)
 *
 *      After a valid pick the loop drops into $A519 which calls
 *      `JSR $DFCD` with A=current page index — that's the page renderer.
 *      $DFCD reads 4 parallel 30-entry tables in bank $01:
 *        $01:C778[page]   1-byte palette index   → palette ROM ofs
 *        $01:C796[page]   1-byte source bank     → for picture decompressor
 *        $01:C7B4[page]   2-byte source offset   → for picture decompressor
 *        $01:C7F0[page]   2-byte text pointer    → for text renderer
 *      It decompresses the picture, uploads the palette, then calls
 *      $04:9000 (the column-wrapped text renderer) on the text pointer.
 *
 *      Text format:
 *        each page = chars + $FE newline + … + $FF end-of-page.
 *        $2C (",") and $2E (".") never trigger an automatic wrap.
 *
 *   2. TUTORIAL (manual p.4) — main-menu pick #3 calls $00:962A which
 *      arms a saved-game load and INCs $0B (→ $17 save picker, then
 *      eventually $03 = GS_TUTORIAL).  GS_TUTORIAL ($00:ACE8) itself is
 *      trivial: wait 4 in-game seconds, fade out, advance.  The actual
 *      tutorial dialog screen is painted by a generic helper at $00:E290
 *      (load-from-script) plus $00:E2A2 (paint-from-table).  The script
 *      table at $00:E2C2 holds 54 16-bit pointers — one per tutorial
 *      message — to text blocks in bank $01 ($B27F..$C777).
 *
 *      The dispatcher reads dp[$72] (current tutorial index), masks to
 *      &$3F, doubles, and indirects into $E2C2.  The pointed-to text
 *      uses the same $FE/$FF/$2C/$2E grammar as the encyclopedia.
 *
 * ROM source: /Users/guilhermedavid/simant-re/simant.sfc (1 MB LoROM).
 * Cross-refs: states_menu.c (GS_ANT_INFORMATION at $B155, GS_TUTORIAL at
 * $ACE8); states_gameplay.c (state $2E setup; state $2F input loop).
 *
 * Conventions: M=1, X=0, DBR=$01, D=$0000 at handler entry.  `extern
 * uint8_t wram[0x20000]; #define dp wram;` per project rules.
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Shared aliases (match other .c files in the decomp).
 * ------------------------------------------------------------------------ */
extern uint8_t wram[0x20000];
#define dp wram

extern volatile uint8_t  mmio[0x10000];
#define MMIO8(addr)   (*(volatile uint8_t  *)&mmio[(addr) & 0xFFFF])

#define INIDISP   MMIO8(0x2100)
#define CGADD     MMIO8(0x2121)
#define APUIO0    MMIO8(0x2140)
#define JOY1L     MMIO8(0x4218)

/* ------------------------------------------------------------------------
 * Externs satisfied by other .c files.
 * ------------------------------------------------------------------------ */
extern void sub_877D(void);                                  /* yield 1 frame  */
extern void sub_8841(uint8_t n);                             /* wait_frames    */
extern void sub_8616_fade(void);                             /* fade out       */
extern void sub_896D(void);                                  /* enable NMI     */
extern void sub_8976(void);                                  /* mask NMI       */
extern void sub_C318(void);
extern void sub_C398(void);
extern void sub_8F08(void);
extern void sub_8D94(void);
extern void sub_8D7E(uint8_t bank, uint16_t off);            /* asset decmp    */
extern void sub_8ACC(uint16_t len, uint16_t vram_dst);       /* vram dma       */
extern void sub_866E(uint16_t fill, uint16_t vram_addr);     /* vram clear     */
extern void sub_8683(uint16_t fill, uint16_t vram_addr);     /* alt vram clear */
extern void sub_8AED(uint8_t bank, uint16_t off);            /* cgram dma      */
extern void sub_490D2(void);                                 /* BG3 init step 1*/
extern void sub_4911B(void);                                 /* BG3 init step 2*/
extern void sub_490DB(void);                                 /* BG3 init step 3*/
extern void sub_499C1(uint16_t x, uint16_t y, uint8_t type); /* spawn entity   */
extern void sub_8E88(uint8_t cmd);                           /* APU IPC        */
extern void sub_8791(void);
extern void sub_87BC(void);

/* Bank-$02 decompressor entry — given src bank in dp[$02D1],
 * src offset in dp[$02CF/02D0], dest bank in dp[$02D6],
 * dest addr in dp[$02D4/02D5]: decompresses to dest. */
extern void asset_decompress_028010(void);

/* Bank-$00 palette uploader: A=bank, X=ROM-offset, Y=$0002.
 * Used by the encyclopedia per-page palette swap. */
extern void palette_upload_0088FF(uint8_t bank, uint16_t rom_off);

/* Bank-$04 column-wrapped text renderer at $04:9000.
 *   A = column width            (e.g. $18 for encyclopedia body)
 *   X = BG3 tilemap dest word   (e.g. $0010 for left-top of body window)
 *   Y = source text pointer     (in bank $01 by default)
 * Walks Y reading bytes until $FF, emitting tile-index chars; $FE
 * forces a newline; $2C "," and $2E "." disable the auto-wrap at the
 * column-end check. */
extern void text_render_049000(uint8_t cols, uint16_t bg3_dest,
                               uint16_t src);

/* Bank-$00 alternate text-into-tilemap-shadow renderer at $00:C91F.
 *   A = column width
 *   X = packed (col,row) origin (high=col*?, low=row*?)
 *   Y = source text pointer
 * Writes to the BG3 update queue at $0C00 (paired with the attribute
 * byte from dp[$8C]).  Used by the encyclopedia for the TOPIC-LIST
 * 6-row panel and by the tutorial-script painter at $00:E2B0. */
extern void text_render_00C91F(uint8_t cols, uint16_t origin,
                               uint16_t src);

/* Bank-$00 caption blitter ($00:C4D8) — direct VRAM tilemap write of a
 * short caption string.  X = packed (col,row), Y = source ptr.  Used
 * by the encyclopedia's A625 to paint the 6 topic-row strings. */
extern void caption_blit_00C4D8(uint16_t origin, uint16_t src);

/* ROM data tables in bank $01.  Symbolic names match the ROM offsets. */
extern const uint8_t  rom_01_C778[30];     /* per-page palette index    */
extern const uint8_t  rom_01_C796[30];     /* per-page picture src bank */
extern const uint16_t rom_01_C7B4[30];     /* per-page picture src ofs  */
extern const uint16_t rom_01_C7F0[30];     /* per-page text pointer     */
extern const uint16_t rom_00_E2C2[54];     /* tutorial script ptrs      */
extern const uint8_t  rom_01_89C5[];       /* topic hit-rect table      */

/* ========================================================================
 * ENCYCLOPEDIA DATA MODEL
 * ========================================================================
 *
 * Six TOPICS, indexed 0..5, listed in the top-left window:
 *
 *      idx  string-ptr  string                topic name
 *      ───  ──────────  ────────────────────  ──────────────────
 *       0   $01:89F9    "  Introduction   "   T_INTRO
 *       1   $01:8A0B    "  Ant Life       "   T_LIFE
 *       2   $01:8A1D    "  Ants at Home   "   T_HOME
 *       3   $01:8A2F    "  Ants&Relatives "   T_RELATIVES
 *       4   $01:8A41    "  SimAnt Strategy"   T_STRATEGY
 *       5   $01:8A53    "  EXIT           "   T_EXIT
 *
 *      $01:89E7 is the "blank-row" filler string (used between topic
 *      labels and as background under the cursor's hover slot).
 *
 * 30 PAGES total, indexed 0..29.  Each page has FOUR parallel records
 * in bank $01 (the renderer at $00:DFCD = $E342 indexes all four):
 *
 *      $01:C778[page]      1-byte palette index → ROM addr $07:9D80+(idx<<7)
 *      $01:C796[page]      1-byte source bank for picture decompressor
 *      $01:C7B4[page]      2-byte source offset for picture decompressor
 *      $01:C7F0[page]      2-byte pointer into bank $01 with the page text
 *
 * The TOPIC → PAGE-RANGE mapping (read off the $A590 NEXT-boundary list):
 *
 *      Topic 0 (Introduction):    pages 0..5    (6 pages)
 *      Topic 1 (Ant Life):        pages 6..16   (11 pages)
 *      Topic 2 (Ants at Home):    pages 17..19  (3 pages)
 *      Topic 3 (Ants&Relatives):  pages 20..25  (6 pages)
 *      Topic 4 (SimAnt Strategy): pages 26..29  (4 pages)
 *
 *      6 + 11 + 3 + 6 + 4 = 30 pages — fills the table exactly.
 *
 * The HIT-RECT table at $01:89C5 describes 8 click targets in screen
 * coordinates {xmin,xmax,ymin,ymax}.  The terminator is xmin==xmax:
 *
 *      rect 0:  x=30..90, y=24..34   Introduction
 *      rect 1:  x=30..90, y=34..44   Ant Life
 *      rect 2:  x=30..90, y=44..54   Ants at Home
 *      rect 3:  x=30..90, y=54..64   Ants&Relatives
 *      rect 4:  x=30..90, y=64..74   SimAnt Strategy
 *      rect 5:  x=30..90, y=74..84   EXIT
 *      rect 6:  x=B0..F0, y=60..70   "Next page" icon (right window top)
 *      rect 7:  x=B0..F0, y=70..80   "Prev page" icon (right window bot)
 *      rect 8:  x=00..00            TERMINATOR
 *
 * The cursor entity (type $02, normal navigation cursor) controls
 * $0014/$0015 within those bounds.  When the player presses A inside a
 * rect, the entity type-$2D "hit-test" handler at $04:B90A writes the
 * rect index into dp[$28] (signed: -1 == no pick).  state $2F's input
 * loop reads dp[$28] and dispatches through the table at $00:A55F.
 * ======================================================================== */

/* ------------------------------------------------------------------------
 * Encyclopedia state (lives in WRAM direct page).
 * ------------------------------------------------------------------------ */
#define ENC_PAGE        dp[0x0056]    /* current page index (0..29)        */
#define ENC_SCROLL      dp[0x0090]    /* current scroll Y within text page */
#define ENC_PICK        dp[0x0028]    /* cursor-pick from entity $2D       */
#define CURSOR_X        dp[0x0014]
#define CURSOR_Y        dp[0x0015]
#define CURSOR_XMIN     dp[0x0016]
#define CURSOR_XMAX     dp[0x0017]
#define CURSOR_YMIN     dp[0x0018]
#define CURSOR_YMAX     dp[0x0019]
#define TILE_ATTR       dp[0x008C]

/* ------------------------------------------------------------------------
 * Topic boundaries.  ENC_PAGE wraps inside [topic_start, next_topic_start);
 * NEXT-pick refuses to advance past the right edge.
 * ------------------------------------------------------------------------ */
enum EncTopic {
    T_INTRO     = 0,   /* pages  0..5   */
    T_LIFE      = 1,   /* pages  6..16  */
    T_HOME      = 2,   /* pages 17..19  */
    T_RELATIVES = 3,   /* pages 20..25  */
    T_STRATEGY  = 4,   /* pages 26..29  */
    T_EXIT      = 5,   /* (no pages)    */
};

static const uint8_t topic_start_page[6] = {
    0,    /* T_INTRO     */
    6,    /* T_LIFE      */
    0x11, /* T_HOME      = 17 */
    0x14, /* T_RELATIVES = 20 */
    0x1A, /* T_STRATEGY  = 26 */
    0x1E, /* T_EXIT marker = 30 (one past end) */
};

/* ========================================================================
 * $00:A56F .. $00:A589 — five topic-pick handlers.  Each writes the
 * starting page of its topic into ENC_PAGE then falls through to the
 * shared render path at $A519 (= encyclopedia_render).
 * ======================================================================== */
static void encyclopedia_render(uint8_t page);  /* fwd                       */

static void enc_pick_intro_A56F(void)      { ENC_PAGE = 0;    encyclopedia_render(ENC_PAGE); }
static void enc_pick_life_A574(void)       { ENC_PAGE = 0x06; encyclopedia_render(ENC_PAGE); }
static void enc_pick_home_A57B(void)       { ENC_PAGE = 0x11; encyclopedia_render(ENC_PAGE); }
static void enc_pick_relatives_A582(void)  { ENC_PAGE = 0x14; encyclopedia_render(ENC_PAGE); }
static void enc_pick_strategy_A589(void)   { ENC_PAGE = 0x1A; encyclopedia_render(ENC_PAGE); }

/* $00:A511 — EXIT handler.  Fade out, go to title (state $16). */
static void enc_pick_exit_A511(void)
{
    extern void sub_8642(void);  /* fade-out variant (no INC at end) */
    sub_8642();
    dp[0x0B] = 0x16;
}

/* $00:A590 — NEXT handler.  Advance ENC_PAGE.  If it hits a topic
 * boundary, REFUSE the advance (jump to $A5AD which resets ENC_PICK
 * and stays on this page). */
static void enc_pick_next_A590(void)
{
    uint8_t next = ENC_PAGE + 1;
    /* Boundary list: 6, $11, $14, $1A, $1E (= next-topic start indices). */
    if (next == 0x06 || next == 0x11 || next == 0x14 ||
        next == 0x1A || next == 0x1E) {
        ENC_PICK = 0xFF;        /* reset, no render */
        return;
    }
    ENC_PAGE = next;
    encyclopedia_render(ENC_PAGE);
}

/* $00:A5B0 — PREV handler.  This one is implemented differently: it
 * decrements dp[$90] (a SCROLL counter for the text body), and only
 * if that underflows does it call $A535 (which is the inner part of
 * the render loop).  So "prev" is actually "scroll text up by one
 * line", and rendering the previous page is done implicitly when the
 * scroll wraps. */
static void enc_pick_prev_A5B0(void)
{
    if (ENC_SCROLL-- == 0) {
        /* falls into $A535 — same body as encyclopedia_render but
         * without the JSR $DFCD page repaint. */
        ENC_PICK = 0xFF;
        sub_877D();
    }
}

/* $00:A55F — the 8-entry pick dispatch table indexed by ENC_PICK<<1. */
typedef void (*EncPickFn)(void);
static const EncPickFn enc_pick_dispatch_A55F[8] = {
    enc_pick_intro_A56F,        /* 0: Introduction       */
    enc_pick_life_A574,         /* 1: Ant Life           */
    enc_pick_home_A57B,         /* 2: Ants at Home       */
    enc_pick_relatives_A582,    /* 3: Ants & Relatives   */
    enc_pick_strategy_A589,     /* 4: SimAnt Strategy    */
    enc_pick_exit_A511,         /* 5: EXIT               */
    enc_pick_next_A590,         /* 6: NEXT (right arrow) */
    enc_pick_prev_A5B0,         /* 7: PREV (right arrow) */
};

/* ========================================================================
 * $00:DFCD = $E342 — ENCYCLOPEDIA PAGE RENDERER.
 *
 * Called from the input loop with A = current page index.  Reads four
 * parallel tables in bank $01 and:
 *   1.  Decompresses the picture into VRAM (via $02:8010).
 *   2.  Uploads the palette via $00:88FF (256-color).
 *   3.  Calls the text renderer $04:9000 with the page-text pointer,
 *       width A=$D8 (24 columns), VRAM dest X=$0010.
 *
 * NOTE the original masks the page index with #$1F (32 = page-table
 * stride) — pages 30..31 wrap into the first two text entries
 * harmlessly.
 * ======================================================================== */
static void encyclopedia_render_DFCD(uint8_t page)
{
    page &= 0x1F;                                   /* clip to 32          */
    sub_8976();                                     /* mask NMI            */

    /* 1) Decompress picture.  rom_01_C796 has the bank, rom_01_C7B4
     * has the in-bank offset.  Both are stored in the decompressor's
     * direct-page handoff slots, then $02:8010 is JSL'd. */
    dp[0x02D1] = rom_01_C796[page];                 /* src bank            */
    *(uint16_t *)&dp[0x02CF] = rom_01_C7B4[page];   /* src offset          */
    dp[0x02D6] = 0x7E;                              /* dest bank (WRAM)    */
    *(uint16_t *)&dp[0x02D4] = 0x2000;              /* dest scratch buffer */
    asset_decompress_028010();

    sub_896D();                                     /* unmask NMI          */
    sub_877D();                                     /* yield 1 frame       */
    dp[0x88] = 0x21;                                /* arm VRAM stream     */
    sub_8841(0x04);                                 /* wait 4 frames       */

    /* 2) Palette upload.  rom_01_C778 has the index; the palette ROM
     * offset is $9D80 + (idx << 7) in bank $07.  The original computes
     * this in 16-bit arithmetic by ASL ASL ASL ASL ASL (× 32 wait, × 128
     * since the values written are byte then shifted up by 16-bit lanes). */
    uint16_t pal_idx = rom_01_C778[page];
    pal_idx = (pal_idx << 3);   /* << 3 from byte form */
    pal_idx = (pal_idx << 2);   /* + << 2 (in 16-bit lanes — total << 5
                                 *   then << 2 over the wide lane = << 7) */
    pal_idx += 0x9D80;
    palette_upload_0088FF(0x07, pal_idx);
    sub_8841(0x02);                                 /* wait 2 frames       */

    /* 3) Text body.  rom_01_C7F0 holds the per-page pointer.  Render
     * with A=$D8 width, X=$0010 dest origin in the BG3 update queue,
     * Y = page-text ptr. */
    sub_8976();
    sub_490D2(); sub_4911B(); sub_490DB();
    text_render_049000(0xD8, 0x0010, rom_01_C7F0[page]);

    /* The original also stashes a constant ($50 ← $FF6E) — a "page
     * complete" flag the VRAM streamer reads to know the BG3 layer
     * is settled. */
    *(uint16_t *)&dp[0x50] = 0xFF6E;

    sub_896D();
    sub_877D();
    dp[0x88] = 0x16;                                /* next VRAM block id  */
    sub_8841(0x09);                                 /* settle 9 frames     */
}

/* ========================================================================
 * $00:A519 — main render path (re-entry point after a topic pick).
 *
 * Pseudo-code:
 *
 *   reset_cursor_bounds_for_topic_list();   ; $A5E0
 *   fade_in_step_by_step();                 ; $A5F5
 *   yield();
 *   encyclopedia_render_DFCD(ENC_PAGE);     ; $DFCD = page renderer
 *   paint_picture_panel();                  ; $A6B4 — picture-side caption strips
 *   paint_topic_panel();                    ; $A608
 *   ENC_SCROLL -= 4;                        ; cumulative scroll trim
 *   loop:
 *     ENC_PICK = -1
 *     yield()
 *     if ENC_PICK >= 0 → dispatch via $A55F
 * ======================================================================== */

/* $00:A5E0 — set cursor bounds to the TOPIC LIST area (x=$EF..$F0,
 * y=$68..$78) and re-center the cursor at ($EF, $68). */
static void enc_cursor_topic_list_A5E0(void)
{
    CURSOR_XMIN = 0xEF;
    CURSOR_X    = 0xEF;
    CURSOR_XMAX = 0xF0;
    CURSOR_YMIN = 0x68;
    CURSOR_Y    = 0x68;
    CURSOR_YMAX = 0x78;
}

/* $00:A5CB — set cursor bounds to the WHOLE encyclopedia screen
 * (x=$7F..$80, y=$28..$78).  Called once per input cycle before
 * polling input (so the cursor can travel between picture-panel
 * picks and topic-list picks). */
static void enc_cursor_full_screen_A5CB(void)
{
    CURSOR_XMIN = 0x7F;
    CURSOR_X    = 0x7F;
    CURSOR_XMAX = 0x80;
    CURSOR_YMIN = 0x28;
    CURSOR_Y    = 0x28;
    CURSOR_YMAX = 0x78;
}

/* $00:A5F5 — 16-step fade-in.  Writes dp[$6C] $00..$0F to INIDISP with
 * a 2-frame pause each step. */
static void enc_fade_in_A5F5(void)
{
    dp[0x6C] = 0;
    do {
        INIDISP = dp[0x6C];
        sub_8841(0x02);
        dp[0x6C]++;
    } while (dp[0x6C] != 0x10);
}

/* $00:A625 — paint the 6-entry TOPIC-LIST panel (left side of screen).
 * Calls the caption blitter $00:C4D8 at column 3, rows {3,5,7,9,11,13,15}.
 * Odd rows are blank fillers ($01:89E7); even rows hold the topic
 * names ($01:89F9 .. $01:8A53 — six entries 18 bytes apart). */
static void enc_paint_topic_list_A625(void)
{
    TILE_ATTR = 0x0D;
    sub_877D();
    /* Row 3 — blank filler                                            */
    caption_blit_00C4D8(0x0303, 0x89E7);
    /* Row 4 — Introduction                                            */
    caption_blit_00C4D8(0x0403, 0x89E7);
    /* Row 5 — first topic                                             */
    caption_blit_00C4D8(0x0503, 0x89F9);
    /* Row 6 — blank                                                   */
    caption_blit_00C4D8(0x0603, 0x89E7);
    /* Row 7 — Ant Life                                                */
    caption_blit_00C4D8(0x0703, 0x8A0B);
    /* Row 8 — blank                                                   */
    caption_blit_00C4D8(0x0803, 0x89E7);
    /* Row 9 — Ants at Home                                            */
    caption_blit_00C4D8(0x0903, 0x8A1D);
    sub_877D();
    /* Row $A — blank                                                  */
    caption_blit_00C4D8(0x0A03, 0x89E7);
    /* Row $B — Ants&Relatives                                         */
    caption_blit_00C4D8(0x0B03, 0x8A2F);
    /* Row $C — blank                                                  */
    caption_blit_00C4D8(0x0C03, 0x89E7);
    /* Row $D — SimAnt Strategy                                        */
    caption_blit_00C4D8(0x0D03, 0x8A41);
    /* Row $E — blank                                                  */
    caption_blit_00C4D8(0x0E03, 0x89E7);
    /* Row $F — EXIT                                                   */
    caption_blit_00C4D8(0x0F03, 0x8A53);
    sub_877D();
    /* The original repaints row $F (EXIT) once more — likely so it
     * appears highlighted as the default focus on first entry.       */
    caption_blit_00C4D8(0x0F03, 0x8A53);
    sub_877D();
}

/* $00:A6B4 — paint the PICTURE-PANEL caption strips (the alphabet test
 * pattern that appears below the picture).  Writes 16 caption rows
 * with sequential 18-byte sub-strings from $01:8A65..$01:8B07 — these
 * are TILE-INDEX strings (consecutive bytes $00..$1F, $40..$5F, etc)
 * used to visualize the encyclopedia's BG3 character set. */
static void enc_paint_picture_panel_A6B4(void)
{
    TILE_ATTR = 0x08;
    sub_877D();
    /* The strings at $01:8A65..$01:8B07 are each 18 bytes:
     *   $8A65: row of tiles $00..$0F
     *   $8A77: row of tiles $00..$0F  (palette-shift indices)
     *   $8A89: row of tiles $10..$1F
     *   $8A9B: " !\"#$%&'()*+,-./" (printable $20..$2F)
     *   $8AAD: "0123456789:;<=>?"  ($30..$3F)
     *   $8ABF: "@ABCDEFGHIJKLMNO"  ($40..$4F)
     *   $8AD1: "PQRSTUVWXYZ[\\]^_" ($50..$5F)
     *   $8AE3: "`abcdefghijklmno"  ($60..$6F)
     *   $8AF5: "pqrstuvwxyz{|}~"   ($70..$7F)
     *   $8B07..$8B3D: tile indices $80..$BF in 16-byte rows
     *   $8B4F: "Why are\nyou here?" — easter-egg debug string
     */
    for (uint8_t row = 3; row <= 12; ++row) {
        uint16_t src = 0x8A65 + (uint16_t)(row - 3) * 0x12;
        caption_blit_00C4D8((uint16_t)row << 8 | 0x03, src);
    }
}

/* $00:A608 — second pass that finishes the picture frame (caption
 * blits for rows $A..$F repaint atomically once the picture panel is
 * settled).  Body is a duplicate of A6B4's loop, kept for clarity. */
static void enc_paint_picture_panel_A608(void)
{
    enc_paint_picture_panel_A6B4();
}

/* $00:A519 — render-and-loop entry. */
static void encyclopedia_render(uint8_t page)
{
    enc_cursor_topic_list_A5E0();
    enc_fade_in_A5F5();
    sub_877D();
    encyclopedia_render_DFCD(page);
    enc_paint_picture_panel_A6B4();
    enc_paint_topic_list_A625();
    enc_paint_picture_panel_A608();
    ENC_SCROLL -= 4;
    /* Falls through to the input loop ($A535/$A4DE). */
}

/* ========================================================================
 * $00:A625 — input poll / cursor read helper.
 * Called once per loop iteration; reads the cursor entity (which is
 * spawned at state $2E setup) and writes dp[$28] when the cursor is
 * over a hit-rect AND a button is freshly pressed.
 *
 * The implementation lives in the entity-handler bank ($04:B90A for
 * type $2D); this stub just yields and points the caller at it. */
static void encyclopedia_input_A625(void)
{
    enc_cursor_full_screen_A5CB();
    sub_877D();
    /* The actual cursor + hit-test fires from the NMI scheduler in
     * the entity dispatcher: type-$2D handler walks rom_01_89C5 and
     * writes ENC_PICK when the cursor is inside a rect during a
     * fresh A or B press. */
}

/* ========================================================================
 * $00:A4DE — STATE $2F input loop body.
 * ======================================================================== */
static void encyclopedia_input_loop_A4DE(void)
{
    extern void sub_8629(void);  /* per-state common end                */
    for (;;) {
        sub_8629();
        ENC_PICK = 0xFF;                            /* signed -1: none */
        encyclopedia_input_A625();
        sub_877D();
        if ((int8_t)ENC_PICK >= 0) {
            unsigned i = ENC_PICK & 0x07;           /* mask 8 picks    */
            enc_pick_dispatch_A55F[i]();
            /* Most pick handlers fall back through to the main render
             * path and then re-enter this loop.  EXIT writes
             * dp[$0B]=$16 (title-screen) so the dispatcher leaves
             * the encyclopedia altogether. */
            if (dp[0x0B] == 0x16) return;
        }
    }
}

/* ========================================================================
 * TUTORIAL DATA MODEL
 * ========================================================================
 *
 * GS_TUTORIAL ($03 in dp[$0B]) at $00:ACE8 is just the "screen
 * settle" stub: hold 4 in-game seconds, fade out, advance.  The
 * actual tutorial works as an overlay on the gameplay states — each
 * tutorial step:
 *
 *   1. Sets dp[$72] (the script-index byte) to the next entry.
 *   2. Calls the painter at $00:E2A2.  That routine masks dp[$72]
 *      with $3F (max 64 entries; the table has 54), ASLs, and
 *      indexes into the 54-entry pointer table at $00:E2C2.  Each
 *      entry is a 16-bit pointer into bank $01 — one tutorial
 *      message.
 *   3. The painter calls $00:C91F to draw the message into the BG3
 *      tilemap-update queue at $0C00 (with column width A=$18 and
 *      origin X=$0706 = row 7, col 6).
 *   4. The caller game-state then runs its normal logic (player
 *      practices the lesson); when ready, advances to the next
 *      tutorial step.
 *
 * Total tutorial messages: 54 — exactly fills the table at $E2C2.
 * Each message ends in $FF and uses $FE for line breaks (same
 * grammar as the encyclopedia text).
 *
 * The first message (index 0) at $01:B27F is:
 *
 *      "Welcome to SimAnt!|Follow the instructions|on the screen
 *       to learn|how to be a pro|...|"
 *
 * Messages 1..53 progressively introduce: cursor + A button (1-7),
 * Surface Close-up + Overview views (5-9), foraging + eating (10-15),
 * carry-back-to-nest (16-19), pebble manipulation + walls (20-23),
 * Recruit Menu (24-26), Evaluation Screens / House map (27-32),
 * Behavior + Caste Control Panels (34-45), final attack-the-red-nest
 * scenario (46-52), and the "this is the end" wrap-up (53).
 * ======================================================================== */

#define TUTORIAL_INDEX  dp[0x0072]    /* current tutorial step    */

/* $00:E2A2 — paint the current tutorial message.  Caller sets dp[$72]
 * to the index, then JSRs here.  X/Y register state is unused — the
 * tile attribute byte at $002E is mirrored to $8C and the painter
 * runs from there. */
static void tutorial_paint_E2A2(void)
{
    TILE_ATTR = dp[0x002E];

    uint8_t idx = TUTORIAL_INDEX & 0x3F;            /* mask to table   */
    uint16_t text_ptr = rom_00_E2C2[idx];

    /* X=$0706 in the original: high byte = row 7, low byte = col 6.
     * The renderer writes to the BG3 update queue, then $00:8791
     * scrubs the sprite-flag for the displayed icon, and $00:87BC
     * flushes the queue. */
    text_render_00C91F(/*cols=*/0x18, /*origin=*/0x0706, text_ptr);
    sub_8791();
    sub_87BC();
}

/* $00:E280 — alternate caller that uses a SEPARATE script table at
 * $01:B18B (28 entries × 2 bytes).  Used by the IN-GAME hints (not
 * the boot tutorial).  Kept here for completeness. */
static void in_game_hint_E280(uint8_t hint_index)
{
    extern const uint16_t rom_01_B18B[];            /* in-game hint table */
    uint16_t text_ptr = rom_01_B18B[hint_index];
    extern void caption_at_default_pos_E259(uint16_t y);
    caption_at_default_pos_E259(text_ptr);
    extern void hint_blit_C4D8_wrapper(void);
    hint_blit_C4D8_wrapper();
}

/* $00:E28D — VIEW-CHANGE message dispatcher.  Reads dp[$0296] (the
 * "active view" byte; one of 0..5 per `enum ViewMode`) and indexes
 * into a 6-entry table at $01:B1D7 of "you are now viewing X" captions.
 *
 * The table $01:B1D7 has the six VIEW-name strings starting with
 * "Surface\nOverview", "B. Nest\nOverview", "R. Nest\nOverview",
 * "Surface\nClose-up", "B. Nest\nClose-up", "R. Nest\nClose-up". */
static void view_change_message_E28D(void)
{
    extern const uint16_t rom_01_B1D7[6];
    uint16_t text_ptr = rom_01_B1D7[dp[0x0296]];
    extern void caption_at_default_pos_E259(uint16_t y);
    caption_at_default_pos_E259(text_ptr);
    extern void hint_blit_C4D8_wrapper(void);
    hint_blit_C4D8_wrapper();
}

/* ========================================================================
 * Reference doc-tables (silenced as `used` so they're not stripped).
 * ======================================================================== */

/* Encyclopedia: 30-page text-pointer table (mirror of $01:C7F0).  Tied
 * here so that other .c files can pull the pointer without having to
 * traverse the page-table layout. */
__attribute__((used))
const uint16_t encyclopedia_page_text_C7F0[30] = {
    /* TOPIC 0  Introduction (pages 0..5):                                 */
    0xC82C, /*  0  "SimAnt is based on real ants. ..."                     */
    0xC93A, /*  1  "Ants are insects. They are the ..."                    */
    0xC9ED, /*  2  "The ant's head has a mouth, two antennae ..."          */
    0xCAB8, /*  3  "Ants have two big eyes ..."                            */
    0xCBC3, /*  4  "The middle part of the ant is the thorax ..."          */
    0xCCBF, /*  5  "The rear of the ant, the abdomen ..."                  */
    /* TOPIC 1  Ant Life (pages 6..16):                                    */
    0xCD5A, /*  6  "There are different types, or Castes, of ants ..."     */
    0xCF08, /*  7  "Workers are the smallest and most numerous ants ..."   */
    0xD016, /*  8  "Soldier ants are all females ..."                      */
    0xD0C4, /*  9  "Male ants have wings ..."                              */
    0xD147, /* 10  "Ants mostly communicate by smelling ..."               */
    0xD274, /* 11  "When ants find food, they need to let others know ..." */
    0xD33C, /* 12  "Ants are ruthless fighters ..."                        */
    0xD494, /* 13  "Some ants act like ranchers and keep 'herds' ..."      */
    0xD58A, /* 14  "One ant job is nursing ..."                            */
    0xD64A, /* 15  "Ants share food with each other ..."                   */
    0xD793, /* 16  "Some ants gather seeds and store them ..."             */
    /* TOPIC 2  Ants at Home (pages 17..19):                               */
    0xD7C9, /* 17  "Ants build their nests in many places ..."             */
    0xD90E, /* 18  "Ants have four life stages: egg, larva, pupa ..."      */
    0xDAF0, /* 19  "Ant colonies also have stages. ..."                    */
    /* TOPIC 3  Ants & Relatives (pages 20..25):                           */
    0xDCD8, /* 20  "Bees are related to ants ..."                          */
    0xDE3C, /* 21  "Wasps are closely related to ants ..."                 */
    0xDEE9, /* 22  "Termite colonies have a social structure ..."          */
    0xDFD2, /* 23  "Out of over 8000 species ... Weaver ant ..."           */
    0xE17F, /* 24  "African Army ants don't build nests ..."               */
    0xE2BD, /* 25  "Leafcutter Ants from South America ..."                */
    /* TOPIC 4  SimAnt Strategy (pages 26..29):                            */
    0xE4AC, /* 26  "The way to win SimAnt is to carefully manage ..."      */
    0xE699, /* 27  "To win the Full Game ..."                              */
    0xE822, /* 28  "The goal of the Scenario Game is to vanquish ..."      */
    0xE941, /* 29  "After you play SimAnt, go outside ..." (closer)        */
};

/* Tutorial: 54-entry pointer table (mirror of $00:E2C2).  Each entry
 * points into bank $01 at one of the 54 sequential messages between
 * $01:B27F and $01:C777. */
__attribute__((used))
const uint16_t tutorial_script_E2C2[54] = {
    0xB27F, 0xB332, 0xB399, 0xB41B, 0xB44B, 0xB4CC, 0xB4EF, 0xB556,
    0xB5B9, 0xB61C, 0xB6B3, 0xB713, 0xB77A, 0xB7FB, 0xB860, 0xB8DF,
    0xB944, 0xB993, 0xB9E8, 0xBA43, 0xBAA6, 0xBAE2, 0xBB54, 0xBBB5,
    0xBC13, 0xBC7D, 0xBCCD, 0xBD08, 0xBD51, 0xBD9C, 0xBDF0, 0xBE46,
    0xBEB9, 0xBFE5, 0xC019, 0xC057, 0xC0A4, 0xC138, 0xC1DD, 0xC259,
    0xC29C, 0xC2CC, 0xC311, 0xC35A, 0xC3E0, 0xC425, 0xC448, 0xC4AD,
    0xC4D6, 0xC561, 0xC5BA, 0xC670, 0xC69E, 0xC6E9,
};

/* ========================================================================
 * Doc-refs — keep symbols reachable for static analysis. */
__attribute__((used))
static const void * const _text_doc_refs[] = {
    (void const *)&encyclopedia_page_text_C7F0,
    (void const *)&tutorial_script_E2C2,
    (void const *)&topic_start_page,
    (void const *)&enc_pick_dispatch_A55F,
    (void const *)encyclopedia_render_DFCD,
    (void const *)encyclopedia_render,
    (void const *)encyclopedia_input_loop_A4DE,
    (void const *)encyclopedia_input_A625,
    (void const *)enc_cursor_topic_list_A5E0,
    (void const *)enc_cursor_full_screen_A5CB,
    (void const *)enc_fade_in_A5F5,
    (void const *)enc_paint_topic_list_A625,
    (void const *)enc_paint_picture_panel_A6B4,
    (void const *)enc_paint_picture_panel_A608,
    (void const *)enc_pick_intro_A56F,
    (void const *)enc_pick_life_A574,
    (void const *)enc_pick_home_A57B,
    (void const *)enc_pick_relatives_A582,
    (void const *)enc_pick_strategy_A589,
    (void const *)enc_pick_exit_A511,
    (void const *)enc_pick_next_A590,
    (void const *)enc_pick_prev_A5B0,
    (void const *)tutorial_paint_E2A2,
    (void const *)in_game_hint_E280,
    (void const *)view_change_message_E28D,
};

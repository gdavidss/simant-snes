/*
 * lifted_helpers_6.c — Batch 6: strong defs for the symbols still in
 * stubs.c. These cover the remaining bank-$00 VRAM/CGRAM uploaders,
 * fade-in/out helpers, text-emitters, joypad task yield, and the few
 * bank-$04 page-pulse routines.
 *
 *   $00:877D  cooperative yield (waits one NMI tick)
 *   $00:8791  poll joypad until any A/B/Start pressed
 *   $00:87B1  yield until $30 reaches $0600 (DMA fill done)
 *   $00:87BC  poll joypad until any button pressed; debounce
 *   $00:87DA  early-out if dp[$0071] (replay mode)
 *   $00:8611  apu reset + "fade-to-black" wrapper
 *   $00:8616  fade-to-black (0..15 INIDISP)
 *   $00:8629  fade-from-black with $8662 (BG-clear) per frame
 *   $00:8642  fade-from-black (full screen visible)
 *   $00:85FC  fade-up brightness (0..15 INIDISP)
 *   $00:8ACC  full 8KB DMA $7E:2000 -> VRAM at Y
 *   $00:8B0C  zero $7E:2000..$7E:9FFF, then stamp 16-byte counter
 *   $00:8D94  setup default cursor view bounds (1)
 *   $00:8DA5  setup default cursor view bounds (2)
 *   $00:8F4B  screen fade-out animation (vertical wipe)
 *   $00:8F74  screen fade-in animation (vertical wipe)
 *   $00:8AED  cgram_upload (lifted before; alias here)
 *   $00:BAF2  text render setup
 *   $00:BB38  reset bg misc
 *   $00:B4ED  vmem block init
 *   $00:B197  small TSR wrapper
 *   $00:B058  trampoline (JMP indirect)
 *   $00:C06C  intro screen setup (mode-switch + asset upload chain)
 *   $00:C243  setup entities at intro
 *   $00:C28A  House screen setup
 *   $00:C318  master "clear background" routine
 *   $00:DDD7  per-frame counter dispatch
 *   $00:EB58  decompress 64x128 tilemap to $7F:6000
 *   $00:CF05  nest scroll commit (lifted in render_helpers.c — alias)
 *   $00:D997  draw indirect (table jump on dp[$0F] mask)
 *   $00:9187  popup-screen entry point (variant)
 *   $00:B29D  composite draw of bnest/rnest panel
 *   $00:DDD7  per-frame counter dispatch
 *   Bank-$04 helpers: sub_499BB, sub_490D2..490E2, sub_4914F, sub_493EF
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
#define MMIO16(addr) (*(volatile uint16_t *)&mmio[(addr) & 0xFFFF])

#define INIDISP    MMIO8(0x2100)
#define BGMODE     MMIO8(0x2105)
#define OBSEL      MMIO8(0x2101)
#define BG12NBA    MMIO8(0x210B)
#define BG34NBA    MMIO8(0x210C)
#define VMADDL     MMIO16(0x2116)
#define VMDATAL    MMIO16(0x2118)
#define CGADD      MMIO8(0x2121)
#define APUIO0     MMIO8(0x2140)
#define APUIO1     MMIO8(0x2141)
#define APUIO2     MMIO8(0x2142)
#define HVBJOY     MMIO8(0x4212)
#define JOY1L      MMIO8(0x4218)
#define JOY1H      MMIO8(0x4219)
#define MDMAEN     MMIO8(0x420B)
#define DMA0CTRL   MMIO8(0x4300)
#define DMA0DEST   MMIO8(0x4301)
#define DMA0SRCL   MMIO16(0x4302)
#define DMA0SRCB   MMIO8(0x4304)
#define DMA0SIZE   MMIO16(0x4305)
#define W12SEL     MMIO8(0x2123)
#define W34SEL     MMIO8(0x2124)
#define WOBJSEL    MMIO8(0x2125)
#define TM         MMIO8(0x212C)
#define TS         MMIO8(0x212D)
#define TMW        MMIO8(0x212E)
#define TSW        MMIO8(0x212F)
#define CGADSUB    MMIO8(0x2131)
#define HDMAEN     MMIO8(0x420C)
#define M7SEL      MMIO8(0x211A)
#define COLDATA    MMIO8(0x2132)

extern void sub_8841(uint8_t a);
extern void sub_8887(void);
extern void sub_8C0C_blank_buffer(void); /* opaque */
extern void sub_8662_bg_blank(void);
extern void sub_E3FD_nmi_settle(void);
extern void sub_8AED(uint8_t a, uint16_t y);
extern void sub_8D7E(uint8_t a, uint16_t y);
extern void sub_88A5(void);
extern void sub_866E(uint16_t x, uint16_t a);
extern void sub_867F(uint8_t a, uint16_t x);
extern void sub_86A9(void);
extern void sub_8694(void);
extern void sub_C398(void);
extern void sub_8F08(uint8_t a);
extern void sub_8FAB(void);    /* fade helpers */
extern void sub_8FCB(void);
extern void sub_905A(void);
extern void sub_BC7F(void);
extern void sub_C318(void);
extern void entity_table_reset_0499BB(void);
extern void sub_499C1(uint16_t x, uint16_t y, uint8_t a);

__attribute__((weak)) void sub_8C0C_blank_buffer(void) {}
__attribute__((weak)) void sub_8662_bg_blank(void) {}
__attribute__((weak)) void sub_E3FD_nmi_settle(void) {}
__attribute__((weak)) void sub_88A5(void) {}
__attribute__((weak)) void sub_86A9(void) {}
__attribute__((weak)) void sub_8694(void) {}
__attribute__((weak)) void sub_8FAB(void) {}
__attribute__((weak)) void sub_8FCB(void) {}
__attribute__((weak)) void sub_905A(void) {}
__attribute__((weak)) void sub_BC7F(void) {}

/* -------------------------------------------------------------------------
 * $00:87DA — early-out if dp[$0071] (replay mode active)
 *   LDA $0071 ; BEQ $87E0 ; RTS ; (otherwise falls through into $87E0)
 *   We treat the not-replay path as a no-op return.
 * ------------------------------------------------------------------------- */
void sub_87DA(void) {
    if (WMEM8(0x0071) == 0) return;
    /* else: fall into $87E0 — opaque continuation */
}

/* -------------------------------------------------------------------------
 * $00:877D — cooperative yield (one NMI worth of "wait until tick changes")
 *   The ROM does: A=$00; CMP $00; BEQ; ... → busy-wait for the NMI to bump
 *   $00 (frame counter). On x86 we just iterate joypad.
 * ------------------------------------------------------------------------- */
void sub_877D(void) {
    sub_8887();          /* poll JOY1/JOY2 */
    sub_E3FD_nmi_settle();
    sub_87DA();
}

/* -------------------------------------------------------------------------
 * $00:8791 — poll until A/B/Start pressed
 * ------------------------------------------------------------------------- */
void sub_8791(void) {
    for (;;) {
        sub_877D();
        if (WMEM8(0x0071) == 0) {
            if (((WMEM8(0x0060) | WMEM8(0x0061)) & 0x80) != 0) return;
        } else {
            if (WMEM8(0x007D) != 0) return;
        }
    }
}

/* -------------------------------------------------------------------------
 * $00:87B1 — yield until $30 == $0600
 * ------------------------------------------------------------------------- */
void sub_87B1(void) {
    while (WMEM16(0x0030) != 0x0600) sub_877D();
}

/* -------------------------------------------------------------------------
 * $00:87BC — wait-for-button-RELEASE debounce primitive.
 *
 * Verified asm at $00:87BC (A1 audit found C polarity was inverted):
 *   JSR $877D                       ; cooperative yield
 *   LDA $0071 / BNE mouse_path
 *   LDA $4218 / ORA $4219            ; combine JOY1L/JOY1H
 *   BMI $87BC                        ; if bit 7 set (any button held), loop
 *   BRA done
 * mouse_path:
 *   LDA $007B / BNE $87BC            ; if mouse-button shadow set, loop
 * done:
 *   LDA #$FF / STA $28 / STA $29
 *   RTS
 *
 * Net: loop WHILE a button is held; exit when released. The C version
 * had the polarity inverted (broke on press instead of waiting for
 * release). Now fixed.
 * ------------------------------------------------------------------------- */
void sub_87BC(void) {
    for (;;) {
        sub_877D();
        if (WMEM8(0x0071) == 0) {
            if (((JOY1L | JOY1H) & 0x80) == 0) break;   /* released */
        } else {
            if (WMEM8(0x007B) == 0) break;              /* released */
        }
    }
    WMEM8(0x0028) = 0xFF;
    WMEM8(0x0029) = 0xFF;
}

/* -------------------------------------------------------------------------
 * $00:8611 / $8616 / $8629 / $8642 / $85FC — fade helpers
 * ------------------------------------------------------------------------- */
static void fade_down(void) {
    uint8_t v = 0x0F;
    do {
        INIDISP = v;
        sub_8841(0x02);
        --v;
    } while ((int8_t)v >= 0);
}

void sub_8611(void) {
    APUIO0 = 0xC4;       /* tell APU "screen-off" */
    fade_down();
}
void sub_8616(void) { fade_down(); }
void inidisp_off_fade_8616(void) { fade_down(); }
void reset_eval_8616(void) { fade_down(); }
/* sub_861A: enters fade_down loop mid-routine — caller supplies dp[$6C] */
void reset_eval_861A(void) {
    while ((int8_t)WMEM8(0x006C) >= 0) {
        INIDISP = WMEM8(0x006C);
        sub_8841(0x02);
        --WMEM8(0x006C);
    }
}

static void fade_up_with_clear(void) {
    uint8_t v = 0x0F;
    do {
        INIDISP = (uint8_t)(0x0F - v);
        sub_8662_bg_blank();
        sub_8841(0x02);
        --v;
    } while ((int8_t)v >= 0);
}

void sub_8629(void) { fade_up_with_clear(); }

void sub_8642(void) {
    APUIO0 = 0xC4;
    WMEM8(0x006C) = 0;
    do {
        INIDISP = (uint8_t)(0x0F - WMEM8(0x006C));
        sub_8662_bg_blank();
        sub_8841(0x02);
        WMEM8(0x006C) = (uint8_t)(WMEM8(0x006C) + 1);
    } while (WMEM8(0x006C) != 0x10);
}

void sub_85FC(void) {
    /* Fade up: brightness 0 -> $0F */
    WMEM8(0x006C) = 0;
    do {
        INIDISP = WMEM8(0x006C);
        sub_8841(0x02);
        WMEM8(0x006C) = (uint8_t)(WMEM8(0x006C) + 1);
    } while (WMEM8(0x006C) != 0x10);
}
void inidisp_on_fade_85FC(void) { sub_85FC(); }

/* -------------------------------------------------------------------------
 * $00:8ACC — DMA $7E:2000 -> VRAM (8KB)
 *   in: X = byte count, Y = VMADDL value
 * ------------------------------------------------------------------------- */
void sub_8ACC(uint16_t x, uint16_t y) {
    DMA0SIZE = x;
    VMADDL   = y;
    DMA0CTRL = 0x01;
    DMA0DEST = 0x18;
    DMA0SRCL = 0x2000;
    DMA0SRCB = 0x7E;
    MDMAEN   = 0x01;
}
void vram_dma_fill_8ACC(uint16_t x, uint16_t y)      { sub_8ACC(x, y); }
void vram_dma_from_scratch_8ACC(uint16_t x, uint16_t y) { sub_8ACC(x, y); }

/* -------------------------------------------------------------------------
 * $00:8B0C — zero $7E:2000..$7E:9FFF then stamp a 16-byte counter
 *   PARTIAL PORT: the zero-clear is faithful; the 16-byte counter stamp
 *   at the tail is omitted (caller treats this buffer as scratch in
 *   every current call site).
 * ------------------------------------------------------------------------- */
void sub_8B0C(void) {
    /* Clear 0x8000 bytes at $7E:2000 — note this is $7E:2000..$7E:9FFF
     * (the ROM body's exact range; $7E:A000+ is preserved). */
    for (uint16_t i = 0; i < 0x8000; i++) wram[(0x2000 + i) & 0x1FFFF] = 0;
    /* TODO: stamp the 16-byte counter pattern the ROM loop tail writes. */
}

/* -------------------------------------------------------------------------
 * $00:8D94 — set cursor bounds (variant 1)
 * ------------------------------------------------------------------------- */
void sub_8D94(void) {
    WMEM8(0x0016) = 0x00;
    WMEM8(0x0017) = 0xF0;
    WMEM8(0x0018) = 0x00;
    WMEM8(0x0019) = 0xD0;
}

/* -------------------------------------------------------------------------
 * $00:8DA5 — set cursor bounds (variant 2)
 * ------------------------------------------------------------------------- */
void sub_8DA5(void) {
    WMEM8(0x0016) = 0x00;
    WMEM8(0x0017) = 0xF0;
    WMEM8(0x0018) = 0x00;
    WMEM8(0x0019) = 0xB0;
}

/* -------------------------------------------------------------------------
 * $00:8F4B / $8F74 — wipe-out / wipe-in screen
 * ------------------------------------------------------------------------- */
void sub_8F4B(void) {
    sub_877D();
    sub_8FAB();
    WMEM16(0x009A) = 0x00B0;
    sub_905A();
    sub_877D();
    sub_8FCB();
    do {
        sub_877D();
        sub_905A();
        WMEM8(0x009A) = (uint8_t)(WMEM8(0x009A) - 0x02);
    } while (WMEM8(0x009A) != 0);
    INIDISP = 0x80;
}

void sub_8F74(void) {
    sub_877D();
    sub_8FAB();
    WMEM16(0x009A) = 0x0002;
    sub_905A();
    sub_877D();
    sub_8FCB();
    sub_877D();
    INIDISP = 0x0F;
    do {
        sub_877D();
        sub_905A();
        WMEM8(0x009A) = (uint8_t)(WMEM8(0x009A) + 0x02);
    } while (WMEM8(0x009A) != 0xB0);
    W12SEL = 0;
    W34SEL = 0;
    WOBJSEL = 0;
}

/* -------------------------------------------------------------------------
 * $00:BAF2 — text render setup
 *   asset_decompress($18, $FF8A) -> scratch, DMA(0x100, $6000),
 *   asset_decompress($07, $D5A6) -> scratch, DMA(0x800, $7400)
 * ------------------------------------------------------------------------- */
void sub_BAF2(void) {
    sub_8D7E(0x18, 0xFF8A);
    sub_8ACC(0x0100, 0x6000);
    sub_8D7E(0x07, 0xD5A6);
    sub_8ACC(0x0800, 0x7400);
}
void text_render_setup_BAF2(void) { sub_BAF2(); }

/* -------------------------------------------------------------------------
 * $00:BB38 — reset BG misc (mode-9 setup + entity-table reset)
 * ------------------------------------------------------------------------- */
extern void sub_490D2_block(void);
extern void sub_4911B_view(void);
extern void sub_490DB_unblock(void);
__attribute__((weak)) void sub_490D2_block(void) {}
__attribute__((weak)) void sub_4911B_view(void) {}
__attribute__((weak)) void sub_490DB_unblock(void) {}
void sub_BB38(void) {
    sub_C318();
    WMEM8(0x0002) = 0;
    WMEM8(0x0001) = 0;
    BGMODE = 0x09;
    sub_C398();
    BG12NBA = 0x60;
    BG34NBA = 0x02;
    OBSEL   = 0xA2;
    sub_8F08(0);
    WMEM8(0x0098) = 0x00;
    WMEM8(0x002B) = 0x03;
    WMEM8(0x002C) = 0x02;
    WMEM8(0x002D) = 0x01;
    sub_490D2_block();
    sub_4911B_view();
    sub_490DB_unblock();
    sub_8ACC(0x4000, 0x2000);
    sub_866E(0x7800, 0x2000);
}
void reset_bg_misc_BB38(void) { sub_BB38(); }

/* -------------------------------------------------------------------------
 * $00:B4ED — vmem block init (asset chain for the population graph)
 * ------------------------------------------------------------------------- */
void sub_B4ED(void) {
    sub_BB38();
    sub_8D7E(0x1B, 0xA1C4);
    sub_8ACC(0x4000, 0x0000);
    sub_8D7E(0x07, 0xEEAE);
    sub_8ACC(0x0800, 0x7000);
    sub_BAF2();
    sub_8D7E(0x1B, 0xBCA8);
    sub_8ACC(0x2000, 0x4000);
    sub_8AED(0x07, 0xAB80);
    sub_499C1(0, 0, 0x01);
}
void vmem_block_init_B4ED(void) { sub_B4ED(); }

/* -------------------------------------------------------------------------
 * $00:B197 — small TSR wrapper: DEX + JSR $8616 + INC $0B; RTS
 * ------------------------------------------------------------------------- */
void sub_B197(void) {
    /* DEX is on dp index — caller pre-set X */
    sub_8616();
    WMEM8(0x000B) = (uint8_t)(WMEM8(0x000B) + 1);
}

/* -------------------------------------------------------------------------
 * $00:B058 — trampoline: ADC dp[$89] (immediate stride) then JMP indirect
 * The disassembly looks like data — leave as inert RTS for now.
 * ------------------------------------------------------------------------- */
void sub_B058(void) {}

/* -------------------------------------------------------------------------
 * $00:B29D — composite draw of B/R-nest panel
 * The disassembly head ("STZ $A9BA" prefix) is the data-table-aliased
 * prologue; real entry is the JSL $0499C1 call sequence.
 * ------------------------------------------------------------------------- */
extern void sub_BA9E_wait_sec(uint8_t a);
void sub_B29D_composite_draw(void) {
    sub_499C1(0, 0, 0x6A);
    sub_BA9E_wait_sec(0x02);
    sub_8616();
    WMEM8(0x000B) = (uint8_t)(WMEM8(0x000B) + 1);
}

/* -------------------------------------------------------------------------
 * $00:C06C — intro screen setup
 *   This entry primes BGMODE=$39, OBSEL=$62, sub_8F08 base, decompresses
 *   3 asset chunks (graphics/title/sprite tiles) and DMAs them into VRAM.
 * ------------------------------------------------------------------------- */
void sub_C06C(void) {
    sub_C318();
    WMEM8(0x02B2) = WMEM8(0x02B1);
    APUIO1 = 0;
    BGMODE = 0x39;
    sub_C398();
    OBSEL  = 0x62;
    sub_8F08(0);
    /* JSR $895B (VMAIN=$80) — perform here too */
    MMIO8(0x2115) = 0x80;
    WMEM8(0x002F) = 0x28;
    WMEM8(0x008C) = 0x2C;
    WMEM8(0x002E) = 0x2C;
    WMEM8(0x0089) = 0x78;
    sub_8D7E(0x10, 0xC57D);
    sub_8ACC(0x2000, 0x0000);
    sub_8D7E(0x16, 0xA16F);
    sub_8ACC(0x2000, 0x4000);
    sub_8D7E(0x10, 0xA590);
    sub_8ACC(0x2000, 0x3000);
}

/* -------------------------------------------------------------------------
 * $00:C243 — setup entities at intro
 *   Resets entity table, spawns the initial 5 entities and the cursor.
 * ------------------------------------------------------------------------- */
void sub_C243(void) {
    entity_table_reset_0499BB();
    sub_499C1(0, 0, 0x02);
    WMEM8(0x0049) = 0;
    WMEM8(0x004B) = 0x18;
    /* JSR $C439 — opaque downstream */
    if (WMEM8(0x02B1) == 0) {
        sub_499C1(0, 0, 0x03);
        sub_499C1(0, 0, 0x1F);
        sub_499C1(0, 0, 0x49);
        sub_499C1(0, 0, 0x4A);
        sub_499C1(0, 0, 0x47);
    } else {
        sub_499C1(0, 0, 0x04);
        sub_499C1(0, 0, 0x05);
    }
}

/* -------------------------------------------------------------------------
 * $00:C28A — House (overworld) screen setup
 * ------------------------------------------------------------------------- */
void sub_C28A(void) {
    sub_C318();
    BGMODE = 0x09;
    sub_C398();
    OBSEL  = 0x62;
    sub_8F08(0);
    WMEM8(0x002F) = 0x28;
    WMEM8(0x008C) = 0x2C;
    WMEM8(0x002E) = 0x2C;
    WMEM8(0x0089) = 0x78;
    WMEM16(0x008A) = 0x00E0;
    sub_8D7E(0x10, 0x8000);
    sub_8ACC(0x2000, 0x0000);
    sub_867F(0x00, 0x7000);
    sub_8D7E(0x16, 0xA16F);
    sub_8ACC(0x2000, 0x4000);
    sub_8D7E(0x17, 0xEE4F);
    sub_8ACC(0x2000, 0x3000);
}

/* -------------------------------------------------------------------------
 * $00:C318 — master "clear background" routine
 * ------------------------------------------------------------------------- */
void sub_C318(void) {
    APUIO2 = 0;
    entity_table_reset_0499BB();
    sub_88A5();
    WMEM16(0x029B) = 0; WMEM16(0x029D) = 0;
    WMEM16(0x029F) = 0; WMEM16(0x02A1) = 0;
    WMEM16(0x02A3) = 0; WMEM16(0x02A5) = 0;
    sub_86A9();
    sub_8694();
    sub_867F(0x00, 0x7800);
    WMEM16(0x002C) = 0;
    WMEM16(0x002E) = 0;
    WMEM8(0x02B5) = 0;
    TM = 0x17;
    TS = 0;
    W12SEL = 0; W34SEL = 0; WOBJSEL = 0;
    TMW = 0; TSW = 0;
    CGADSUB = 0;
    MDMAEN = 0; HDMAEN = 0;
    M7SEL = 0;
    COLDATA = 0xE0;
    COLDATA = 0xFF;  /* second write — combined into the next? */
}

/* -------------------------------------------------------------------------
 * $00:DDD7 — per-frame counter dispatch (uses dp[$0052] as event idx)
 *   The JMP indirect ($DDEB,X) routes to one of several handlers based
 *   on the doubled counter. We don't unwind the table here.
 * ------------------------------------------------------------------------- */
void sub_DDD7(void) {
    WMEM8(0x008C) = WMEM8(0x002E);
    WMEM8(0x0052) = (uint8_t)(WMEM8(0x0052) + 1);
    /* dispatcher jump — left as no-op */
}

/* -------------------------------------------------------------------------
 * $00:EB58 — decompress 64x128 tilemap to $7F:6000.   *** PORT STUB ***
 *   Body shape is faithful (64x128 grid walk; dp[$79]/[$7C] long ptrs
 *   built from $04:FF00..FF50), but the LZSS source tables are not
 *   linked in, so every output cell is written as zero. Until the
 *   real $04:FF00 tables are extracted, every caller sees an all-zero
 *   tilemap at $7F:6000.
 * ------------------------------------------------------------------------- */
void sub_EB58(void) {
    uint8_t idx = WMEM8(0x0296);
    /* far reads from $04:FF00, $04:FF30: we approximate as zero */
    WMEM8(0x007B) = 0;
    WMEM8(0x007E) = 0;
    uint16_t a = (uint16_t)(idx << 1);
    (void)a;
    WMEM16(0x0079) = 0;
    WMEM16(0x007C) = 0;
    WMEM8(0x0096) = 0;
    WMEM8(0x0095) = 0;
    /* Inner loop: 64 rows of 128 bytes — copy nothing as we don't have
     * the source tables. Caller writes the destination $7F:6000 buffer. */
    for (uint16_t r = 0; r < 0x40; r++) {
        for (uint16_t c = 0; c < 0x80; c++) {
            wram[(0x6000 + r * 0x80 + c) & 0x1FFFF] = 0;
        }
    }
}

/* -------------------------------------------------------------------------
 * $00:CF05 — nest scroll commit (alias — strong def in render_helpers.c).
 * Provide a weak fallback only.
 * ------------------------------------------------------------------------- */
__attribute__((weak)) void sub_CF05_redraw_nest(void) {}

/* -------------------------------------------------------------------------
 * $00:D997 — draw indirect (table jump on bits of dp[X+$0F])
 * The jump table at $D9A8 dispatches into one of 8 draw entries.
 * ------------------------------------------------------------------------- */
void sub_D997_draw_indirect(uint16_t x) { (void)x; }

/* -------------------------------------------------------------------------
 * $00:9187 — popup-screen entry point (variant).
 * The variant takes a special arg-pack on stack.
 * ------------------------------------------------------------------------- */
void sub_9187_popup(void) {}

/* -------------------------------------------------------------------------
 * Bank-04 small helpers used by lots of bank-04 entry points.
 * ------------------------------------------------------------------------- */
void sub_499BB(void) {
    /* LDY #$0600 ; STY $30 ; RTL */
    WMEM16(0x0030) = 0x0600;
}

/* DBR push/pop for bank-04 calls: $04:90D2 pushes DBR to dp[$D2],
 * then loads $7E into DB. $04:90DB restores.
 *
 * NOTE: the C model has no DBR register, so we approximate by storing
 * the bank where this helper lives ($04) instead of the caller's actual
 * DBR. Every caller in this port resides in bank $04, so the value is
 * correct in practice; if a future bank-$XX caller is lifted, this stub
 * will need a parameterized form. */
void sub_0490D2(void) { WMEM8(0x00D2) = 0x04; /* stand-in for current DBR */ }
void sub_0490DB(void) { /* restore DBR from $D2 — no-op for us */ }
void sub_490D2 (void) { sub_0490D2(); }
void sub_490DB (void) { sub_0490DB(); }

/* $04:90E0 — split A's bits into 8 bytes ($E5..$EC) where each is 0xFF
 * or 0x00 depending on the bit being set. */
void sub_490E0(uint8_t a) {
    WMEM8(0x00E4) = a;
    for (int i = 0; i < 8; i++) {
        WMEM8(0x00E5 + i) = 0;
        if (a & 1) WMEM8(0x00E5 + i) = (uint8_t)(WMEM8(0x00E5 + i) - 1);
        a >>= 1;
    }
}
void sub_490E2(uint8_t a) {
    /* same as 490E0 minus the leading STA $E4 */
    for (int i = 0; i < 8; i++) {
        WMEM8(0x00E5 + i) = 0;
        if (a & 1) WMEM8(0x00E5 + i) = (uint8_t)(WMEM8(0x00E5 + i) - 1);
        a >>= 1;
    }
}

/* $04:911B / $04:914F / $04:93EF — these dispatch on dp[$98] via JMP
 * (table). We expose them as no-ops; callers can set up dp[$FD] first. */
void sub_4911B(void) {}
void sub_4914F(void) {}
void sub_493EF(void) {}
void sub_49617(void) {}
/* Also bank-prefixed variants */
void sub_04911B(void) {}
void sub_0490DB_alt(void) {}
void sub_0490D2_alt(void) {}
void sub_049000(void)   {}
void sub_0490DB_void(void){}
void sub_0490DB_dup(void){}
void sub_0490D2_dup(void){}
void sub_0490D2_v(void) {}
void sub_0490DB_v(void) {}
void sub_049000_v(void) {}

/* -------------------------------------------------------------------------
 * Random helpers — bank $02 functions.
 *
 * $02:F3BD — "rand modulo".
 *     STA $E71A           ; modulus argument
 *     JSL $02F3EF         ; advance RNG state at $E710..$E716 (Fibonacci-style)
 *     LDA $E710 / STA $E71E
 *     JSL $02F420         ; 16x16 hardware multiply -> $E720..$E725
 *     LDA $E724           ; high byte of (rand16 * modulus) -> result in [0..mod)
 *     CLC / RTL
 *
 * $02:F3EF — RNG state update.  Four-byte additive state $E710..$E716,
 *     a Fibonacci-style lagged-add generator (each byte = sum of next ones).
 *
 * $02:F420 — 8x8 -> 16-bit hardware multiply pyramid producing a 16-bit
 *     product at $E722:$E723 of (E71A * E71E + low-cross terms).  The high
 *     byte ($E724) is the modulo result via the "scale-down" trick.
 *
 * We lift the state machine faithfully so the sequence is deterministic;
 * it's an independent stream from the canonical $04:DCD5 rng_byte LCG.
 * ------------------------------------------------------------------------- */
static uint8_t fib_E710 = 0x12;
static uint8_t fib_E712 = 0x34;
static uint8_t fib_E714 = 0x56;
static uint8_t fib_E716 = 0x78;

static uint8_t rng_F3EF_advance(void) {
    /* F3EF:
     *   $E718 = $E716       (saved)
     *   $E72A = $E716
     *   $E716 = $E714
     *   $E72A += $E714      (now $E72A = old_E716 + E714)
     *   $E714 = $E712
     *   $E72A += $E712
     *   $E712 = $E710
     *   $E72A += $E710
     *   $E710 = $E72A
     */
    uint8_t acc = fib_E716;
    fib_E716 = fib_E714;
    acc = (uint8_t)(acc + fib_E714);
    fib_E714 = fib_E712;
    acc = (uint8_t)(acc + fib_E712);
    fib_E712 = fib_E710;
    acc = (uint8_t)(acc + fib_E710);
    fib_E710 = acc;
    return acc; /* new $E710 */
}

/* B1 audit fix: signature was uint8_t(uint8_t) but every extern across
 * combat.c / simulation.c / territory.c / mechanics_extra.c declared it
 * uint16_t(uint16_t). The ABI mismatch silently truncated bounds >= 256
 * to zero, breaking the 1/512 engagement gate at combat.c:493 (fights
 * ALWAYS triggered). Now matches the canonical 16-bit signature.
 *
 * ROM at $02:F3BD takes a 16-bit bound, advances a Fibonacci PRNG once,
 * then multiplies bound × rnd16 via the hardware-multiply chain at
 * $02:F420 and returns the high 16 bits. For 8-bit bounds this reduces
 * to (bound * fib_E710) >> 8. For 16-bit bounds the ROM reuses the
 * Fibonacci-state pair (fib_E712 << 8 | fib_E710) as a 16-bit rnd
 * without a second advance.
 */
uint16_t rand_modulo_F3BD(uint16_t bound) {
    if (bound == 0) return 0;
    (void)rng_F3EF_advance();
    if (bound <= 0xFF) {
        return (uint16_t)(((uint16_t)bound * (uint16_t)fib_E710) >> 8);
    }
    uint16_t rnd16 = ((uint16_t)fib_E712 << 8) | (uint16_t)fib_E710;
    return (uint16_t)(((uint32_t)bound * (uint32_t)rnd16) >> 16);
}

int8_t rand_signed_F38D(uint8_t a) {
    /* Helper now widens to the 16-bit canonical form; result fits in 8-bit
     * because callers only pass 8-bit bounds. */
    uint16_t v = rand_modulo_F3BD((uint16_t)a);
    return (int8_t)(uint8_t)v;
}

/* -------------------------------------------------------------------------
 * Caption-blit alias.
 * ------------------------------------------------------------------------- */
void task_yield_BA9E(uint8_t a) {
    sub_BA9E_wait_sec(a);
}

/* -------------------------------------------------------------------------
 * Aliases to fulfil generated stubs.
 * ------------------------------------------------------------------------- */
void cgram_upload_8AED(uint8_t a, uint16_t y) { sub_8AED(a, y); }
void asset_decompress_8D7E(uint8_t a, uint16_t y) { sub_8D7E(a, y); }
void reset_eval_859E(void) {
    /* Disassembly head is data — the real entry is the partial DMA write
     * sequence at $85A8 (REP/SEP + MDMAEN+RTL). */
    MDMAEN = 0x01;
}

/* -------------------------------------------------------------------------
 * The text-border helpers — these all return immediately if dp[$30] is 0,
 * otherwise emit the border tile pattern. We just bail on the !=0 path.
 * ------------------------------------------------------------------------- */
void text_border_top_C844(void)    { if (WMEM8(0x0030) == 0) return; }
void text_border_sides_C88D(void)  { if (WMEM8(0x0030) == 0) return; }
void text_border_bottom_C8D6(void) { if (WMEM8(0x0030) == 0) return; }

/* text_emit_tile_word_C4BB — bank 0 helper at $C4BB.  Strong def already
 * in lifted_helpers_3.c (sub_C4BB). Alias here. */
extern void sub_C4BB(void);
void text_emit_tile_word_C4BB(void) { sub_C4BB(); }

void text_print_BACA(uint8_t a) {
    extern void sub_BAD3(void);
    sub_BAD3();
    sub_BA9E_wait_sec(a);
}

void view_setup_C90D2(void)   { sub_0490D2(); }
void view_setup_C90DB(void)   { sub_0490DB(); }
void view_setup_C9911B(void)  { sub_4911B(); }
void view_restore_00A0F4(void){
    /* same body as sub_A0F4 */
    extern void sub_A0F4(void);
    sub_A0F4();
}

/* -------------------------------------------------------------------------
 * Stubs that we can't lift but need to be linked (dispatchers and
 * deep-state helpers; many are weak-aliased to a no-op).
 * ------------------------------------------------------------------------- */
void action_schedule_03D10D(void)     {}
void apu_sfx_play_02F65A(uint8_t a)   { (void)a; }
void dig_kernel_03B7A7(void)          {}
void kill_dispatcher_03D334(void)     {}
void kill_resolver_02C379(void)       {}
void menu_dispatcher_009187(void)     {}
void scent_dir_dx_028065(void)        {}
void scent_dir_dy_028077(void)        {}
void scent_dir_from_to_0298ED(void)   {}
void scent_turn_smooth_02AAC7(void)   {}
void scent_wander_random_02AA51(void) {}
void state12_load_897D(void)          {}
void state12_load_8B98(void)          {}
void state14_open_assets_8976(void)   {}
void tile_event_handler_03E1DC(void)  {}
void sprite_morph_check_B3F5(void)    {}
void starvation_tick_D89B(void)       {}
void cgram_shadow(void)               {}
__attribute__((weak)) const uint8_t rom[] = {0};

/* -------------------------------------------------------------------------
 * Weak ROM data tables for symbols referenced by lifted code but whose
 * actual byte contents we haven't extracted from the cartridge yet.
 * The 0x200-byte sizing matches gen_stubs.py's earlier placeholders.
 * Any module that DOES extract the real table provides a strong def
 * (which wins the linker tiebreak).
 * ------------------------------------------------------------------------- */
__attribute__((weak)) const uint8_t landing_pick_table[0x200] = {0};
__attribute__((weak)) const uint8_t nest_close_substates[0x200] = {0};
__attribute__((weak)) const uint8_t surface_closeup_table[0x200] = {0};
__attribute__((weak)) const uint8_t surface_overview_decorations[0x200] = {0};
__attribute__((weak)) const uint8_t rom_00_E2C2[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_8143[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_817B[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_81B3[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_81D3[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_86D3[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_996F[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_999F[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_99FF[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_9A0F[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_9A2F[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_9A3F[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_9A5F[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_9A6F[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_9C10[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_B18B[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_B1D7[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_C778[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_C796[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_C7B4[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01_C7F0[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01F218[0x200] = {0};
__attribute__((weak)) const uint8_t rom_01F308[0x200] = {0};
__attribute__((weak)) const uint8_t rom_B070[0x200] = {0};
__attribute__((weak)) const uint8_t rom_B080[0x200] = {0};
__attribute__((weak)) const uint8_t rom_B088[0x200] = {0};
__attribute__((weak)) const uint8_t rom_B28D[0x200] = {0};
__attribute__((weak)) const uint8_t rom_B295[0x200] = {0};
__attribute__((weak)) const uint8_t rom_F198[0x200] = {0};
__attribute__((weak)) const uint8_t rom_F1A8[0x200] = {0};
__attribute__((weak)) const uint8_t rom_F1B8[0x200] = {0};
__attribute__((weak)) const uint8_t rom_F1E8[0x200] = {0};

/*
 * assets.c — SimAnt (SNES, 1993) graphics-asset map.
 *
 * Source ROM: /Users/guilhermedavid/simant-re/simant.sfc (1,048,576 bytes,
 * standard LoROM, no header).
 *
 * SCOPE
 * =====
 * This file is the *index* of every graphics blob the title-screen state
 * machine and the in-game view state machine reach for. It is NOT the actual
 * pixel data — the pixels are still living in simant.sfc.  Instead we list:
 *
 *   - WHERE the blob lives (24-bit SNES address: bank:offset, plus the
 *     equivalent ROM file offset).
 *   - HOW it's loaded (LZSS-decompressed via $00:8D7E -> $7E:2000 -> VRAM DMA,
 *     OR raw CGRAM DMA via $00:8AED / $00:8AF3).
 *   - WHAT it becomes (VRAM destination + DMA byte-count, OR CGRAM size).
 *   - WHICH state handler owns the call site.
 *   - THE COMPRESSED *AND* UNCOMPRESSED SIZE of each blob (verified by
 *     re-running the LZSS decoder in asset_extract.py).
 *
 * COVERAGE
 * ========
 *   - 53 distinct (bank, offset) blob references found across:
 *       states_menu.c     (10 menu/title state handlers)
 *       states_gameplay.c (25+ gameplay/view state handlers, incl. nest CU)
 *       simant.c          (the two boot-time helpers: GS_FULL_GAME + B07B)
 *   - 10 of those are RAW palette/CGRAM blobs (sub_8AED / sub_8AF3).
 *   - 43 are LZSS-compressed; all decompress cleanly to the header's
 *     declared uncompressed length.
 *   - Plus 16 entries in the per-view ROM dispatch tables at $01:996F..
 *     $01:9A6F (the 16 view-mode "scenery packs" used by the 8 scenarios
 *     + 8 Full-Game view variants).
 *
 * Totals (from `python3 asset_extract.py`):
 *   - LZSS bytes (compressed)    : 114,025  (covers state-handler chains)
 *   - LZSS bytes (uncompressed)  : 299,200
 *   - Raw CGRAM bytes            :   2,880
 *   - Per-view tile compressed   : 151,294  (26 unique blobs, 3 per view × 16 views)
 *   - Per-view tile uncompressed : 212,992  (16 KB per view, since each view
 *                                            decompresses 3 × $2000-byte chunks)
 *
 * Combined: ROM banks $07 (palettes/tilemaps), $10-$1E (BG tiles, scenery,
 * sprite chr) hold all observable graphics. Banks $00-$06 are CODE and lookup
 * tables.  Bank $1F is mostly unused by graphics (it holds menu/text
 * fragments — see $1E:FA24 below for the canonical text-tile graphics).
 *
 * ROM-SIZE BREAKDOWN (approximate)
 * ===============================
 *   Bank      Bytes    Purpose
 *   --------  -------  --------------------------------------------------
 *   $00-$06   458 752  65816 code (state handlers, helpers, dispatcher,
 *                        per-state asset chains, save/load serializer).
 *   $07       65 536  Mixed: CGRAM palettes ($8000-$86FF area) + 0x800-
 *                        byte LZSS palette/tilemap chunks ($B380-$E7FF).
 *   $08-$0F   524 288  SPC700 driver + audio tables + scenario danger
 *                        handlers + bank-$08 asset loader + entity logic.
 *   $10-$1E   720 896  Compressed BG tiles, scenery, sprite chr, tilemaps,
 *                        landing/map/scent tilemaps.
 *   $1F       65 536  Mostly unused / small text fragments at $1E:FA24
 *                        (text-tile graphics for $0800 bytes).
 *
 * THE 8 SCENARIOS
 * ===============
 * Per scenarios.c, the 8 Scenario Game levels are dispatched by view mode
 * (0..15). Manual order:  L1=Park, L2=Garden, L3=Yard, L4=House, L5=Road,
 * L6=River, L7=Porch, L8=Woods. The dispatch table at $01:996F (counts)
 * and $01:999F (src pointers) gives each view 3 × $2000-byte tile blobs.
 *
 *   Scenario             View   Tile blobs (bank:ofs)            Unique
 *   -------------------  ----   -------------------------------  ------
 *   L6 / River           1      $11:A8F4, $11:BA9B, $11:D095     yes
 *   L7 / Porch           3      $12:ADCB, $12:C662, $12:DD8B     yes
 *   L4 / House           4      $12:F53A, $12:FE51, $13:8F82     yes (re-used)
 *   L5 / Road            5      $13:A0CF, $13:B9A8, $13:CDC8     yes
 *   L1 / Park            6      $13:E9B5, $14:83E9, $14:97F0     yes
 *   L2 / Garden          7      $14:A947, $14:C363, $14:DEFE     yes
 *   L3 / Yard            8      $15:F0BC, $16:8886, $16:8886     yes (with dup)
 *   L8 / Woods           10     $15:F0BC, $16:8886, $16:8886     same as Yard!
 *
 *   (Yard, Woods, and Full-Game variant view 9 all share the same 3 blobs.
 *   That's how SimAnt fits 16 view-modes into 26 unique tile blobs — view 9
 *   ≡ view 10 graphics; views 11-15 alias view 4.)
 *
 * VRAM LAYOUT (typical view setup, e.g. state $1B view-switch)
 * ============================================================
 *   VRAM $0000-$3FFF   BG1 character tiles  (16 KB)
 *   VRAM $3000-$5FFF   BG3/sprite character tiles
 *   VRAM $4000-$5FFF   shared BG tile pack ($16:A16F)
 *   VRAM $5000-$6FFF   per-view extra BG ($15:E9E0)
 *   VRAM $6000-$7FFF   sprite character tiles ($10:8AE3)
 *   VRAM $7000-$73FF   BG1 tilemap (decompressed from $07:xxxx palette/map)
 *   VRAM $7400-$77FF   BG2 tilemap (fill or palette)
 *   VRAM $7800-$7FFF   BG3 tilemap
 *
 * CGRAM is uploaded SEPARATELY via sub_8AED ($00:8AED) for 0x200-byte full
 * palettes, or sub_8AF3 ($00:8AF3) for partial palette regions. The CGRAM
 * sources are RAW (no LZSS header); see the "cgr" entries in asset_table[].
 *
 * THE 6 VIEW-SPECIFIC PALETTES (B.Nest / R.Nest / Surface per scenario)
 * ====================================================================
 * The per-view BG palette dispatch table $01:9A5F (count) / $01:9A6F (src)
 * gives each of the 16 view modes a BG palette. The dispatch table $01:99FF /
 * $01:9A0F gives a per-view SPRITE palette pair A (0x80 bytes — 4 sub-
 * palettes). And $01:9A2F / $01:9A3F gives a per-view SPRITE palette pair B
 * (0x40 bytes). See per_view_bg_palette_*, per_view_spr_palette_*, and
 * per_view_spr2_palette_* below.
 *
 * The dedicated B.Nest / R.Nest close-up palettes are NOT in the per-view
 * tables — they're hard-coded into state $24/$26 setup:
 *   $07:B380  B.Nest close-up palette/tilemap chunk (0x800 LZSS)
 *   $07:B671  R.Nest close-up palette/tilemap chunk (0x800 LZSS)
 *   $07:B975  shared nest close-up palette          (0x800 LZSS, -> VRAM $7400)
 *   $07:9000  B.Nest sprite palette                 (raw CGRAM 0x200)
 *   $07:9200  R.Nest sprite palette                 (raw CGRAM 0x200)
 *
 * THE LZSS BITSTREAM FORMAT
 * =========================
 * (Re-statement of the decoder lifted into simant.c at $03:8467.)
 *
 *   Header (4 bytes):
 *     [0..1]  uncompressed length (16-bit, little-endian)
 *     [2..3]  reserved (skipped by the decoder)
 *
 *   Body: a sequence of "groups". Each group:
 *     1 control byte, then up to 8 sub-units (one per bit, MSB-first).
 *       bit=0  -> emit 1 literal byte (read 1 byte from source)
 *       bit=1  -> back-reference (read 2 bytes from source, little-endian
 *                  pair B):
 *                    offset_back = (B >> 4) & 0x0FFF    (1..4096 back)
 *                    length      = (B & 0x0F) + 3       (3..18 bytes)
 *                  Copy `length` bytes from out[out_pos-1-offset_back]
 *                  forward to out[out_pos] (overlap is allowed and is the
 *                  trick that makes RLE patterns compress well).
 *
 *   Compression ratios observed in this ROM: ~1.4x (already-tiny tilemap)
 *   to ~8.1x (mostly-zero palette block at $07:D5A6).
 *
 * Verify:
 *   cd /Users/guilhermedavid/simant-re &&
 *   clang -Wall -Wextra -c assets.c -o /tmp/a.o
 */

#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * Asset-entry record + load-kind tag.
 * ======================================================================== */
typedef enum {
    LOAD_LZSS_VRAM = 0,   /* sub_8D7E -> scratch -> sub_8ACC -> VRAM       */
    LOAD_RAW_CGRAM = 1,   /* sub_8AED (full pal) or sub_8AF3 (partial)     */
} AssetKind;

typedef struct {
    uint8_t   bank;          /* SNES bank (high byte of 24-bit addr)      */
    uint16_t  src_ofs;       /* SNES offset in bank ($8000..$FFFF)        */
    uint32_t  rom_off;       /* equivalent flat ROM-file offset           */
    uint16_t  comp_size;     /* compressed size in ROM (LZSS) / raw bytes */
    uint16_t  uncomp_size;   /* uncompressed size; same as comp for raw   */
    uint16_t  dma_dest;      /* VRAM word-addr OR CGRAM start; 0 if none  */
    uint16_t  dma_len;       /* DMA byte-count (0 if unknown)             */
    AssetKind kind;
    const char *owner_state;
    const char *purpose;
} AssetEntry;

/* ========================================================================
 * MASTER ASSET TABLE — every (bank, ofs) referenced by a state handler.
 *
 * Verified at build time of this file by running asset_extract.py against
 * the ROM (which decompresses each LZSS blob and confirms the header
 * length matches the actual output size).
 *
 * Layout sort: by ROM bank ascending, then by src_ofs ascending.
 * ======================================================================== */
static const AssetEntry asset_table[] = {
    /* ---- Bank $07 — RAW CGRAM palettes ($00:8AED / $00:8AF3) ---- */
    { 0x07, 0x8000, 0x38000, 0x0200, 0x0200, 0x0000, 0x0200, LOAD_RAW_CGRAM,
      "1B view-switch landing",   "BG palette (full 256-color)" },
    { 0x07, 0x8400, 0x38400, 0x0200, 0x0200, 0x0000, 0x0200, LOAD_RAW_CGRAM,
      "GS_FULL_GAME @ACF3",       "256-color title palette" },
    { 0x07, 0x8600, 0x38600, 0x0020, 0x0020, 0x0000, 0x0020, LOAD_RAW_CGRAM,
      "$1D surface overview",     "CGRAM partial #1 (BG)" },
    { 0x07, 0x8620, 0x38620, 0x0020, 0x0020, 0x0010, 0x0020, LOAD_RAW_CGRAM,
      "$1D surface overview",     "CGRAM partial #2 (BG)" },
    { 0x07, 0x8640, 0x38640, 0x0040, 0x0040, 0x0020, 0x0040, LOAD_RAW_CGRAM,
      "$1D surface overview",     "CGRAM partial #3 (BG)" },
    { 0x07, 0x8680, 0x38680, 0x0060, 0x0060, 0x0080, 0x0060, LOAD_RAW_CGRAM,
      "$1D surface overview",     "sprite sub-palette A" },
    { 0x07, 0x86E0, 0x386E0, 0x0060, 0x0060, 0x00C0, 0x0060, LOAD_RAW_CGRAM,
      "$1D surface overview",     "sprite sub-palette B" },
    { 0x07, 0x9F80, 0x39F80, 0x0200, 0x0200, 0x0000, 0x0200, LOAD_RAW_CGRAM,
      "GS_SAVED_GAME @AC63",      "saved-game 256-color palette" },
    { 0x07, 0xA180, 0x3A180, 0x0200, 0x0200, 0x0000, 0x0200, LOAD_RAW_CGRAM,
      "GS_FULL_END @B07B / $0A",  "credits 256-color palette" },
    { 0x07, 0xA380, 0x3A380, 0x0200, 0x0200, 0x0000, 0x0200, LOAD_RAW_CGRAM,
      "screen_template_B1D2",     "ant-info 256-color palette" },

    /* ---- Bank $07 — LZSS-compressed palette/tilemap chunks ---- */
    { 0x07, 0xB380, 0x3B380, 0x02F1, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "state $24 (B.Nest CU)",    "B.Nest CU palette + tilemap" },
    { 0x07, 0xB671, 0x3B671, 0x0304, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "state $26 (R.Nest CU)",    "R.Nest CU palette + tilemap" },
    { 0x07, 0xB975, 0x3B975, 0x05B9, 0x0800, 0x7400, 0x0800, LOAD_LZSS_VRAM,
      "state $24/$26 nest CU",    "shared nest CU palette" },
    { 0x07, 0xBF2E, 0x3BF2E, 0x014C, 0x0800, 0x7400, 0x0800, LOAD_LZSS_VRAM,
      "state $2A sound options",  "sound-options palette/tilemap" },
    { 0x07, 0xC865, 0x3C865, 0x01D2, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "state $2C scent display",  "RED scent palette" },
    { 0x07, 0xCA37, 0x3CA37, 0x0197, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "state $2C scent display",  "BLACK scent palette" },
    { 0x07, 0xCBCE, 0x3CBCE, 0x019D, 0x0800, 0x7400, 0x0800, LOAD_LZSS_VRAM,
      "state $2C scent display",  "scent palette B (red overlay)" },
    { 0x07, 0xCD6B, 0x3CD6B, 0x019D, 0x0800, 0x7400, 0x0800, LOAD_LZSS_VRAM,
      "state $2C scent display",  "scent palette B (black overlay)" },
    { 0x07, 0xD035, 0x3D035, 0x0571, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "state $2E landing pick",   "landing-screen palette" },
    { 0x07, 0xD5A6, 0x3D5A6, 0x00FC, 0x0800, 0x7400, 0x0800, LOAD_LZSS_VRAM,
      "load_end_secondary_BAF2",  "FULL_END secondary palette" },
    { 0x07, 0xD79E, 0x3D79E, 0x044F, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "GS_SAVED_GAME / state $0C", "saved-game caption tilemap" },
    { 0x07, 0xE070, 0x3E070, 0x02C9, 0x0800, 0x7800, 0x0800, LOAD_LZSS_VRAM,
      "GS_SAVED_GAME / state $0C", "saved-game extra tilemap" },
    { 0x07, 0xE339, 0x3E339, 0x03B0, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "GS_FULL_END / state $0A",  "credits caption tilemap" },
    { 0x07, 0xE6E9, 0x3E6E9, 0x04C9, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "screen_template_B1D2",     "ant-info caption tilemap" },

    /* ---- Bank $10 — sprite tile graphics (chr data, 8x8 4bpp) ---- */
    { 0x10, 0x8000, 0x80000, 0x0AE3, 0x2000, 0x6000, 0x2000, LOAD_LZSS_VRAM,
      "GS_FULL_GAME @ACF3",       "sprite tile chr (title)" },
    /* Same blob, re-aimed to VRAM $3000 by state $1B view-switch. */
    { 0x10, 0x8AE3, 0x80AE3, 0x08C1, 0x1000, 0x6000, 0x1000, LOAD_LZSS_VRAM,
      "states $24/$26/$2A/$2C",   "shared sprite chr (small block)" },
    { 0x10, 0x93A4, 0x813A4, 0x11EC, 0x2000, 0x3000, 0x2000, LOAD_LZSS_VRAM,
      "$1D surface overview",     "additional sprite chr" },

    /* ---- Bank $15 — extra BG tile data ---- */
    { 0x15, 0xE9E0, 0xAE9E0, 0x06DC, 0x2000, 0x5000, 0x2000, LOAD_LZSS_VRAM,
      "$1B view-switch landing",  "BG tile data (landing screen)" },

    /* ---- Bank $16 — BG tile graphics / tilemaps ---- */
    { 0x16, 0x9D63, 0xB1D63, 0x040C, 0x0D00, 0x0000, 0x2000, LOAD_LZSS_VRAM,
      "GS_FULL_GAME @ACF3",       "title BG1 tile graphics" },
    { 0x16, 0xA16F, 0xB216F, 0x0CCC, 0x2000, 0x4000, 0x2000, LOAD_LZSS_VRAM,
      "$1B/$1D view setup",       "shared BG tile pack" },
    { 0x16, 0xCBF3, 0xB4BF3, 0x0977, 0x2000, 0x4000, 0x2000, LOAD_LZSS_VRAM,
      "GS_SAVED_GAME / $0E",      "save-game BG2 tilemap / mode-7 init" },
    { 0x16, 0xD56A, 0xB556A, 0x0E07, 0x2000, 0x0000, 0x0000, LOAD_LZSS_VRAM,
      "state $0E marriage flight", "mode-7 secondary tiles" },
    { 0x16, 0xE371, 0xB6371, 0x13FB, 0x4000, 0x0000, 0x4000, LOAD_LZSS_VRAM,
      "state $24/$26 nest CU",    "nest CU BG tiles (16 KB)" },
    { 0x16, 0xF76C, 0xB776C, 0x061D, 0x13E0, 0x3000, 0x2000, LOAD_LZSS_VRAM,
      "state $24/$26 nest CU",    "nest CU BG tilemap" },
    { 0x16, 0xFD89, 0xB7D89, 0x0163, 0x0500, 0x5000, 0x2000, LOAD_LZSS_VRAM,
      "state $2A sound options",  "sound-options BG tiles" },
    { 0x16, 0xFEEC, 0xB7EEC, 0x1043, 0x2000, 0x4000, 0x2000, LOAD_LZSS_VRAM,
      "state $24/$2A",            "shared BG tilemap (B-Nest/sound)" },

    /* ---- Bank $17 — labels / scent tilemaps / landing ---- */
    { 0x17, 0x8F2F, 0xB8F2F, 0x110F, 0x2000, 0x4000, 0x0000, LOAD_LZSS_VRAM,
      "state $24 (B.Nest)",       "B.Nest label tiles" },
    { 0x17, 0xA03E, 0xBA03E, 0x11AB, 0x2000, 0x4000, 0x2000, LOAD_LZSS_VRAM,
      "state $26 (R.Nest)",       "R.Nest tilemap" },
    { 0x17, 0xB1E9, 0xBB1E9, 0x0F55, 0x2000, 0x4000, 0x0000, LOAD_LZSS_VRAM,
      "state $26 (R.Nest)",       "R.Nest label tiles" },
    { 0x17, 0xEE4F, 0xBEE4F, 0x05AA, 0x2000, 0x3000, 0x2000, LOAD_LZSS_VRAM,
      "state $2C scent display",  "scent tilemap B" },
    { 0x17, 0xF3F9, 0xBF3F9, 0x05D4, 0x2000, 0x0000, 0x2000, LOAD_LZSS_VRAM,
      "state $2C scent display",  "scent tilemap A" },
    { 0x17, 0xF9CD, 0xBF9CD, 0x248C, 0x45C0, 0x0000, 0x4000, LOAD_LZSS_VRAM,
      "state $2E landing pick",   "landing-screen BG tiles" },

    /* ---- Bank $18 — end-screen + save-menu BG ---- */
    { 0x18, 0xFF8A, 0xC7F8A, 0x0013, 0x0060, 0x6000, 0x0100, LOAD_LZSS_VRAM,
      "load_end_secondary_BAF2",  "FULL_END secondary tile patch" },
    { 0x18, 0xFF9E, 0xC7F9E, 0x2A2B, 0x3520, 0x0000, 0x4000, LOAD_LZSS_VRAM,
      "GS_SAVED_GAME / $0C",      "saved-game BG tiles" },

    /* ---- Bank $19 — extra BG ---- */
    { 0x19, 0xA9C9, 0xCA9C9, 0x0666, 0x1000, 0x2000, 0x2000, LOAD_LZSS_VRAM,
      "GS_SAVED_GAME / $0C",      "saved-game extra BG" },
    { 0x19, 0xFC44, 0xCFC44, 0x0A1E, 0x14A0, 0x0000, 0x2000, LOAD_LZSS_VRAM,
      "GS_FULL_END / $0A",        "credits BG tiles" },

    /* ---- Bank $1A — credits, marriage flight, ant-info BG ---- */
    { 0x1A, 0x8662, 0xD0662, 0x0A2F, 0x2000, 0x4000, 0x2000, LOAD_LZSS_VRAM,
      "GS_FULL_END / B1D2",       "shared end-screen BG" },
    { 0x1A, 0x9091, 0xD1091, 0x0E9F, 0x2000, 0x5000, 0x2000, LOAD_LZSS_VRAM,
      "GS_FULL_END @B07B",        "credits BG tiles B" },
    { 0x1A, 0x9F31, 0xD1F31, 0x18B2, 0x2300, 0x0000, 0x4000, LOAD_LZSS_VRAM,
      "screen_template_B1D2",     "ant-info BG tiles" },
    { 0x1A, 0xB7E3, 0xD37E3, 0x0A78, 0x2000, 0x4000, 0x2000, LOAD_LZSS_VRAM,
      "state $0A credits cont",   "credits scroll BG" },
    { 0x1A, 0xC25C, 0xD425C, 0x1B28, 0x8000, 0x0000, 0x8000, LOAD_LZSS_VRAM,
      "state $0E marriage flight", "mode-7 BG tiles (32 KB)" },

    /* ---- Bank $1B — map overlay (mode-7 minimap) ---- */
    { 0x1B, 0x8447, 0xD8447, 0x1D7D, 0x8000, 0x0000, 0x8000, LOAD_LZSS_VRAM,
      "state $12 map overlay",    "mode-7 minimap BG (32 KB)" },

    /* ---- Bank $1E — text/caption tile graphics ---- */
    { 0x1E, 0xFA24, 0xF7A24, 0x01D6, 0x0800, 0x7000, 0x0800, LOAD_LZSS_VRAM,
      "GS_FULL_GAME @ACF3",       "BG3/text tile graphics" },
};

#define ASSET_TABLE_COUNT (sizeof(asset_table) / sizeof(asset_table[0]))

/* ========================================================================
 * PER-VIEW DISPATCH TABLES (decoded from bank $01 of the ROM).
 *
 * Each row keys on dp[$0296] (current view mode, 0..15). The 8 scenarios
 * use view modes 1, 5, 10, 3, 4, 8, 6, 7 (River, Road, Woods, Porch,
 * House, Yard, Park, Garden respectively). Views 0, 2, 9, 11-15 are
 * "Full Game" variant slots.
 *
 * sub_8AF3(bank, src, len)  =  raw CGRAM DMA of `len` bytes starting at
 * the current CGRAM address pointer ($2121).
 *
 * The TILE blobs are LZSS — three $2000-byte chunks per view, decompressed
 * one at a time into successive $7E:2000+i*0x2000 scratch slots, then DMA'd
 * to VRAM $0000 in one $6000-byte DMA.
 * ======================================================================== */

/* $01:996F — per-view tile decompress count (=source bank) (16 × 3 entries).
 * Indexed as: bank = per_view_tile_bank[view][chunk]. */
static const uint8_t per_view_tile_bank[16][3] = {
    /* view  0 (Full Game) */ { 0x10, 0x10, 0x11 },
    /* view  1 / L6 River  */ { 0x11, 0x11, 0x11 },
    /* view  2 (Full Game) */ { 0x11, 0x11, 0x12 },
    /* view  3 / L7 Porch  */ { 0x12, 0x12, 0x12 },
    /* view  4 / L4 House  */ { 0x12, 0x12, 0x13 },
    /* view  5 / L5 Road   */ { 0x13, 0x13, 0x13 },
    /* view  6 / L1 Park   */ { 0x13, 0x14, 0x14 },
    /* view  7 / L2 Garden */ { 0x14, 0x14, 0x14 },
    /* view  8 / L3 Yard   */ { 0x15, 0x16, 0x16 },
    /* view  9 (Full Game) */ { 0x15, 0x16, 0x16 },
    /* view 10 / L8 Woods  */ { 0x15, 0x16, 0x16 },
    /* view 11 (variant)   */ { 0x12, 0x12, 0x13 },
    /* view 12 (variant)   */ { 0x12, 0x12, 0x13 },
    /* view 13 (variant)   */ { 0x12, 0x12, 0x13 },
    /* view 14 (variant)   */ { 0x12, 0x12, 0x13 },
    /* view 15 (variant)   */ { 0x12, 0x12, 0x13 },
};

/* $01:999F — per-view tile decompress src offset (16 × 3 entries). */
static const uint16_t per_view_tile_src[16][3] = {
    /* view  0 */ { 0xD58D, 0xF369, 0x8EFC },
    /* view  1 */ { 0xA8F4, 0xBA9B, 0xD095 },
    /* view  2 */ { 0xE565, 0xFB86, 0x9747 },
    /* view  3 */ { 0xADCB, 0xC662, 0xDD8B },
    /* view  4 */ { 0xF53A, 0xFE51, 0x8F82 },
    /* view  5 */ { 0xA0CF, 0xB9A8, 0xCDC8 },
    /* view  6 */ { 0xE9B5, 0x83E9, 0x97F0 },
    /* view  7 */ { 0xA947, 0xC363, 0xDEFE },
    /* view  8 */ { 0xF0BC, 0x8886, 0x8886 },
    /* view  9 */ { 0xF0BC, 0x8886, 0x8886 },
    /* view 10 */ { 0xF0BC, 0x8886, 0x8886 },
    /* view 11 */ { 0xF53A, 0xFE51, 0x8F82 },
    /* view 12 */ { 0xF53A, 0xFE51, 0x8F82 },
    /* view 13 */ { 0xF53A, 0xFE51, 0x8F82 },
    /* view 14 */ { 0xF53A, 0xFE51, 0x8F82 },
    /* view 15 */ { 0xF53A, 0xFE51, 0x8F82 },
};

/* $01:9A5F (bank) / $01:9A6F (src) — per-view BG palette (LZSS, $2000 bytes
 * uncompressed, decompressed then DMA'd to VRAM $5000 by state $1D). */
static const uint8_t  per_view_bg_palette_bank[16] = {
    0x14, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
    0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
};
static const uint16_t per_view_bg_palette_src [16] = {
    0xFA85, 0x8927, 0x9776, 0xA2B1, 0xACF1, 0xBBA8, 0xCCAB, 0xD897,
    0xE0F2, 0xE0F2, 0xE0F2, 0xACF1, 0xACF1, 0xACF1, 0xACF1, 0xACF1,
};

/* $01:99FF (bank) / $01:9A0F (src) — per-view sprite palette pair A
 * (0x80 bytes RAW CGRAM, via sub_8AF3). All entries are bank $07. */
static const uint8_t  per_view_spr_palette_bank[16] = {
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
};
static const uint16_t per_view_spr_palette_src [16] = {
    0x8740, 0x87C0, 0x8840, 0x88C0, 0x8940, 0x89C0, 0x8A40, 0x8AC0,
    0x8B40, 0x8B40, 0x8B40, 0x8940, 0x8940, 0x8940, 0x8940, 0x8940,
};

/* $01:9A2F (bank) / $01:9A3F (src) — per-view sprite palette pair B
 * (0x40 bytes RAW CGRAM, via sub_8AF3). All entries are bank $07. */
static const uint8_t  per_view_spr2_palette_bank[16] = {
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
};
static const uint16_t per_view_spr2_palette_src [16] = {
    0x8BC0, 0x8C00, 0x8C40, 0x8C80, 0x8CC0, 0x8D00, 0x8D40, 0x8D80,
    0x8DC0, 0x8DC0, 0x8DC0, 0x8CC0, 0x8CC0, 0x8CC0, 0x8CC0, 0x8CC0,
};

/* ========================================================================
 * SAMPLE PALETTE — first 16 colours of the GS_FULL_GAME title palette at
 * $07:8400. Format: SNES 5-5-5 (R in low 5 bits, G in mid 5, B in high 5).
 * The "#RRGGBB" comment is an 8-bit-per-channel approximation (5-bit value
 * left-shifted 3 = times 8) for sanity-checking the colour wheel.
 *
 * Read RAW (no LZSS) because this address is passed to sub_8AED.
 * ======================================================================== */
static const uint16_t example_palette[16] = {
    0x0000,  /* color  0: R= 0 G= 0 B= 0   -> #000000  (transparent / BG)  */
    0x001F,  /* color  1: R=31 G= 0 B= 0   -> #F80000  (pure red)          */
    0x4E73,  /* color  2: R=19 G=19 B=19   -> #989898  (mid grey)          */
    0x03FF,  /* color  3: R=31 G=31 B= 0   -> #F8F800  (yellow)            */
    0x294A,  /* color  4: R=10 G=10 B=10   -> #505050  (dark grey)         */
    0x7D80,  /* color  5: R= 0 G=12 B=31   -> #0060F8  (blue)              */
    0x4E73,  /* color  6: R=19 G=19 B=19   -> #989898  (mid grey, dup)     */
    0x7FFF,  /* color  7: R=31 G=31 B=31   -> #F8F8F8  (white)             */
    0x294A,  /* color  8: R=10 G=10 B=10   -> #505050  (dark grey, dup)    */
    0x03FF,  /* color  9: R=31 G=31 B= 0   -> #F8F800  (yellow, dup)       */
    0x4E73,  /* color 10: R=19 G=19 B=19   -> #989898  (mid grey, dup)     */
    0x7FFF,  /* color 11: R=31 G=31 B=31   -> #F8F8F8  (white, dup)        */
    0x294A,  /* color 12: R=10 G=10 B=10   -> #505050  (dark grey, dup)    */
    0x77BD,  /* color 13: R=29 G=29 B=29   -> #E8E8E8  (near-white)        */
    0x0000,  /* color 14: R= 0 G= 0 B= 0   -> #000000  (transparent, dup)  */
    0x7FFF,  /* color 15: R=31 G=31 B=31   -> #F8F8F8  (white, dup)        */
};

/* ========================================================================
 * SUMMARY STATISTICS — derived from the data above; intended as
 * documentation only (not consumed by other translation units).
 * ======================================================================== */
typedef struct {
    uint32_t total_entries;
    uint32_t lzss_entries;
    uint32_t raw_cgram_entries;
    uint32_t per_view_tile_entries;   /* 16 views * 3 chunks                */
    uint32_t per_view_palette_entries;/* 16 views * 3 palette tables       */

    uint32_t lzss_compressed_bytes;
    uint32_t lzss_uncompressed_bytes;
    uint32_t raw_cgram_bytes;
    uint32_t per_view_tile_compressed;
    uint32_t per_view_tile_uncompressed;
} AssetStats;

static const AssetStats asset_stats = {
    .total_entries             = ASSET_TABLE_COUNT,
    .lzss_entries              = 43,
    .raw_cgram_entries         = 10,
    .per_view_tile_entries     = 16 * 3,
    .per_view_palette_entries  = 16 * 3,

    .lzss_compressed_bytes     = 114025,
    .lzss_uncompressed_bytes   = 299200,
    .raw_cgram_bytes           = 2880,
    .per_view_tile_compressed  = 151294,
    .per_view_tile_uncompressed = 212992,
};

/* Keep the per-view tables referenced so -Wunused-variable doesn't fire. */
__attribute__((used))
static const void *const _keep_alive[] = {
    asset_table,
    per_view_tile_bank, per_view_tile_src,
    per_view_bg_palette_bank, per_view_bg_palette_src,
    per_view_spr_palette_bank, per_view_spr_palette_src,
    per_view_spr2_palette_bank, per_view_spr2_palette_src,
    example_palette,
    &asset_stats,
};

/* Export a single getter so the tables can be reached from elsewhere. */
const AssetEntry *asset_table_get(size_t i, size_t *count_out)
{
    if (count_out) *count_out = ASSET_TABLE_COUNT;
    if (i >= ASSET_TABLE_COUNT) return NULL;
    return &asset_table[i];
}

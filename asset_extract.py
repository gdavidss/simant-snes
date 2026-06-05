#!/usr/bin/env python3
"""
Extract SimAnt SNES ROM asset map from state handlers.

The ROM at simant.sfc is 1 MB LoROM. Banks $00-$3F (and mirrors $80-$BF) map
to ROM offsets:  rom_offset = ((bank & 0x7F) * 0x8000) + (addr - 0x8000)
where addr is in $8000..$FFFF (LoROM upper-half mapping).

Each asset blob is LZSS-compressed:
  Header (4 bytes):
    [0..1]  uncompressed length (little-endian)
    [2..3]  reserved (skipped by decompressor)
  Body: 9-byte groups
    1 control byte (MSB-first)
      bit=0 -> 1 literal byte
      bit=1 -> 2-byte back-reference: B = lo|hi<<8
                 offset = (B>>4) & 0xFFF   (back from current out_pos-1)
                 length = (B & 0xF) + 3
"""
import os, struct, sys

ROM = open("/Users/guilhermedavid/simant-re/simant.sfc", "rb").read()
ROM_SIZE = len(ROM)
assert ROM_SIZE == 0x100000, f"expected 1MB, got 0x{ROM_SIZE:X}"

def lorom(bank, addr):
    """LoROM bank:addr -> ROM file offset.  addr in $8000..$FFFF, bank low 7 bits."""
    bank &= 0x7F
    if not (0x8000 <= addr <= 0xFFFF):
        return None
    return (bank * 0x8000) + (addr - 0x8000)

def lz_decompress(rom, offset, max_out=0x10000):
    """Decompress LZSS blob at rom[offset]. Returns (compressed_len, uncompressed_data)."""
    if offset is None or offset + 4 > len(rom):
        return None, None
    out_len = rom[offset] | (rom[offset+1] << 8)
    if out_len == 0 or out_len > max_out:
        return None, None
    src = offset + 4
    out = bytearray()
    try:
        while len(out) < out_len:
            if src >= len(rom):
                return None, None
            ctrl = rom[src]; src += 1
            for bit in range(8):
                if len(out) >= out_len:
                    break
                if (ctrl & 0x80) == 0:
                    if src >= len(rom):
                        return None, None
                    out.append(rom[src]); src += 1
                else:
                    if src + 2 > len(rom):
                        return None, None
                    b = rom[src] | (rom[src+1] << 8); src += 2
                    off = (b >> 4) & 0xFFF
                    length = (b & 0xF) + 3
                    sp = len(out) - 1 - off
                    for i in range(length):
                        if len(out) >= out_len:
                            break
                        if sp < 0:
                            out.append(0)
                        else:
                            out.append(out[sp])
                        sp += 1
                ctrl = (ctrl << 1) & 0xFF
    except Exception as e:
        return None, None
    return (src - offset), bytes(out)

# All distinct (bank, src_ofs) pairs from state handlers,
# tagged with (vram_dst, dma_length, purpose-guess, owner-state, kind).
# "kind" = "lz"  -> via sub_8D7E (LZSS-decompressed to $7E:2000 then DMA'd)
#         "cgr" -> via sub_8AED / sub_8AF3 (RAW DMA straight to CGRAM, NO decompress)
# Compiled from grep of states_menu.c + states_gameplay.c + simant.c.
ASSET_REFS = [
    # (bank, ofs, vram_dst, dma_len, owner_state, purpose, kind)
    # ==== bank $07 — RAW palettes (via sub_8AED/8AF3, NO LZSS) ====
    (0x07, 0x8000, 0x0000, 0x0200, "1B view-switch-landing", "BG palette",                "cgr"),
    (0x07, 0x8400, 0x0000, 0x0200, "GS_FULL_GAME @ACF3",     "256-color palette",         "cgr"),
    (0x07, 0x8600, None,   0x0020, "1D surface OV",          "CGRAM partial #1",          "cgr"),
    (0x07, 0x8620, None,   0x0020, "1D surface OV",          "CGRAM partial #2",          "cgr"),
    (0x07, 0x8640, None,   0x0040, "1D surface OV",          "CGRAM partial #3",          "cgr"),
    (0x07, 0x8680, None,   0x0060, "1D surface OV",          "CGRAM sprite #1",           "cgr"),
    (0x07, 0x86E0, None,   0x0060, "1D surface OV",          "CGRAM sprite #2",           "cgr"),
    (0x07, 0x9F80, 0x0000, 0x0200, "GS_SAVED_GAME @AC63",    "saved-game palette",        "cgr"),
    (0x07, 0xA180, 0x0000, 0x0200, "GS_FULL_END @B07B/$0A",  "credits palette",           "cgr"),
    (0x07, 0xA380, 0x0000, 0x0200, "screen_template_B1D2",   "ant-info palette",          "cgr"),
    # ==== bank $07 — LZSS palettes/tilemaps (via sub_8D7E + sub_8ACC into VRAM $7000-) ====
    (0x07, 0xB380, None,   0x0800, "state $24 (B.Nest CU)",  "B.Nest palette/tilemap",    "lz"),
    (0x07, 0xB671, None,   0x0800, "state $26 (R.Nest CU)",  "R.Nest palette/tilemap",    "lz"),
    (0x07, 0xB975, 0x7400, 0x0800, "state $24/$26 (nest CU)", "shared nest palette",      "lz"),
    (0x07, 0xBF2E, 0x7400, 0x0800, "state $2A (sound opts)", "sound-options palette",     "lz"),
    (0x07, 0xC865, 0x7000, 0x0800, "state $2C (scent)",      "RED scent palette",         "lz"),
    (0x07, 0xCA37, 0x7000, 0x0800, "state $2C (scent)",      "BLACK scent palette",       "lz"),
    (0x07, 0xCBCE, 0x7400, 0x0800, "state $2C (scent)",      "scent palette B (red)",     "lz"),
    (0x07, 0xCD6B, 0x7400, 0x0800, "state $2C (scent)",      "scent palette B (black)",   "lz"),
    (0x07, 0xD035, 0x7000, 0x0800, "state $2E landing pick", "landing palette",           "lz"),
    (0x07, 0xD5A6, 0x7400, 0x0800, "load_end_secondary_BAF2","FULL_END secondary palette","lz"),
    (0x07, 0xD79E, 0x7000, 0x0800, "GS_SAVED_GAME / state $0C","saved-game caption",      "lz"),
    (0x07, 0xE070, 0x7800, 0x0800, "GS_SAVED_GAME / state $0C","saved-game extra tilemap","lz"),
    (0x07, 0xE339, 0x7000, 0x0800, "GS_FULL_END / state $0A","credits caption",           "lz"),
    (0x07, 0xE6E9, 0x7000, 0x0800, "screen_template_B1D2",   "ant-info caption",          "lz"),
    # ==== bank $10 — sprite tile graphics (LZSS) ====
    (0x10, 0x8000, 0x6000, 0x2000, "GS_FULL_GAME @ACF3",     "BG3 sprite tiles",          "lz"),
    (0x10, 0x8000, 0x3000, 0x2000, "1B view-switch-landing", "BG3 sprite tiles (alias)",  "lz"),
    (0x10, 0x8AE3, 0x6000, 0x2000, "1B view-switch-landing", "BG3 sprite tiles B",        "lz"),
    (0x10, 0x8AE3, 0x6000, 0x1000, "state $24/$26/$2A/$2C",  "BG3 sprite tiles (small)",  "lz"),
    (0x10, 0x8AE3, 0x3000, 0x2000, "1D surface OV",          "BG3 sprite tiles (variant)","lz"),
    (0x10, 0x93A4, 0x3000, 0x2000, "1D surface OV",          "BG3 sprite tiles C",        "lz"),
    # ==== bank $15 — BG tiles ====
    (0x15, 0xE9E0, 0x5000, 0x2000, "1B view-switch-landing", "BG tile data",              "lz"),
    # ==== bank $16 — BG tiles / tilemaps ====
    (0x16, 0x9D63, 0x0000, 0x2000, "GS_FULL_GAME @ACF3",     "title BG1 tile graphics",   "lz"),
    (0x16, 0xA16F, 0x4000, 0x2000, "1B/1D view setup",       "shared BG tile pack",       "lz"),
    (0x16, 0xCBF3, 0x4000, 0x2000, "GS_SAVED_GAME / state $0E", "save-game BG2 tilemap",  "lz"),
    (0x16, 0xD56A, None,   None,   "state $0E marriage flight","mode-7 secondary",        "lz"),
    (0x16, 0xE371, 0x0000, 0x4000, "state $24/$26 nest CU",  "nest CU BG tiles",          "lz"),
    (0x16, 0xF76C, 0x3000, 0x2000, "state $24/$26 nest CU",  "nest CU BG tilemap",        "lz"),
    (0x16, 0xFD89, 0x5000, 0x2000, "state $2A sound opts",   "sound-options BG tiles",    "lz"),
    (0x16, 0xFEEC, 0x4000, 0x2000, "state $24/$2A",          "shared tilemap",            "lz"),
    # ==== bank $17 — nest labels / scent maps / landing ====
    (0x17, 0x8F2F, None,   None,   "state $24 (B.Nest)",     "B.Nest labels",             "lz"),
    (0x17, 0xA03E, 0x4000, 0x2000, "state $26 (R.Nest)",     "R.Nest tilemap",            "lz"),
    (0x17, 0xB1E9, None,   None,   "state $26 (R.Nest)",     "R.Nest labels",             "lz"),
    (0x17, 0xEE4F, 0x3000, 0x2000, "state $2C scent display","scent tilemap B",           "lz"),
    (0x17, 0xF3F9, 0x0000, 0x2000, "state $2C scent display","scent tilemap A",           "lz"),
    (0x17, 0xF9CD, 0x0000, 0x4000, "state $2E landing pick", "landing BG tiles",          "lz"),
    # ==== bank $18 — credit BG tiles + save-menu BG ====
    (0x18, 0xFF8A, 0x6000, 0x0100, "load_end_secondary_BAF2","FULL_END secondary patch",  "lz"),
    (0x18, 0xFF9E, 0x0000, 0x4000, "GS_SAVED_GAME / state $0C","saved-game BG tiles",     "lz"),
    # ==== bank $19 — extra BG ====
    (0x19, 0xA9C9, 0x2000, 0x2000, "GS_SAVED_GAME / state $0C","saved-game extra BG",     "lz"),
    (0x19, 0xFC44, 0x0000, 0x2000, "GS_FULL_END / state $0A","credits BG tiles",          "lz"),
    # ==== bank $1A — credit & marriage-flight & ant-info BG ====
    (0x1A, 0x8662, 0x4000, 0x2000, "GS_FULL_END / B1D2",     "shared end-screen BG",      "lz"),
    (0x1A, 0x9091, 0x5000, 0x2000, "GS_FULL_END @B07B",      "credits BG tiles B",        "lz"),
    (0x1A, 0x9F31, 0x0000, 0x4000, "screen_template_B1D2",   "ant-info BG tiles",         "lz"),
    (0x1A, 0xB7E3, 0x4000, 0x2000, "state $0A credits cont", "credits scroll BG",         "lz"),
    (0x1A, 0xC25C, 0x0000, 0x8000, "state $0E marriage flight","mode-7 BG tiles (huge)",  "lz"),
    # ==== bank $1B — map overlay ====
    (0x1B, 0x8447, None,   None,   "state $12 map overlay",  "mode-7 map BG",             "lz"),
    # ==== bank $1E — text/caption tiles ====
    (0x1E, 0xFA24, 0x7000, 0x0800, "GS_FULL_GAME @ACF3",     "BG3/text tile graphics",    "lz"),
]

print(f"# Total asset references: {len(ASSET_REFS)}")

# Distinct compressed blobs (key by (bank, ofs))
distinct = {}
for entry in ASSET_REFS:
    key = (entry[0], entry[1])
    if key not in distinct:
        distinct[key] = entry

print(f"# Distinct blobs: {len(distinct)}")

# Validate each blob — LZSS ones get decompressed, CGRAM ones are read raw.
results = []
total_compressed = 0
total_uncompressed = 0
total_cgram_raw = 0
fail_count = 0
for (bank, ofs), entry in sorted(distinct.items()):
    kind = entry[6]
    dma_len = entry[3]
    rom_off = lorom(bank, ofs)
    if rom_off is None:
        results.append((bank, ofs, rom_off, 0, 0, "BAD ADDR", kind))
        fail_count += 1
        continue
    if rom_off >= len(ROM):
        results.append((bank, ofs, rom_off, 0, 0, "OOB", kind))
        fail_count += 1
        continue
    if kind == "cgr":
        # Raw CGRAM — dma_len bytes straight from ROM, no header
        results.append((bank, ofs, rom_off, dma_len, dma_len, "RAW", kind))
        total_cgram_raw += dma_len
        continue
    # LZSS-compressed; decompress and check length
    out_len = ROM[rom_off] | (ROM[rom_off+1] << 8)
    comp_len, data = lz_decompress(ROM, rom_off, max_out=0x10000)
    if data is None:
        results.append((bank, ofs, rom_off, out_len, 0, "DECOMPRESS FAIL", kind))
        fail_count += 1
        continue
    actual_out = len(data)
    if actual_out != out_len:
        status = f"LEN MISMATCH ({actual_out} vs {out_len})"
    else:
        status = "OK"
    results.append((bank, ofs, rom_off, out_len, comp_len, status, kind))
    if status == "OK":
        total_compressed += comp_len
        total_uncompressed += out_len

print(f"# Failed decompressions: {fail_count}")
print(f"# LZSS compressed bytes total: {total_compressed}")
print(f"# LZSS uncompressed bytes total: {total_uncompressed}")
print(f"# Compression ratio: {total_uncompressed / total_compressed:.2f}x")
print(f"# Raw CGRAM bytes total: {total_cgram_raw}")

# Dump a table
print("\n# bank:ofs  rom_off   declared_out  comp_len  ratio  kind  status")
print("# --------  --------  ------------  --------  -----  ----  ------")
for bank, ofs, ro, out_len, cl, st, kind in results:
    ratio = (out_len / cl) if cl else 0
    print(f"# ${bank:02X}:{ofs:04X}  ${ro:05X}    ${out_len:04X} ({out_len:5d})  ${cl:04X}    {ratio:.2f}x  {kind:4s}  {st}")

# Sample palette: RAW 16 colors from $07:8400 (GS_FULL_GAME 256-color palette,
# first 16 entries — the BG0 sub-palette).
print("\n# Sample palette decode (first 16 colors of $07:8400 GS_FULL_GAME, RAW):")
rom_off = lorom(0x07, 0x8400)
pal_data = ROM[rom_off:rom_off+0x200]
if pal_data:
    print("static const uint16_t example_palette[16] = {")
    for i in range(16):
        lo = pal_data[i*2]
        hi = pal_data[i*2+1]
        c = lo | (hi << 8)
        r = c & 0x1F
        g = (c >> 5) & 0x1F
        b = (c >> 10) & 0x1F
        print(f"    0x{c:04X},  /* color {i:2d}: R={r:2d} G={g:2d} B={b:2d}  -> #{r*8:02X}{g*8:02X}{b*8:02X} */")
    print("};")

# Per-view tables at $01:996F (count) and $01:999F (src ptrs).
# Each view uses 3 entries: tab_off = 3*view + i  (i = 0..2).
# That means tables span at least 3*N view configs. With 16 view modes,
# the count table is 48 bytes ($01:996F..$01:99FE) and the src table is 96 bytes
# ($01:999F..$01:99FE -- this overlaps with $01:99FF so actually $01:999F..$01:99FE).
# Note from grep: $01:99FF is the start of rom_01_99FF (per-view sprite-palette bank).
# That gives us:
#   $01:996F .. $01:99FE  (48 bytes = 16 views * 3 counts)
#   $01:999F .. $01:99FE  (32 bytes ... wait, $99FE-$999F = 0x60 = 96 bytes)
# Actually $99FF starts the next table, so $999F..$99FE = 0x60 = 96 bytes = 16*3*2.
# 16 views * 3 entries * 2 bytes = 96. Checks out.

# Decode the per-view tables from bank $01:
print("\n# Per-view tables (decoded from ROM):")
rom01 = lorom(0x01, 0x8000)
def b01(addr):  # convert bank-$01 addr to ROM offset
    return lorom(0x01, addr)

def read_byte_table(addr_start, n):
    ro = b01(addr_start)
    return [ROM[ro+i] for i in range(n)]

def read_word_table(addr_start, n):
    ro = b01(addr_start)
    return [ROM[ro+2*i] | (ROM[ro+2*i+1] << 8) for i in range(n)]

# 16 views * 3 entries = 48 of each per-view-tile table
count_996F = read_byte_table(0x996F, 48)  # banks for each of 16 views * 3 chunks
src_999F   = read_word_table(0x999F, 48)  # source offsets, same indexing

# Per-view BG palette (1 per view)
count_9A5F = read_byte_table(0x9A5F, 16)
src_9A6F   = read_word_table(0x9A6F, 16)
# Per-view sprite palette pair (1 per view, sub_8AF3 with size $80)
count_99FF = read_byte_table(0x99FF, 16)
src_9A0F   = read_word_table(0x9A0F, 16)
# Per-view sprite palette pair B (size $40)
count_9A2F = read_byte_table(0x9A2F, 16)
src_9A3F   = read_word_table(0x9A3F, 16)

print("# --- $01:996F per-view tile decompress count (16 views × 3 chunks) ---")
print("static const uint8_t per_view_tile_bank[16][3] = {")
for v in range(16):
    parts = [f"0x{count_996F[3*v+i]:02X}" for i in range(3)]
    print(f"    /* view {v:2d} */ {{ {', '.join(parts)} }},")
print("};")

print("# --- $01:999F per-view tile decompress src ptr (16 views × 3 chunks) ---")
print("static const uint16_t per_view_tile_src[16][3] = {")
for v in range(16):
    parts = [f"0x{src_999F[3*v+i]:04X}" for i in range(3)]
    print(f"    /* view {v:2d} */ {{ {', '.join(parts)} }},")
print("};")

print("# --- BG palette ($01:9A5F count / $01:9A6F src) ---")
print("static const uint8_t  per_view_bg_palette_bank[16] = {",
      ", ".join(f"0x{x:02X}" for x in count_9A5F), "};")
print("static const uint16_t per_view_bg_palette_src [16] = {",
      ", ".join(f"0x{x:04X}" for x in src_9A6F), "};")

print("# --- Sprite palette pair A ($01:99FF count / $01:9A0F src) ---")
print("static const uint8_t  per_view_spr_palette_bank[16] = {",
      ", ".join(f"0x{x:02X}" for x in count_99FF), "};")
print("static const uint16_t per_view_spr_palette_src [16] = {",
      ", ".join(f"0x{x:04X}" for x in src_9A0F), "};")

print("# --- Sprite palette pair B ($01:9A2F count / $01:9A3F src) ---")
print("static const uint8_t  per_view_spr2_palette_bank[16] = {",
      ", ".join(f"0x{x:02X}" for x in count_9A2F), "};")
print("static const uint16_t per_view_spr2_palette_src [16] = {",
      ", ".join(f"0x{x:04X}" for x in src_9A3F), "};")

# Validate per-view tile entries by decompressing each unique (bank, src) it points to
print("\n# Validating per-view tile blobs:")
per_view_results = []
for v in range(16):
    for i in range(3):
        b = count_996F[3*v+i]
        s = src_999F[3*v+i]
        ro = lorom(b, s)
        ok = "??"
        out_len = 0
        cl = 0
        if ro is None or ro + 4 > len(ROM):
            ok = "BAD"
        else:
            out_len = ROM[ro] | (ROM[ro+1] << 8)
            comp_len, data = lz_decompress(ROM, ro, max_out=0x10000)
            if data is None:
                ok = "FAIL"
            else:
                cl = comp_len
                if len(data) == out_len:
                    ok = "OK"
                else:
                    ok = f"MIS({len(data)})"
        per_view_results.append((v, i, b, s, ro, out_len, cl, ok))

for v, i, b, s, ro, ol, cl, ok in per_view_results:
    print(f"# view {v:2d} chunk {i}  ${b:02X}:{s:04X} -> ROM ${ro:05X}  out=${ol:04X} comp=${cl:04X}  {ok}")

# Compute total ROM bytes occupied by per-view tile assets
unique_tile_blobs = set()
tile_compressed = 0
tile_uncompressed = 0
for _, _, b, s, ro, ol, cl, ok in per_view_results:
    key = (b, s)
    if key not in unique_tile_blobs and ok == "OK":
        unique_tile_blobs.add(key)
        tile_compressed += cl
        tile_uncompressed += ol
print(f"# unique per-view tile blobs: {len(unique_tile_blobs)}")
print(f"# per-view tile compressed: {tile_compressed} bytes")
print(f"# per-view tile uncompressed: {tile_uncompressed} bytes")

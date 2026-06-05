#!/usr/bin/env python3
"""
Generate asset_data*.c — decompressed pixel/tile/palette bytes for every
graphics blob referenced by the SimAnt SNES ROM.

Output:
  asset_data.h     — index table + extern declarations (small)
  asset_data_1.c   — Bank-$07 LZSS palette/tilemap chunks
  asset_data_2.c   — Bank-$10..$15 sprite + BG tile chunks
  asset_data_3.c   — Bank-$16..$17 BG tile / scent / landing chunks
  asset_data_4.c   — Bank-$18..$1E end/credit/mode-7 chunks
  asset_data_5.c   — Per-view scenario tile chunks (26 unique)
  asset_data_6.c   — Raw CGRAM palettes (10 entries, as uint16_t arrays)
                     + central AssetEntry index table

All blobs decompressed via the LZSS implementation in asset_extract.py.
Decompression was verified to succeed on every entry (0 failures).
"""
import os, sys, textwrap

sys.path.insert(0, '/Users/guilhermedavid/simant-re')
from asset_extract import ASSET_REFS, ROM, lorom, lz_decompress

OUT_DIR = '/Users/guilhermedavid/simant-re'

# ---------------------------------------------------------------------------
# 1. Distinct LZSS blobs from state handlers (43).
#    Drop the (bank, ofs, kind=='lz') duplicates: the alias rows in ASSET_REFS
#    re-use the same source data but get re-aimed at a different VRAM dest.
# ---------------------------------------------------------------------------
lz_blobs = {}        # (bank, ofs) -> {'rom_off','comp','uncomp_data','purpose','owner','vram'}
cgr_blobs = {}       # (bank, ofs) -> {'rom_off','data','dma_len','purpose','owner'}

for entry in ASSET_REFS:
    bank, ofs, vram, dma_len, owner, purpose, kind = entry
    key = (bank, ofs)
    ro = lorom(bank, ofs)
    if kind == 'cgr':
        if key in cgr_blobs:
            continue
        data = bytes(ROM[ro:ro+dma_len])
        cgr_blobs[key] = {
            'rom_off': ro,
            'data': data,
            'dma_len': dma_len,
            'purpose': purpose,
            'owner': owner,
            'vram_or_cgram': vram,
        }
    else:
        if key in lz_blobs:
            continue
        comp_len, data = lz_decompress(ROM, ro, max_out=0x10000)
        if data is None:
            print(f"FAIL: ${bank:02X}:{ofs:04X}", file=sys.stderr)
            continue
        lz_blobs[key] = {
            'rom_off': ro,
            'comp': comp_len,
            'uncomp_data': data,
            'purpose': purpose,
            'owner': owner,
            'vram': vram,
            'dma_len': dma_len,
        }

# ---------------------------------------------------------------------------
# 2. Per-view tile blobs (26 unique) — keyed by (bank, src).
# ---------------------------------------------------------------------------
def b01(addr): return lorom(0x01, addr)
count_996F = [ROM[b01(0x996F)+i] for i in range(48)]
src_999F   = [ROM[b01(0x999F)+2*i] | (ROM[b01(0x999F)+2*i+1]<<8) for i in range(48)]

view_blobs = {}      # (bank, ofs) -> {'rom_off','comp','uncomp_data','users':[(view,chunk),...]}
for v in range(16):
    for i in range(3):
        b = count_996F[3*v+i]
        s = src_999F[3*v+i]
        key = (b, s)
        if key in view_blobs:
            view_blobs[key]['users'].append((v, i))
            continue
        ro = lorom(b, s)
        comp_len, data = lz_decompress(ROM, ro, max_out=0x10000)
        if data is None:
            print(f"FAIL view {v} chunk {i} ${b:02X}:{s:04X}", file=sys.stderr)
            continue
        view_blobs[key] = {
            'rom_off': ro,
            'comp': comp_len,
            'uncomp_data': data,
            'users': [(v, i)],
        }

print(f"LZSS state-handler blobs: {len(lz_blobs)}")
print(f"CGRAM raw blobs        : {len(cgr_blobs)}")
print(f"Per-view tile blobs    : {len(view_blobs)}")
print(f"Total uncompressed LZSS+view: "
      f"{sum(len(v['uncomp_data']) for v in lz_blobs.values()) + sum(len(v['uncomp_data']) for v in view_blobs.values())} bytes")
print(f"Total raw CGRAM         : "
      f"{sum(len(v['data']) for v in cgr_blobs.values())} bytes")

# ---------------------------------------------------------------------------
# 3. Emit one .c per bank-group, then a master .c with the AssetEntry table.
# ---------------------------------------------------------------------------
def hex_array(data, indent='    ', per_line=16):
    """Emit a C-array body — 16 bytes per line, prefixed with `indent`."""
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i+per_line]
        s = ', '.join(f'0x{b:02X}' for b in chunk)
        lines.append(f'{indent}{s},')
    return '\n'.join(lines)

def palette_array(data, indent='    '):
    """Emit a uint16_t palette array from raw SNES 5-5-5 little-endian bytes."""
    assert len(data) % 2 == 0
    n = len(data) // 2
    lines = []
    per_line = 8
    for i in range(0, n, per_line):
        words = []
        for j in range(i, min(i+per_line, n)):
            c = data[2*j] | (data[2*j+1] << 8)
            words.append(f'0x{c:04X}')
        lines.append(f'{indent}' + ', '.join(words) + ',')
    return '\n'.join(lines)

def asset_var(bank, ofs):
    return f'asset_{bank:02X}_{ofs:04X}'

def palette_var(bank, ofs):
    return f'palette_{bank:02X}_{ofs:04X}'

def view_var(bank, ofs):
    return f'view_tile_{bank:02X}_{ofs:04X}'

# ---------------------------------------------------------------------------
# File 1 — Bank $07 LZSS palette/tilemap chunks (14 entries, ~28 KB raw).
# Files 2..4 — Bank $10..$1E LZSS chunks (29 entries, ~272 KB raw).
# File 5 — per-view tile chunks (26 entries, ~213 KB raw — 8 KB each).
# File 6 — CGRAM palettes (10 entries, 2,880 bytes) + asset_table + summary.
# ---------------------------------------------------------------------------
def file_header(name, summary):
    return f"""/*
 * {name} — auto-generated decompressed graphics data for SimAnt SNES.
 *
 * Source: /Users/guilhermedavid/simant-re/simant.sfc (1 MB LoROM).
 * Generator: gen_asset_data.py (re-runs the LZSS decoder lifted in
 *            asset_extract.py against every blob in the asset table).
 *
 * {summary}
 *
 * SNES tile format reminder (for the BG/sprite chr blobs):
 *   - 4bpp planar: 32 bytes per 8x8 tile.
 *   - bytes 0..15  = bitplanes 0+1 interleaved (lo plane then hi plane per row).
 *   - bytes 16..31 = bitplanes 2+3 interleaved.
 *   To convert to a 4bpp pixel: for each of 8 rows, read
 *     p0 = byte[2*row]; p1 = byte[2*row+1];
 *     p2 = byte[16 + 2*row]; p3 = byte[16 + 2*row+1];
 *   Then bit `7-x` of each plane stacks into the pixel's 4-bit color index.
 *
 * Tilemap blobs (the bank-$07 LZSS chunks aimed at VRAM $7000-$7FFF) use the
 * standard SNES tilemap word format: hi8 = vhopppcc, lo8 = ttttttttt (10-bit
 * tile, 3-bit palette, prio, V/H flip). The viewer/port re-parses these.
 *
 * NOTE: pixel rows are NOT pre-decoded to 4bpp packed format here — the raw
 * planar bytes are emitted so a Flipper-port or other re-targeter can pick
 * its own pixel storage layout. The header bytes (first 4 of each LZSS source)
 * are NOT included — only the uncompressed payload starts each array.
 */
#include <stdint.h>

"""

# Build per-file content
files = []

# -----------------------------------------------------------------------
# File 1: bank $07 LZSS chunks
# -----------------------------------------------------------------------
bank07_lzss = sorted([k for k in lz_blobs if k[0] == 0x07])
total7 = sum(len(lz_blobs[k]['uncomp_data']) for k in bank07_lzss)
buf = file_header(
    "asset_data_1.c",
    f"Bank-$07 LZSS palette/tilemap chunks ({len(bank07_lzss)} blobs, "
    f"{total7} bytes uncompressed)."
)
for (bank, ofs) in bank07_lzss:
    info = lz_blobs[(bank, ofs)]
    d = info['uncomp_data']
    buf += (
        f"/* ${bank:02X}:{ofs:04X}  ROM ${info['rom_off']:05X}  "
        f"comp=${info['comp']:04X} uncomp=${len(d):04X}  "
        f"vram=${info['vram'] or 0:04X}\n"
        f" * owner: {info['owner']}\n"
        f" * purpose: {info['purpose']}\n */\n"
    )
    buf += f"const uint8_t {asset_var(bank, ofs)}[{len(d)}] = {{\n"
    buf += hex_array(d)
    buf += "\n};\n\n"
files.append(("asset_data_1.c", buf))

# -----------------------------------------------------------------------
# Files 2-4: bank $10..$1E LZSS (split by bank range for size)
# -----------------------------------------------------------------------
def emit_lzss_file(name, banks, blurb):
    keys = sorted([k for k in lz_blobs if k[0] in banks])
    total = sum(len(lz_blobs[k]['uncomp_data']) for k in keys)
    buf = file_header(name, f"{blurb} ({len(keys)} blobs, {total} bytes uncompressed).")
    for (bank, ofs) in keys:
        info = lz_blobs[(bank, ofs)]
        d = info['uncomp_data']
        buf += (
            f"/* ${bank:02X}:{ofs:04X}  ROM ${info['rom_off']:05X}  "
            f"comp=${info['comp']:04X} uncomp=${len(d):04X}  "
            f"vram=${info['vram'] or 0:04X}\n"
            f" * owner: {info['owner']}\n"
            f" * purpose: {info['purpose']}\n */\n"
        )
        buf += f"const uint8_t {asset_var(bank, ofs)}[{len(d)}] = {{\n"
        buf += hex_array(d)
        buf += "\n};\n\n"
    return name, buf

files.append(emit_lzss_file(
    "asset_data_2.c", {0x10, 0x15, 0x16},
    "Banks $10/$15/$16 LZSS sprite-chr + BG tile data"
))
files.append(emit_lzss_file(
    "asset_data_3.c", {0x17, 0x18, 0x19},
    "Banks $17/$18/$19 LZSS scent/landing/save-game tile data"
))
files.append(emit_lzss_file(
    "asset_data_4.c", {0x1A, 0x1B, 0x1E},
    "Banks $1A/$1B/$1E LZSS credit/mode-7/text tile data"
))

# -----------------------------------------------------------------------
# File 5: per-view tile chunks (26 unique blobs)
# -----------------------------------------------------------------------
view_keys = sorted(view_blobs.keys())
total_view = sum(len(view_blobs[k]['uncomp_data']) for k in view_keys)
buf = file_header(
    "asset_data_5.c",
    f"Per-view scenario tile chunks ({len(view_keys)} unique blobs, "
    f"{total_view} bytes uncompressed). Each view consumes 3 of these (16 KB total)."
)
for (bank, ofs) in view_keys:
    info = view_blobs[(bank, ofs)]
    d = info['uncomp_data']
    users = ', '.join(f'view{u[0]}.{u[1]}' for u in info['users'])
    buf += (
        f"/* ${bank:02X}:{ofs:04X}  ROM ${info['rom_off']:05X}  "
        f"comp=${info['comp']:04X} uncomp=${len(d):04X}\n"
        f" * used by: {users}\n */\n"
    )
    buf += f"const uint8_t {view_var(bank, ofs)}[{len(d)}] = {{\n"
    buf += hex_array(d)
    buf += "\n};\n\n"
files.append(("asset_data_5.c", buf))

# -----------------------------------------------------------------------
# File 6: CGRAM palettes (10) + master AssetEntry table + summary
# -----------------------------------------------------------------------
buf = file_header(
    "asset_data_6.c",
    f"Raw CGRAM palettes ({len(cgr_blobs)} entries, "
    f"{sum(len(v['data']) for v in cgr_blobs.values())} bytes) + master "
    f"AssetEntry index table covering all decompressed blobs."
)
buf += "/* ---- CGRAM palettes (SNES 5-5-5 little-endian uint16_t) ---- */\n\n"

cgr_keys = sorted(cgr_blobs.keys())
for (bank, ofs) in cgr_keys:
    info = cgr_blobs[(bank, ofs)]
    d = info['data']
    nwords = len(d) // 2
    buf += (
        f"/* ${bank:02X}:{ofs:04X}  ROM ${info['rom_off']:05X}  "
        f"raw {len(d)} bytes ({nwords} colours)\n"
        f" * owner: {info['owner']}\n"
        f" * purpose: {info['purpose']}\n"
        f" * cgram dest: ${info['vram_or_cgram'] or 0:04X}\n */\n"
    )
    buf += f"const uint16_t {palette_var(bank, ofs)}[{nwords}] = {{\n"
    buf += palette_array(d)
    buf += "\n};\n\n"

# Index table — emits a typedef + array describing every blob.
buf += """\
/* ========================================================================
 * MASTER ASSET INDEX
 *
 * Indexes every decompressed blob shipped in asset_data_1..6.c.
 * Each entry records:
 *   - The SNES source (bank:offset, flat ROM file offset).
 *   - The decompressed byte length AND a pointer to the data array.
 *   - The asset kind (LZSS_state, LZSS_view, RAW_CGRAM_palette).
 *   - The VRAM/CGRAM destination address (0 = N/A / dispatched later).
 *   - The owner state handler and a human-readable purpose string.
 * ======================================================================== */
typedef enum {
    ASSET_LZSS_STATE   = 0,  /* LZSS-decompressed via state-handler chain   */
    ASSET_LZSS_VIEW    = 1,  /* LZSS-decompressed via per-view dispatch     */
    ASSET_RAW_CGRAM    = 2,  /* raw CGRAM palette, no LZSS                  */
} AssetDataKind;

typedef struct {
    uint8_t              bank;       /* source bank ($07..$1E)            */
    uint16_t             src_ofs;    /* source offset within bank         */
    uint32_t             rom_off;    /* flat ROM file offset (sanity)     */
    uint16_t             data_len;   /* size of `data` in bytes           */
    uint16_t             dma_dest;   /* VRAM/CGRAM target (0 if N/A)      */
    const void          *data;       /* pointer to bytes (or uint16_t[])  */
    AssetDataKind        kind;
    const char          *owner_state;
    const char          *purpose;
} AssetDataEntry;

"""

# Forward decls — declare every const array we reference.
for (bank, ofs) in bank07_lzss:
    d = lz_blobs[(bank, ofs)]['uncomp_data']
    buf += f"extern const uint8_t {asset_var(bank, ofs)}[{len(d)}];\n"
for (bank, ofs) in sorted([k for k in lz_blobs if k[0] in {0x10,0x15,0x16}]):
    d = lz_blobs[(bank, ofs)]['uncomp_data']
    buf += f"extern const uint8_t {asset_var(bank, ofs)}[{len(d)}];\n"
for (bank, ofs) in sorted([k for k in lz_blobs if k[0] in {0x17,0x18,0x19}]):
    d = lz_blobs[(bank, ofs)]['uncomp_data']
    buf += f"extern const uint8_t {asset_var(bank, ofs)}[{len(d)}];\n"
for (bank, ofs) in sorted([k for k in lz_blobs if k[0] in {0x1A,0x1B,0x1E}]):
    d = lz_blobs[(bank, ofs)]['uncomp_data']
    buf += f"extern const uint8_t {asset_var(bank, ofs)}[{len(d)}];\n"
for (bank, ofs) in view_keys:
    d = view_blobs[(bank, ofs)]['uncomp_data']
    buf += f"extern const uint8_t {view_var(bank, ofs)}[{len(d)}];\n"

buf += "\nconst AssetDataEntry asset_data_index[] = {\n"

# LZSS state-handler entries
for (bank, ofs) in (sorted(lz_blobs.keys())):
    info = lz_blobs[(bank, ofs)]
    d = info['uncomp_data']
    owner = info['owner'].replace('"', '\\"')
    purpose = info['purpose'].replace('"', '\\"')
    buf += (f"    {{ 0x{bank:02X}, 0x{ofs:04X}, 0x{info['rom_off']:05X}, "
            f"{len(d)}, 0x{info['vram'] or 0:04X}, "
            f"{asset_var(bank, ofs)}, ASSET_LZSS_STATE, "
            f"\"{owner}\", \"{purpose}\" }},\n")

# Per-view tile entries
for (bank, ofs) in view_keys:
    info = view_blobs[(bank, ofs)]
    d = info['uncomp_data']
    users = ', '.join(f'v{u[0]}.{u[1]}' for u in info['users'])
    users_esc = users.replace('"', '\\"')
    buf += (f"    {{ 0x{bank:02X}, 0x{ofs:04X}, 0x{info['rom_off']:05X}, "
            f"{len(d)}, 0x0000, "
            f"{view_var(bank, ofs)}, ASSET_LZSS_VIEW, "
            f"\"per-view tile dispatch\", \"used by: {users_esc}\" }},\n")

# CGRAM entries
for (bank, ofs) in cgr_keys:
    info = cgr_blobs[(bank, ofs)]
    d = info['data']
    owner = info['owner'].replace('"', '\\"')
    purpose = info['purpose'].replace('"', '\\"')
    buf += (f"    {{ 0x{bank:02X}, 0x{ofs:04X}, 0x{info['rom_off']:05X}, "
            f"{len(d)}, 0x{info['vram_or_cgram'] or 0:04X}, "
            f"{palette_var(bank, ofs)}, ASSET_RAW_CGRAM, "
            f"\"{owner}\", \"{purpose}\" }},\n")

buf += "};\n\n"

buf += f"const unsigned int asset_data_index_count = "\
       f"sizeof(asset_data_index) / sizeof(asset_data_index[0]);\n\n"

# Summary stats baked into the file.
total_lz_uncomp = sum(len(v['uncomp_data']) for v in lz_blobs.values())
total_view_uncomp = sum(len(v['uncomp_data']) for v in view_blobs.values())
total_cgr = sum(len(v['data']) for v in cgr_blobs.values())

buf += f"""\
/* ========================================================================
 * SUMMARY STATISTICS (verified by gen_asset_data.py)
 *
 *   LZSS state-handler blobs (distinct): {len(lz_blobs):>3d}
 *      total uncompressed bytes        : {total_lz_uncomp:>7d}
 *
 *   Per-view scenario tile blobs       : {len(view_blobs):>3d}
 *      total uncompressed bytes        : {total_view_uncomp:>7d}
 *
 *   Raw CGRAM palettes                 : {len(cgr_blobs):>3d}
 *      total bytes                     : {total_cgr:>7d}
 *
 *   GRAND TOTAL (all decompressed)     : {total_lz_uncomp + total_view_uncomp + total_cgr:>7d} bytes
 *
 * Decompression failures during extract: 0
 * ======================================================================== */
"""

files.append(("asset_data_6.c", buf))

# Write all files
for name, content in files:
    path = os.path.join(OUT_DIR, name)
    with open(path, 'w') as f:
        f.write(content)
    print(f"Wrote {path} ({len(content)} chars)")

# Print summary
print()
print("=== Summary ===")
print(f"LZSS state-handler blobs: {len(lz_blobs)}, {total_lz_uncomp} bytes uncompressed")
print(f"Per-view tile blobs     : {len(view_blobs)}, {total_view_uncomp} bytes uncompressed")
print(f"Raw CGRAM palettes      : {len(cgr_blobs)}, {total_cgr} bytes raw")
print(f"TOTAL DATA              : {total_lz_uncomp + total_view_uncomp + total_cgr} bytes")

#!/usr/bin/env python3
"""Extract encyclopedia + tutorial text from the SimAnt SNES ROM.

LoROM mapping: bank $XX:YYYY (with YYYY >= $8000) maps to file offset
  (XX * 0x8000) + (YYYY - 0x8000)
This game is 1 MB (32 banks).

Encyclopedia tables (all in bank $01):
  $01:C778  30 bytes  palette index (per page)
  $01:C796  30 bytes  picture src bank
  $01:C7B4  60 bytes  picture src offset (16-bit LE per page)
  $01:C7F0  60 bytes  text pointer (16-bit LE per page, into bank $01)

Tutorial table:
  $00:E2C2  108 bytes  54 × 16-bit LE pointers into bank $01

Text grammar:
  $FF = end of message
  $FE = newline (-> '\n')
  $2C / $2E = comma / period (suppress wrap)
  printable ASCII otherwise
"""
from pathlib import Path
import sys

ROM_PATH = Path(__file__).parent / "simant.sfc"
OUT_PATH = Path(__file__).parent / "text_content.c"

ROM = ROM_PATH.read_bytes()
assert len(ROM) == 0x100000, f"unexpected ROM size {len(ROM)}"

def lorom(bank, addr):
    """Convert SNES LoROM bank:addr (addr >= $8000) to file offset."""
    assert 0x8000 <= addr <= 0xFFFF, f"bad addr ${addr:04X}"
    return (bank * 0x8000) + (addr - 0x8000)

def read_u8(bank, addr):
    return ROM[lorom(bank, addr)]

def read_u16(bank, addr):
    off = lorom(bank, addr)
    return ROM[off] | (ROM[off+1] << 8)

# ---------------------------------------------------------------- read tables
PALETTE_IDX = [read_u8(0x01, 0xC778 + i) for i in range(30)]
PICTURE_BANK = [read_u8(0x01, 0xC796 + i) for i in range(30)]
PICTURE_OFFSET = [read_u16(0x01, 0xC7B4 + i*2) for i in range(30)]
TEXT_PTR = [read_u16(0x01, 0xC7F0 + i*2) for i in range(30)]
TUTORIAL_PTR = [read_u16(0x00, 0xE2C2 + i*2) for i in range(54)]

# ---------------------------------------------------------------- text decode
NOTES = []  # stash unusual bytes here

def decode_text(bank, addr, label, max_len=8192):
    """Walk bytes from bank:addr until $FF; return decoded string + raw len.

    Returns (decoded_str, raw_byte_length).
    raw_byte_length INCLUDES the terminating $FF.
    """
    off = lorom(bank, addr)
    out = []
    n = 0
    while n < max_len:
        b = ROM[off + n]
        n += 1
        if b == 0xFF:
            break
        elif b == 0xFE:
            out.append('\n')
        elif 0x20 <= b <= 0x7E:
            out.append(chr(b))
        else:
            # Unusual byte — record and emit a placeholder so we don't
            # silently corrupt the C string.
            NOTES.append(f"  {label} @ ${bank:02X}:{addr:04X}+${n-1:X}: byte ${b:02X}")
            out.append(f"\\x{b:02X}")
    else:
        NOTES.append(f"  {label} @ ${bank:02X}:{addr:04X}: NO $FF within {max_len}")
    return "".join(out), n

# ---------------------------------------------------------------- C escape
def c_escape(s):
    """Escape a Python string for a C string literal.

    Newlines become \\n.  Backslash and double-quote get escaped.
    \\xNN escape sequences already in `s` (from unknown bytes) are passed
    through unchanged so a C compiler reads them as hex escapes.
    Trigraphs (??) are split with a string concatenation to silence
    -Wtrigraphs and to keep the ROM text faithful.
    """
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s) and s[i+1] == 'x':
            # Pass through \xNN sequence verbatim (4 chars).
            out.append(s[i:i+4])
            i += 4
            continue
        if c == '\\':
            out.append('\\\\')
        elif c == '"':
            out.append('\\"')
        elif c == '\n':
            out.append('\\n')
        elif c == '\t':
            out.append('\\t')
        elif c == '?' and i + 1 < len(s) and s[i+1] == '?':
            # Defuse trigraphs by breaking the digraph "??".
            out.append('?" "')
        else:
            out.append(c)
        i += 1
    return "".join(out)

# ---------------------------------------------------------------- pull text
TOPIC_RANGES = [
    ("Introduction",     0, 6),
    ("Ant Life",         6, 17),
    ("Ants at Home",     17, 20),
    ("Ants & Relatives", 20, 26),
    ("SimAnt Strategy",  26, 30),
]

def topic_for(page):
    for name, lo, hi in TOPIC_RANGES:
        if lo <= page < hi:
            return name
    return "?"

enc_pages = []
enc_total_bytes = 0
for i in range(30):
    text, raw = decode_text(0x01, TEXT_PTR[i], f"encyclopedia[{i}]")
    enc_pages.append({
        "index": i,
        "topic": topic_for(i),
        "palette_idx": PALETTE_IDX[i],
        "pic_bank": PICTURE_BANK[i],
        "pic_offset": PICTURE_OFFSET[i],
        "text_ptr": TEXT_PTR[i],
        "raw_bytes": raw,
        "text": text,
    })
    enc_total_bytes += raw

tut_messages = []
tut_total_bytes = 0
for i in range(54):
    text, raw = decode_text(0x01, TUTORIAL_PTR[i], f"tutorial[{i}]")
    tut_messages.append({
        "index": i,
        "ptr": TUTORIAL_PTR[i],
        "raw_bytes": raw,
        "text": text,
    })
    tut_total_bytes += raw

# Sanity: tutorial pointers should be increasing and within $B27F..$C777.
for i, m in enumerate(tut_messages):
    if not (0xB27F <= m["ptr"] <= 0xC777):
        NOTES.append(f"  tutorial[{i}] ptr ${m['ptr']:04X} outside expected range")

# ---------------------------------------------------------------- short prompts
SHORT_PROMPTS = [
    # (label, bank, addr) — addresses lifted from player_actions.c /
    # text_screens.c comments.  These are all $FF-terminated and use
    # the same $FE/$FF/$2C/$2E grammar as the encyclopedia.
    ("yellow_ant_fed_by_nestmate",  0x01, 0xA215),
    ("yellow_ant_ate_regained",     0x01, 0xB860),  # tutorial[14], also stand-alone
    ("will_pick_up_pebble",         0x01, 0xBAE2),  # NOTE: comment says $BB28 (mid-msg)
    ("yellow_ant_died_last_life",   0x01, 0xA351),  # comment says $A34F (off by 2)
    ("walk_toward_food",            0x01, 0xB77A),  # comment says $B7D5 (mid-msg)
    # Menu picker strings (12-byte $FF-terminated).
    ("recruit_5",                   0x01, 0x86F3),
    ("recruit_10",                  0x01, 0x86FF),
    ("recruit_all",                 0x01, 0x870B),
    ("release_half",                0x01, 0x8717),
    ("release_all",                 0x01, 0x8723),
    ("dig_new_nest",                0x01, 0x8734),
    ("lay_eggs",                    0x01, 0x8741),
    # Encyclopedia topic-list labels (18-byte $FF-terminated).
    ("topic_blank_filler",          0x01, 0x89E7),
    ("topic_introduction",          0x01, 0x89F9),
    ("topic_ant_life",              0x01, 0x8A0B),
    ("topic_ants_at_home",          0x01, 0x8A1D),
    ("topic_ants_relatives",        0x01, 0x8A2F),
    ("topic_simant_strategy",       0x01, 0x8A41),
    ("topic_exit",                  0x01, 0x8A53),
]

# The status-message blob at $01:B07A is a SINGLE 9-entry × 32-byte
# space-padded array (NO $FE/$FF inside — only one terminating $FF after
# the "PAUSE" footer).  Strip trailing spaces from each 32-byte cell.
STATUS_BLOB_BANK = 0x01
STATUS_BLOB_ADDR = 0xB07A
STATUS_ENTRY_LEN = 32
STATUS_ENTRY_COUNT = 9
STATUS_LABELS = [
    "is_hungry",
    "colony_needs_more_food",
    "nest_too_crowded_dig",
    "red_ants_attacking",
    "feed_queen_hungry",
    "human_coming_hide",
    "press_B_dig_new_nest",
    "press_B_lay_eggs",
    "pause",
]

def decode_status_blob():
    """Decode the 9-cell status blob.

    Layout discovered empirically: the first 8 cells are each 32 bytes
    of space-padded ASCII; the 9th cell is the 16-byte 'PAUSE' footer
    that terminates with $FF.  Total = 8*32 + 16 = 272 bytes followed
    by $FF.  After the $FF the ROM continues with an unrelated 4-entry
    scent-menu pointer table — outside our concern here.
    """
    base = lorom(STATUS_BLOB_BANK, STATUS_BLOB_ADDR)
    out = []
    # 8 full-width 32-byte cells
    for i in range(STATUS_ENTRY_COUNT - 1):
        cell = ROM[base + i*STATUS_ENTRY_LEN : base + (i+1)*STATUS_ENTRY_LEN]
        for b in cell:
            if not (0x20 <= b <= 0x7E):
                NOTES.append(
                    f"  status_blob[{i}]: non-ASCII byte ${b:02X}")
        text = cell.decode("latin-1", "replace").rstrip(" ")
        out.append((STATUS_LABELS[i], text))
    # 9th: short PAUSE cell, ending in $FF
    pause_off = base + (STATUS_ENTRY_COUNT - 1) * STATUS_ENTRY_LEN
    end = ROM.index(0xFF, pause_off)
    pause = ROM[pause_off:end].decode("latin-1", "replace").strip()
    out.append((STATUS_LABELS[-1], pause))
    return out

status_blob = decode_status_blob()

short_extracted = []
for label, bank, addr in SHORT_PROMPTS:
    text, raw = decode_text(bank, addr, label, max_len=256)
    short_extracted.append((label, bank, addr, text, raw))

# ---------------------------------------------------------------- emit C
def emit_c():
    lines = []
    a = lines.append
    a("/*")
    a(" * text_content.c — extracted English text content of SimAnt (SNES).")
    a(" *")
    a(" * Auto-generated by extract_text.py from simant.sfc.  See")
    a(" * text_screens.c for the pointer-table layout (encyclopedia + tutorial)")
    a(" * and player_actions.c / scenarios.c for the short prompts.")
    a(" *")
    a(" * Text grammar (per Agent M):")
    a(" *   $FF      end-of-message")
    a(" *   $FE      newline (rendered as '\\n' here)")
    a(" *   $2C/$2E  comma / period (suppress wrap at column edge)")
    a(" *   ASCII    otherwise")
    a(" *")
    a(" * Each encyclopedia page carries metadata so the renderer can")
    a(" * pair the prose with its 256-color palette + compressed picture:")
    a(" *   .palette_idx  → ROM addr $07:9D80 + (idx << 7) (128-byte CGRAM)")
    a(" *   .picture_src_bank / .picture_src_offset")
    a(" *                 → asset_decompress_028010() source")
    a(" *")
    a(f" * Totals: encyclopedia = {enc_total_bytes} raw bytes across 30 pages,")
    a(f" *         tutorial     = {tut_total_bytes} raw bytes across 54 messages.")
    a(" */")
    a("")
    a("#include <stdint.h>")
    a("")
    a("/* ============================================================")
    a(" * ENCYCLOPEDIA — 30 pages, 5 topics")
    a(" *   Topic 0  Introduction      pages  0..5    (6 pages)")
    a(" *   Topic 1  Ant Life          pages  6..16   (11 pages)")
    a(" *   Topic 2  Ants at Home      pages 17..19   (3 pages)")
    a(" *   Topic 3  Ants & Relatives  pages 20..25   (6 pages)")
    a(" *   Topic 4  SimAnt Strategy   pages 26..29   (4 pages)")
    a(" * ============================================================ */")
    a("")
    a("typedef struct {")
    a("    uint8_t  page_index;          /* 0..29                              */")
    a("    uint8_t  palette_idx;         /* $01:C778[page]                     */")
    a("    uint8_t  picture_src_bank;    /* $01:C796[page]                     */")
    a("    uint16_t picture_src_offset;  /* $01:C7B4[page]                     */")
    a("    uint16_t text_rom_ptr;        /* $01:C7F0[page] (in bank $01)       */")
    a("    const char *topic;            /* topic name, for debug              */")
    a("    const char *text;             /* decoded English text               */")
    a("} EncyclopediaPage;")
    a("")
    a("static const EncyclopediaPage encyclopedia_pages_full[30] = {")
    for p in enc_pages:
        a(f"    /* page {p['index']:2d} — {p['topic']} */")
        a("    {")
        a(f"        .page_index         = {p['index']},")
        a(f"        .palette_idx        = 0x{p['palette_idx']:02X},")
        a(f"        .picture_src_bank   = 0x{p['pic_bank']:02X},")
        a(f"        .picture_src_offset = 0x{p['pic_offset']:04X},")
        a(f"        .text_rom_ptr       = 0x{p['text_ptr']:04X},")
        a(f"        .topic              = \"{p['topic']}\",")
        a(f"        .text =")
        # Split per source-line (each newline in the page becomes one C-string).
        page_lines = p["text"].split("\n")
        for li, line in enumerate(page_lines):
            esc = c_escape(line)
            sep = "\\n" if li < len(page_lines) - 1 else ""
            a(f"            \"{esc}{sep}\"")
        a("        ,")
        a("    },")
    a("};")
    a("")
    a("/* Convenience: text-only pointer array for callers that want strings. */")
    a("static const char *const encyclopedia_pages[30] = {")
    for p in enc_pages:
        a(f"    encyclopedia_pages_full[{p['index']}].text,")
    a("};")
    a("")
    a("/* ============================================================")
    a(" * TUTORIAL — 54 messages, pointers at $00:E2C2 → bank $01")
    a(" * ============================================================ */")
    a("")
    a("static const char *const tutorial_messages[54] = {")
    for m in tut_messages:
        a(f"    /* {m['index']:2d}  $01:{m['ptr']:04X}  ({m['raw_bytes']} bytes) */")
        msg_lines = m["text"].split("\n")
        for li, line in enumerate(msg_lines):
            esc = c_escape(line)
            sep = "\\n" if li < len(msg_lines) - 1 else ""
            a(f"    \"{esc}{sep}\"")
        a("    ,")
    a("};")
    a("")
    a("/* ============================================================")
    a(" * SHORT PROMPTS — referenced by player_actions.c / scenarios.c /")
    a(" * text_screens.c.  Each entry is a label + the literal English")
    a(" * string ($FF-terminated in ROM, $FE → '\\n' here).")
    a(" * ============================================================ */")
    a("")
    a("typedef struct {")
    a("    const char *label;")
    a("    uint8_t     bank;")
    a("    uint16_t    addr;")
    a("    const char *text;")
    a("} ShortPrompt;")
    a("")
    a(f"static const ShortPrompt short_prompts[{len(short_extracted)}] = {{")
    for label, bank, addr, text, raw in short_extracted:
        esc = c_escape(text)
        a(f"    {{ \"{label}\", 0x{bank:02X}, 0x{addr:04X},")
        a(f"      \"{esc}\" }},  /* {raw} bytes raw */")
    a("};")
    a("")
    a("/* ============================================================")
    a(" * STATUS-MESSAGE BLOB at $01:B07A")
    a(" *   9 × 32-byte space-padded fixed-width strings, NO $FE/$FF")
    a(" *   inside (one $FF after the trailing 'PAUSE' entry).  These")
    a(" *   are indexed by ID for one-line on-screen alerts.")
    a(" * ============================================================ */")
    a("")
    a("typedef struct {")
    a("    const char *label;")
    a("    const char *text;")
    a("} StatusMessage;")
    a("")
    a(f"static const StatusMessage status_messages[{len(status_blob)}] = {{")
    for label, text in status_blob:
        a(f"    {{ \"{label}\", \"{c_escape(text)}\" }},")
    a("};")
    a("")
    a("/* Keep symbols reachable for static analysis. */")
    a("__attribute__((used))")
    a("static const void * const _text_content_doc_refs[] = {")
    a("    (void const *)encyclopedia_pages,")
    a("    (void const *)tutorial_messages,")
    a("    (void const *)short_prompts,")
    a("    (void const *)status_messages,")
    a("    (void const *)encyclopedia_pages_full,")
    a("};")
    a("")
    return "\n".join(lines)

OUT_PATH.write_text(emit_c())

# ---------------------------------------------------------------- report
print(f"wrote {OUT_PATH}", file=sys.stderr)
print(f"encyclopedia: {enc_total_bytes} raw bytes / 30 pages", file=sys.stderr)
print(f"tutorial:     {tut_total_bytes} raw bytes / 54 messages", file=sys.stderr)
print(f"short prompts: {len(short_extracted)}", file=sys.stderr)
if NOTES:
    print(f"\nNOTES ({len(NOTES)} unusual byte(s) / warnings):", file=sys.stderr)
    for n in NOTES[:50]:
        print(n, file=sys.stderr)

# Quick sanity preview
print("\n--- encyclopedia page 0 ---", file=sys.stderr)
print(enc_pages[0]["text"][:400], file=sys.stderr)
print("\n--- tutorial message 0 ---", file=sys.stderr)
print(tut_messages[0]["text"], file=sys.stderr)
print("\n--- status_messages ---", file=sys.stderr)
for label, text in status_blob:
    print(f"  [{label}] {text!r}", file=sys.stderr)

#!/usr/bin/env python3
"""text_verify.py — byte-verify text_content.c against simant.sfc.

Independent re-extraction of:
  - 30 encyclopedia pages (table at $01:C7F0)
  - 54 tutorial messages   (table at $00:E2C2)
  - encyclopedia metadata (palette/picture_bank/picture_offset/text_ptr)

Then parse the C arrays in text_content.c and byte-diff each pair.

This script is intentionally independent of extract_text.py:
  - own ROM reader
  - own pointer-table walker
  - own C string-literal parser (no shelling out to a compiler)

Exit code: 0 if all bytes match the ROM, 1 otherwise.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).parent
ROM_PATH = ROOT / "simant.sfc"
C_PATH = ROOT / "text_content.c"

ROM = ROM_PATH.read_bytes()
C_SRC = C_PATH.read_text()

assert len(ROM) == 0x100000, f"unexpected ROM size {len(ROM)}"


# ---------------------------------------------------------------- ROM helpers
def lorom(bank, addr):
    assert 0x8000 <= addr <= 0xFFFF, f"bad addr ${addr:04X}"
    return (bank * 0x8000) + (addr - 0x8000)


def rd_u8(bank, addr):
    return ROM[lorom(bank, addr)]


def rd_u16(bank, addr):
    o = lorom(bank, addr)
    return ROM[o] | (ROM[o + 1] << 8)


def read_until_ff(bank, addr, max_len=8192):
    """Return raw byte sequence INCLUDING the terminating $FF."""
    base = lorom(bank, addr)
    for n in range(max_len):
        if ROM[base + n] == 0xFF:
            return ROM[base : base + n + 1]
    raise RuntimeError(f"no $FF within {max_len} bytes at ${bank:02X}:{addr:04X}")


# ---------------------------------------------------------------- ROM tables
PALETTE_IDX     = [rd_u8 (0x01, 0xC778 + i)        for i in range(30)]
PICTURE_BANK    = [rd_u8 (0x01, 0xC796 + i)        for i in range(30)]
PICTURE_OFFSET  = [rd_u16(0x01, 0xC7B4 + i * 2)    for i in range(30)]
TEXT_PTR        = [rd_u16(0x01, 0xC7F0 + i * 2)    for i in range(30)]
TUTORIAL_PTR    = [rd_u16(0x00, 0xE2C2 + i * 2)    for i in range(54)]

ENC_ROM_BYTES = [read_until_ff(0x01, ptr) for ptr in TEXT_PTR]
TUT_ROM_BYTES = [read_until_ff(0x01, ptr) for ptr in TUTORIAL_PTR]


# ---------------------------------------------------------------- C parser
def parse_c_string_literal(text, start):
    """Parse a sequence of one-or-more adjacent C string literals.

    Starting from text[start], skip whitespace + C/C++ comments, then
    read consecutive "..."-quoted runs (concatenated per C rules).  Decode
    escapes \\n \\t \\\\ \\\" \\xNN.  Stop at the first non-string token.

    Returns (raw_bytes_with_FF_appended, end_index).

    The terminating $FF is appended automatically because the C arrays
    strip it during emission; we must add it back to match ROM length.
    """
    i = start
    n = len(text)
    out = bytearray()
    while True:
        # skip whitespace + comments
        while i < n:
            c = text[i]
            if c in " \t\r\n":
                i += 1
            elif c == '/' and i + 1 < n and text[i + 1] == '/':
                # // comment to EOL
                while i < n and text[i] != '\n':
                    i += 1
            elif c == '/' and i + 1 < n and text[i + 1] == '*':
                j = text.find('*/', i + 2)
                if j < 0:
                    raise ValueError("unterminated /* comment")
                i = j + 2
            else:
                break
        if i >= n or text[i] != '"':
            break
        # consume one quoted literal
        i += 1
        while i < n:
            c = text[i]
            if c == '"':
                i += 1
                break
            if c == '\\':
                if i + 1 >= n:
                    raise ValueError("dangling backslash")
                e = text[i + 1]
                if e == 'n':
                    out.append(0xFE)
                    i += 2
                elif e == 't':
                    out.append(0x09)
                    i += 2
                elif e == '\\':
                    out.append(0x5C)
                    i += 2
                elif e == '"':
                    out.append(0x22)
                    i += 2
                elif e == 'x':
                    hexchars = text[i + 2:i + 4]
                    if len(hexchars) < 2 or not all(
                        h in "0123456789abcdefABCDEF" for h in hexchars
                    ):
                        raise ValueError(f"bad \\x escape near {text[i:i+6]!r}")
                    out.append(int(hexchars, 16))
                    i += 4
                elif e == '0':
                    out.append(0x00)
                    i += 2
                elif e == 'r':
                    out.append(0x0D)
                    i += 2
                elif e == "'":
                    out.append(0x27)
                    i += 2
                else:
                    raise ValueError(f"unknown escape \\{e} near {text[i:i+6]!r}")
            else:
                # plain ASCII character
                out.append(ord(c))
                i += 1
        # loop to look for another adjacent literal
    # Append the implicit $FF terminator that the C arrays drop.
    out.append(0xFF)
    return bytes(out), i


def parse_array_of_strings(c_src, marker):
    """Find ``marker`` (e.g. ``static const char *const tutorial_messages[``),
    skip past ``= {``, then iterate: each entry is a string literal
    (possibly multi-line) followed by ``,`` (or ``};`` for the last).

    Returns list[bytes].
    """
    m = re.search(re.escape(marker), c_src)
    if not m:
        raise RuntimeError(f"could not find {marker!r}")
    # find the opening '{'
    brace = c_src.index('{', m.end())
    i = brace + 1
    entries = []
    while True:
        # skip ws/comments and check for end
        j = i
        n = len(c_src)
        while j < n:
            c = c_src[j]
            if c in " \t\r\n":
                j += 1
            elif c == '/' and j + 1 < n and c_src[j + 1] == '/':
                while j < n and c_src[j] != '\n':
                    j += 1
            elif c == '/' and j + 1 < n and c_src[j + 1] == '*':
                k = c_src.find('*/', j + 2)
                if k < 0:
                    raise ValueError("unterminated comment")
                j = k + 2
            else:
                break
        if j >= n:
            raise RuntimeError("ran off end of file")
        if c_src[j] == '}':
            break
        # parse one string literal entry
        raw, end_i = parse_c_string_literal(c_src, j)
        entries.append(raw)
        i = end_i
        # skip ws/comments then expect ','
        n = len(c_src)
        while i < n:
            c = c_src[i]
            if c in " \t\r\n":
                i += 1
            elif c == '/' and i + 1 < n and c_src[i + 1] == '/':
                while i < n and c_src[i] != '\n':
                    i += 1
            elif c == '/' and i + 1 < n and c_src[i + 1] == '*':
                k = c_src.find('*/', i + 2)
                if k < 0:
                    raise ValueError("unterminated comment")
                i = k + 2
            else:
                break
        if i < n and c_src[i] == ',':
            i += 1
    return entries


def parse_encyclopedia_full(c_src):
    """Walk encyclopedia_pages_full[] and return list of dicts with:
        palette_idx, picture_src_bank, picture_src_offset, text_rom_ptr,
        text_bytes (raw, with appended $FF).
    """
    m = re.search(r'encyclopedia_pages_full\[\d+\]\s*=\s*\{', c_src)
    if not m:
        raise RuntimeError("encyclopedia_pages_full[] not found")
    body_start = m.end()
    # find matching closing brace of the array (track depth)
    depth = 1
    i = body_start
    while depth > 0 and i < len(c_src):
        c = c_src[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1
    body = c_src[body_start : i - 1]

    # Split on `},` at depth 0 — each chunk is one struct initializer.
    entries = []
    depth = 0
    chunk_start = 0
    for j, c in enumerate(body):
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                entries.append(body[chunk_start : j + 1])
                # skip optional ','
                k = j + 1
                while k < len(body) and body[k] in " \t\r\n,":
                    k += 1
                chunk_start = k

    out = []
    field_re = re.compile(
        r'\.palette_idx\s*=\s*(0x[0-9a-fA-F]+)\s*,'
        r'.*?\.picture_src_bank\s*=\s*(0x[0-9a-fA-F]+)\s*,'
        r'.*?\.picture_src_offset\s*=\s*(0x[0-9a-fA-F]+)\s*,'
        r'.*?\.text_rom_ptr\s*=\s*(0x[0-9a-fA-F]+)\s*,'
        r'.*?\.text\s*=',
        re.DOTALL,
    )
    for chunk in entries:
        mm = field_re.search(chunk)
        if not mm:
            raise RuntimeError(f"could not parse metadata in chunk: {chunk[:120]!r}")
        palette = int(mm.group(1), 16)
        pic_bank = int(mm.group(2), 16)
        pic_off = int(mm.group(3), 16)
        text_ptr = int(mm.group(4), 16)
        # The string literal sits right after ".text =".
        raw, _ = parse_c_string_literal(chunk, mm.end())
        out.append({
            "palette_idx": palette,
            "picture_src_bank": pic_bank,
            "picture_src_offset": pic_off,
            "text_rom_ptr": text_ptr,
            "text_bytes": raw,
        })
    return out


# ---------------------------------------------------------------- diff
def diff_bytes(rom, c, label):
    """Return list of (offset, rom_byte, c_byte) divergences."""
    divs = []
    n = max(len(rom), len(c))
    for k in range(n):
        rb = rom[k] if k < len(rom) else None
        cb = c[k] if k < len(c) else None
        if rb != cb:
            divs.append((k, rb, cb))
    return divs


def main():
    enc_full = parse_encyclopedia_full(C_SRC)
    enc_text_c = parse_array_of_strings(
        C_SRC, "static const char *const encyclopedia_pages[30]"
    )
    tut_c = parse_array_of_strings(
        C_SRC, "static const char *const tutorial_messages[54]"
    )

    # The shortlist-array `encyclopedia_pages[]` is just pointers
    # into encyclopedia_pages_full[].text — parse_array_of_strings will
    # see no string literals and produce 0 entries, so use the .text
    # bytes from encyclopedia_pages_full instead.
    if not enc_text_c:
        enc_text_c = [e["text_bytes"] for e in enc_full]

    failures = []
    enc_bytes_total = 0
    tut_bytes_total = 0

    # ---------- encyclopedia metadata ----------
    meta_fail = []
    for i in range(30):
        e = enc_full[i]
        if e["palette_idx"] != PALETTE_IDX[i]:
            meta_fail.append((i, "palette_idx", PALETTE_IDX[i], e["palette_idx"]))
        if e["picture_src_bank"] != PICTURE_BANK[i]:
            meta_fail.append((i, "picture_src_bank", PICTURE_BANK[i], e["picture_src_bank"]))
        if e["picture_src_offset"] != PICTURE_OFFSET[i]:
            meta_fail.append((i, "picture_src_offset", PICTURE_OFFSET[i], e["picture_src_offset"]))
        if e["text_rom_ptr"] != TEXT_PTR[i]:
            meta_fail.append((i, "text_rom_ptr", TEXT_PTR[i], e["text_rom_ptr"]))

    # ---------- encyclopedia text byte-diff ----------
    enc_fail = []
    for i in range(30):
        rom_bytes = ENC_ROM_BYTES[i]
        c_bytes = enc_text_c[i]
        enc_bytes_total += len(rom_bytes)
        divs = diff_bytes(rom_bytes, c_bytes, f"enc[{i}]")
        if divs:
            enc_fail.append((i, len(rom_bytes), len(c_bytes), divs))

    # ---------- tutorial text byte-diff ----------
    tut_fail = []
    for i in range(54):
        rom_bytes = TUT_ROM_BYTES[i]
        c_bytes = tut_c[i]
        tut_bytes_total += len(rom_bytes)
        divs = diff_bytes(rom_bytes, c_bytes, f"tut[{i}]")
        if divs:
            tut_fail.append((i, len(rom_bytes), len(c_bytes), divs))

    # ---------- report ----------
    ok = (not meta_fail) and (not enc_fail) and (not tut_fail)
    print(f"=== text_verify.py ===")
    print(f"ROM:                 {ROM_PATH}")
    print(f"C file:              {C_PATH}")
    print(f"Encyclopedia pages:  30  ({enc_bytes_total} bytes incl $FF)")
    print(f"Tutorial messages:   54  ({tut_bytes_total} bytes incl $FF)")
    print(f"Metadata failures:   {len(meta_fail)}")
    print(f"Encyclopedia diffs:  {len(enc_fail)} pages with byte divergences")
    print(f"Tutorial diffs:      {len(tut_fail)} messages with byte divergences")

    if meta_fail:
        print()
        print("---- METADATA DIVERGENCES ----")
        for i, field, rom, c in meta_fail:
            print(f"  page {i:2d} {field}: ROM=0x{rom:04X}  C=0x{c:04X}")

    if enc_fail:
        print()
        print("---- ENCYCLOPEDIA TEXT DIVERGENCES ----")
        for i, rl, cl, divs in enc_fail:
            print(f"  page {i:2d}: ROM={rl} bytes  C={cl} bytes  ({len(divs)} diffs)")
            for off, rb, cb in divs[:12]:
                rs = f"0x{rb:02X}" if rb is not None else "--"
                cs = f"0x{cb:02X}" if cb is not None else "--"
                print(f"     offset 0x{off:04X}: ROM={rs}  C={cs}")
            if len(divs) > 12:
                print(f"     ... +{len(divs)-12} more")

    if tut_fail:
        print()
        print("---- TUTORIAL TEXT DIVERGENCES ----")
        for i, rl, cl, divs in tut_fail:
            print(f"  msg {i:2d}: ROM={rl} bytes  C={cl} bytes  ({len(divs)} diffs)")
            for off, rb, cb in divs[:12]:
                rs = f"0x{rb:02X}" if rb is not None else "--"
                cs = f"0x{cb:02X}" if cb is not None else "--"
                print(f"     offset 0x{off:04X}: ROM={rs}  C={cs}")
            if len(divs) > 12:
                print(f"     ... +{len(divs)-12} more")

    if ok:
        print()
        print("PASS: text_content.c is byte-identical to ROM for all 30 pages + 54 messages.")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())

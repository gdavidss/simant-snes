#!/usr/bin/env python3
"""
Coverage analysis for SimAnt SNES decomp.

Maps every lifted function to its ROM address, finds dead zones,
called-but-unlifted symbols, lifted-but-uncalled functions, and
disassembles control flow targets from key banks to find missing lifts.
"""
import os, re, sys, struct, subprocess, collections
from pathlib import Path

ROOT = Path("/Users/guilhermedavid/simant-re")
ROM = ROOT / "simant.sfc"
ROM_DATA = ROM.read_bytes()
ROM_SIZE = len(ROM_DATA)  # 0x100000 = 1 MB
BANK_SIZE = 0x8000

import importlib.util
spec = importlib.util.spec_from_file_location("disasm", ROOT / "disasm.py")
disasm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(disasm)
OPCODES = disasm.OPCODES
operand_size = disasm.operand_size
lorom_to_file = disasm.lorom_to_file

# ---------- 1. Lifted functions: parse all .c files ---------------------------
C_FILES = sorted(ROOT.glob("*.c"))

# An addr token in this codebase looks like a trailing _XXXX (4-6 hex) where
# the value, when interpreted as a 24-bit ROM address, is in range. We do not
# match purely alphabetic tokens (e.g. `_fade`) because the project always uses
# at least one decimal digit in real addresses. So require at least one digit.
ADDR_RE = re.compile(r"_(?=[0-9A-Fa-f]*[0-9])([0-9A-Fa-f]{4,6})$")
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")

def token_to_rom_off(tok):
    """Decode an addr token (4, 5, or 6 hex chars) to a ROM file offset.
    Conventions used in the project:
      - 4 chars XXXX  : bank-0 CPU addr (0x8000..0xFFFF). Bank 0 file off = XXXX & 0x7FFF.
      - 5 chars BXXXX : bank 0x0B, off XXXX  (B is 1 hex digit, 0..7).
      - 6 chars BBXXXX: bank 0xBB, off XXXX.
    """
    L = len(tok)
    if L == 4:
        v = int(tok, 16)
        if 0x8000 <= v <= 0xFFFF:
            return (0 << 15) | (v & 0x7FFF)
    elif L == 5:
        bank = int(tok[0], 16)
        off  = int(tok[1:], 16)
        if 0x8000 <= off <= 0xFFFF and bank < 0x40:
            return ((bank & 0x7F) << 15) | (off & 0x7FFF)
    elif L == 6:
        bank = int(tok[0:2], 16)
        off  = int(tok[2:], 16)
        if 0x8000 <= off <= 0xFFFF and bank < 0x40:
            return ((bank & 0x7F) << 15) | (off & 0x7FFF)
    return None

lifted = {}        # name -> {file, addr_token, rom_off, body_len}
all_func_defs = [] # list of (name, file)
all_calls = collections.Counter()
call_sites_by_func = collections.defaultdict(list)

stub_funcs = set()
weak_funcs = set()

def parse_c_file(path: Path):
    text = path.read_text(errors="replace")
    text_nc = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text_nc = re.sub(r"//[^\n]*", "", text_nc)
    funcs = []
    sig_re = re.compile(
        r"(?P<sig>(?:^|\n)\s*(?:static\s+|extern\s+|inline\s+|__attribute__\(\(weak\)\)\s+)*"
        r"(?:void|u?int(?:8|16|32)_t|char|bool|unsigned\s+\w+|signed\s+\w+|long|short)"
        r"\s+(?P<name>\w+)\s*\([^)]*\))\s*\{"
    )
    for m in sig_re.finditer(text_nc):
        name = m.group("name")
        start = m.end()
        depth = 1
        i = start
        L = len(text_nc)
        while i < L and depth > 0:
            ch = text_nc[i]
            if ch == '{': depth += 1
            elif ch == '}': depth -= 1
            i += 1
        if depth != 0:
            continue
        body = text_nc[start:i-1]
        is_weak = "__attribute__((weak))" in m.group("sig")
        if is_weak:
            weak_funcs.add(name)
        stripped = body.strip()
        if stripped == "" or re.fullmatch(r"return\s*(?:\(?0\)?|\w*)\s*;", stripped):
            stub_funcs.add(name)
        funcs.append((name, body, m.group("sig")))
    return funcs

print("[1] Parsing .c files for function defs + calls ...", file=sys.stderr)
file_to_funcs = {}
for path in C_FILES:
    funcs = parse_c_file(path)
    file_to_funcs[path.name] = funcs
    for name, body, sig in funcs:
        m = ADDR_RE.search(name)
        addr_token = m.group(1) if m else None
        rom_off = token_to_rom_off(addr_token) if addr_token else None
        if name not in lifted or (lifted[name].get("rom_off") is None and rom_off is not None):
            lifted[name] = {
                "file": path.name,
                "addr_token": addr_token,
                "rom_off": rom_off,
                "body_len": len(body),
            }
        all_func_defs.append((name, path.name))
        for cm in CALL_RE.finditer(body):
            cname = cm.group(1)
            if cname in {"if","while","for","switch","return","sizeof","do",
                         "void","int","unsigned","signed","char","short","long",
                         "uint8_t","uint16_t","uint32_t","int8_t","int16_t","int32_t",
                         "bool","static","extern","inline","case","default","goto",
                         "break","continue","__attribute__"}:
                continue
            all_calls[cname] += 1
            call_sites_by_func[cname].append(name)

print(f"   parsed {len(C_FILES)} files, {len(all_func_defs)} function defs", file=sys.stderr)
print(f"   {len(stub_funcs)} stub-like, {len(weak_funcs)} weak", file=sys.stderr)

# ---------- 2. Build ROM-coverage bitmap ---------------------------
print("[2] Building per-byte coverage map ...", file=sys.stderr)

lifts_by_off = sorted(
    [(info["rom_off"], name) for name, info in lifted.items() if info["rom_off"] is not None],
    key=lambda x: x[0],
)
seen_off = {}
for off, name in lifts_by_off:
    seen_off.setdefault(off, name)
lifts_by_off = sorted(seen_off.items())

covered = bytearray(ROM_SIZE)

def estimate_func_size(rom_off, max_len=0x800):
    M, X = 1, 1
    i = 0
    while i < max_len:
        p = rom_off + i
        if p >= ROM_SIZE:
            break
        opc = ROM_DATA[p]
        info = OPCODES.get(opc)
        if info is None:
            i += 1; continue
        mnem, mode = info
        n = operand_size(mode, M, X)
        if mnem == "SEP":
            v = ROM_DATA[p+1] if p+1 < ROM_SIZE else 0
            if v & 0x20: M = 1
            if v & 0x10: X = 1
        elif mnem == "REP":
            v = ROM_DATA[p+1] if p+1 < ROM_SIZE else 0
            if v & 0x20: M = 0
            if v & 0x10: X = 0
        i += 1 + n
        if mnem in ("RTS","RTL","RTI","BRA","BRL","JMP","JML","STP"):
            break
    return i

for idx, (off, name) in enumerate(lifts_by_off):
    next_off = lifts_by_off[idx+1][0] if idx+1 < len(lifts_by_off) else ROM_SIZE
    cap = min(0x800, max(1, next_off - off))
    size = estimate_func_size(off, max_len=cap)
    for b in range(off, min(off + size, ROM_SIZE)):
        covered[b] = 1

total_covered = sum(covered)
print(f"   {total_covered:,} / {ROM_SIZE:,} bytes covered ({100*total_covered/ROM_SIZE:.1f}%)", file=sys.stderr)

# ---------- 3. Scan ALL banks for JSR/JSL/JMP/JML targets ----------
# We use the targets to build a "definitely code" map and refine data/code split.
print("[3] Scanning ALL banks for JSR/JSL/JMP/JML targets ...", file=sys.stderr)

def linear_disasm_collect_targets(bank, start_off=0x8000):
    targets = []
    M, X = 1, 1
    pc = start_off
    end = 0x10000
    while pc < end:
        f = lorom_to_file((bank << 16) | pc)
        if f is None or f >= ROM_SIZE: break
        opc = ROM_DATA[f]
        info = OPCODES.get(opc)
        if info is None:
            pc += 1; continue
        mnem, mode = info
        n = operand_size(mode, M, X)
        raw = ROM_DATA[f+1:f+1+n]
        if mnem == "SEP":
            v = raw[0] if raw else 0
            if v & 0x20: M = 1
            if v & 0x10: X = 1
        elif mnem == "REP":
            v = raw[0] if raw else 0
            if v & 0x20: M = 0
            if v & 0x10: X = 0
        if mnem == "JSR" and mode == "a" and len(raw) >= 2:
            target = raw[0] | (raw[1] << 8)
            targets.append(("JSR", (bank << 16) | target))
        elif mnem == "JSL" and mode == "al" and len(raw) >= 3:
            target = raw[0] | (raw[1] << 8) | (raw[2] << 16)
            targets.append(("JSL", target))
        elif mnem == "JMP" and mode == "a" and len(raw) >= 2:
            target = raw[0] | (raw[1] << 8)
            targets.append(("JMP", (bank << 16) | target))
        elif mnem == "JML" and mode == "al" and len(raw) >= 3:
            target = raw[0] | (raw[1] << 8) | (raw[2] << 16)
            targets.append(("JML", target))
        pc += 1 + n
    return targets

all_targets = []
for b in range(N_BANKS := (ROM_SIZE // BANK_SIZE)):
    all_targets.extend(linear_disasm_collect_targets(b))

target_counts = collections.Counter()
for kind, t in all_targets:
    target_counts[t] += 1

# Convert targets to ROM offsets and a "this offset is definitely a function entry" map
target_entry_offs = set()
for t in target_counts:
    bank = (t >> 16) & 0xFF
    off  = t & 0xFFFF
    if off < 0x8000: continue
    ro = ((bank & 0x7F) << 15) | (off & 0x7FFF)
    if 0 <= ro < ROM_SIZE:
        target_entry_offs.add(ro)

print(f"   {len(all_targets):,} call/jump instances, {len(target_counts):,} unique targets, "
      f"{len(target_entry_offs):,} ROM-mapped", file=sys.stderr)

# ---------- 4. Estimate code vs data using a stricter heuristic ---------------
# Definition: a 256-byte chunk is "code" if:
#   - it contains at least one byte that is a JSR/JSL target (definitively an entry), OR
#   - it contains at least 4 RTS/RTL/RTI opcodes (function terminators), OR
#   - it is reachable by walking forward from a known function entry (covered map).
# Otherwise it is "data".
print("[4] Estimating code vs data per 256-byte chunk ...", file=sys.stderr)

CHUNK = 256
RTS_OPCODES = {0x60, 0x6B, 0x40}  # RTS, RTL, RTI
chunk_class = []
for c in range(0, ROM_SIZE, CHUNK):
    chunk = ROM_DATA[c:c+CHUNK]
    is_code = False
    # rule 1: contains a target entry
    for to in target_entry_offs:
        if c <= to < c + CHUNK:
            is_code = True; break
    # rule 2: contains many returns
    if not is_code:
        rts_count = sum(1 for b in chunk if b in RTS_OPCODES)
        # 4+ returns in a 256-byte block strongly indicates code (each subroutine ends in one)
        if rts_count >= 4:
            is_code = True
    # rule 3: heavily covered already
    if not is_code:
        cov = sum(covered[c:c+CHUNK])
        if cov > CHUNK * 0.5:
            is_code = True
    chunk_class.append("code" if is_code else "data")

estimated_code_bytes = sum(1 for k in chunk_class if k == "code") * CHUNK
estimated_data_bytes = sum(1 for k in chunk_class if k == "data") * CHUNK
print(f"   ~{estimated_code_bytes:,} bytes code, ~{estimated_data_bytes:,} bytes data", file=sys.stderr)

# Per-bank coverage
banks = []
N_BANKS = (ROM_SIZE + BANK_SIZE - 1) // BANK_SIZE
for b in range(N_BANKS):
    s = b * BANK_SIZE
    e = min(s + BANK_SIZE, ROM_SIZE)
    cov = sum(covered[s:e])
    code_chunks = sum(1 for ci in range(s // CHUNK, e // CHUNK) if chunk_class[ci] == "code")
    code_b = code_chunks * CHUNK
    n_targets = sum(1 for to in target_entry_offs if s <= to < e)
    n_lifts = sum(1 for off, _ in lifts_by_off if s <= off < e)
    banks.append({
        "bank": b,
        "total": e - s,
        "covered": cov,
        "code_est": code_b,
        "data_est": (e - s) - code_b,
        "pct_total": 100 * cov / (e - s) if (e - s) else 0,
        "pct_code":  100 * cov / code_b if code_b else 0,
        "n_targets": n_targets,
        "n_lifts": n_lifts,
    })

# ---------- 5. DEAD ZONES -----------------------------------------------------
print("[5] Finding dead zones ...", file=sys.stderr)
dead_zones = []
i = 0
while i < ROM_SIZE:
    if covered[i] == 0:
        j = i
        while j < ROM_SIZE and covered[j] == 0:
            j += 1
        size = j - i
        if size >= 0x100:
            n_code = sum(1 for ci in range(i // CHUNK, (j + CHUNK - 1) // CHUNK)
                         if ci < len(chunk_class) and chunk_class[ci] == "code")
            n_data = sum(1 for ci in range(i // CHUNK, (j + CHUNK - 1) // CHUNK)
                         if ci < len(chunk_class) and chunk_class[ci] == "data")
            # Count call targets inside the gap
            n_in_targets = sum(1 for to in target_entry_offs if i <= to < j)
            if n_in_targets > 0:
                kind = f"unlifted-code ({n_in_targets} call targets inside)"
            elif n_data > n_code:
                kind = "data (no call targets)"
            else:
                kind = "code (no call targets — but RTS-density triggered)"
            dead_zones.append({
                "start": i, "end": j, "size": size,
                "code_chunks": n_code, "data_chunks": n_data,
                "n_targets": n_in_targets,
                "kind": kind,
            })
        i = j
    else:
        i += 1

dead_zones_sorted = sorted(dead_zones, key=lambda z: -z["size"])
print(f"   {len(dead_zones)} dead zones >= 256 bytes", file=sys.stderr)

# ---------- 6. LIFTED-BUT-UNCALLED -------------------------------------------
print("[6] Lifted-but-uncalled detection ...", file=sys.stderr)
uncalled = []
for name, info in lifted.items():
    if name in {"main", "snes_main", "_start"}:
        continue
    if name.endswith("_test") or name.startswith("test_"):
        continue
    if name in stub_funcs:
        continue
    if all_calls.get(name, 0) == 0:
        uncalled.append((name, info["file"], info.get("addr_token"), info.get("rom_off")))
uncalled.sort(key=lambda x: x[0])

# ---------- 7. CALLED-BUT-NOT-LIFTED (linker probe) --------------------------
print("[7] Running linker probe for unresolved symbols ...", file=sys.stderr)
o_files = sorted(ROOT.glob("*.o"))
unresolved = []
if o_files:
    cmd = ["clang", "-o", "/tmp/coverage_probe"] + [str(o) for o in o_files] + ["-Wl,-undefined,dynamic_lookup"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    for line in (r.stderr + r.stdout).splitlines():
        m = re.search(r'"_([A-Za-z_][A-Za-z0-9_]*)"', line)
        if m:
            unresolved.append(m.group(1))
unresolved = list(set(unresolved))

nm_out = subprocess.run(["nm", "-u"] + [str(o) for o in o_files], capture_output=True, text=True).stdout
nm_undef = set()
for line in nm_out.splitlines():
    line = line.strip()
    if line.startswith("_"):
        nm_undef.add(line[1:])
all_undef = set(unresolved) | nm_undef
weak_only = [n for n in all_undef if n in lifted and n in weak_funcs]
genuinely_missing = [n for n in all_undef if n not in lifted]
weak_call_counts = sorted([(n, all_calls.get(n, 0)) for n in weak_only], key=lambda x: -x[1])
missing_call_counts = sorted([(n, all_calls.get(n, 0)) for n in genuinely_missing], key=lambda x: -x[1])
print(f"   {len(weak_only)} weak-only symbols, {len(genuinely_missing)} genuinely missing", file=sys.stderr)

# ---------- 8. STUB DENSITY by file ------------------------------------------
print("[8] Stub density by file ...", file=sys.stderr)
file_stub_counts = collections.Counter()
file_total_counts = collections.Counter()
for name, file in all_func_defs:
    file_total_counts[file] += 1
    if name in stub_funcs or name in weak_funcs:
        file_stub_counts[file] += 1

file_stub_density = []
for file, total in file_total_counts.items():
    sc = file_stub_counts[file]
    file_stub_density.append((file, sc, total, 100*sc/total if total else 0))
file_stub_density.sort(key=lambda x: -x[1])

# ---------- 9. Unlifted entry points (call targets with no lift) --------------
print("[9] Building unlifted-entry-point list ...", file=sys.stderr)
lifted_offsets = set(off for off, _ in lifts_by_off)
unlifted_targets = []
for t, ct in target_counts.most_common():
    bank = (t >> 16) & 0xFF
    off  = t & 0xFFFF
    if off < 0x8000:
        continue  # WRAM target
    ro = ((bank & 0x7F) << 15) | (off & 0x7FFF)
    if ro >= ROM_SIZE:
        continue
    if covered[ro] == 0 and ro not in lifted_offsets:
        unlifted_targets.append((t, ct, ro))
print(f"   {len(unlifted_targets)} unlifted entry points", file=sys.stderr)

# Cluster unlifted targets into "subsystem buckets" by neighborhood
def bucket(t):
    bank = (t >> 16) & 0xFF
    off  = t & 0xFFFF
    # 0x800-byte buckets
    return (bank, off & 0xF800)
bucket_counts = collections.Counter()
for t, ct, ro in unlifted_targets:
    bucket_counts[bucket(t)] += ct

# ---------- 10. Write report --------------------------------------------------
print("[10] Writing COVERAGE_ANALYSIS.md ...", file=sys.stderr)

lines = []
W = lines.append

W("# SimAnt SNES decomp — Coverage Analysis (V3-C)")
W("")
W(f"_Generated by `coverage_analysis.py` against ROM `{ROM.name}` (1 MB LoROM).  "
  f"Reflects state of `{ROOT.name}/` at run time._")
W("")
W("## Methodology")
W("")
W("1. Each lifted function name encodes a ROM address: `sub_XXXX` (bank-0), "
  "`sub_BXXXX` (5-char: bank B, off XXXX), or `sub_BBXXXX` (6-char: explicit bank).")
W("2. For every lifted function we walk the ROM at its entry until the first "
  "`RTS/RTL/RTI/JMP/JML/BRA` terminator (capped at 2 KB) to estimate its size, "
  "and mark those bytes covered.")
W("3. Code-vs-data per 256-byte chunk: a chunk is **code** if it contains a "
  "JSR/JSL target, has ≥4 `RTS/RTL/RTI` opcodes, or is >50% already covered. "
  "Otherwise it is **data**.")
W("4. Call counts: every `name(...)` in any function body (across all .c files) "
  "counts as one call site for that name.")
W("5. Stub detection: a function whose body is empty or `return 0;`; or has the "
  "`__attribute__((weak))` linkage.")
W("6. Call targets discovered by linearly disassembling every bank and "
  "collecting `JSR/JSL/JMP/JML` operands.")
W("")
W("## Top-level numbers")
W("")
W(f"- ROM size: **{ROM_SIZE:,}** bytes (1,048,576 / 0x100000)")
total_loc = sum(p.read_text(errors='replace').count(chr(10)) for p in C_FILES)
W(f"- C files: **{len(C_FILES)}**, **{total_loc:,} lines total**")
W(f"- Lifted function defs: **{len(all_func_defs)}** (unique names: **{len(lifted)}**)")
W(f"- Lifts with a parseable ROM address: **{len(lifts_by_off)}**")
W(f"- Weak (link-glue) functions: **{len(weak_funcs)}**")
W(f"- Stub-bodied functions (empty / `return 0`): **{len(stub_funcs)}**")
W("")
W(f"### Byte-level coverage")
W("")
W(f"- Lifted **executable bytes** (sum of disassembly-walked function sizes): **{total_covered:,}** ({100*total_covered/ROM_SIZE:.2f}% of total ROM)")
W(f"- Estimated **code bytes** in ROM (target/RTS-density heuristic): **{estimated_code_bytes:,}** ({100*estimated_code_bytes/ROM_SIZE:.1f}% of ROM)")
W(f"- Estimated **data bytes** in ROM: **{estimated_data_bytes:,}** ({100*estimated_data_bytes/ROM_SIZE:.1f}% of ROM)")
W(f"- **Coverage of executable code: {100*total_covered/max(1,estimated_code_bytes):.1f}%**")
W("")

# Per-bank table
W("## Per-bank breakdown")
W("")
W("| Bank | Total | Est. code | Est. data | Lifted bytes | Lifts | Call targets | % of total | % of est. code |")
W("|------|-------|-----------|-----------|--------------|-------|--------------|------------|----------------|")
for b in banks:
    W(f"| ${b['bank']:02X} | {b['total']:,} | {b['code_est']:,} | {b['data_est']:,} | "
      f"{b['covered']:,} | {b['n_lifts']} | {b['n_targets']} | "
      f"{b['pct_total']:.1f}% | {b['pct_code']:.1f}% |")
W("")

# Dead zones
W("## Dead zones (uncovered ROM ranges ≥ 256 bytes)")
W("")
W(f"Found **{len(dead_zones)}** such zones. Top 40 by size:")
W("")
W("| ROM start | ROM end | Size | CPU range | # call-targets inside | Kind |")
W("|-----------|---------|------|-----------|----------------------|------|")
for z in dead_zones_sorted[:40]:
    sb = (z['start'] >> 15) & 0x7F
    so = (z['start'] & 0x7FFF) | 0x8000
    eb = ((z['end']-1) >> 15) & 0x7F
    eo = ((z['end']-1) & 0x7FFF) | 0x8000
    W(f"| 0x{z['start']:06X} | 0x{z['end']:06X} | {z['size']:#x} ({z['size']:,}) | "
      f"${sb:02X}:{so:04X}–${eb:02X}:{eo:04X} | {z['n_targets']} | {z['kind']} |")
W("")

# Lifted-but-uncalled
W(f"## Lifted-but-uncalled functions ({len(uncalled)})")
W("")
W("Functions defined but with no caller in any .c file. Likely causes:")
W("")
W("- **(a) Indirect-jump targets** — called via `JMP (a,x)` / `JSR (a,x)` jump tables that the static scan can't follow.")
W("- **(b) State/entity handlers** — registered by ID into a dispatch table; never called by name.")
W("- **(c) Top-level entry points** — `boot`, `nmi_handler`, etc.")
W("- **(d) Genuine dead code** — never reached in the original ROM.")
W("")
W("**Do not remove without checking the dispatch tables first.**")
W("")
W("| Function | File | ROM token | ROM offset |")
W("|----------|------|-----------|-----------|")
for n, f, t, ro in uncalled[:250]:
    W(f"| `{n}` | {f} | {t or '—'} | {('0x%06X'%ro) if ro is not None else '—'} |")
if len(uncalled) > 250:
    W(f"| ...{len(uncalled)-250} more... | | | |")
W("")

# Called-but-not-lifted
W("## Called-but-not-lifted (weak stubs)")
W("")
W(f"Total **{len(weak_only)}** symbols where the only body is a weak stub. "
  f"Top 30 by call count — **these are the highest-value lift targets.**")
W("")
W("| Symbol | Call sites | File of stub | Inferred ROM addr |")
W("|--------|-----------|--------------|--------------------|")
for name, ct in weak_call_counts[:30]:
    file = lifted.get(name, {}).get("file", "—")
    tok  = lifted.get(name, {}).get("addr_token") or "—"
    ro   = lifted.get(name, {}).get("rom_off")
    addr = f"0x{ro:06X}" if ro is not None else "—"
    W(f"| `{name}` | {ct} | {file} | {addr} (`{tok}`) |")
W("")
W(f"### Genuinely unresolved (no body anywhere)")
W("")
W(f"There are **{len(genuinely_missing)}** symbols the linker probe couldn't "
  f"resolve at all. Top 25 by call count:")
W("")
W("| Symbol | Call sites |")
W("|--------|-----------|")
for name, ct in missing_call_counts[:25]:
    W(f"| `{name}` | {ct} |")
W("")

# Stub density
W("## Stub density by file")
W("")
W("| File | Stub funcs | Total defs | Stub % |")
W("|------|-----------|------------|--------|")
for f, sc, tot, pct in file_stub_density:
    W(f"| {f} | {sc} | {tot} | {pct:.1f}% |")
W("")

# Missing entry points - now project-wide
W(f"## Disassembly-discovered unlifted entry points")
W("")
W(f"Linearly disassembled **every** ROM bank ($00..${N_BANKS-1:02X}) and collected "
  f"every `JSR`, `JSL`, `JMP`, `JML` target operand. Found "
  f"**{len(target_counts):,}** unique targets, **{len(unlifted_targets)}** of which "
  f"have no lifted function at the exact ROM offset.")
W("")
W("Top 60 unlifted call targets by frequency:")
W("")
W("| CPU addr | ROM off | Call sites | Chunk class |")
W("|----------|---------|-----------|-------------|")
for t, ct, ro in unlifted_targets[:60]:
    bank = (t >> 16) & 0xFF
    off  = t & 0xFFFF
    chunk_kind = chunk_class[ro // CHUNK] if ro // CHUNK < len(chunk_class) else "?"
    W(f"| ${bank:02X}:{off:04X} | 0x{ro:06X} | {ct} | {chunk_kind} |")
W("")

# Bucket subsystems
W("### Hottest subsystem regions (2 KB buckets, by total call weight)")
W("")
W("Each row collects the call-counts of *all* unlifted entries in a 0x800-byte "
  "neighborhood. Useful for prioritizing which slice of ROM to attack next.")
W("")
W("| Bank | Region | Total weight |")
W("|------|--------|--------------|")
for (bank, region), weight in bucket_counts.most_common(25):
    W(f"| ${bank:02X} | ${region:04X}–${region+0x7FF:04X} | {weight} |")
W("")

W("---")
W("")
W(f"_Generated by `coverage_analysis.py`. Re-run after each lift round._")

out_path = ROOT / "COVERAGE_ANALYSIS.md"
out_path.write_text("\n".join(lines))
print(f"\nWrote {out_path} ({out_path.stat().st_size:,} bytes)", file=sys.stderr)

import json
summary = {
    "rom_size": ROM_SIZE,
    "lifts_with_addr": len(lifts_by_off),
    "total_func_defs": len(all_func_defs),
    "weak": len(weak_funcs),
    "stub": len(stub_funcs),
    "covered_bytes": total_covered,
    "covered_pct_total": 100*total_covered/ROM_SIZE,
    "est_code_bytes": estimated_code_bytes,
    "est_data_bytes": estimated_data_bytes,
    "covered_pct_code": 100*total_covered/max(1,estimated_code_bytes),
    "per_bank": banks,
    "dead_zones_total": len(dead_zones),
    "lifted_but_uncalled": len(uncalled),
    "weak_stubbed_calls": len(weak_only),
    "weak_top10": weak_call_counts[:10],
    "unlifted_targets": len(unlifted_targets),
    "unlifted_targets_top10": [(f"{(t>>16)&0xFF:02X}:{t&0xFFFF:04X}", ct, ro) for t, ct, ro in unlifted_targets[:10]],
    "missing_genuine": len(genuinely_missing),
    "stub_density_top": [(f, sc, tot, pct) for f, sc, tot, pct in file_stub_density[:10]],
}
(ROOT / "coverage_summary.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))

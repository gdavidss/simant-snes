#!/usr/bin/env python3
"""Aggressive ROM exploration: entity handlers, state handlers, hardware
access scans, and known data tables. Built on top of disasm.py."""
import sys, struct
sys.path.insert(0, ".")
from disasm import (data, lorom_to_file, file_to_lorom,
                    disassemble, OPCODES, HW)

def hex_table(addr_bank, base_lorom, count, stride=2, label="entry"):
    """Read `count` little-endian 16-bit pointers from bank/lorom and print
    them with the file offset they decode to."""
    f0 = lorom_to_file((addr_bank<<16) | base_lorom)
    out = []
    for i in range(count):
        off = f0 + i*stride
        if off+stride > len(data): break
        ptr = data[off] | (data[off+1] << 8)
        out.append((i, ptr))
    return out

# ---------------------------------------------------------------------------
# 1. Entity handler dispatch table at $04:9A30. 32 entries observed.
# ---------------------------------------------------------------------------
print("=" * 72)
print("ENTITY HANDLER TABLE @ $04:9A30")
print("=" * 72)
HANDLERS = hex_table(0x04, 0x9A30, 32)
for i, ptr in HANDLERS:
    print(f"  type={i:02d}  -> $04:{ptr:04X}  (file ${lorom_to_file(0x040000|ptr):05X})")

print("\n--- Prologue of each entity handler (first 0x18 bytes) ---")
print("(M=1, X=0 assumed — caller is in NMI prologue)\n")
seen = set()
for i, ptr in HANDLERS:
    if ptr in seen:
        print(f"  type={i:02d}: alias of an earlier handler at $04:{ptr:04X}")
        continue
    seen.add(ptr)
    print(f"\n[type {i:02d}] $04:{ptr:04X}")
    print(disassemble(ptr, length=0x30, M=1, X=0, bank=0x04, stop_on_return=True))

# ---------------------------------------------------------------------------
# 2. Game-state jump table at $00:9369 (called from task #1 at $935C).
# ---------------------------------------------------------------------------
print("\n\n" + "=" * 72)
print("GAME-STATE JUMP TABLE @ $00:9369")
print("=" * 72)
# Number of entries unknown — manual mentions 10 states. Dump 16 to be safe.
STATES = hex_table(0x00, 0x9369, 16)
state_names = {
    0:"GS_FULL_GAME", 1:"GS_SCENARIO_GAME", 2:"GS_SAVED_GAME",
    3:"GS_TUTORIAL", 4:"GS_ANT_INFORMATION", 5:"GS_MARRIAGE_FLIGHT",
    6:"GS_FULL_END", 7:"GS_SCENARIO_END", 8:"GS_GAME_OVER",
    9:"GS_SOUND",
}
for i, ptr in STATES[:10]:
    nm = state_names.get(i, "(?)")
    print(f"  state={i:02d} {nm:<20s} -> $00:{ptr:04X}")

print("\n--- Prologue of each game-state handler (first 0x40 bytes) ---\n")
for i, ptr in STATES[:10]:
    nm = state_names.get(i, "(?)")
    print(f"\n[{nm}] $00:{ptr:04X}")
    print(disassemble(ptr, length=0x40, M=1, X=0, bank=0x00, stop_on_return=True))

# ---------------------------------------------------------------------------
# 3. Hardware-access scan: find code touching APU IO, SRAM, joypad shadows.
#    Strategy: scan absolute writes (8D/9D/9F = STA abs/abx/abl) and the
#    long versions (8F = STA abl) where the operand falls in interesting
#    ranges. Cheap and effective for finding "touches HW X".
# ---------------------------------------------------------------------------
print("\n\n" + "=" * 72)
print("HARDWARE-ACCESS SCAN (writes only)")
print("=" * 72)

def scan_writes(predicate):
    """Yield (file_off, lorom_addr, op_bytes, target_addr) for each write
    whose target satisfies predicate(addr). Doesn't track M/X — fine for
    locating call sites by hardware-register name."""
    out = []
    n = len(data)
    i = 0
    while i < n - 4:
        op = data[i]
        if op in (0x8D, 0x9D, 0x9C):    # STA abs, STA abx, STZ abs
            addr = data[i+1] | (data[i+2] << 8)
            if predicate(addr):
                out.append((i, file_to_lorom(i), op, addr))
            i += 3
        elif op == 0x8F:                # STA abl
            addr24 = data[i+1] | (data[i+2]<<8) | (data[i+3]<<16)
            if predicate(addr24 & 0xFFFF):
                out.append((i, file_to_lorom(i), op, addr24))
            i += 4
        else:
            i += 1
    return out

def show(title, hits, hex_w=6, n=20):
    print(f"\n--- {title} ({len(hits)} sites) ---")
    for h in hits[:n]:
        f, lo, op, t = h
        op_name = {0x8D:"STA abs",0x9D:"STA abx",0x9C:"STZ abs",0x8F:"STA abl"}[op]
        hw = HW.get(t & 0xFFFF, "")
        bank = f >> 15
        print(f"  bank ${bank:02X} file ${f:05X}  lorom ${lo:06X}  {op_name:8s} ${t:0{hex_w}X}  {hw}")
    if len(hits) > n:
        print(f"  ... and {len(hits)-n} more")

# APU IO ports: $2140-$2143 — used to upload SPC700 program/data.
show("APU IO writes ($2140-$2143)",
     scan_writes(lambda a: 0x2140 <= a <= 0x2143))

# SRAM in LoROM: banks $70-$7D, addresses $0000-$7FFF. Since 32 KB SRAM,
# only $70:0000-$70:7FFF is real. Long stores (8F) are the giveaway since
# SRAM lives outside the program bank.
print("\n--- SRAM access (STA $70:xxxx via 8F) ---")
sram_hits = []
for i in range(len(data) - 4):
    if data[i] == 0x8F:
        bank = data[i+3]
        if 0x70 <= bank <= 0x7D:
            addr = data[i+1] | (data[i+2]<<8)
            sram_hits.append((i, file_to_lorom(i), bank, addr))
for h in sram_hits[:25]:
    f, lo, b, a = h
    print(f"  file ${f:05X}  lorom ${lo:06X}  STA ${b:02X}:{a:04X}")
print(f"  total SRAM writes: {len(sram_hits)}")

# Long loads from SRAM (AF = LDA abl)
print("\n--- SRAM reads (LDA $70:xxxx via AF) ---")
sram_reads = []
for i in range(len(data) - 4):
    if data[i] == 0xAF:
        bank = data[i+3]
        if 0x70 <= bank <= 0x7D:
            addr = data[i+1] | (data[i+2]<<8)
            sram_reads.append((i, file_to_lorom(i), bank, addr))
for h in sram_reads[:25]:
    f, lo, b, a = h
    print(f"  file ${f:05X}  lorom ${lo:06X}  LDA ${b:02X}:{a:04X}")
print(f"  total SRAM reads: {len(sram_reads)}")

# NMITIMEN / joypad enable
show("Joypad/NMI control ($4200, $4218-$421B)",
     scan_writes(lambda a: a in (0x4200,) or 0x4218 <= a <= 0x421B), n=10)

# ---------------------------------------------------------------------------
# 4. Direct-cross-reference for menu strings: find code that loads a 16-bit
#    immediate equal to one of the string offsets (these are the "load
#    string pointer" sites — LDA #imm16 / STA ptr).
# ---------------------------------------------------------------------------
print("\n\n" + "=" * 72)
print("MENU-STRING LOAD SITES (LDA #imm16 with imm = string address)")
print("=" * 72)
# Just check the top-level main menu items.
TARGETS = {
    0x80AD:"FULL GAME",        0x80B7:"SCENARIO GAME",
    0x80C5:"SAVED GAME",       0x80D0:"TUTORIAL",
    0x80D9:"ANT INFORMATION",
}
# A9 imm16 — LDA immediate. With M=0, opcode A9 + 2 bytes operand.
for i in range(len(data) - 3):
    if data[i] == 0xA9:
        imm = data[i+1] | (data[i+2] << 8)
        if imm in TARGETS:
            print(f"  file ${i:05X}  lorom ${file_to_lorom(i):06X}  LDA #${imm:04X}  ; "
                  f"\"{TARGETS[imm]}\"")

# ---------------------------------------------------------------------------
# 5. Focused dumps of recovered HW routines.
# ---------------------------------------------------------------------------
print("\n\n" + "=" * 72)
print("FOCUSED ROUTINES")
print("=" * 72)

print("\n--- SRAM signature routine @ $00:AA2E (the save-game probe) ---")
print(disassemble(0xAA2E, length=0x60, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- 'Wait for vblank' window @ $00:985F (NMITIMEN toggling) ---")
print(disassemble(0x985F, length=0x40, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- APU IPL / init @ $00:8613 ---")
print(disassemble(0x8613, length=0x80, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- VRAM/decompressor at $00:8ACC (sub_8ACC) ---")
print(disassemble(0x8ACC, length=0x80, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- 'Asset block loader' at $00:8D7E ($A=count, $Y=ptr) ---")
print(disassemble(0x8D7E, length=0x60, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- Save-game write code at $00:959D ---")
print(disassemble(0x959D, length=0x40, M=0, X=0, bank=0x00, stop_on_return=True))

print("\n--- 'Decompressor'/asset loader entry at $08:8000 (JSL target) ---")
print(disassemble(0x8000, length=0x80, M=1, X=0, bank=0x08, stop_on_return=True))

print("\n--- $04:9966 entity dispatch indirect target ($DCD2 inside $04) ---")
print(disassemble(0xDCD2, length=0x40, M=1, X=0, bank=0x04, stop_on_return=True))

print("\n--- 'Sprite creation' helper $04:99C1 (called from GS_GAME_OVER etc.) ---")
print(disassemble(0x99C1, length=0x80, M=1, X=0, bank=0x04, stop_on_return=True))

print("\n--- $00:8616 'fade out' (called by SCENARIO/TUTORIAL/SOUND) ---")
print(disassemble(0x8616, length=0x40, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- Decompressor entry @ $02:8010 (callee of $00:8D7E) ---")
print(disassemble(0x8010, length=0x100, M=1, X=0, bank=0x02, stop_on_return=True))

print("\n--- Decompressor secondary @ $02:801F (callee of save-game $00:959D) ---")
print(disassemble(0x801F, length=0x100, M=1, X=0, bank=0x02, stop_on_return=True))

print("\n--- Asset loader real body @ $08:8006 ---")
print(disassemble(0x8006, length=0x100, M=1, X=0, bank=0x08, stop_on_return=True))

# Per-entity-type initialization tables.
print("\n--- Per-type init pointer table @ $01:EF59 (16-bit each, 32 entries) ---")
base = 0xEF59
for i in range(32):
    v = data[base + i*2] | (data[base + i*2 + 1] << 8)
    note = ""
    if v >= 0x8000: note = f"  (probably $01:{v:04X} ROM ptr)"
    elif v < 0x200: note = f"  (small constant — pos offset?)"
    print(f"  type {i:2d}: ${v:04X}{note}")

print("\n--- Per-type init byte table @ $01:F043 (1 byte each, 32 entries) ---")
base = 0xF043
for i in range(32):
    v = data[base + i]
    note = ""
    if v in (0x18, 0x9C, 0x9E, 0x9F, 0x98, 0x1E, 0x1F):
        note = "  (looks like OAM attribute byte: priority+palette)"
    print(f"  type {i:2d}: ${v:02X}{note}")

# ---------------------------------------------------------------------------
# 6. Detailed dumps of important targets we haven't fully lifted.
# ---------------------------------------------------------------------------
print("\n\n" + "=" * 72)
print("DEEP DIVES")
print("=" * 72)

print("\n--- Cursor entity (type 01) handler @ $04:9D9D, FULL body ---")
print(disassemble(0x9D9D, length=0x100, M=1, X=0, bank=0x04, stop_on_return=True))

print("\n--- View-switch @ $00:A3BD (called when SELECT is pressed) ---")
print(disassemble(0xA3BD, length=0x80, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- $00:8611 — paired with $A3BD in vsync routine ---")
print(disassemble(0x8611, length=0x40, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- Decompressor real body @ $03:8467 (LZ-style?) ---")
print(disassemble(0x8467, length=0x100, M=1, X=0, bank=0x03, stop_on_return=True))

print("\n--- Save serializer real body @ $03:FA74 ---")
print(disassemble(0xFA74, length=0x100, M=1, X=0, bank=0x03, stop_on_return=True))

print("\n--- PPU init @ $00:BC7F (called early from boot_init_BB8D) ---")
print(disassemble(0xBC7F, length=0x100, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- $00:BB38 — common per-state setup (used by many GS_*) ---")
print(disassemble(0xBB38, length=0x80, M=1, X=0, bank=0x00, stop_on_return=True))

print("\n--- $00:BA9E — 'screen template by index' (multiple GS_* call this) ---")
print(disassemble(0xBA9E, length=0x80, M=1, X=0, bank=0x00, stop_on_return=True))

# Look for the Will Wright / Maxis credit handler.
print("\n--- Credits area around $93E0 ('Will Wright' string) ---")
# Disassemble nearby code that might reference it.
print(disassemble(0x93DC, length=0x80, M=1, X=0, bank=0x00, stop_on_return=True))

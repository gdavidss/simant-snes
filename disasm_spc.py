#!/usr/bin/env python3
"""
SPC700 (Sony S-DSP coprocessor) disassembler for the SimAnt audio driver.

The SPC700 is the SNES's audio coprocessor — a Sony 8-bit CPU with a
6502-flavored ISA but a different opcode map.  Registers: A, X, Y, SP,
PSW, PC.  64 KB ARAM ($0000-$FFFF).  Memory-mapped DSP I/O at $00F0-
$00FF (test, control, dsp-addr, dsp-data, ports 0-3, timer regs).
IPL ROM at $FFC0-$FFFF reads commands from the 65816.

For SimAnt: the upload at boot streams 3327 bytes from ROM $0B:F000
into SPC RAM $0600-$12FE and jumps to $0600.  Driver lives entirely
inside that 3.3 KB blob.

This file:
  - implements the 256-opcode table with addressing modes,
  - rebuilds the uploaded SPC RAM from the standard IPL stream,
  - linearly disassembles a region with operand decoding,
  - exports `disassemble(addr, length, source)` like disasm.py.

Run it directly to print the entire driver disassembly to stdout
(can be redirected into audio_driver.txt).
"""
import struct
from pathlib import Path

ROM_PATH = Path(__file__).parent / "simant.sfc"

# ---------------------------------------------------------------------------
# Addressing-mode tags.  Operand bytes follow the opcode in memory.
# ---------------------------------------------------------------------------
IMP    = "imp"      # no operand
A_REG  = "A"        # A implicit
X_REG  = "X"
Y_REG  = "Y"
YA     = "YA"       # 16-bit Y:A
SP_REG = "SP"
PSW    = "PSW"
C_FLAG = "C"        # carry flag

# Single-byte operand modes
IMM    = "#imm"     # #$nn          (immediate byte)
DP     = "dp"       # $nn           (direct page)
DP_X   = "dp+X"     # $nn+X
DP_Y   = "dp+Y"     # $nn+Y
IND_X  = "(X)"      # (X)           -- indirect through X (no operand)
IND_Y  = "(Y)"      # (Y)           -- indirect through Y
IND_XP = "(X)+"     # (X)+          -- indirect-X with post-inc
DP_IND_X = "[dp+X]"  # [$nn+X]      -- indexed-indirect
DP_IND_Y = "[dp]+Y"  # [$nn]+Y      -- indirect-indexed
DP_DP    = "dp,dp"   # ($dd, $ss)   -- two direct-page operands
DP_IMM   = "dp,#imm" # ($dd, #$nn)  -- direct + immediate
REL    = "rel"      # PC-relative branch (1-byte signed)

# Two-byte operand modes
ABS    = "abs"      # !$nnnn        (16-bit absolute)
ABS_X  = "abs+X"    # !$nnnn+X
ABS_Y  = "abs+Y"    # !$nnnn+Y
ABS_X_IND = "[abs+X]"   # [!$nnnn+X]    -- indexed-indirect, used by JMP
ABS_BIT = "absbit"  # mem.bit       -- 13-bit address + 3-bit bit selector
                    #   2-byte operand: (lo=addr[7:0], hi=bit[2:0]|addr[12:8])
BBC    = "bbc"      # $nn, rel      -- direct-page byte + relative branch
                    #   used by BBS/BBC opcodes

# Operand byte counts per mode
OPERAND_SIZE = {
    IMP:0, A_REG:0, X_REG:0, Y_REG:0, YA:0, SP_REG:0, PSW:0, C_FLAG:0,
    IMM:1,
    DP:1, DP_X:1, DP_Y:1,
    IND_X:0, IND_Y:0, IND_XP:0,
    DP_IND_X:1, DP_IND_Y:1,
    DP_DP:2, DP_IMM:2,
    REL:1,
    ABS:2, ABS_X:2, ABS_Y:2, ABS_X_IND:2, ABS_BIT:2,
    BBC:2,
}

# ---------------------------------------------------------------------------
# The 256-opcode SPC700 table.
# Format: opcode -> (mnemonic, list-of-operand-fields)
# operand-fields is a list of (kind, role) where role is "src"/"dst"/"both"/"imp"
#   — we keep it simple and just store the mode tags in display order.
# ---------------------------------------------------------------------------
# Reference: official Sony S-SMP spec (SPC700 ISA), as documented in the
# super-famicom wiki, anomie/fullsnes, and ARChive.

# Each entry: opcode -> (mnemonic, mode1, mode2)  (mode2 may be None)
# The display will be "mnem  arg1, arg2".  Operand bytes always pack
# in the natural order (mode1's bytes first, then mode2's).

OPCODES = {
    # 0x00 series
    0x00: ("NOP",  None, None),
    0x01: ("TCALL", "0", None),
    0x02: ("SET1", DP, "0"),       # SET1 dp.0
    0x03: ("BBS",  DP, "0/REL"),   # BBS dp.0, rel
    0x04: ("OR",   A_REG, DP),
    0x05: ("OR",   A_REG, ABS),
    0x06: ("OR",   A_REG, IND_X),
    0x07: ("OR",   A_REG, DP_IND_X),
    0x08: ("OR",   A_REG, IMM),
    0x09: ("OR",   DP_DP, None),         # OR dp, dp (dst, src)
    0x0A: ("OR1",  C_FLAG, ABS_BIT),
    0x0B: ("ASL",  DP, None),
    0x0C: ("ASL",  ABS, None),
    0x0D: ("PUSH", PSW, None),
    0x0E: ("TSET1",ABS, None),
    0x0F: ("BRK",  None, None),

    # 0x10
    0x10: ("BPL", REL, None),
    0x11: ("TCALL","1", None),
    0x12: ("CLR1", DP, "0"),
    0x13: ("BBC",  DP, "0/REL"),
    0x14: ("OR",   A_REG, DP_X),
    0x15: ("OR",   A_REG, ABS_X),
    0x16: ("OR",   A_REG, ABS_Y),
    0x17: ("OR",   A_REG, DP_IND_Y),
    0x18: ("OR",   DP_IMM, None),
    0x19: ("OR",   IND_X, IND_Y),    # OR (X), (Y)
    0x1A: ("DECW", DP, None),
    0x1B: ("ASL",  DP_X, None),
    0x1C: ("ASL",  A_REG, None),
    0x1D: ("DEC",  X_REG, None),
    0x1E: ("CMP",  X_REG, ABS),
    0x1F: ("JMP",  ABS_X_IND, None), # JMP [!$nnnn+X]

    # 0x20
    0x20: ("CLRP", None, None),
    0x21: ("TCALL","2", None),
    0x22: ("SET1", DP, "1"),
    0x23: ("BBS",  DP, "1/REL"),
    0x24: ("AND",  A_REG, DP),
    0x25: ("AND",  A_REG, ABS),
    0x26: ("AND",  A_REG, IND_X),
    0x27: ("AND",  A_REG, DP_IND_X),
    0x28: ("AND",  A_REG, IMM),
    0x29: ("AND",  DP_DP, None),
    0x2A: ("OR1",  C_FLAG, "/ABSBIT"),  # OR1 C, /mem.bit   (negated source)
    0x2B: ("ROL",  DP, None),
    0x2C: ("ROL",  ABS, None),
    0x2D: ("PUSH", A_REG, None),
    0x2E: ("CBNE", DP, "REL"),         # CBNE dp, rel
    0x2F: ("BRA",  REL, None),

    # 0x30
    0x30: ("BMI",  REL, None),
    0x31: ("TCALL","3", None),
    0x32: ("CLR1", DP, "1"),
    0x33: ("BBC",  DP, "1/REL"),
    0x34: ("AND",  A_REG, DP_X),
    0x35: ("AND",  A_REG, ABS_X),
    0x36: ("AND",  A_REG, ABS_Y),
    0x37: ("AND",  A_REG, DP_IND_Y),
    0x38: ("AND",  DP_IMM, None),
    0x39: ("AND",  IND_X, IND_Y),
    0x3A: ("INCW", DP, None),
    0x3B: ("ROL",  DP_X, None),
    0x3C: ("ROL",  A_REG, None),
    0x3D: ("INC",  X_REG, None),
    0x3E: ("CMP",  X_REG, DP),
    0x3F: ("CALL", ABS, None),

    # 0x40
    0x40: ("SETP", None, None),
    0x41: ("TCALL","4", None),
    0x42: ("SET1", DP, "2"),
    0x43: ("BBS",  DP, "2/REL"),
    0x44: ("EOR",  A_REG, DP),
    0x45: ("EOR",  A_REG, ABS),
    0x46: ("EOR",  A_REG, IND_X),
    0x47: ("EOR",  A_REG, DP_IND_X),
    0x48: ("EOR",  A_REG, IMM),
    0x49: ("EOR",  DP_DP, None),
    0x4A: ("AND1", C_FLAG, ABS_BIT),
    0x4B: ("LSR",  DP, None),
    0x4C: ("LSR",  ABS, None),
    0x4D: ("PUSH", X_REG, None),
    0x4E: ("TCLR1",ABS, None),
    0x4F: ("PCALL",IMM, None),       # PCALL #$nn -> jump to $FF00+nn

    # 0x50
    0x50: ("BVC",  REL, None),
    0x51: ("TCALL","5", None),
    0x52: ("CLR1", DP, "2"),
    0x53: ("BBC",  DP, "2/REL"),
    0x54: ("EOR",  A_REG, DP_X),
    0x55: ("EOR",  A_REG, ABS_X),
    0x56: ("EOR",  A_REG, ABS_Y),
    0x57: ("EOR",  A_REG, DP_IND_Y),
    0x58: ("EOR",  DP_IMM, None),
    0x59: ("EOR",  IND_X, IND_Y),
    0x5A: ("CMPW", YA, DP),
    0x5B: ("LSR",  DP_X, None),
    0x5C: ("LSR",  A_REG, None),
    0x5D: ("MOV",  X_REG, A_REG),
    0x5E: ("CMP",  Y_REG, ABS),
    0x5F: ("JMP",  ABS, None),

    # 0x60
    0x60: ("CLRC", None, None),
    0x61: ("TCALL","6", None),
    0x62: ("SET1", DP, "3"),
    0x63: ("BBS",  DP, "3/REL"),
    0x64: ("CMP",  A_REG, DP),
    0x65: ("CMP",  A_REG, ABS),
    0x66: ("CMP",  A_REG, IND_X),
    0x67: ("CMP",  A_REG, DP_IND_X),
    0x68: ("CMP",  A_REG, IMM),
    0x69: ("CMP",  DP_DP, None),
    0x6A: ("AND1", C_FLAG, "/ABSBIT"),
    0x6B: ("ROR",  DP, None),
    0x6C: ("ROR",  ABS, None),
    0x6D: ("PUSH", Y_REG, None),
    0x6E: ("DBNZ", DP, "REL"),
    0x6F: ("RET",  None, None),

    # 0x70
    0x70: ("BVS",  REL, None),
    0x71: ("TCALL","7", None),
    0x72: ("CLR1", DP, "3"),
    0x73: ("BBC",  DP, "3/REL"),
    0x74: ("CMP",  A_REG, DP_X),
    0x75: ("CMP",  A_REG, ABS_X),
    0x76: ("CMP",  A_REG, ABS_Y),
    0x77: ("CMP",  A_REG, DP_IND_Y),
    0x78: ("CMP",  DP_IMM, None),
    0x79: ("CMP",  IND_X, IND_Y),
    0x7A: ("ADDW", YA, DP),
    0x7B: ("ROR",  DP_X, None),
    0x7C: ("ROR",  A_REG, None),
    0x7D: ("MOV",  A_REG, X_REG),
    0x7E: ("CMP",  Y_REG, DP),
    0x7F: ("RETI", None, None),

    # 0x80
    0x80: ("SETC", None, None),
    0x81: ("TCALL","8", None),
    0x82: ("SET1", DP, "4"),
    0x83: ("BBS",  DP, "4/REL"),
    0x84: ("ADC",  A_REG, DP),
    0x85: ("ADC",  A_REG, ABS),
    0x86: ("ADC",  A_REG, IND_X),
    0x87: ("ADC",  A_REG, DP_IND_X),
    0x88: ("ADC",  A_REG, IMM),
    0x89: ("ADC",  DP_DP, None),
    0x8A: ("EOR1", C_FLAG, ABS_BIT),
    0x8B: ("DEC",  DP, None),
    0x8C: ("DEC",  ABS, None),
    0x8D: ("MOV",  Y_REG, IMM),
    0x8E: ("POP",  PSW, None),
    0x8F: ("MOV",  DP_IMM, None),    # MOV dp, #imm

    # 0x90
    0x90: ("BCC",  REL, None),
    0x91: ("TCALL","9", None),
    0x92: ("CLR1", DP, "4"),
    0x93: ("BBC",  DP, "4/REL"),
    0x94: ("ADC",  A_REG, DP_X),
    0x95: ("ADC",  A_REG, ABS_X),
    0x96: ("ADC",  A_REG, ABS_Y),
    0x97: ("ADC",  A_REG, DP_IND_Y),
    0x98: ("ADC",  DP_IMM, None),
    0x99: ("ADC",  IND_X, IND_Y),
    0x9A: ("SUBW", YA, DP),
    0x9B: ("DEC",  DP_X, None),
    0x9C: ("DEC",  A_REG, None),
    0x9D: ("MOV",  X_REG, SP_REG),
    0x9E: ("DIV",  YA, X_REG),
    0x9F: ("XCN",  A_REG, None),

    # 0xA0
    0xA0: ("EI",   None, None),
    0xA1: ("TCALL","10", None),
    0xA2: ("SET1", DP, "5"),
    0xA3: ("BBS",  DP, "5/REL"),
    0xA4: ("SBC",  A_REG, DP),
    0xA5: ("SBC",  A_REG, ABS),
    0xA6: ("SBC",  A_REG, IND_X),
    0xA7: ("SBC",  A_REG, DP_IND_X),
    0xA8: ("SBC",  A_REG, IMM),
    0xA9: ("SBC",  DP_DP, None),
    0xAA: ("MOV1", C_FLAG, ABS_BIT),
    0xAB: ("INC",  DP, None),
    0xAC: ("INC",  ABS, None),
    0xAD: ("CMP",  Y_REG, IMM),
    0xAE: ("POP",  A_REG, None),
    0xAF: ("MOV",  IND_XP, A_REG),   # MOV (X)+, A

    # 0xB0
    0xB0: ("BCS",  REL, None),
    0xB1: ("TCALL","11", None),
    0xB2: ("CLR1", DP, "5"),
    0xB3: ("BBC",  DP, "5/REL"),
    0xB4: ("SBC",  A_REG, DP_X),
    0xB5: ("SBC",  A_REG, ABS_X),
    0xB6: ("SBC",  A_REG, ABS_Y),
    0xB7: ("SBC",  A_REG, DP_IND_Y),
    0xB8: ("SBC",  DP_IMM, None),
    0xB9: ("SBC",  IND_X, IND_Y),
    0xBA: ("MOVW", YA, DP),          # MOVW YA, dp  (load 16 bits)
    0xBB: ("INC",  DP_X, None),
    0xBC: ("INC",  A_REG, None),
    0xBD: ("MOV",  SP_REG, X_REG),
    0xBE: ("DAS",  A_REG, None),
    0xBF: ("MOV",  A_REG, IND_XP),   # MOV A, (X)+

    # 0xC0
    0xC0: ("DI",   None, None),
    0xC1: ("TCALL","12", None),
    0xC2: ("SET1", DP, "6"),
    0xC3: ("BBS",  DP, "6/REL"),
    0xC4: ("MOV",  DP, A_REG),
    0xC5: ("MOV",  ABS, A_REG),
    0xC6: ("MOV",  IND_X, A_REG),
    0xC7: ("MOV",  DP_IND_X, A_REG),
    0xC8: ("CMP",  X_REG, IMM),
    0xC9: ("MOV",  ABS, X_REG),
    0xCA: ("MOV1", ABS_BIT, C_FLAG),
    0xCB: ("MOV",  DP, Y_REG),
    0xCC: ("MOV",  ABS, Y_REG),
    0xCD: ("MOV",  X_REG, IMM),
    0xCE: ("POP",  X_REG, None),
    0xCF: ("MUL",  YA, None),

    # 0xD0
    0xD0: ("BNE",  REL, None),
    0xD1: ("TCALL","13", None),
    0xD2: ("CLR1", DP, "6"),
    0xD3: ("BBC",  DP, "6/REL"),
    0xD4: ("MOV",  DP_X, A_REG),
    0xD5: ("MOV",  ABS_X, A_REG),
    0xD6: ("MOV",  ABS_Y, A_REG),
    0xD7: ("MOV",  DP_IND_Y, A_REG),
    0xD8: ("MOV",  DP, X_REG),
    0xD9: ("MOV",  DP_Y, X_REG),
    0xDA: ("MOVW", DP, YA),          # MOVW dp, YA (store 16 bits)
    0xDB: ("MOV",  DP_X, Y_REG),
    0xDC: ("DEC",  Y_REG, None),
    0xDD: ("MOV",  A_REG, Y_REG),
    0xDE: ("CBNE", DP_X, "REL"),
    0xDF: ("DAA",  A_REG, None),

    # 0xE0
    0xE0: ("CLRV", None, None),
    0xE1: ("TCALL","14", None),
    0xE2: ("SET1", DP, "7"),
    0xE3: ("BBS",  DP, "7/REL"),
    0xE4: ("MOV",  A_REG, DP),
    0xE5: ("MOV",  A_REG, ABS),
    0xE6: ("MOV",  A_REG, IND_X),
    0xE7: ("MOV",  A_REG, DP_IND_X),
    0xE8: ("MOV",  A_REG, IMM),
    0xE9: ("MOV",  X_REG, ABS),
    0xEA: ("NOT1", ABS_BIT, None),
    0xEB: ("MOV",  Y_REG, DP),
    0xEC: ("MOV",  Y_REG, ABS),
    0xED: ("NOTC", None, None),
    0xEE: ("POP",  Y_REG, None),
    0xEF: ("SLEEP",None, None),

    # 0xF0
    0xF0: ("BEQ",  REL, None),
    0xF1: ("TCALL","15", None),
    0xF2: ("CLR1", DP, "7"),
    0xF3: ("BBC",  DP, "7/REL"),
    0xF4: ("MOV",  A_REG, DP_X),
    0xF5: ("MOV",  A_REG, ABS_X),
    0xF6: ("MOV",  A_REG, ABS_Y),
    0xF7: ("MOV",  A_REG, DP_IND_Y),
    0xF8: ("MOV",  X_REG, DP),
    0xF9: ("MOV",  X_REG, DP_Y),
    0xFA: ("MOV",  DP_DP, None),     # MOV dp, dp  (dst, src order)
    0xFB: ("MOV",  Y_REG, DP_X),
    0xFC: ("INC",  Y_REG, None),
    0xFD: ("MOV",  Y_REG, A_REG),
    0xFE: ("DBNZ", Y_REG, "REL"),
    0xFF: ("STOP", None, None),
}

# Sanity check: 256 entries
assert len(OPCODES) == 256, f"Opcode table has {len(OPCODES)} entries"

# ---------------------------------------------------------------------------
# DSP register names (memory-mapped at $00F0-$00FF + the DSP-addressed bank)
# ---------------------------------------------------------------------------
SPC_IO = {
    0xF0: "TEST", 0xF1: "CONTROL",
    0xF2: "DSPADDR", 0xF3: "DSPDATA",
    0xF4: "CPUIO0", 0xF5: "CPUIO1", 0xF6: "CPUIO2", 0xF7: "CPUIO3",
    0xF8: "AUXIO4", 0xF9: "AUXIO5",
    0xFA: "T0DIV", 0xFB: "T1DIV", 0xFC: "T2DIV",
    0xFD: "T0OUT", 0xFE: "T1OUT", 0xFF: "T2OUT",
}

# DSP register names (set via DSPADDR + DSPDATA).  Voice n at $n0-$n7.
DSP_REG = {
    0x00: "V0_VOL_L", 0x01: "V0_VOL_R", 0x02: "V0_PITCH_L", 0x03: "V0_PITCH_H",
    0x04: "V0_SRCN",  0x05: "V0_ADSR1", 0x06: "V0_ADSR2",  0x07: "V0_GAIN",
    0x08: "V0_ENVX",  0x09: "V0_OUTX",
    0x0C: "MVOL_L",   0x1C: "MVOL_R",
    0x2C: "EVOL_L",   0x3C: "EVOL_R",
    0x4C: "KON",      0x5C: "KOF",      0x6C: "FLG",        0x7C: "ENDX",
    0x0D: "EFB",      0x2D: "PMON",     0x3D: "NON",        0x4D: "EON",
    0x5D: "DIR",      0x6D: "ESA",      0x7D: "EDL",
}

# ---------------------------------------------------------------------------
# IPL stream decoder.  ROM data starts at file 0x5F000 ($0B:F000).
# Stream format (each block):
#   uint16 count_le  | uint16 dest_addr_le | count bytes ... |
# Last block: count=0, dest_addr = jump address (driver entry).
# ---------------------------------------------------------------------------
def reconstruct_spc_ram(rom_bytes, start_file_offset=0x5F000):
    """Returns (spc_ram_bytes, entry_pc, [(dest, length, src_offset),...])."""
    spc = bytearray(0x10000)
    chunks = []
    pos = start_file_offset
    entry_pc = None
    while pos < len(rom_bytes) - 4:
        count = rom_bytes[pos] | (rom_bytes[pos+1] << 8)
        addr  = rom_bytes[pos+2] | (rom_bytes[pos+3] << 8)
        if count == 0:
            entry_pc = addr
            break
        if pos + 4 + count > len(rom_bytes):
            raise ValueError(f"chunk overruns ROM at file 0x{pos:X}")
        spc[addr:addr+count] = rom_bytes[pos+4:pos+4+count]
        chunks.append((addr, count, pos+4))
        pos += 4 + count
    return bytes(spc), entry_pc, chunks

# ---------------------------------------------------------------------------
# Disassembler core
# ---------------------------------------------------------------------------
def fmt_dp(b):
    return f"${b:02X}" + (f" ;{SPC_IO[b]}" if b in SPC_IO else "")

def fmt_abs(addr):
    s = f"!${addr:04X}"
    if 0x00F0 <= addr <= 0x00FF:
        s += f" ;{SPC_IO[addr & 0xFF]}"
    return s

def fmt_imm(b):
    return f"#${b:02X}"

def fmt_rel(pc_after, off):
    s = off - 256 if off >= 0x80 else off
    return f"${(pc_after + s) & 0xFFFF:04X}"

def fmt_absbit(operand16):
    addr = operand16 & 0x1FFF
    bit  = (operand16 >> 13) & 7
    return f"!${addr:04X}.{bit}"

def render_operand(mode, raw, pc_after, second_operand_bytes=0):
    """Render a single operand mode given its raw bytes (already sliced)."""
    if mode is None: return ""
    if mode in (IMP, A_REG, X_REG, Y_REG, YA, SP_REG, PSW, C_FLAG):
        return {A_REG:"A", X_REG:"X", Y_REG:"Y", YA:"YA",
                SP_REG:"SP", PSW:"PSW", C_FLAG:"C", IMP:""}[mode]
    if mode == IMM:        return fmt_imm(raw[0])
    if mode == DP:         return fmt_dp(raw[0])
    if mode == DP_X:       return f"${raw[0]:02X}+X"
    if mode == DP_Y:       return f"${raw[0]:02X}+Y"
    if mode == IND_X:      return "(X)"
    if mode == IND_Y:      return "(Y)"
    if mode == IND_XP:     return "(X)+"
    if mode == DP_IND_X:   return f"[${raw[0]:02X}+X]"
    if mode == DP_IND_Y:   return f"[${raw[0]:02X}]+Y"
    if mode == ABS:        return fmt_abs(raw[0] | (raw[1]<<8))
    if mode == ABS_X:      return fmt_abs(raw[0] | (raw[1]<<8)) + "+X"
    if mode == ABS_Y:      return fmt_abs(raw[0] | (raw[1]<<8)) + "+Y"
    if mode == ABS_X_IND:  return f"[!${(raw[0]|(raw[1]<<8)):04X}+X]"
    if mode == ABS_BIT:    return fmt_absbit(raw[0] | (raw[1]<<8))
    if mode == REL:        return fmt_rel(pc_after, raw[0])
    # DP_DP / DP_IMM are 2-operand modes handled separately
    return f"?{mode}?"

def disassemble_instruction(pc, mem):
    """
    Disassemble one instruction at SPC address pc using mem[] as ARAM.
    Returns (mnemonic_text, operand_text, num_bytes, raw_bytes_list).
    """
    opc = mem[pc]
    if opc not in OPCODES:
        return (f".db ${opc:02X}", "", 1, [opc])
    mnem, m1, m2 = OPCODES[opc]

    # Special-case the multi-operand modes whose ORDER is unique:
    if m1 == DP_DP and m2 is None:
        # "MOV dp, dp" — operand bytes are SOURCE then DEST (SPC convention)
        # so byte0 = src, byte1 = dst.  We display "dst, src".
        src = mem[(pc+1) & 0xFFFF]
        dst = mem[(pc+2) & 0xFFFF]
        return (mnem, f"{fmt_dp(dst)}, {fmt_dp(src)}", 3, [opc, src, dst])

    if m1 == DP_IMM and m2 is None:
        # "MOV dp, #imm" / "OR dp, #imm" / etc.
        # Operand bytes are IMMEDIATE then DP.  Display as "dp, #imm".
        imm = mem[(pc+1) & 0xFFFF]
        dpb = mem[(pc+2) & 0xFFFF]
        return (mnem, f"{fmt_dp(dpb)}, {fmt_imm(imm)}", 3, [opc, imm, dpb])

    if m1 == IND_X and m2 == IND_Y:
        # OR/AND/etc (X), (Y) — no operand bytes
        return (mnem, "(X), (Y)", 1, [opc])

    if mnem == "MOV" and m1 == IND_XP and m2 == A_REG:
        return (mnem, "(X)+, A", 1, [opc])
    if mnem == "MOV" and m1 == A_REG and m2 == IND_XP:
        return (mnem, "A, (X)+", 1, [opc])

    # TCALL — single fixed nibble argument (no operand bytes)
    if mnem == "TCALL":
        return (mnem, m1, 1, [opc])

    # SET1/CLR1 dp.bit — single dp byte, bit encoded in opcode
    if mnem in ("SET1", "CLR1") and m2 in ("0","1","2","3","4","5","6","7"):
        dp = mem[(pc+1) & 0xFFFF]
        return (mnem, f"${dp:02X}.{m2}", 2, [opc, dp])

    # BBS/BBC dp.bit, rel — dp byte + rel byte, bit in opcode
    if mnem in ("BBS", "BBC") and isinstance(m2, str) and "/REL" in m2:
        bit = m2.split("/")[0]
        dp = mem[(pc+1) & 0xFFFF]
        rel = mem[(pc+2) & 0xFFFF]
        return (mnem, f"${dp:02X}.{bit}, {fmt_rel(pc+3, rel)}", 3, [opc, dp, rel])

    # CBNE / DBNZ dp,rel and CBNE dp+X,rel
    if mnem in ("CBNE", "DBNZ") and m1 == DP and m2 == "REL":
        dp = mem[(pc+1) & 0xFFFF]
        rel = mem[(pc+2) & 0xFFFF]
        return (mnem, f"{fmt_dp(dp)}, {fmt_rel(pc+3, rel)}", 3, [opc, dp, rel])
    if mnem == "CBNE" and m1 == DP_X and m2 == "REL":
        dp = mem[(pc+1) & 0xFFFF]
        rel = mem[(pc+2) & 0xFFFF]
        return (mnem, f"${dp:02X}+X, {fmt_rel(pc+3, rel)}", 3, [opc, dp, rel])
    # DBNZ Y, rel
    if mnem == "DBNZ" and m1 == Y_REG and m2 == "REL":
        rel = mem[(pc+1) & 0xFFFF]
        return (mnem, f"Y, {fmt_rel(pc+2, rel)}", 2, [opc, rel])

    # OR1/AND1/EOR1/MOV1/NOT1 forms with /ABSBIT (negated source)
    if m2 == "/ABSBIT":
        lo = mem[(pc+1) & 0xFFFF]; hi = mem[(pc+2) & 0xFFFF]
        return (mnem, f"C, /{fmt_absbit(lo | (hi<<8))}", 3, [opc, lo, hi])

    # Single-operand-mode instructions (most opcodes)
    sz1 = OPERAND_SIZE.get(m1, 0) if m1 else 0
    sz2 = OPERAND_SIZE.get(m2, 0) if m2 and m2 != "REL" else 0

    # OPERAND BYTES come from memory just after the opcode.  Mode order varies:
    # For SPC700, when there are two operand modes (e.g. MOV X, !$nnnn), the
    # bytes for m2 typically come AFTER bytes for m1, BUT for store-style
    # MOV "abs, X" (0xC9), the immediate comes first... actually no, for
    # MOV !$nnnn, X (0xC9) and similar, the operand is on the LEFT visually
    # but encoded as a normal 2-byte abs.  We handle the simple case where
    # operand bytes are read in display order.
    raw_bytes = []
    for b in range(1, 1 + sz1 + sz2):
        raw_bytes.append(mem[(pc + b) & 0xFFFF])

    # For "MOV dst, src" where dst is on LHS but operand is just src's mode
    # (e.g. MOV X, #imm = CD nn), the LHS is implicit (register), no bytes.
    # Where both sides have operand bytes (rare — covered above for DP_DP/DP_IMM).

    raw_for_m1 = raw_bytes[:sz1]
    raw_for_m2 = raw_bytes[sz1:sz1+sz2]
    pc_after = pc + 1 + len(raw_bytes)
    arg1 = render_operand(m1, raw_for_m1, pc_after)
    arg2 = render_operand(m2, raw_for_m2, pc_after) if m2 else ""

    operand_text = arg1
    if arg2:
        operand_text = f"{arg1}, {arg2}"

    return (mnem, operand_text, 1 + len(raw_bytes), [opc] + raw_bytes)

# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------
def disassemble(start_pc, length, source):
    """Linear disassembly from start_pc for length bytes, using source[]
    as the address-space buffer (64 KB).  Returns a string."""
    out = []
    pc = start_pc & 0xFFFF
    end = (start_pc + length) & 0x1FFFF
    while pc < end:
        mnem, op, n, raw = disassemble_instruction(pc, source)
        bytestr = " ".join(f"{b:02X}" for b in raw)
        line = f"{pc:04X}  {bytestr:<11} {mnem:<6} {op}".rstrip()
        out.append(line)
        pc += n
        if pc >= 0x10000: break
    return "\n".join(out)

def disassemble_range(start_pc, end_pc, source, label=""):
    """Helper that mirrors disasm.py.dump_sub."""
    header = f"; ---- {label or ''} {start_pc:04X}..{end_pc:04X} ----"
    return header + "\n" + disassemble(start_pc, end_pc - start_pc, source)

# ---------------------------------------------------------------------------
# Standalone main: rebuild SPC RAM and dump a sample disassembly.
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    rom = ROM_PATH.read_bytes()
    spc, entry_pc, chunks = reconstruct_spc_ram(rom)
    print("=== SimAnt SPC700 audio driver — IPL upload analysis ===")
    print(f"ROM upload stream starts at file 0x5F000 (= ROM ${0x0B:02X}:F000)")
    print(f"Chunks:")
    for (dest, length, src) in chunks:
        print(f"  ROM file 0x{src:05X}: {length:>5} bytes -> SPC ${dest:04X}-${dest+length-1:04X}")
    print(f"Driver entry point (jump address): ${entry_pc:04X}")
    print()
    # Quick sample disassembly: first 256 bytes from the entry point
    print(f"=== Sample disassembly: 256 bytes from ${entry_pc:04X} ===")
    print(disassemble(entry_pc, 256, spc))

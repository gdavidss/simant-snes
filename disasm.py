#!/usr/bin/env python3
"""
Minimal 65816 disassembler for the SimAnt (SNES) ROM.

LoROM mapping: bank $00-$7F, addresses $8000-$FFFF map to ROM offsets
linearly. CPU address $00:8000 == file offset 0, $00:FFFF == 0x7FFF,
$01:8000 == 0x8000, etc.

This is not a full reconstruction tool — it's a starting point that produces
readable assembly so we can begin lifting code into C by hand.
"""
import sys, struct
from pathlib import Path

ROM = Path(__file__).parent / "simant.sfc"
data = ROM.read_bytes()

# ---- LoROM address translation ---------------------------------------------
def lorom_to_file(addr):
    bank = (addr >> 16) & 0xFF
    off  = addr & 0xFFFF
    if off < 0x8000:
        return None  # RAM / I/O
    return ((bank & 0x7F) << 15) | (off & 0x7FFF)

def file_to_lorom(off):
    bank = (off >> 15) & 0x7F
    return (bank << 16) | 0x8000 | (off & 0x7FFF)

# ---- Internal header at $00:FFC0 (LoROM) -> file 0x7FC0 --------------------
def print_header():
    title = data[0x7FC0:0x7FD5].decode("ascii", errors="replace")
    mapper      = data[0x7FD5]
    cartridge   = data[0x7FD6]
    rom_size    = data[0x7FD7]   # log2(KB)
    ram_size    = data[0x7FD8]
    region      = data[0x7FD9]
    dev_id      = data[0x7FDA]
    version     = data[0x7FDB]
    chksum_c    = struct.unpack("<H", data[0x7FDC:0x7FDE])[0]
    chksum      = struct.unpack("<H", data[0x7FDE:0x7FE0])[0]
    print(f"Title:           {title!r}")
    print(f"Mapper byte:     ${mapper:02X}  ({'LoROM' if mapper & 1 == 0 else 'HiROM'}, "
          f"{'FastROM' if mapper & 0x10 else 'SlowROM'})")
    print(f"Cartridge type:  ${cartridge:02X}")
    print(f"ROM size:        ${rom_size:02X}  ({1<<rom_size} KB nominal)")
    print(f"RAM size:        ${ram_size:02X}  ({1<<ram_size if ram_size else 0} KB SRAM)")
    print(f"Region:          ${region:02X}  ({'NTSC' if region in (0,1,13) else 'PAL'})")
    print(f"Dev ID:          ${dev_id:02X}")
    print(f"Version:         ${version:02X}")
    print(f"Checksum:        ${chksum:04X}  complement ${chksum_c:04X}")
    print()

def print_vectors():
    print("Native-mode vectors (CPU runs in native after RESET sets it up):")
    names = [("COP",    0x7FE4),
             ("BRK",    0x7FE6),
             ("ABORT",  0x7FE8),
             ("NMI",    0x7FEA),
             ("IRQ",    0x7FEE)]
    for n, off in names:
        v = struct.unpack("<H", data[off:off+2])[0]
        print(f"  {n:6s} -> ${v:04X}  (file ${lorom_to_file(0x008000 | v if v < 0x8000 else v):05X})")
    print("\nEmulation-mode vectors (used at power-on):")
    names = [("COP",    0x7FF4),
             ("ABORT",  0x7FF8),
             ("NMI",    0x7FFA),
             ("RESET",  0x7FFC),
             ("IRQ/BRK",0x7FFE)]
    for n, off in names:
        v = struct.unpack("<H", data[off:off+2])[0]
        f = lorom_to_file(0x000000 | v)
        print(f"  {n:7s} -> ${v:04X}  (file ${f:05X})" if f is not None else
              f"  {n:7s} -> ${v:04X}  (not in ROM)")
    print()

# ---- 65816 opcode table ----------------------------------------------------
# (mnemonic, addressing mode). Operand length is derived from mode + M/X flags.
IMP, ACC, IMM_M, IMM_X, IMM8 = "imp","A","#M","#X","#8"
ZP, ZPX, ZPY, ZPI, ZPIY, ZPIX = "d","d,x","d,y","(d)","(d),y","(d,x)"
ZPIL, ZPILY = "[d]","[d],y"
ABS, ABX, ABY, ABI, ABIX = "a","a,x","a,y","(a)","(a,x)"
ABSL, ABLX = "al","al,x"
REL, RELL = "r","rl"
STK, STKI, STKIL = "s","(s),y","[s]"
BLK = "blk"

OPCODES = {
0x00:("BRK",IMM8),0x01:("ORA",ZPIX),0x02:("COP",IMM8),0x03:("ORA",STK),
0x04:("TSB",ZP),0x05:("ORA",ZP),0x06:("ASL",ZP),0x07:("ORA",ZPIL),
0x08:("PHP",IMP),0x09:("ORA",IMM_M),0x0A:("ASL",ACC),0x0B:("PHD",IMP),
0x0C:("TSB",ABS),0x0D:("ORA",ABS),0x0E:("ASL",ABS),0x0F:("ORA",ABSL),
0x10:("BPL",REL),0x11:("ORA",ZPIY),0x12:("ORA",ZPI),0x13:("ORA",STKI),
0x14:("TRB",ZP),0x15:("ORA",ZPX),0x16:("ASL",ZPX),0x17:("ORA",ZPILY),
0x18:("CLC",IMP),0x19:("ORA",ABY),0x1A:("INC",ACC),0x1B:("TCS",IMP),
0x1C:("TRB",ABS),0x1D:("ORA",ABX),0x1E:("ASL",ABX),0x1F:("ORA",ABLX),
0x20:("JSR",ABS),0x21:("AND",ZPIX),0x22:("JSL",ABSL),0x23:("AND",STK),
0x24:("BIT",ZP),0x25:("AND",ZP),0x26:("ROL",ZP),0x27:("AND",ZPIL),
0x28:("PLP",IMP),0x29:("AND",IMM_M),0x2A:("ROL",ACC),0x2B:("PLD",IMP),
0x2C:("BIT",ABS),0x2D:("AND",ABS),0x2E:("ROL",ABS),0x2F:("AND",ABSL),
0x30:("BMI",REL),0x31:("AND",ZPIY),0x32:("AND",ZPI),0x33:("AND",STKI),
0x34:("BIT",ZPX),0x35:("AND",ZPX),0x36:("ROL",ZPX),0x37:("AND",ZPILY),
0x38:("SEC",IMP),0x39:("AND",ABY),0x3A:("DEC",ACC),0x3B:("TSC",IMP),
0x3C:("BIT",ABX),0x3D:("AND",ABX),0x3E:("ROL",ABX),0x3F:("AND",ABLX),
0x40:("RTI",IMP),0x41:("EOR",ZPIX),0x42:("WDM",IMM8),0x43:("EOR",STK),
0x44:("MVP",BLK),0x45:("EOR",ZP),0x46:("LSR",ZP),0x47:("EOR",ZPIL),
0x48:("PHA",IMP),0x49:("EOR",IMM_M),0x4A:("LSR",ACC),0x4B:("PHK",IMP),
0x4C:("JMP",ABS),0x4D:("EOR",ABS),0x4E:("LSR",ABS),0x4F:("EOR",ABSL),
0x50:("BVC",REL),0x51:("EOR",ZPIY),0x52:("EOR",ZPI),0x53:("EOR",STKI),
0x54:("MVN",BLK),0x55:("EOR",ZPX),0x56:("LSR",ZPX),0x57:("EOR",ZPILY),
0x58:("CLI",IMP),0x59:("EOR",ABY),0x5A:("PHY",IMP),0x5B:("TCD",IMP),
0x5C:("JML",ABSL),0x5D:("EOR",ABX),0x5E:("LSR",ABX),0x5F:("EOR",ABLX),
0x60:("RTS",IMP),0x61:("ADC",ZPIX),0x62:("PER",RELL),0x63:("ADC",STK),
0x64:("STZ",ZP),0x65:("ADC",ZP),0x66:("ROR",ZP),0x67:("ADC",ZPIL),
0x68:("PLA",IMP),0x69:("ADC",IMM_M),0x6A:("ROR",ACC),0x6B:("RTL",IMP),
0x6C:("JMP",ABI),0x6D:("ADC",ABS),0x6E:("ROR",ABS),0x6F:("ADC",ABSL),
0x70:("BVS",REL),0x71:("ADC",ZPIY),0x72:("ADC",ZPI),0x73:("ADC",STKI),
0x74:("STZ",ZPX),0x75:("ADC",ZPX),0x76:("ROR",ZPX),0x77:("ADC",ZPILY),
0x78:("SEI",IMP),0x79:("ADC",ABY),0x7A:("PLY",IMP),0x7B:("TDC",IMP),
0x7C:("JMP",ABIX),0x7D:("ADC",ABX),0x7E:("ROR",ABX),0x7F:("ADC",ABLX),
0x80:("BRA",REL),0x81:("STA",ZPIX),0x82:("BRL",RELL),0x83:("STA",STK),
0x84:("STY",ZP),0x85:("STA",ZP),0x86:("STX",ZP),0x87:("STA",ZPIL),
0x88:("DEY",IMP),0x89:("BIT",IMM_M),0x8A:("TXA",IMP),0x8B:("PHB",IMP),
0x8C:("STY",ABS),0x8D:("STA",ABS),0x8E:("STX",ABS),0x8F:("STA",ABSL),
0x90:("BCC",REL),0x91:("STA",ZPIY),0x92:("STA",ZPI),0x93:("STA",STKI),
0x94:("STY",ZPX),0x95:("STA",ZPX),0x96:("STX",ZPY),0x97:("STA",ZPILY),
0x98:("TYA",IMP),0x99:("STA",ABY),0x9A:("TXS",IMP),0x9B:("TXY",IMP),
0x9C:("STZ",ABS),0x9D:("STA",ABX),0x9E:("STZ",ABX),0x9F:("STA",ABLX),
0xA0:("LDY",IMM_X),0xA1:("LDA",ZPIX),0xA2:("LDX",IMM_X),0xA3:("LDA",STK),
0xA4:("LDY",ZP),0xA5:("LDA",ZP),0xA6:("LDX",ZP),0xA7:("LDA",ZPIL),
0xA8:("TAY",IMP),0xA9:("LDA",IMM_M),0xAA:("TAX",IMP),0xAB:("PLB",IMP),
0xAC:("LDY",ABS),0xAD:("LDA",ABS),0xAE:("LDX",ABS),0xAF:("LDA",ABSL),
0xB0:("BCS",REL),0xB1:("LDA",ZPIY),0xB2:("LDA",ZPI),0xB3:("LDA",STKI),
0xB4:("LDY",ZPX),0xB5:("LDA",ZPX),0xB6:("LDX",ZPY),0xB7:("LDA",ZPILY),
0xB8:("CLV",IMP),0xB9:("LDA",ABY),0xBA:("TSX",IMP),0xBB:("TYX",IMP),
0xBC:("LDY",ABX),0xBD:("LDA",ABX),0xBE:("LDX",ABY),0xBF:("LDA",ABLX),
0xC0:("CPY",IMM_X),0xC1:("CMP",ZPIX),0xC2:("REP",IMM8),0xC3:("CMP",STK),
0xC4:("CPY",ZP),0xC5:("CMP",ZP),0xC6:("DEC",ZP),0xC7:("CMP",ZPIL),
0xC8:("INY",IMP),0xC9:("CMP",IMM_M),0xCA:("DEX",IMP),0xCB:("WAI",IMP),
0xCC:("CPY",ABS),0xCD:("CMP",ABS),0xCE:("DEC",ABS),0xCF:("CMP",ABSL),
0xD0:("BNE",REL),0xD1:("CMP",ZPIY),0xD2:("CMP",ZPI),0xD3:("CMP",STKI),
0xD4:("PEI",ZP),0xD5:("CMP",ZPX),0xD6:("DEC",ZPX),0xD7:("CMP",ZPILY),
0xD8:("CLD",IMP),0xD9:("CMP",ABY),0xDA:("PHX",IMP),0xDB:("STP",IMP),
0xDC:("JML",ABI),0xDD:("CMP",ABX),0xDE:("DEC",ABX),0xDF:("CMP",ABLX),
0xE0:("CPX",IMM_X),0xE1:("SBC",ZPIX),0xE2:("SEP",IMM8),0xE3:("SBC",STK),
0xE4:("CPX",ZP),0xE5:("SBC",ZP),0xE6:("INC",ZP),0xE7:("SBC",ZPIL),
0xE8:("INX",IMP),0xE9:("SBC",IMM_M),0xEA:("NOP",IMP),0xEB:("XBA",IMP),
0xEC:("CPX",ABS),0xED:("SBC",ABS),0xEE:("INC",ABS),0xEF:("SBC",ABSL),
0xF0:("BEQ",REL),0xF1:("SBC",ZPIY),0xF2:("SBC",ZPI),0xF3:("SBC",STKI),
0xF4:("PEA",ABS),0xF5:("SBC",ZPX),0xF6:("INC",ZPX),0xF7:("SBC",ZPILY),
0xF8:("SED",IMP),0xF9:("SBC",ABY),0xFA:("PLX",IMP),0xFB:("XCE",IMP),
0xFC:("JSR",ABIX),0xFD:("SBC",ABX),0xFE:("INC",ABX),0xFF:("SBC",ABLX),
}

# ---- SNES hardware register names (most common) ----------------------------
HW = {
0x2100:"INIDISP",0x2101:"OBSEL",0x2102:"OAMADDL",0x2103:"OAMADDH",
0x2104:"OAMDATA",0x2105:"BGMODE",0x2106:"MOSAIC",
0x2107:"BG1SC",0x2108:"BG2SC",0x2109:"BG3SC",0x210A:"BG4SC",
0x210B:"BG12NBA",0x210C:"BG34NBA",
0x210D:"BG1HOFS",0x210E:"BG1VOFS",0x210F:"BG2HOFS",0x2110:"BG2VOFS",
0x2111:"BG3HOFS",0x2112:"BG3VOFS",0x2113:"BG4HOFS",0x2114:"BG4VOFS",
0x2115:"VMAIN",0x2116:"VMADDL",0x2117:"VMADDH",0x2118:"VMDATAL",0x2119:"VMDATAH",
0x211A:"M7SEL",0x2121:"CGADD",0x2122:"CGDATA",
0x2123:"W12SEL",0x2124:"W34SEL",0x2125:"WOBJSEL",
0x212C:"TM",0x212D:"TS",0x212E:"TMW",0x212F:"TSW",
0x2130:"CGWSEL",0x2131:"CGADSUB",0x2132:"COLDATA",0x2133:"SETINI",
0x2140:"APUIO0",0x2141:"APUIO1",0x2142:"APUIO2",0x2143:"APUIO3",
0x2180:"WMDATA",0x2181:"WMADDL",0x2182:"WMADDM",0x2183:"WMADDH",
0x4200:"NMITIMEN",0x4201:"WRIO",0x4202:"WRMPYA",0x4203:"WRMPYB",
0x4204:"WRDIVL",0x4205:"WRDIVH",0x4206:"WRDIVB",
0x4207:"HTIMEL",0x4208:"HTIMEH",0x4209:"VTIMEL",0x420A:"VTIMEH",
0x420B:"MDMAEN",0x420C:"HDMAEN",0x420D:"MEMSEL",
0x4210:"RDNMI",0x4211:"TIMEUP",0x4212:"HVBJOY",0x4213:"RDIO",
0x4214:"RDDIVL",0x4215:"RDDIVH",0x4216:"RDMPYL",0x4217:"RDMPYH",
0x4218:"JOY1L",0x4219:"JOY1H",0x421A:"JOY2L",0x421B:"JOY2H",
}

def hw_name(addr):
    return HW.get(addr & 0xFFFF)

# ---- Disassembler core -----------------------------------------------------
def operand_size(mode, M, X):
    return {
        IMP:0,ACC:0,
        IMM_M:(2 if M==0 else 1),
        IMM_X:(2 if X==0 else 1),
        IMM8:1,
        ZP:1,ZPX:1,ZPY:1,ZPI:1,ZPIY:1,ZPIX:1,ZPIL:1,ZPILY:1,
        ABS:2,ABX:2,ABY:2,ABI:2,ABIX:2,
        ABSL:3,ABLX:3,
        REL:1,RELL:2,
        STK:1,STKI:1,STKIL:1,
        BLK:2,
    }[mode]

def fmt_op(pc, op, mode, M, X, raw):
    if mode == IMP:  return ""
    if mode == ACC:  return ""
    if mode in (IMM_M, IMM_X):
        sz = 2 if (mode==IMM_M and M==0) or (mode==IMM_X and X==0) else 1
        v  = raw[0] | (raw[1]<<8 if sz==2 else 0)
        return f"#${v:0{sz*2}X}"
    if mode == IMM8: return f"#${raw[0]:02X}"
    if mode == ZP:   return f"${raw[0]:02X}"
    if mode == ZPX:  return f"${raw[0]:02X},x"
    if mode == ZPY:  return f"${raw[0]:02X},y"
    if mode == ZPI:  return f"(${raw[0]:02X})"
    if mode == ZPIY: return f"(${raw[0]:02X}),y"
    if mode == ZPIX: return f"(${raw[0]:02X},x)"
    if mode == ZPIL: return f"[${raw[0]:02X}]"
    if mode == ZPILY:return f"[${raw[0]:02X}],y"
    if mode in (ABS,ABX,ABY,ABI,ABIX):
        a = raw[0] | (raw[1]<<8)
        suf = {ABS:"",ABX:",x",ABY:",y",ABI:"",ABIX:""}[mode]
        wrap = "(" if mode in (ABI,ABIX) else ""
        wrapc= ")" if mode in (ABI,ABIX) else ""
        s = f"{wrap}${a:04X}{suf}{wrapc}"
        hw = hw_name(a)
        if hw: s += f" ; {hw}"
        return s
    if mode in (ABSL,ABLX):
        a = raw[0] | (raw[1]<<8) | (raw[2]<<16)
        return f"${a:06X}" + (",x" if mode==ABLX else "")
    if mode == REL:
        o = raw[0]; s = o - 256 if o >= 0x80 else o
        return f"${(pc+2+s) & 0xFFFF:04X}"
    if mode == RELL:
        o = raw[0] | (raw[1]<<8); s = o - 65536 if o >= 0x8000 else o
        return f"${(pc+3+s) & 0xFFFF:04X}"
    if mode == STK:   return f"${raw[0]:02X},s"
    if mode == STKI:  return f"(${raw[0]:02X},s),y"
    if mode == STKIL: return f"[${raw[0]:02X}],y"
    if mode == BLK:   return f"${raw[1]:02X},${raw[0]:02X}"
    return "?"

def disassemble(start_pc, length=0x200, M=1, X=1, bank=0, stop_on_return=False):
    """Linear disassembly from start_pc for `length` bytes.
    M/X are the initial register-size flags (1 = 8-bit, 0 = 16-bit).
    Tracks M/X across SEP/REP so immediate sizes stay sane.
    If stop_on_return=True, stop after the first RTS/RTL/RTI/JMP/JML/BRA.
    """
    out = []
    end_pc = start_pc + length
    pc = start_pc
    while pc < end_pc:
        f = lorom_to_file((bank<<16) | pc)
        if f is None or f >= len(data):
            out.append(f"{bank:02X}:{pc:04X}  ; out of ROM")
            break
        opc = data[f]
        info = OPCODES.get(opc)
        if info is None:
            out.append(f"{bank:02X}:{pc:04X}  {opc:02X}        .db ${opc:02X}")
            pc += 1; continue
        mnem, mode = info
        n = operand_size(mode, M, X)
        raw = data[f+1:f+1+n]
        bytestr = " ".join(f"{b:02X}" for b in data[f:f+1+n])
        operand = fmt_op(pc, opc, mode, M, X, raw)
        out.append(f"{bank:02X}:{pc:04X}  {bytestr:<11} {mnem} {operand}".rstrip())

        # Track M/X across SEP/REP for accurate immediate sizing
        if mnem == "SEP":
            v = raw[0]
            if v & 0x20: M = 1
            if v & 0x10: X = 1
        elif mnem == "REP":
            v = raw[0]
            if v & 0x20: M = 0
            if v & 0x10: X = 0

        pc += 1 + n
        if stop_on_return and mnem in ("RTS","RTL","RTI","JMP","JML","BRL","STP"):
            break
    return "\n".join(out)

def dump_sub(label, addr, length=0x200, M=1, X=1, bank=0):
    print(f"\n--- {label} @ ${addr:04X} ---")
    print(disassemble(addr, length=length, M=M, X=X, bank=bank, stop_on_return=True))

# ---- Main ------------------------------------------------------------------
if __name__ == "__main__":
    print("=== SimAnt (SNES) — ROM info ===\n")
    print_header()
    print_vectors()

    reset = struct.unpack("<H", data[0x7FFC:0x7FFE])[0]
    print(f"=== RESET @ ${reset:04X} (emulation mode entry) ===")
    print(disassemble(reset, length=0x100, M=1, X=1, bank=0))

    nmi = struct.unpack("<H", data[0x7FEA:0x7FEC])[0]
    print(f"\n=== NMI @ ${nmi:04X} (full, 0x200 bytes) ===")
    print(disassemble(nmi, length=0x200, M=0, X=0, bank=0))

    # Subroutines called from NMI (in call order), plus main init.
    # Each disassembled until first return; M/X assumed 8/8 unless we know
    # otherwise from caller context.
    print("\n\n========== SUBROUTINES CALLED FROM NMI ==========")
    dump_sub("sub_814F (NMI: 1st call, runs every frame)", 0x814F, 0x200, M=1, X=0)
    dump_sub("sub_8553 (NMI: even-frame work)",           0x8553, 0x200, M=1, X=0)
    dump_sub("sub_85B2 (NMI: odd-frame work)",            0x85B2, 0x200, M=1, X=0)
    dump_sub("sub_C804 (NMI: every-frame after odd/even)", 0xC804, 0x200, M=1, X=0)
    dump_sub("sub_8937 (NMI)",                            0x8937, 0x200, M=1, X=0)
    dump_sub("sub_884A (NMI)",                            0x884A, 0x200, M=1, X=0)
    dump_sub("sub_88A5 (NMI)",                            0x88A5, 0x200, M=1, X=0)
    dump_sub("sub_8101 (NMI: last before clock tick)",    0x8101, 0x200, M=1, X=0)

    # Bank-04 NMI helper (JSL target).
    print("\n\n========== BANK 04 NMI HELPER ==========")
    print("\n--- sub_049966 @ $04:9966 ---")
    print(disassemble(0x9966, length=0x200, M=1, X=0, bank=0x04,
                      stop_on_return=True))

    # Main init / event loop reached from RESET.
    # At entry M=1, X=0 (from REP #$10 at $801B never undone before JMP).
    print("\n\n========== MAIN INIT REACHED FROM RESET ==========")
    dump_sub("main_9340 (RESET tail jumps here)", 0x9340, 0x40, M=1, X=0)

    # Init helpers called from main_9340.
    dump_sub("sub_BB8D (1st main init call)",   0xBB8D, 0x100, M=1, X=0)
    dump_sub("sub_896D (2nd main init — task setup?)", 0x896D, 0x100, M=1, X=0)

    # Entity-handler dispatch table at $04:9A30. Each entry is 2 bytes
    # (jump target in bank $04). Up to ~32 handlers.
    print("\n\n--- Entity handler table @ $04:9A30 (first 0x40 bytes) ---")
    base = 0x9A30
    f = lorom_to_file(0x040000 | base)
    for i in range(0, 0x40, 2):
        ptr = data[f+i] | (data[f+i+1] << 8)
        print(f"  table[{i//2:2d}] = $04:{ptr:04X}")

    # The indirect dispatcher used by the entity walker.
    dump_sub("sub_04DCD2 (entity dispatch helper)", 0xDCD2, 0x40, M=1, X=0, bank=0x04)

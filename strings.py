#!/usr/bin/env python3
"""Locate manual-vocabulary strings in the ROM and report file offsets +
LoROM addresses so we can hunt for cross-references in code."""
from pathlib import Path
import re

ROM = (Path(__file__).parent / "simant.sfc").read_bytes()

def file_to_lorom(off):
    bank = (off >> 15) & 0x7F
    return (bank << 16) | 0x8000 | (off & 0x7FFF)

# Manual-derived vocabulary. Group by category.
GROUPS = {
    "Main menu": [
        "FULL GAME", "SCENARIO GAME", "SAVED GAME", "TUTORIAL",
        "ANT INFORMATION",
    ],
    "Game-state titles": [
        "MARRIGE", "FULL END", "SCENARIO END", "GAME OVER", "SOUND",
    ],
    "Save UI": [
        "Erase", "No data", "BaN data", "Save Game", "Save data",
        "is too big", "Summarize", "can't save", "during game",
        "Saving.", "Please wait.",
    ],
    "Recruit menu": [
        "Recruit 5", "Recruit 10", "Recruit All",
        "Release 1/2", "Release All",
    ],
    "Queen menu": ["Dig New Nest", "Lay Eggs"],
    "View menu": [
        "Surface Overview", "B. Nest Overview", "R. Nest Overview",
        "Surface Close-up", "B. Nest Close-up", "R. Nest Close-up",
    ],
    "Scent menu": [
        "Hide Scent", "Black Nest", "Red Nest", "Black Trail", "Red Trail",
    ],
    "Control panel": ["Behavior", "Caste"],
    "Top-of-menu": ["Main Menu"],
    "Encyclopedia": [
        "SimAnt Strategy", "Introduction", "Ant Life", "Ants at Home",
        "Ants&Relatives", "EXIT",
    ],
    "Narrative bits": [
        "The Black Queen", "The Red Queen", "Will Wright", "the Maxis",
        "your queen", "nestmate",
    ],
}

def find_all(needle):
    """Return all byte offsets where needle (ASCII) appears in the ROM."""
    b = needle.encode("latin-1")
    out, start = [], 0
    while True:
        i = ROM.find(b, start)
        if i < 0:
            break
        out.append(i)
        start = i + 1
    return out

print(f"{'Group':<22} {'String':<24} {'File':>7}   LoROM")
print("-" * 72)
for group, items in GROUPS.items():
    for s in items:
        for off in find_all(s):
            addr = file_to_lorom(off)
            shown = s.replace("\n","\\n")
            print(f"{group:<22} {shown:<24} ${off:05X}  ${addr:06X}")

#!/usr/bin/env python3
"""
Verify that the (byte, dp[$2A], dp[$2B]) tuple emitted at every step matches
the Python reference. This is stricter than the byte-only diff because it
catches any state-update bug whose effect happens to be invisible at the
output of a given step.
"""
import subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from rng_reference import RngState, rng_step

HARNESS = Path(__file__).parent / "rng_state_test"

def c_run(s2A, s2B, mask, n):
    out = subprocess.check_output(
        [str(HARNESS), hex(s2A), hex(s2B), hex(mask), str(n)],
        text=True,
    )
    rows = []
    for line in out.split("\n"):
        parts = line.split()
        if len(parts) == 3:
            rows.append(tuple(int(x,16) for x in parts))
    return rows

def py_run(s2A, s2B, mask, n):
    st = RngState(s2A, s2B)
    out = []
    for _ in range(n):
        b = rng_step(st, mask)
        out.append((b, st.s2A, st.s2B))
    return out

SEEDS = [(0x12,0x34),(0x00,0x00),(0xFF,0xFF),(0xA5,0x5A),(0x01,0x80),(0x7E,0x42),(0xDE,0xAD)]
MASKS = [0xFF, 0x7F, 0x0F, 0x03, 0x01, 0x00]   # include 0x00 to test edge case
N = 1000

fail = 0
for s2A,s2B in SEEDS:
    for mask in MASKS:
        c = c_run(s2A,s2B,mask,N)
        p = py_run(s2A,s2B,mask,N)
        if c == p:
            continue
        fail += 1
        # find first divergence
        for i,(cr,pr) in enumerate(zip(c,p)):
            if cr != pr:
                print(f"DIVERGE @ seed=({s2A:#04x},{s2B:#04x}) mask={mask:#04x} step={i}")
                print(f"  C ref: byte={cr[0]:#04x} s2A={cr[1]:#04x} s2B={cr[2]:#04x}")
                print(f"  Py ref: byte={pr[0]:#04x} s2A={pr[1]:#04x} s2B={pr[2]:#04x}")
                break

if fail == 0:
    total = len(SEEDS)*len(MASKS)*N
    print(f"PASS: all {total} (byte, s2A, s2B) tuples matched.")
else:
    print(f"FAIL: {fail} configurations diverged.")
    sys.exit(1)

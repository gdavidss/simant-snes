#!/usr/bin/env python3
"""
Drive the C harness (rng_diff_test) and compare its output to the Python
reference (rng_reference.py) for several seed/mask combinations.

Reports first divergence (if any) per test, total matched count, and exits
nonzero on any mismatch so it's CI-friendly.
"""
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from rng_reference import sequence

ROOT = Path(__file__).parent
HARNESS = ROOT / "rng_diff_test"

N = 1000          # bytes per test
SEEDS = [
    (0x12, 0x34),
    (0x00, 0x00),
    (0xFF, 0xFF),
    (0xA5, 0x5A),
    (0x01, 0x80),
    (0x7E, 0x42),
]
MASKS = [0xFF, 0x7F, 0x0F, 0x03, 0x01]

def c_run(s2A, s2B, mask, n):
    out = subprocess.check_output(
        [str(HARNESS), hex(s2A), hex(s2B), hex(mask), str(n)],
        text=True,
    )
    return [int(line, 16) for line in out.split()]

def diff_first(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    return -1 if len(a) == len(b) else min(len(a), len(b))

results = []
fail = False
for s2A, s2B in SEEDS:
    for mask in MASKS:
        c = c_run(s2A, s2B, mask, N)
        p = sequence(s2A, s2B, mask, N)
        d = diff_first(c, p)
        if d < 0:
            results.append((s2A, s2B, mask, "MATCH", N, None))
        else:
            fail = True
            # show ~16 bytes of context around the divergence
            lo = max(0, d - 4)
            hi = min(N, d + 12)
            ctx_c = " ".join(f"{b:02X}" for b in c[lo:hi])
            ctx_p = " ".join(f"{b:02X}" for b in p[lo:hi])
            results.append((s2A, s2B, mask, "MISMATCH", d, (ctx_c, ctx_p)))

print(f"{'s2A':>4} {'s2B':>4} {'mask':>5}  {'verdict':<9} {'1st diff':>9}")
print("-" * 40)
for s2A, s2B, mask, v, d, ctx in results:
    print(f"{s2A:#04X} {s2B:#04X} {mask:#04X}  {v:<9} {d:>9}")
    if ctx is not None:
        ctx_c, ctx_p = ctx
        print(f"      C ref window: {ctx_c}")
        print(f"      Py ref window: {ctx_p}")

print()
if fail:
    print("RESULT: FAIL (some seeds/masks diverged)")
    sys.exit(1)
else:
    print(f"RESULT: PASS — all {len(results)} (seed,mask) combinations matched for {N} samples each.")

#!/usr/bin/env python3
"""Read /tmp/missing_syms.txt (output of `nm`-style sweep) and emit a
stubs.c that declares the unresolved symbols so the bundle links.

Functions get empty WEAK stubs (any file providing a strong definition
wins). Data symbols get plausible storage. `dp` is aliased to `wram`."""
from pathlib import Path

syms = [l.strip() for l in open("/tmp/missing_syms.txt") if l.strip()]
# Trim known control / data names.
SKIP_DEFINE = {"main"}    # we provide our own
# Classify: those starting with `rom_` are data tables; rest are functions.
def is_data(s):
    return s.startswith("rom_") or s in ("nest_close_substates",
                                          "landing_pick_table",
                                          "surface_closeup_table",
                                          "surface_overview_decorations")

out = []
out.append("/*")
out.append(" * stubs.c — link glue for the SimAnt decomp.")
out.append(" *")
out.append(" * Provides:")
out.append(" *   - The shared WRAM / MMIO storage (uint8_t wram[], mmio[])")
out.append(" *   - `dp` as a linker alias of `wram`, so files that wrote")
out.append(" *     `extern uint8_t dp[]` link without UB.")
out.append(" *   - Weak empty bodies for every unresolved ROM subroutine the")
out.append(" *     agents referenced but didn't lift. Real files providing")
out.append(" *     strong definitions take precedence.")
out.append(" *   - Weak data placeholders for ROM tables (so cross-file refs")
out.append(" *     to `rom_01_XXXX` resolve to a zero-filled buffer).")
out.append(" *")
out.append(" * NOTE: this exists ONLY to make the bundle LINK. It is not")
out.append(" * runnable — every stub returns 0 / does nothing.")
out.append(" */")
out.append("")
out.append("#include <stdint.h>")
out.append("")
out.append("/* ---- shared storage ---- */")
out.append("uint8_t wram[0x20000];")
out.append("volatile uint8_t mmio[0x10000];")
out.append('/* dp is provided as `#define dp wram` in each .c file. */')
out.append("")
out.append("/* ---- ROM data tables (zero-filled placeholders) ---- */")
data_syms = [s for s in syms if is_data(s)]
for s in data_syms:
    out.append(f"__attribute__((weak)) const uint8_t {s}[0x200] = {{0}};")
out.append("")
out.append("/* ---- function stubs ---- */")
out.append("/* `void X()` is interpreted as 'unspecified args' in C99 so it")
out.append(" * accepts any caller signature without a warning. */")
func_syms = [s for s in syms if not is_data(s) and s not in {"dp","wram","mmio","main"} and s not in SKIP_DEFINE]
for s in func_syms:
    out.append(f"__attribute__((weak)) int {s}() {{ return 0; }}")
out.append("")
out.append("/* ---- main() so the link succeeds ---- */")
out.append("extern void reset(void);")
out.append("int main(void) { reset(); return 0; }")

Path("/Users/guilhermedavid/simant-re/stubs.c").write_text("\n".join(out))
print(f"Wrote stubs.c with {len(func_syms)} function stubs + "
      f"{len(data_syms)} data placeholders.")

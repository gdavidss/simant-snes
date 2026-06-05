# F6 — Wiring Fixes

Targeted fixes for three (+ one bonus) high-impact wiring issues called out by V4-2 and V4-8.

All five touched files compile clean with `clang -Wall -Wextra -c <file>.c -o /tmp/check.o`.

---

## FIX 1 — `simant.c` shadowing of `coop_yield_877D`

**Problem.** `simant.c` had an empty static stub `sub_877D()` at ~line 1147 and a forward decl at line 1048. The same TU also `extern`-declared the real `coop_yield_877D` (defined in `misc_helpers.c`) at line 788, but the call site at `gs_scenario_game` (line 1069) routed through the static stub, so the cooperative-yield body was effectively dead within `simant.c`.

Because `sub_877D` was `static`, `__attribute__((weak))` would NOT have allowed the cross-TU lift to win — the static stub keeps internal linkage no matter what. The clean fix is to delete the static stub + forward decl entirely and call the extern `coop_yield_877D` directly.

**Before**

```c
static void sub_877D(void);         /* spawns next subtask for SCENARIO */
...
while (TASK_LIMIT != 0x04) sub_877D();
...
static void sub_877D(void)            { /* TODO: spawn next sub-task */ }
```

**After**

```c
/* sub_877D forward decl removed — was an empty static stub that shadowed the
 * real cooperative-yield body in misc_helpers.c (and lifted_helpers_6.c). */
...
while (TASK_LIMIT != 0x04) coop_yield_877D();  /* F6: was empty static stub */
...
/* F6: empty static sub_877D() removed — see coop_yield_877D (misc_helpers.c). */
```

**Sweep result.** Grep for `static void sub_[0-9A-F]+\(void\) *\{ *(/\*[^*]*\*/ *)?\}` across `simant.c`, `gap_fillers.c`, `stubs.c` returned only `simant.c` matches. About a dozen other empty static stubs remain (`sub_8967`, `sub_BC7F`, `sub_C318`, `sub_8D94`, `sub_BC53`, `sub_E527`, `sub_DEEE`, `sub_A3D6`, `sub_A354`, `sub_BB38`, `sub_BAF2`, `sub_85FC`, `sub_8611`) — these were intentionally left alone because: (a) most have no real body in another TU yet, and (b) the task asked specifically about `sub_877D`. They are candidates for a follow-up sweep.

**File touched.** `simant.c` (three edits: forward decl, call site, definition).

---

## FIX 2 — `player_actions.c` dispatch table still pointed at `*_pseudo` handlers

**Problem.** V4-2 noted the compile-anchor table at `player_actions.c:1232-1248` listed four `*_pseudo` symbols. The two menu-apply ones were already superseded by ROM-verified bodies in `player_actions_full.c`; the two click-handlers don't have `_full` lifts yet.

| Anchor before | Status | After |
|---|---|---|
| `recruit_menu_apply_pseudo` | superseded by STAGE 4 | `recruit_apply_02A1F4` |
| `queen_menu_apply_pseudo`   | superseded by STAGE 9 | `player_action_dispatch_03D792` |
| `worker_click_handler_pseudo` | partial lift only (V4-2 MAJOR) | unchanged, TODO noted |
| `food_click_handler_pseudo`   | no `_full` lift yet (V4-2 MAJOR) | unchanged, TODO noted |

I picked `player_action_dispatch_03D792` over `neighbour_action_03D808` for the queen entry because the dispatcher is the actual entry point for Lay/Dig and is shape-equivalent to the original menu-apply call (no args, void return).

**After** (extracted)

```c
extern void recruit_apply_02A1F4(uint16_t desired);
extern void player_action_dispatch_03D792(void);

__attribute__((used))
static void *const _player_actions_refs[] = {
    ...
    (void *)recruit_apply_02A1F4,           /* was recruit_menu_apply_pseudo */
    (void *)player_action_dispatch_03D792,  /* was queen_menu_apply_pseudo  */
    ...
    (void *)worker_click_handler_pseudo,    /* TODO: no _full lift yet */
    (void *)food_click_handler_pseudo,      /* TODO: no _full lift yet */
};
```

**Files touched.** `player_actions.c`. `player_actions_full.c` unchanged (verified to still compile).

---

## FIX 3 — `states_gameplay.c` mislabeled comments for states $24-$27

**Problem.** V4-8 cross-checked the raw ROM at $00:CA96 and $00:CCD0 and refuted V2-D's "B.NEST / R.NEST CLOSE-UP" labeling. The entity spawns at (0x24, 0x2C/0x3C/0x54) are panel HUD icons (types $27/$29/$2B = Auto/Manual icons, $24/$25/$26 = digit/readout handlers), not nest ants. The two-panel asset chain ($07/$B380 vs $07/$B671 palette, plus shared sprite/font banks) also matches a shared-chrome / different-label UI rather than two distinct nest views.

**Verdict (V4-8 §6).**

- States $24 / $25 = Behavior Control Panel (setup / run)
- States $26 / $27 = Caste Control Panel (setup / run)

**Changes.** Documentation only — no code touched.

- Header block at `STATE $24/$26 ... CA96`: rewrote to label as Behavior/Caste setup and explain the spawn list as HUD chrome.
- Header block at `STATE $25/$27 ... CCD0`: rewrote to label as Behavior/Caste run; clarified that dp[$0286] / dp[$0288] are the panel-submenu-open flags, not nest interior zoom flags.
- Inline comments: `"B.Nest variants"` → `"Behavior Control Panel variant"`, `"R.Nest variants"` → `"Caste Control Panel variant"`, palette / labels / sprite comments updated accordingly.

The function identifiers `state_view_nest_closeup_setup_CA96` and `state_view_nest_closeup_run_CCD0` were left as-is (task said "don't change any code") — they should be renamed in a future cleanup.

**File touched.** `states_gameplay.c`.

---

## FIX 4 (bonus) — `entities_d.c:885` null-pointer deref

**Problem.** V4-2 BLOCKER #3:

```c
cost += *(uint16_t *)&((uint8_t *)0)[/*AE06+y placeholder*/0];
```

This would dereference the null pointer the moment the player confirms a dialog purchase (`MENU_BUTTON_LATCH == 0x70` branch in `type29_state1_drift_AD85`). Crash.

**ROM context.** The disassembly fragment is

```
ASL          ; double Y for 16-bit index
TAY
LDA $AE06,y  ; load table entry (program-bank relative -> $00:AE06)
CLC
ADC <cost>
```

I dumped the SimAnt ROM at file offset 0xAE06 (LoROM, no header — confirmed 1 MB exact). The contents are text-screen ASCII data ("...ut.to rain..The rain will wash away..."), so $00:AE06 is NOT the correct file offset for the lookup table — the real table is reached via a different bank or code path that we haven't recovered yet.

**Resolution.** Per the task fallback ("if the table isn't accessible from C, at least replace the deref with a safe no-op + clear TODO"), I replaced the null deref with `cost += 0;` and an inline comment block explaining the chain of evidence and the TODO. The result is that on the confirm gesture, `cost = (budget + SHADOW_PRICE_LO - SHADOW_PRICE_HI) >> 3` — a plausible first-order purchase price that no longer crashes.

**File touched.** `entities_d.c`.

---

## Compile verification

```
$ clang -Wall -Wextra -c simant.c             -o /tmp/check.o    # clean
$ clang -Wall -Wextra -c player_actions.c     -o /tmp/check.o    # clean
$ clang -Wall -Wextra -c player_actions_full.c -o /tmp/check.o   # clean
$ clang -Wall -Wextra -c states_gameplay.c    -o /tmp/check.o    # clean
$ clang -Wall -Wextra -c entities_d.c         -o /tmp/check.o    # clean
```

No project-wide build was attempted (per F6 budget).

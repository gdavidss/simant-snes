# G2 — entities_e.c per-state body lift

## Summary

| Dispatcher | ROM | Sub-states | Lifted | Stubbed | Notes |
|-----------|------|------------|--------|---------|-------|
| `$21` | `$B68D` | 2 | 2 | 0 | Caste-split digit (dp[$A4]); init+random / branch on count |
| `$22` | `$B6DD` | 2 | 2 | 0 | Caste-split digit (dp[$A6]) |
| `$23` | `$B72D` | 2 | 2 | 0 | Caste-split digit (dp[$A8]) |
| `$24` | `$B77D` | 2 | 2 | 0 | Behavior-split digit (dp[$A8]) |
| `$25` | `$B7C1` | 2 | 2 | 0 | Behavior-split digit (dp[$A6]) |
| `$26` | `$B7FF` | 2 | 2 | 0 | Behavior-split digit (dp[$A4]) + "%" suffix blits |
| `$2D` | `$B90A` | many | 0 | 1 | menu cursor, ~200 bytes table-indirect (kept weak TODO) |
| `$2E` | `$B991` | many | 0 | 1 | dialog panel, switch on dp[$0240] nibble (kept weak TODO) |
| `$2F` | `$BA84` | many | 0 | 1 | sibling dialog panel (kept weak TODO) |
| `$31` | `$BB4F` | 2 | 2 | 0 | attr=$98 init / DB52 draw w/ dp[$44]=3 toggle |
| `$32` | `$BB74` | 2 | 2 | 0 | attr=$18 init w/ target=pos / pos += scroll-base |
| `$33` | `$BBC9` | 2 | 2 | 0 | APU-port wait + SFX $24 / DB52 + anim_frame += 4 |
| `$37` | `$BEEE` | 2 | 2 | 0 | timer init / VRAM-row push at $F673+pair_c |
| `$38` | `$BF37` | 2 | 2 | 0 | rand-spawn placement / drift + timer-destroy |
| `$3A` | `$C02B` | 6 | 6 | 0 | state-0 init, state-1 setup (already done by F1), states 2-5 lifted (moving sprite w/ bounds-check + sfx) |
| `$3B` | `$C247` | 4 | 4 | 0 | 4-phase scrolling banner (x±2, y=$30→$60), C2DC anim |
| `$3D` | `$C36E` | 5 | 5 | 0 | BICYCLE: spawn, roll-in, mid-cross, exit, wrap-around |
| `$3E` | `$C48F` | 2 | 2 | 0 | SFX $40 + timed self-destroy |
| `$3F` | `$C5C8` | tail | tail | 0 | watchdog tail lifted (`flag_11 < dp[$12]` => DB52) |

## Stats

- **State bodies lifted: 41** across 16 dispatchers
- **Dispatchers fully lifted: 13** ($21-$26, $31-$33, $37, $38, $3A, $3B, $3D, $3E, $3F)
- **Dispatchers still weak-stubbed: 3** ($2D, $2E, $2F) — explicitly called out as data-mixed / >200 bytes
- **Compile:** `clang -Wall -Wextra -c entities_e.c -o /tmp/check.o` — clean, no warnings

## New extern signatures

```c
extern void    sub_B87E_anim_from_count(Entity *self, uint8_t mouse_cnt);
extern void    sub_B8AC_advance_drawer(Entity *self, uint8_t a, uint16_t y);
extern void    sub_B8CB_anim_step(Entity *self, uint8_t a, uint16_t y);
extern void    sub_DD7F_rand_xy(uint16_t *out_x, uint16_t *out_y);
extern uint8_t sub_DC84_in_bounds(Entity *self);
extern void    sub_008E9D_play_sfx(uint8_t id);
extern void    sub_008EA3_play_sfx2(uint8_t id);
extern uint8_t mmio_read_2143(void);
extern void    sub_C2DC_C3_anim(Entity *self);
extern void    sub_C41C_bike_step(Entity *self, uint8_t delta);
```

These now need bodies somewhere downstream (probably `lifted_helpers_*.c` or `stubs.c`).

## Unexpected patterns

1. **$21-$23 and $24-$26 are nearly identical triples**: only the dp source byte ($A4/$A6/$A8) and the Y/A pointer pair (`$F640..$F670`) differ. State-1 has a 5-vs-<5 branch on the count.
2. **`$3A` state-0 was off by one**: F1's note said C055 but the actual table entry was C04F which sets `timer_10 = $C0` first; corrected.
3. **`$3F` tail is trivial**: only ~5 bytes — fully lifted instead of stubbed.
4. **`$33` state-0 reads APU port `$002143`** to gate SFX — modeled via `mmio_read_2143()`.
5. **`$3A` state 2 (`$C098`) has a bounce/flip pattern** with SFX $46 + attr-XOR $20 (mirror flip on bounce) — useful pattern to remember when lifting other dispatchers.
6. **`$3D` BICYCLE state-4 wraps around** to state 1 (not state 0) when x goes negative; preserves timer at 0.

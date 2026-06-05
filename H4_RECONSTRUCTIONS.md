# H4 — Wiring Port-Only Reconstructions Into `sim_tick`

This pass takes the manual-fidelity reconstructions G5 wrote in
`mechanics_extra.c` and connects them to the live simulation loop, while
keeping the original SNES port's byte-exact behavior reachable via a
single compile flag.

## Background

The SNES port of SimAnt simplified out two mechanics that the printed
manual still documents:

| Mechanic                     | Manual page | Status in ROM       | Status here                       |
|------------------------------|-------------|---------------------|-----------------------------------|
| Caterpillar 15-ant harvest   | p.34        | Absent (Tomcat cut) | Reconstructed (`mechanics_extra.c`) |
| Aphid ranching / honeydew    | p.21        | Absent              | Reconstructed (`mechanics_extra.c`) |

G5 produced manual-fidelity rebuilds clearly labeled `RECONSTRUCTED` /
`ABSENT_IN_PORT`. H4's job: wire them so they actually run during
`sim_tick`, without contaminating the byte-exact ROM build.

## Feature gate: `WRAP_PORT_RECONSTRUCTIONS`

A single preprocessor symbol gates the restorations everywhere.

* Defined (default) -> port behavior. The restored mechanics run each
  sim tick.
* Undefined (`clang -UWRAP_PORT_RECONSTRUCTIONS`) -> ROM-exact build.
  The reconstruction bodies compile down to empty stubs, and the call
  sites in `sim_tick` vanish entirely.

The gate lives at the top of `mechanics_extra.c`:

```c
#ifndef WRAP_PORT_RECONSTRUCTIONS
#define WRAP_PORT_RECONSTRUCTIONS 1
#endif
```

Both bodies and call sites are wrapped in `#ifdef
WRAP_PORT_RECONSTRUCTIONS` blocks. ROM-lifted functions in the same file
(`ant_lion_tick_C0FD_lifted`, `pebble_drop_on_crevice_IMPLICIT`,
`stone_block_red_entrance_IMPLICIT`, `lr_cursor_fixed_scroll_lifted`,
`y_button_cursor_to_view_icon_lifted`) are intentionally NOT gated —
they reconstruct bodies that genuinely exist in the cart.

## What was wired

### Caterpillar 15-ant harvest

The original V4-4 lift took a per-entity signature
`(VisEntity *self, uint16_t self_slot)`. I refactored it into:

* `caterpillar_harvest_check_one()` — static helper, the original
  per-entity logic.
* `caterpillar_harvest_check_RECONSTRUCTED(void)` — new parameterless
  wrapper that sweeps `vis_entities[0..0x3F]` and harvests every
  caterpillar that meets the threshold this frame.

Wired into `simulation.c::sim_tick` right after `ant_lion_tick_C0FD()`,
in the per-tick chain (gated):

```c
ant_lion_tick_C0FD();
#ifdef WRAP_PORT_RECONSTRUCTIONS
    caterpillar_harvest_check_RECONSTRUCTED();
    aphid_honeydew_drip_RECONSTRUCTED();
#endif
```

### Aphid honeydew drip

The original `aphid_honeydew_drip_ABSENT_IN_PORT()` no-op marker is
preserved verbatim (it documents the search that proved the mechanic is
absent in the cart).

Next to it, H4 adds a new, gated reconstruction:

* `aphid_honeydew_drip_RECONSTRUCTED(void)` — models the aphid as a
  static "honeydew source" entity (type `0x3A`, a slot V4-4 already
  flagged as unused in the dispatch table).
* The aphid's `state` field is repurposed as a per-entity cooldown
  counter, decremented each tick.
* When the cooldown reaches 0 AND at least one B worker ant
  (`vis_entities[i].alive == 0x0E`) is within `APHID_TEND_RADIUS=24`
  pixels, the aphid drips: `B_FOOD_AREA += 1`, `FOOD_TOTAL += 1`, and
  the cooldown resets to `APHID_DRIP_PERIOD=128`.

Roughly 50 lines, well under the 100-line budget. Wired alongside the
caterpillar sweep in `sim_tick`, behind the same `#ifdef`.

## Design notes on the aphid reconstruction

The drip amount is deliberately tiny (1 food per 128 ticks per aphid) so
that aphid placement is a nice-to-have rather than a primary food
source. Three caretaker ants near three aphids approximates one
caterpillar payout (`$40`) over ~3 minutes — slow, ambient, never
dominant.

The "only drips when tended" rule honors the manual phrasing: ants
*milk* the aphid; the aphid does not spontaneously gift food. This also
makes scenario placement trivial — scatter aphid entities and let normal
forage AI do the rest. No new AI behavior, no caste-mode plumbing, no
new sprite asset (the type `0x3A` slot can borrow any small static
sprite for now).

A future refinement could give the aphid a real entity-dispatch
handler in `entities_d.c` so it animates and shows a honeydew bead
sprite when its cooldown is near zero. That's outside H4's scope — H4
keeps the reconstruction in `mechanics_extra.c` so it can be removed in
one file edit.

## Verification

Both files compile cleanly in both modes:

```text
clang -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -O0 -g \
      -c mechanics_extra.c -o /tmp/check.o          # default (port mode)
clang -UWRAP_PORT_RECONSTRUCTIONS ... -c mechanics_extra.c -o /tmp/check.o
clang ... -c simulation.c -o /tmp/check.o            # default
clang -UWRAP_PORT_RECONSTRUCTIONS ... -c simulation.c -o /tmp/check.o
```

All four invocations exit with no warnings or errors.

## Files touched

* `/Users/guilhermedavid/simant-re/mechanics_extra.c` — feature gate,
  caterpillar refactor + no-arg sweep wrapper, aphid reconstruction
  alongside the existing absence marker.
* `/Users/guilhermedavid/simant-re/simulation.c` — two new `extern`
  declarations and a 2-line `#ifdef` block in `sim_tick`.

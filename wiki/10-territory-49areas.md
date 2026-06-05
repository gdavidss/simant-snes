# Territory: The 49-Area Full Game World

> **Manual reference:** pp. 18–20 ("Playing the Full Game", "Expanding Territory"),
> p. 30 ("House Screen").
> **Source:** [`territory.c`](../territory.c) — every routine listed here cross-
> references an exact line in that file plus the original ROM address.

## 1. Overview

The Full Game (`PLAY_MODE == 2`, `$7E:0099`) treats the yard + house as a
**7 × 7 grid of 49 sections**. Each section can hold a Black-colony (B)
population count and a Red-colony (R) population count, both `0..250`.
Internally the grid is allocated as **8 × 8** for byte alignment: only the
inner 7 × 7 is play area; column 7 and row 7 are inaccessible padding.

The two per-colony pop-maps live at:

| Address    | Size       | Meaning                                       |
| ---------- | ---------- | --------------------------------------------- |
| `$7E:EA46` | 8×8 × 16   | `AREA_B_POP_MAP[y][x]` — B ants per section   |
| `$7E:EAC6` | 8×8 × 16   | `AREA_R_POP_MAP[y][x]` — R ants per section   |

Indexing helper (`area_offset_F5B2`, lifted from `$02:F5B2`,
[`territory.c:170`](../territory.c)):

```c
offset_bytes = ((y << 3) + x) << 1;
AREA_B_POP(x,y) = WMEM16(0xEA46 + offset_bytes);
AREA_R_POP(x,y) = WMEM16(0xEAC6 + offset_bytes);
```

## 2. The "Current Area" — a SNES-port peculiarity

The current-area coordinates live at:

| Address    | Cell           |
| ---------- | -------------- |
| `$7E:E736` | `CUR_AREA_X`   |
| `$7E:E738` | `CUR_AREA_Y`   |

**Key finding (V3 verification, [`territory.c:780-810`](../territory.c)):**

> The SNES port simulates only **one** area at a time. `CUR_AREA_X` /
> `CUR_AREA_Y` are written **once** by the boot path at `$03:8507` — to
> `(3,3)` (the centre cell, mid-yard) — and **never** modified by any other
> lifted code. The 49-area map is a **presence-only abstraction**: it tracks
> where B and R have a foothold and animates the House-Screen icons
> accordingly, but the only live entity simulation runs against the central
> tile.

This deviates from the Mac/Apple II "true Full Game" where the player can
walk into any black-section. On SNES, the "click to move" UI is overlay-
only — see the **House Screen** wiki page (`11-house-screen-ui.md`) for the
visual effect, and §6 below for the click-gate that rejects R-only sections.

## 3. Area-State Encoding (the "live area" entries)

Up to **12 live-area display slots** at `$7F:E882..E8B2` hold the
"highlighted" sections rendered on the House Screen. Their `STATE` byte
([`territory.c:127`](../territory.c)) follows the encoding table at
`$04:BE41`:

| Value | Name              | Tile-base | Meaning                       |
| ----- | ----------------- | --------- | ----------------------------- |
| 0     | `EMPTY`           | `$42`     | green (no colony)             |
| 1     | `BLACK`           | `$48`     | B-only                        |
| 2     | `RED`             | `$4A`     | R-only                        |
| 3     | `BLACK_ALT`       | `$48`     | post state-advance (B)        |
| 4     | `RED_ALT`         | `$4A`     | post state-advance (R)        |
| 5     | `STRIPED_A`       | `$4C`     | contested (animation cel 1)   |
| 6     | `STRIPED_B`       | `$4E`     | contested (animation cel 2)   |

**Important refutation of earlier guesses (V4-8 finding):** there is **no
flashing-bit** in the state byte. The "current-area-flashes" effect comes
from a separate sprite-compositing pass in the renderer
(`$04:BDD4`), which overlays an animated highlight on the
`LIVE_AREA_LAST_IDX` entry. See `11-house-screen-ui.md` §4 for details.

## 4. Mating Flight Trigger

ROM `$02:9E35`, lifted at [`territory.c:417`](../territory.c) as
`territory_marriage_flight_trigger_9E35()`.

Conditions for the event:

1. `PLAY_MODE == 2` (Full Game only).
2. `AREA_B_POP_LIVE >= 100` (B ant count in current area).
3. `POP_B_BREEDER + POP_R_BREEDER >= 20` (combined winged breeders).
4. `MARRIAGE_COOLDOWN == 0` (re-armable 200-tick timer at `$7E:EC94`).

When (1)–(3) are met and the cooldown is zero, event `$4B` is queued on
the ring buffer at `$000FE0` via `queue_event_F65A`. **Whether the event
fires or not**, the cooldown is re-armed to **200 ticks** (~25 s wall
clock at the 8.5 Hz Full-Game tick).

```c
void territory_marriage_flight_trigger_9E35(void) {
    if (PLAY_MODE != 0x02)                                return;
    if (AREA_B_POP_LIVE < 100)                            return;
    if ((POP_B_BREEDER + POP_R_BREEDER) < 20)             return;
    if (MARRIAGE_COOLDOWN == 0) queue_event_F65A(0x4B);
    MARRIAGE_COOLDOWN = 200;            /* re-arm even if event fired */
}
```

> Note: the manual implies the 20-breeder check is **per colony**. The SNES
> port **sums B and R breeders** before the check — a port simplification
> verified at `$02:9E47` (`ADC $E79E`). See [`territory.c:391`](../territory.c).

## 5. Mass Exodus (per-tick territory spread)

ROM `$03:F050..F1F2`, lifted at [`territory.c:488`](../territory.c) as
`mass_exodus_cap_and_presence_F050()` (the cap pass — function header at
line 488, body at 489) and [`territory.c:651`](../territory.c) as
`area_grid_scan_F02A()` (the full walk; this is the per-area scan that
**calls** `mass_exodus_cap_and_presence_F050` at `territory.c:663` —
earlier wiki drafts said the call came from `pop_aggregator_956E`, which
is incorrect at the C-code level).

Triggered when `SIM_COUNTER & 0x1F == 0` (every 32 sim-ticks, ~3.8 s
wall clock). `SIM_COUNTER` lives at `$7E:E788` — earlier drafts wrote
`$E878`, which was a `78↔87` byte transposition typo:

```
mass_exodus_cap_and_presence_F050():
   AREA_B_POP[CUR_X][CUR_Y] = clamp(AREA_B_POP_LIVE & 0x3FF, 0..250);
   AREA_R_POP[CUR_X][CUR_Y] = clamp(AREA_R_POP_LIVE & 0x3FF, 0..250);

area_grid_scan_F02A():
   for (x = 0..6) for (y = 0..6):
       if ((x,y) == current) skip;        // already capped
       bal = neighbour_balance_F2D9(x,y); // ±3 per occupied neighbour
       if (B_pop > 0):
           new_B = B_pop + bal;
           if (new_B <= 0):  B_pop = 0; E794++;     // colony death
           if (new_B >= 250): B_pop = 250;
                              if rand(10)==0 -> split_to_neighbour(0,x,y);
           else:              B_pop = new_B;
       if (R_pop > 0):
           new_R = R_pop - bal;
           if (new_R <= 0):
               R_pop = 0; E792++;
               if ((x,y) == (6,6)):           // queen escapes!
                   rx = rand(4) + 2; AREA_R_POP[rx][0] = 20;
           ...
```

### 5a. Neighbour Balance

`neighbour_balance_F2D9` ([`territory.c:506`](../territory.c)): walks the 4
cardinal neighbours (plus 2 zero-delta entries used as no-op padding by
the ROM); each B-occupied neighbour adds **+3**, each R-occupied neighbour
adds **−3**. The result biases the cell's population delta toward whichever
colony is more entrenched nearby.

### 5b. Split-to-Neighbour

`area_split_to_neighbour_F358` ([`territory.c:538`](../territory.c), ROM
`$03:F358`): picks `dx, dy ∈ {-1, 0, +1}` via `rand_signed_F38D(2)`,
clamps the destination to `[0..6]`, and increments either `AREA_B_POP` or
`AREA_R_POP` at the new cell. If the destination cell was previously zero,
the per-colony "new-area" counters at `$E78E` / `$E790` also tick.

### 5c. Queen Escape

When R is driven to zero **specifically at corner (6, 6)**, the ROM seeds
**20 R ants** at `AREA_R_POP(rand(4)+2, 0)` — i.e. somewhere in row 0,
columns 2–5. This is the manual's "the Red queen escapes to a new area"
behaviour (p. 19). See [`territory.c:696-699`](../territory.c).

## 6. House Screen Click Gate

Manual p. 19: *"Remember: you can only move to sections that already have a
black colony."* The reject string lives at `$01:A550` and is reachable via
message-codes 18..24 on the per-state message dispatcher at `$00:DFCD`.

In the SNES port, the gate is a UI overlay only (since the simulation never
actually changes area). The valid-target test reduces to inspecting the
clicked live-area's state byte — see [`territory.c:764-776`](../territory.c):

```c
int house_can_move_to_clicked(uint8_t clicked_live_idx) {
    return area_state_has_B(LIVE_AREA_STATE(clicked_live_idx));
}
```

## 7. Game-End Check

ROM `$03:F25B..F2CE`, documented at [`territory.c:935-963`](../territory.c).
Fires as the tail of `area_grid_scan_F02A`. Three outcomes:

| Condition (Full Game)                                              | Outcome              | Bits set                       |
| ------------------------------------------------------------------ | -------------------- | ------------------------------ |
| `AREA_R_PRESENCE == 0` AND scenario active                         | **Victory** (B wins) | `dp[$A7] \|= 0x08`, `dp[$E1] = 2` |
| `AREA_B_PRESENCE < 2` AND no live B entities of any caste          | **Defeat** (B dies)  | `dp[$A7] \|= 0x10`, `dp[$E1] = 2` |
| Otherwise                                                          | continue             | —                              |

Bit `$08` = "victory"; bit `$10` = "defeat". The state-1A handler reads
these at the next render-cycle and triggers the level-completion cinematic.

## 8. Mermaid: Per-32-Tick Territory Flow

```mermaid
flowchart TB
    Start([sim tick — every 1/8.5 s]) --> Mod{tick & 0x1F == 0?}
    Mod -- no --> End([continue])
    Mod -- yes --> ResetP[Reset AREA_B_PRESENCE / AREA_R_PRESENCE]
    ResetP --> CapLive[mass_exodus_cap_and_presence_F050<br/>cap live area at 250]
    CapLive --> Loop{for each 7×7 cell<br/>except current}
    Loop -- non-empty --> Bal[neighbour_balance_F2D9<br/>±3 per B/R neighbour]
    Bal --> Bcell{B_pop > 0?}
    Bcell -- yes --> Bdelta[new_B = B_pop + bal]
    Bdelta -- "= 0" --> Bdie[B dies in cell · E794++]
    Bdelta -- ">= 250" --> Bcap[cap to 250<br/>rand(10)==0 → split B]
    Bdelta -- normal --> Bstore[store new_B]
    Bcell -- no --> Rcell{R_pop > 0?}
    Bstore --> Rcell
    Bcap --> Rcell
    Bdie --> Rcell
    Rcell -- yes --> Rdelta[new_R = R_pop - bal]
    Rdelta -- "= 0" --> Rdie{cell == 6,6?}
    Rdie -- yes --> Queen[Queen escapes:<br/>R[rand+2,0] = 20]
    Rdie -- no --> Rzero[R = 0 · E792++]
    Rdelta -- ">= 250" --> Rcap[cap to 250<br/>joypad-rand → split R]
    Rdelta -- normal --> Rstore[store new_R]
    Rcell -- no --> Next[next cell]
    Queen --> Next
    Rzero --> Next
    Rstore --> Next
    Rcap --> Next
    Next --> Loop
    Loop -- done --> WinCheck[area_border_win_check_F25B<br/>sets dp_A7 / dp_E1]
    WinCheck --> End
```

## 9. Symbol / Address Index

| ROM address      | Function (`territory.c` line)           | Role                           |
| ---------------- | --------------------------------------- | ------------------------------ |
| `$02:9E35`       | `territory_marriage_flight_trigger_9E35` ([:417](../territory.c)) | Mating Flight event queue   |
| `$03:F050`       | `mass_exodus_cap_and_presence_F050` ([:488](../territory.c))     | Cap current-area pop @ 250  |
| `$03:F02A`       | `area_grid_scan_F02A` ([:651](../territory.c))                   | 7×7 walk, balance & split (calls F050 at :663) |
| `$03:F2D9`       | `neighbour_balance_F2D9` ([:506](../territory.c))                | ±3 per occupied neighbour   |
| `$03:F358`       | `area_split_to_neighbour_F358` ([:538](../territory.c))          | Single ant → adjacent cell  |
| `$02:F5B2`       | `area_offset_F5B2` ([:170](../territory.c))                      | `(y<<3 + x)<<1`             |
| `$03:F25B`       | (documented) `area_border_win_check` ([:935](../territory.c))    | Game-end bits                |
| `$03:96B0`       | `area_transition_append_96B0` ([:844](../territory.c))           | Live-area display append    |
| `$03:9930`       | `area_state_advance_9930` ([:888](../territory.c))               | Tile-state cycle 0→1→…→4    |

## 10. Cross-References

* **House Screen rendering**: `wiki/11-house-screen-ui.md` (the visual layer
  built on top of the data documented here).
* **Status Screen percentages**: also in `territory.c` (`pct100` /
  `status_screen_compute_territory`) — manual p. 32.
* **Simulation tick driver**: `simulation.c` (per-frame entry that calls
  `area_grid_scan_F02A` and `marriage_flight_trigger_9E35`).

# Pathfinding

How ants choose which direction to step. The manual does **not**
document this — it speaks vaguely about ants following "scent trails"
and "the strongest scent." The actual ROM body at `$02:A710` is much
richer: three concentric paths covering "scent present," "no scent but
target known," and "lost — wander." Every walking ant (Worker,
Soldier, Queen body, Yellow Ant) goes through the same code.

Cross-links: [Entity system](04-entity-system.md),
[Yellow Ant](05-yellow-ant.md).

---

## 1. Surface pathfinding — `scent_follow_gradient_02A710`

The full lift lives in
[`player_actions_full.c`](../player_actions_full.c) line 625 (the
earlier simplification at [`scent.c`](../scent.c) line 429 only
covered Path A).

Inputs (per-call scratch frame in WRAM bank `$7F` at `$F6xx`):

```
$F61B  x_cell (0..63)         $F619  color  (0 = black, !=0 = red)
$F61D  y_cell (0..31)         $F607  current_direction (0..7)
$F603  self_x (raw)           $F605  self_y (raw)
$EE38/$EE3A  Black home X/Y   $EE3C/$EE3E  Red home X/Y
$000100      volatile per-frame RNG noise (long-mode read)
```

```mermaid
flowchart TD
    Start[Read center scent cell<br/>at $7F:4000 (black) or<br/>$7F:4800 (red)] --> Cmp{center != 0?}
    Cmp -- YES --> A[<b>PATH A — gradient</b><br/>scan 8 neighbours,<br/>pick max scent direction]
    Cmp -- NO --> Tgt[Call $02:98ED to get<br/>direction toward home colony]
    Tgt --> TgtOk{target known?<br/>dir != 0}
    TgtOk -- YES --> Rng{rng & 0x03 != 0?<br/>(75 percent)}
    Rng -- YES --> B[<b>PATH B — biased</b><br/>grad_dir = dir - 1]
    Rng -- NO --> C[<b>PATH C — wander</b><br/>rng & 0x07 = random dir]
    TgtOk -- NO --> C
    A --> Smooth[turn-smooth via $02:AAC7<br/>(8x8 table at $02:AAD8)]
    B --> Smooth
    C --> Smooth
    Smooth --> Out[return next direction 0..7]
```

### Path A — scent present

Center cell is nonzero, so the ant is "in the gradient." Scan the 8
compass neighbours using the direction offset tables at
`$02:8065` (dx) and `$02:8077` (dy):

```c
/* player_actions_full.c:79 */
static const int8_t scent_dir_dx_028065[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
static const int8_t scent_dir_dy_028077[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
/* N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7 */
```

Pick the direction with the maximum neighbour value (ties go to the
first scan, which is N).

### Path B — no scent, target known (75%)

Center is zero. Call `scent_dir_from_to_0298ED(self, home)` — a
ROM helper at `$02:98ED` that returns 0 if `self == home`, else
1..8 (where the gradient direction is `dir - 1`). With
**probability 3/4** the ant biases toward home using this direction.

The probability fence is `(rng >> 1) & 0x03 != 0`, which evaluates
non-zero for 3 of every 4 RNG samples. An earlier lift had this
inverted (claimed 25%); see the comment block in
[`player_actions_full.c`](../player_actions_full.c) line 685.

### Path C — no scent and (target == self OR RNG forced)

Pick a random direction `rng & 0x07`. The earlier lift incorrectly
called `$02:AA51` (edge-aware random) here — the ROM at
`$02:A800..A80C` just does a plain 3-bit RNG masked direction:

```asm
LDX $F607              ; current direction
LDA $000100 / LSR / AND #$0007 / TAY
JSL $02:AAC7           ; smooth current toward random
```

### Turn smoothing — `$02:AAC7`

All three paths funnel into the smoothing helper. Indexed as
`smooth(current_dir, gradient_dir) -> next_dir`, the 8x8 table at
`$02:AAD8` blunts 180° flips so ants don't snap-turn. The table is
recovered in full at [`scent.c`](../scent.c) line 369.

Example row (current = N): if the gradient says "go S" the table
returns "NW" — i.e. the ant U-turns over two ticks, not one.

---

## 2. Nest pathfinding — tile-bitmap test

The surface gradient drives long-range movement. Inside the
nest, however, ants navigate **structurally** by testing the tile
bitmap stored at:

```
$7F:6000  Black nest tile grid  (4 KB; 64 wide x 32 tall, 16-bit per cell)
$7F:8000  Red   nest tile grid  (4 KB; same shape)
```

See [`scent.c`](../scent.c) lines 45 & 162 for the layout. The second
byte of each 16-bit cell holds rendering attributes; the first byte
is the tile id used by the AI for walkability tests.

### Tile codes (recovered from `$03:9269` seed routine and the per-tile
collision test in `combat.c`)

| Code  | Meaning              | Walkable? | Notes                                    |
|-------|----------------------|-----------|------------------------------------------|
| `$51` | floor / tunnel       | yes       | Seed routine treats `$51` as "no scent inside" (open chamber). |
| `$52` | own-team marker      | yes       | Combat marker — friendly tile, hold for tick decay. |
| `$48` | enemy combat marker  | yes (combat) | Active combatant slot of opposite color. |
| `$49` | enemy combat marker  | yes (combat) | Variant.                                 |
| `$4A` | enemy combat marker  | yes (combat) | Variant.                                 |
| `$4E` | wall                 | no        | Solid — pathfinder treats as blocked.   |
| `$1A` | pebble / loose dirt  | yes       | Walkable but counts as "rough" terrain. |

The walking-ant collision check `collision_check_DC84()`
([`entities_c.c`](../entities_c.c) line 419 for an example call) reads
the destination tile, sets carry if the move is blocked, and plays
SFX `$4E` ("ouch") for hits. The Queen handler reacts by stunning
for 120 frames and turning around.

### Diagonal blocking

Diagonal moves use the same 8-direction offset tables at
`$02:8065/$02:8077` listed above. When the diagonal target is open
but either of the adjacent cardinals is a wall (`$4E`), the
pathfinder still allows the diagonal step — there is **no L-shape
guard** in the ROM. This is observable in-game as ants "squeezing
through corners."

---

## 3. Walking-ant tile-hold (post-fight decay)

After a combat resolves, the **winner's tile stays up** for a
fixed number of ticks before clearing. This blocks re-engagement by
freshly-spawned enemies and is the mechanism that makes Soldiers
effectively "tougher" than Workers — they hold the contested tile
twice as long:

| Caste   | Post-fight HP | Decay time |
|---------|---------------|------------|
| Worker  | `$19` = 25    | 25 ticks   |
| Soldier | `$32` = 50    | 50 ticks   |

Implementation in [`combat.c`](../combat.c):

```c
/* combat.c:525 */
COMBAT_HP(i) = 0x19;        /* Worker — 25 ticks of decay */
/* combat.c:604 */
COMBAT_HP(i) = 0x32;        /* Soldier — 50 ticks of decay */
```

The manual hints at this on p.16 ("Soldiers are tougher than
Workers") but never names the decay timer. Every tick the resolver
decrements HP, and on `HP & 1` (every other tick) rolls a frame in
`[3..6]` for the animation. When HP underflows, the combatant is
removed and the tile clears.

---

## 4. Walking-ant AI hookpoints

The Worker (type `$0E`) and Soldier (type `$0F`) walking handlers in
[`entities_b.c`](../entities_b.c) call the gradient routine each
"physics tick" (`dp[$00] == 0x04`):

```c
/* entities_b.c:671 — type14_state1_A13E_walking */
sub_D747_physics_step(e);             /* integrates velocity */
/* heading is refreshed from gradient in sub_D721 / sub_D747 chain */
```

Velocity is set from heading by `sub_D721_set_velocity_from_heading`
([`entities_b.c`](../entities_b.c) line 726). Heading is the smoothed
direction returned by `scent_follow_gradient_02A710` for the current
cell. So the full pipeline is:

```
gradient direction  ->  AAC7 smoothing  ->  D721 velocity  ->  D747 physics step
```

The Queen wander in [`entities_c.c`](../entities_c.c) line 409 uses
a simpler model — random heading with periodic re-rolls — because
queens are not supposed to chase scent (they stay near the nest).

---

## 5. References

- Full gradient lift: [`player_actions_full.c`](../player_actions_full.c) line 560.
- Simplified version (Path A only): [`scent.c`](../scent.c) line 429.
- Smoothing table: [`scent.c`](../scent.c) line 369.
- Walking AI: [`entities_b.c`](../entities_b.c) lines 671-752.
- Queen wander: [`entities_c.c`](../entities_c.c) lines 409-540.
- Combat decay (tile-hold): [`combat.c`](../combat.c) lines 522-621.
- 49-area territory grid (scenario-level navigation): [`territory.c`](../territory.c).

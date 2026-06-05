# V4-5 — SimAnt SNES Architectural Diagrams

Mermaid diagrams produced from the lifted decomp in this tree. Source files
referenced: `simant.c`, `simulation.c`, `scent.c`, `combat.c`,
`entities_b.c`, `states_gameplay.c`, `states_menu.c`, `save_options.c`,
`audio_driver.c`, `misc_helpers.c`, `player_actions.c`, plus
`README.md`, `COVERAGE.md`, `ENTITIES.md`, `PORTING.md`.

Conventions: ROM addresses in `$bank:offset`; WRAM in `$7E/$7F:offset`;
direct-page (DP) in `dp[$xx]`.

---

## 1. Boot flow

From RESET vector at `$00:8009` through `main_9340` (task 0),
`boot_init_BB8D`, the SPC700 upload, NMI enable, and the spawn of task 1
which becomes the game-state dispatcher. After init, task 0 idles
forever and the NMI's cooperative scheduler runs the dispatcher.

```mermaid
flowchart TD
    RESET["RESET vector $00:8009<br/>SEI / CLD / CLC+XCE -> native<br/>DBR=0, D=0, SP=$03FF"]
    CLR["Clear WRAM $00:0000-$1FFF<br/>MDMAEN=0, HDMAEN=0"]
    MAIN9340["main_9340 (task #0)<br/>$00:9340"]
    TLIMIT["TASK_LIMIT = 2<br/>(one slot already reserved)"]
    SPAWN1["spawn_task pc=$935C a=$00<br/>$00:8113"]
    DP0100["D := $0100<br/>(task-0 DP differs from NMI's $0000)"]
    BOOT["boot_init_BB8D<br/>$00:BB8D"]
    INIDISP["INIDISP = $80 (force blank)"]
    PPUINIT["sub_BC7F<br/>PPU register triples at $01:98A3"]
    SHADOW["seed_persistent_shadow<br/>$7F:E710..E719"]
    SPC["spc700_upload_driver_088006<br/>uploads 3,327-byte driver from $08:0A00"]
    NMIEN["enable_nmi_896D<br/>NMITIMEN = $81<br/>(NMI + auto joypad)"]
    IDLE["task 0 spins: BRA $935A<br/>(NMI scheduler swaps it out)"]

    DISPATCH["task #1 body @ $935C<br/>game_state_dispatch loop"]
    GS["read dp[$0B] -> index 64-entry table @ $00:9369<br/>handler runs once, INC dp[$0B], next frame next state"]

    RESET --> CLR --> MAIN9340 --> TLIMIT --> SPAWN1 --> DP0100 --> BOOT
    BOOT --> INIDISP --> PPUINIT --> SHADOW --> SPC --> NMIEN --> IDLE
    SPAWN1 -. "task 1 stack page $04xx, PC=$935C" .-> DISPATCH
    DISPATCH --> GS
```

---

## 2. NMI handler flow

The NMI vector targets `$00:803E`. It is the entire render+scheduler
heartbeat: every frame it pushes shadow OAM via DMA, streams 1/8 of a
tileset to VRAM, flushes the queues, walks the entity table, ticks the
wall clock, then switches stacks to the next ready task and RTIs.

```mermaid
flowchart TD
    NMI["NMI vector -> $00:803E<br/>D := $0000 on entry"]
    ACK["RDNMI ack<br/>($4210)"]
    OAMDMA["OAM DMA<br/>$00:0D00 -> $2104 OAMDATA<br/>0x220 bytes via DMA0"]
    VSTREAM["vram_stream_step_814F<br/>jump table @ $815A indexed by dp[$88]<br/>1/8 of a tileset per frame"]
    SPLIT{"CUR_TASK & 1 == 0?"}
    EVEN["per_frame_even_8553<br/>guarded by dp[$57]==dp[$58]"]
    ODD["per_frame_odd_85B2<br/>guarded by dp[$5B] >= 4"]
    QFLUSH["vram_queue_flush_C804<br/>drain $00:0C00, terminator $FFFF"]
    CGRAM["cgram_queue_flush_8937<br/>palette triples @ $0F20"]
    SCROLL["bg_scroll_push_884A<br/>BG1/2/3 H+V from dp[$46-$51]"]
    OAMCLR["shadow_oam_clear_88A5<br/>park 128 sprites at Y=$E0<br/>reset OAM allocators dp[$32]=$0010, dp[$34]=$0110"]
    EWALK["JSL entity_step_all_049966<br/>walk $04:0600 until dp[$30]<br/>dispatch via 32-entry table @ $04:9A30"]
    PAUSE["pause_toggle_on_start_8101<br/>JOY1H bit 4 -> dp[$2A]=1<br/>gated by dp[$71] menu lockout"]
    CLOCK["wall clock tick dp[$00..$04]<br/>dp[$00]++ (also = CUR_TASK)<br/>60/60/60 cascade"]
    LONG["if dp[$00]==4: INC dp[$02B9..BA]<br/>(long-tick counter)"]
    SCHED["scheduler_switch_and_rti<br/>save SP to dp[$0A+2*CUR_TASK]<br/>find next ready task, swap SP, RTI"]

    NMI --> ACK --> OAMDMA --> VSTREAM --> SPLIT
    SPLIT -- yes --> EVEN
    SPLIT -- no --> ODD
    EVEN --> QFLUSH
    ODD --> QFLUSH
    QFLUSH --> CGRAM --> SCROLL --> OAMCLR --> EWALK --> PAUSE --> CLOCK --> LONG --> SCHED
```

---

## 3. Game-state dispatcher

`dp[$0B]` is the master game-state index into a 64-entry table at
`$00:9369`. Most handlers run **once** and `INC dp[$0B]` to advance. The
SELECT button branches into a family of view-switch states
($1D/$1F/$21/$23/$25/$27), the main menu lives at $16, and the save
flow lands back at $1A after the serializer.

```mermaid
flowchart TD
    DISP["task 1 loop @ $935C<br/>read dp[$0B], index $00:9369"]

    subgraph MENU["Menu / Transition states $00-$09"]
        S00["$00 GS_FULL_GAME<br/>$00:ACF3"]
        S01["$01 GS_SCENARIO_GAME<br/>$00:AD5B"]
        S02["$02 GS_SAVED_GAME<br/>$00:AC63"]
        S03["$03 GS_TUTORIAL<br/>$00:ACE8"]
        S04["$04 GS_ANT_INFORMATION<br/>$00:B155"]
        S05["$05 GS_MARRIAGE_FLIGHT<br/>$00:B18C"]
        S06["$06 GS_FULL_END<br/>$00:B07B"]
        S07["$07 GS_SCENARIO_END<br/>$00:B0FC"]
        S08["$08 GS_GAME_OVER<br/>$00:B19F"]
        S09["$09 GS_SOUND<br/>$00:B1BF"]
    end

    S16["$16 MAIN MENU<br/>$00:93F3<br/>spawns cursor type 2"]

    subgraph VIEW["View setup states (SELECT toggles)"]
        S1D["$1D Surface Overview setup<br/>$00:BC9C"]
        S1F["$1F B-Nest Overview setup<br/>$00:BFC8"]
        S21["$21 R-Nest Overview setup<br/>$00:C01A"]
        S23["$23 Surface Close-up<br/>$00:A7DD"]
        S25["$25 B-Nest Close-up<br/>$00:CCD0"]
        S27["$27 R-Nest Close-up<br/>$00:CCD0"]
    end

    S1B["$1B view-switch LANDING PAD<br/>sub_A3BD"]

    S1E["$1E B/Surf Overview run"]
    S20["$20 B-Nest Overview run"]
    S22["$22 R-Nest Overview run"]

    SAVE_ENTRY["save UI -> $00:959D<br/>STA #$1A -> dp[$0B]"]
    S1A["$1A AFTER-SAVE landing<br/>spawns sim task #4 via sub_9832"]

    DISP -- "dp[$0B] < $0A" --> MENU
    MENU --> S16
    DISP -- "dp[$0B] == $16" --> S16
    S16 --> S1D
    S1D --> S1E
    S1F --> S20
    S21 --> S22
    DISP -- SELECT toggle --> S1B
    S1B --> S1D
    S1B --> S1F
    S1B --> S21
    S1B --> S23
    S1B --> S25
    S1B --> S27
    SAVE_ENTRY --> S1A
    S1A --> S1B
```

---

## 4. Entity lifecycle

Entities are 20-byte records at `$04:0600`. `entity_spawn_99C1` finds a
free slot and copies per-type init constants from ROM tables. Every NMI
the walker at `entity_step_all_049966` dispatches each live entity to
its handler via the 32-entry table at `$04:9A30`; most handlers then
dispatch on byte +1 (state) through a per-type indirect JMP table.

```mermaid
flowchart TD
    SPAWN["entity_spawn_99C1<br/>X=pos_x, Y=pos_y, A=type"]
    SLOT["walk $04:0600.. for first slot with type==0<br/>or extend dp[$30]"]
    POP["populate:<br/>+0 type, +1 state=0,<br/>+2-3 X, +4-5 Y,<br/>+C-D from $01:EF59[type] (init_word),<br/>+F from $01:F043[type] (init_attr)"]

    NMITICK["NMI -> entity_step_all_049966<br/>JSL from bank-0 NMI<br/>DBR := $04"]
    WALK["for X = $0600; X < dp[$30]; X += 20"]
    LIVE{"wram[X+0] type<br/>!= 0?"}
    HANDLER["handler := *(uint16)*$9A30 + type*2$<br/>JSR (handler) with X = entity ptr"]
    STATE["typical handler prologue:<br/>TXY / LDA $0001,x<br/>ASL / TAX / JMP (state_table,pc)"]
    BODY["per-state body<br/>physics gate (D747 when CUR_TASK==4),<br/>click test (DC84),<br/>draw, advance state"]
    DEATH["state body zeros +0 (type)<br/>-> slot reclaimed next pass"]
    COMPACT["entity_table_compact_04999A<br/>walk back from dp[$30],<br/>shrink past trailing empties"]

    SPAWN --> SLOT --> POP
    POP -. "added to table" .-> NMITICK
    NMITICK --> WALK --> LIVE
    LIVE -- no --> WALK
    LIVE -- yes --> HANDLER --> STATE --> BODY
    BODY -. "iterate" .-> WALK
    BODY --> DEATH
    DEATH -. "next NMI" .-> COMPACT
```

---

## 5. Scent system

Four 2048-byte maps at `$7F:4000-$7F:5FFF` (64x32 cells, 32x32 px each).
Place uses MAX semantics, decay differs between nest (linear) and trail
(exponential halving), follow scans 8 compass neighbours and smooths via
an 8x8 table at `$02:AAC7`, rain (Scenario 3) weakens nest by $14 and
zeros trail entirely.

```mermaid
flowchart LR
    subgraph MAPS["$7F WRAM scent maps"]
        BN["$7F:4000 BLACK NEST<br/>2048 B"]
        RN["$7F:4800 RED NEST<br/>2048 B"]
        BT["$7F:5000 BLACK TRAIL<br/>2048 B"]
        RT["$7F:5800 RED TRAIL<br/>2048 B"]
    end

    subgraph PLACE["PLACE (MAX)"]
        PN1["$03:9389 -> Black Nest"]
        PN2["$03:93AD -> Red Nest"]
        PT1["$03:93D1 -> Black Trail"]
        PT2["$03:93F5 -> Red Trail"]
        SEED["seed pass $03:9269<br/>walks nest column tables<br/>$7F:E946 (B), $7F:E9C6 (R)<br/>tile=$51 -> 0, else $FF"]
    end

    subgraph DECAY["DECAY (per sim tick)"]
        DN["NEST: new = max(0, old-1)<br/>linear, 256 ticks from $FF"]
        DT["TRAIL: new = (old<8) ? 0 : old>>1<br/>~5 halvings to vanish"]
    end

    subgraph FOLLOW["FOLLOW gradient"]
        F1["sub_gradient_follow_02A710"]
        F2["read center cell<br/>0 -> no scent, wander"]
        F3["loop 8 dirs (dx,dy) @ $02:8065/8077<br/>track max + arg-max"]
        F4["smooth via 8x8 table @ $02:AAC7"]
        F5["entity.heading := smoothed"]
        F1 --> F2 --> F3 --> F4 --> F5
    end

    subgraph WASH["WASH (Scenario 3 Rainy Yard)"]
        W1["$02:96A0 per frame while rain"]
        W2["Black/Red Nest:  -= $14 (clamp 0)"]
        W3["Black/Red Trail: := 0"]
        W1 --> W2
        W1 --> W3
    end

    ANT["ant AI (entities_b.c walker)"] --> PLACE
    PLACE --> MAPS
    SEED --> MAPS
    MAPS --> DECAY --> MAPS
    MAPS --> FOLLOW --> ANT
    WASH --> MAPS
```

---

## 6. Combat flow

Two ant tables exist: the visual entity pool at `$7E:0600` and the
abstract per-colony parallel arrays at `$7F:C000..$7F:E328`. Active
fighters get pushed into the combatant pool at `$7F:E87E` (max 5
entries). `fight_resolver_96D7` ticks each combatant per sim_tick, and
all kill outcomes funnel through `kill_dispatcher_D334` which bumps
`E844` (B wins), `E848` (R wins), or `E84C` (draws).

```mermaid
flowchart TD
    BAB["B-colony array<br/>$7F:CBB8 (type) + $7F:C000 (x)<br/>count $7E:E77E"]
    RAB["R-colony array<br/>$7F:D964 + $7F:D388<br/>count $7E:E780"]
    DAB["Danger array<br/>$7F:E328 + $7F:DD4C<br/>count $7E:E782"]

    PROX["proximity detected by per-area scan"]
    APPEND["combatant_append_96B0<br/>$03:96B0<br/>push (X,Y) into pool slot i<br/>state=0, HP=0, frame=0"]

    POOL["COMBATANT POOL $7F:E87E<br/>up to 5 entries,<br/>(X@E882, Y@E88E, state@E89A,<br/> HP@E8A6, frame@E8B2) interleaved word"]

    RES["fight_resolver_96D7<br/>$03:96D7 (per sim tick)"]
    ST0["state 0 ACTIVE SCAN<br/>read self tile (A626)<br/>if own-team or enemy: maybe fight<br/>1/512 fight probability<br/>scan 8 neighbours @ $02:8065"]
    ST1["state 1 DECAY<br/>frame 0..3 advance, then DEC HP<br/>HP=25 Worker, HP=50 Soldier"]
    ST2["state 2 TERMINAL<br/>frame -> 0, clear state,<br/>INC E880 (engagements resolved)"]

    KDISP["kill_dispatcher_D334<br/>$03:D334<br/>dp[$02B5] := 1 (world-changed)<br/>jump table @ $03:D3C0 (10 codes)"]

    BWIN["B WINS -> INC $7E:E844 FIGHTS_B_WON<br/>codes 3,4,5,6,7,9"]
    RWIN["R WINS -> INC $7E:E848 FIGHTS_R_WON<br/>codes 1,2"]
    DRAW["DRAW   -> INC $7E:E84C FIGHTS_DRAW<br/>code 8"]
    EVT["queue_event_F65A<br/>(0x40..0x4D events: SFX, popups)"]
    CORPSE["corpse_spawn_C3E3<br/>codes 7-9 leave body sprite"]

    BAB --> PROX
    RAB --> PROX
    PROX --> APPEND --> POOL
    POOL --> RES
    RES --> ST0
    ST0 --> ST1
    ST1 --> ST2
    ST2 -. "back to slot scan" .-> POOL
    ST0 -. "kill" .-> KDISP
    ST1 -. "HP underflow" .-> KDISP
    KDISP --> BWIN
    KDISP --> RWIN
    KDISP --> DRAW
    KDISP --> EVT
    KDISP --> CORPSE
    DAB -. "predator kills" .-> KDISP
```

---

## 7. Simulation tick

Task #4 (spawned from gameplay state `$1A` via `sub_9832`) is the
simulation task. It calls `sim_tick` (`$02:AB58`) once and then waits
~7 NMIs (`dp[$B9] < 7`), giving ~8.5 Hz. Each call advances master
counter `$E788`, runs 11 always-subsystems, ant motion every 2 ticks,
the 4-way slow round-robin every 32 ticks, and colony health decay
every 64 ticks.

```mermaid
flowchart TD
    SPAWN["state $1A tail sub_9832<br/>TASK_LIMIT=4, spawn_task(pc=$02:8024)"]
    LOOP["sim_main_loop $02:8024<br/>for(;;)"]
    HALT{"dp[$E1] flag"}
    TICK["sim_tick $02:AB58"]
    COMMIT["if WORLD_MODIFIED dp[$02B7]:<br/>world_modify_commit_D792"]
    WAIT["spin until dp[$B9] >= 7<br/>(NMI tail increments dp[$B9])"]
    YIELD["cooperative_yield_877D"]

    INC["INC $7E:E788 SIM_COUNTER<br/>wrap >$1000<br/>++sim wall-clock E73E/E740"]
    RESET["zero per-tick cursors E912/E914/E922/E924/<br/>E91A/E91C/E90A/E90C = $FFFF"]
    SLOW64{"E788 & $3F == 0?"}
    H64["history_snapshot_ACC9<br/>colony health decay (E776/E778)<br/>danger trigger if E770<E746"]
    SLOW32{"E788 & $1F == 0?"}
    RR["round_robin_slow_ABEF<br/>phase = (E788 >> 5) & 3<br/>0: caste shuffle + history<br/>1: behavior adj + diffusion<br/>2: history snapshot<br/>3: visit decay + foraging"]

    ALWAYS["ALWAYS chain (11 subsystems):<br/>per_area_food_tick_E4DB<br/>pop_aggregator_956E<br/>fight_resolver_96D7<br/>starvation_tick_D89B"]

    ANT2{"E788 & 1 == 0?"}
    ANTMOT["ant_motion_update_9A86<br/>Yellow-Ant / Queen walker"]

    TAIL["TAIL chain:<br/>per_area_visit_tick_9D96<br/>cooldown_dec_AC41<br/>area_event_tick_ACF9<br/>breeder_movement_C6A9<br/>danger_event_tick_DD5F<br/>ant_lion_tick_C0FD<br/>summary build (E7AE..E7C4)<br/>colony_health_update_BC2E<br/>render_post_8000/80CA"]

    SPAWN --> LOOP --> HALT
    HALT -- "==0 run" --> TICK
    HALT -- "==2 halt" --> YIELD
    TICK --> COMMIT --> WAIT --> YIELD --> LOOP

    TICK --> INC --> RESET --> SLOW64
    SLOW64 -- yes --> H64 --> SLOW32
    SLOW64 -- no --> SLOW32
    SLOW32 -- yes --> RR --> ALWAYS
    SLOW32 -- no --> ALWAYS
    ALWAYS --> ANT2
    ANT2 -- yes --> ANTMOT --> TAIL
    ANT2 -- no --> TAIL
```

---

## 8. Save / load flow

Game-state $1A is the save landing pad. The save UI sets `dp[$0B]=$1A`
(at `$00:959D`), serializer entry compresses live WRAM into the staging
buffer at `$7E:6000`, writes either "DOBBY" (full game) or "DURRY"
(scenario summary) signature, copies to SRAM `$70:0000`, then writes a
16-bit checksum at `$70:0005` via `$03:FC3A`. Load mirrors it: signature
match -> decompress -> parallel-array restore.

```mermaid
flowchart TD
    UI["save UI choice<br/>state $1A entry $00:959D"]
    SET["STA #$1A -> dp[$0B]<br/>RTS to dispatcher"]

    SER["serializer $03:FA74<br/>compress live colony state via $03:8000 (LZSS)"]
    STAGE["staging buffer $7E:6000<br/>+0 sig 'DOBBY' or 'DURRY' (5 B)<br/>+5 checksum slot<br/>+8 compressed body"]
    SIGW1["save_signature_write_AA2E<br/>$03:FA44 writes DOBBY at $7E:6000<br/>(full game path)"]
    SIGW2["$03:FA5C writes DURRY at $7E:6000<br/>(scenario summary path)"]
    SRAM["copy staging -> SRAM $70:0000<br/>(also signature mirror at $70:7FA0)"]
    CK["save_checksum_03_FC3A<br/>16-bit running sum of words<br/>$70:0008..$7F9D -> $70:0005"]

    BOOT["boot path notices SRAM sig"]
    LOAD["load_game_03_FA74"]
    SIGR["read SRAM $70:0000 bytes 0..4<br/>compare to 'DOBBY' or 'DURRY'<br/>(signatures @ $03:F97E / $03:F983)"]
    OK{"signature ok?"}
    CKR["recompute checksum,<br/>compare $70:0005"]
    CKOK{"match?"}
    DEC["LZSS decompress -> WRAM"]
    RESTORE["parallel-array restore:<br/>colony summary $7E:E700+<br/>area map $7E:EA46/EAC6<br/>scent maps $7F:4000+<br/>parallel ant arrays $7F:Cxxx"]
    RUN["resume gameplay (state $1A)"]
    FAIL["fresh-game fallback"]

    UI --> SET --> SER --> STAGE
    STAGE --> SIGW1
    STAGE --> SIGW2
    SIGW1 --> SRAM
    SIGW2 --> SRAM
    SRAM --> CK

    BOOT --> LOAD --> SIGR --> OK
    OK -- no --> FAIL
    OK -- yes --> CKR --> CKOK
    CKOK -- no --> FAIL
    CKOK -- yes --> DEC --> RESTORE --> RUN
```

---

## 9. Yellow Ant lifecycle

The Yellow Ant is the player avatar — a *composite* across cursor
entities (types 1/2), the visual ant body (Worker type 14 or Queen
type 18), and the abstract walker tracker at `$7E:E8BE..E8C6`. Menu
actions (Recruit / Queen popups, type 29) and player attack flow
through `player_actions.c`. On death (hunger=0 or fight loss) the next
egg rebirth bumps the lives counter at `$7E:E8C4`.

```mermaid
stateDiagram-v2
    [*] --> Spawn
    Spawn: SPAWN<br/>egg hatch chooses Yellow Ant<br/>walker $7E:E8BE := 1<br/>tile coord set in E8C0/E8C2
    Spawn --> Walking

    Walking: WALKING<br/>cursor click sets target<br/>type 14 walking AI (entities_b.c)<br/>scent following + physics
    Walking --> Menu : click on Yellow Ant<br/>(surface close-up)
    Walking --> Fight : B-button on enemy
    Walking --> Eat : food cell collision
    Walking --> Starve : hunger E7B8 -> 0

    Menu: MENU (popup type 29)<br/>Worker role -> Recruit menu<br/>(Recruit 5/10/All, Release 1/2/All)<br/>Queen role -> Queen menu<br/>(Dig New Nest, Lay Eggs)
    Menu --> Recruit
    Menu --> Queen
    Menu --> Walking : close

    Recruit: RECRUIT ACTION<br/>set followers state=6<br/>(parallel array $7F:C7D0..)
    Recruit --> Walking

    Queen: QUEEN ACTION<br/>Dig New Nest -> spawn excavator type 20<br/>Lay Eggs -> spawn egg type 24<br/>bump EGGS_LAID $7E:E80E
    Queen --> Walking

    Fight: FIGHT<br/>combatant_append_96B0<br/>fight_resolver_96D7
    Fight --> Walking : survive
    Fight --> Death : kill_dispatcher D334

    Eat: EAT<br/>trophallaxis or direct feed<br/>FEED_HUNGER E7D2 raised
    Eat --> Walking

    Starve --> Death

    Death: DEATH<br/>walker E8BE := 0<br/>INC kill counter
    Death --> NextEgg : E8C4 lives > 0
    Death --> [*]      : lives exhausted -> game over

    NextEgg: NEXT-EGG REBIRTH<br/>find next hatched egg in pool<br/>E8C4-- (lives counter)<br/>walker E8BE := 1
    NextEgg --> Walking
```

---

## 10. Audio command path

Game code never talks to the SPC700 directly. It calls one of two
front doors: `apu_send_music_8E88` (writes `dp[$37]` to APUIO0 if music
enabled `dp[$33]`) or `apu_play_sfx_8EA3` (writes `dp[$3A]` to APUIO3
with an alternating bit `dp[$3E]&1` so the SPC700 always sees a state
change, gated by `dp[$36]`). The SPC700 driver polls APUIO0..3 every
4 ms tick at `$0690`, dispatches the byte, and routes music bytes
through the song-event interpreter at `$099B`.

```mermaid
sequenceDiagram
    autonumber
    participant GAME as 65816 game code
    participant MUSIC as $00:8E88 music dispatcher
    participant SFX as $00:8EA3 SFX dispatcher
    participant APU as APUIO0..3 ($2140-$2143)
    participant SPC as SPC700 driver (ARAM)
    participant POLL as $0690 poll_all_cpuios
    participant SONG as $099B song_event_dispatch

    GAME->>MUSIC: JSL $00:8E88 cmd
    Note over MUSIC: if dp[$33] enable set:<br/>dp[$37] := cmd<br/>APUIO0 := dp[$37]
    MUSIC->>APU: APUIO0 <- music cmd

    GAME->>SFX: JSL $00:8EA3 cmd
    Note over SFX: if dp[$36] enable set:<br/>dp[$3A] := cmd<br/>APUIO3 := (dp[$3E]&1) | dp[$3A]<br/>(alternation bit forces edge)
    SFX->>APU: APUIO3 <- SFX cmd

    loop every ~4ms timer tick
        SPC->>POLL: CALL $0690
        POLL->>APU: read APUIO0/1/2/3
        alt APUIO0 changed (music)
            POLL->>SPC: dispatch $06CC<br/>look up song-start table $12FE+Y<br/>set song pointer in $0C/$0D
        else APUIO3 changed (SFX)
            POLL->>SPC: trigger SFX channel
        end
    end

    loop song-tick (per-channel cadence)
        SPC->>SONG: CALL $099B with channel ptr
        SONG->>SPC: read next sequence byte<br/>jump table $09D5 by class<br/>(note / rest / loop / set-instr / etc.)
        SONG->>SPC: pitch lookup $09FF<br/>envelope_tick_helper $0D41
    end
```

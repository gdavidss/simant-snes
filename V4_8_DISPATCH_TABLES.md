# V4-8 — Dispatch Table Re-Verification

Re-decoded directly from `simant.sfc` (LoROM, map_mode=$20).

- Game-state table  $00:9369 (file 0x01369), 68 × 2 bytes
- Entity-handler table $04:9A30 (file 0x21A30), 118 × 2 bytes

> **Z1 addendum (2026-05-22):** §4 below ("Unlifted game states") and
> §5 below ("Unlifted entity handlers") are **stale**. The post-V4
> fix rounds lifted most of those entries:
>
> - **F4** lifted all 15 game states $30-$3E into `states_late.c`.
>   Game-state table is now **68 / 68 (100 %)**.
> - **G2** lifted entity types $20-$2B into `entities_e.c` (41 state
>   bodies across 13 dispatchers).
> - **G3** lifted entity types $2C-$5F dispatchers into `entities_f.c`
>   (24 dispatcher state bodies; H2-verified).
> - **G4** lifted entity types $60-$71 into `entities_g.c` (28 per-state
>   bodies across 14 multi-state handlers).
> - **H1** lifted dialog renderers $2D / $2E / $2F.
> - Entity table is now **~110 / 118 (~93 %)** with bodies; the
>   remaining ~8 stubs include $3D (bicycle danger) and $4B (hand /
>   cat-paw danger).
>
> The raw addresses in the tables below are still correct — only the
> "UNLIFTED" column needs to be read with the addendum in mind.

## 1. Game state dispatch table ($00:9369) — 68 entries

| idx | addr   | C function                                | file |
|-----|--------|-------------------------------------------|------|
| $00 | $ACF3 | gs_full_game_ACF3                         | states_menu.c |
| $01 | $AD5B | gs_scenario_game_AD5B                     | states_menu.c |
| $02 | $AC63 | gs_saved_game_AC63                        | states_menu.c |
| $03 | $ACE8 | gs_tutorial_ACE8                          | states_menu.c |
| $04 | $B155 | gs_ant_information_B155                   | states_menu.c |
| $05 | $B18C | gs_marriage_flight_B18C                   | states_menu.c |
| $06 | $B07B | gs_full_end_B07B                          | states_menu.c |
| $07 | $B0FC | gs_scenario_end_B0FC                      | states_menu.c |
| $08 | $B19F | gs_game_over_B19F                         | states_menu.c |
| $09 | $B1BF | gs_sound_B1BF                             | states_menu.c |
| $0A | $B21A | state_0A_credits_continue_B21A            | states_gameplay.c |
| $0B | $B281 | state_0B_scenario_end_celebration_B281    | states_gameplay.c |
| $0C | $AEAD | state_0C_saved_game_screen_AEAD           | states_gameplay.c |
| $0D | $AF3F | state_0D_saved_game_scroll_in_AF3F        | states_gameplay.c |
| $0E | $B2B0 | state_0E_marriage_flight_setup_B2B0       | states_gameplay.c |
| $0F | $B352 | state_0F_marriage_flight_animate_B352     | states_gameplay.c |
| $10 | $B47C | state_10_ant_info_left_B47C               | states_gameplay.c |
| $11 | $B490 | state_11_ant_info_text_B490               | states_gameplay.c |
| $12 | $B3D8 | state_12_map_overlay_B3D8                 | states_gameplay.c |
| $13 | $B45D | state_13_map_scroll_B45D                  | states_gameplay.c |
| $14 | $B4BA | state_14_bug_cutin_B4BA                   | states_gameplay.c |
| $15 | $B4DA | state_15_bug_cutin_caption_B4DA           | states_gameplay.c |
| $16 | $93F3 | state_16_title_input_93F3                 | states_gameplay.c |
| $17 | $D57E | state_17_save_picker_D57E                 | states_gameplay.c |
| $18 | $D68A | state_18_save_picker_navigate_D68A        | states_gameplay.c |
| $19 | $96B1 | state_19_save_commit_choice_96B1          | states_gameplay.c |
| $1A | $96DF | state_1A_save_load_world_96DF             | states_gameplay.c |
| $1B | $C12F | state_1B_view_switch_landing_C12F         | states_gameplay.c |
| $1C | $9850 | state_1C_post_view_switch_9850            | states_gameplay.c |
| $1D | $BC9C | state_view_surface_overview_BC9C          | states_gameplay.c |
| $1E | $98D5 | state_view_surface_overview_run_98D5      | states_gameplay.c |
| $1F | $BFC8 | state_view_bnest_overview_BFC8            | states_gameplay.c |
| $20 | $9A14 | state_view_bnest_overview_run_9A14        | states_gameplay.c |
| $21 | $C01A | state_view_rnest_overview_C01A            | states_gameplay.c |
| $22 | $9B7D | state_view_rnest_overview_run_9B7D        | states_gameplay.c |
| $23 | $A7DD | state_view_surface_closeup_A7DD           | states_gameplay.c |
| $24 | $CA96 | state_view_nest_closeup_setup_CA96        | states_gameplay.c |
| $25 | $CCD0 | state_view_nest_closeup_run_CCD0          | states_gameplay.c |
| $26 | $CA96 | state_view_nest_closeup_setup_CA96        | states_gameplay.c |
| $27 | $CCD0 | state_view_nest_closeup_run_CCD0          | states_gameplay.c |
| $28 | $D7CE | state_28_save_picker_ui_D7CE              | states_gameplay.c |
| $29 | $D943 | state_29_save_run_D943                    | states_gameplay.c |
| $2A | $D256 | state_2A_sound_options_setup_D256         | states_gameplay.c |
| $2B | $D35A | state_2B_sound_options_input_D35A         | states_gameplay.c |
| $2C | $D09E | state_2C_scent_display_setup_D09E         | states_gameplay.c |
| $2D | $D24C | state_2D_scent_display_exit_D24C          | states_gameplay.c |
| $2E | $A3EC | state_2E_landing_pick_setup_A3EC          | states_gameplay.c |
| $2F | $A4DE | state_2F_landing_pick_input_A4DE          | states_gameplay.c |
| $30 | $AF9A | — UNLIFTED —                              |  |
| $31 | $B060 | — UNLIFTED —                              |  |
| $32 | $AD6A | — UNLIFTED —                              |  |
| $33 | $AE33 | — UNLIFTED —                              |  |
| $34 | $B36D | — UNLIFTED —                              |  |
| $35 | $B3CB | — UNLIFTED —                              |  |
| $36 | $B535 | — UNLIFTED —                              |  |
| $37 | $B5F7 | — UNLIFTED —                              |  |
| $38 | $B612 | — UNLIFTED —                              |  |
| $39 | $B695 | — UNLIFTED —                              |  |
| $3A | $B6B0 | — UNLIFTED —                              |  |
| $3B | $B6CC | — UNLIFTED —                              |  |
| $3C | $B6E9 | — UNLIFTED —                              |  |
| $3D | $B72E | — UNLIFTED —                              |  |
| $3E | $B7AC | — UNLIFTED —                              |  |
| $3F | $B833 | state_3F_winter_ending_B833               | audio_intro.c |
| $40 | $B875 | state_40_credits_setup_B875               | audio_intro.c |
| $41 | $B8B9 | state_41_credits_pageloop_B8B9            | audio_intro.c |
| $42 | $B996 | state_42_credits_wrapup_B996              | audio_intro.c |
| $43 | $BA4D | state_43_post_credits_BA4D                | audio_intro.c |

## 2. Entity handler dispatch table ($04:9A30) — 118 entries

| idx | addr   | C function                                | file |
|-----|--------|-------------------------------------------|------|
| $00 | $9B1A | — UNLIFTED —                              |  |
| $01 | $9D9D | cursor_handler_type1_9D9D                 | entities_a.c |
| $02 | $9B9B | cursor_handler_type2_9B9B                 | entities_a.c |
| $03 | $9B1B | marker_handler_type3_9B1B                 | entities_a.c |
| $04 | $9B30 | marker_handler_type4_9B30                 | entities_a.c |
| $05 | $9B41 | marker_handler_type5_9B41                 | entities_a.c |
| $06 | $9C46 | timed_indicator_type6_9C46                | entities_a.c |
| $07 | $9CC6 | prop_handler_type7_9CC6                   | entities_a.c |
| $08 | $9CF0 | type8_dispatch_9CF0                       | entities_b.c |
| $09 | $9E3F | type9_dispatch_9E3F                       | entities_b.c |
| $0A | $9E9C | type10_dispatch_9E9C                      | entities_b.c |
| $0B | $9F1D | type11_dispatch_9F1D                      | entities_b.c |
| $0C | $9F7A | type12_dispatch_9F7A                      | entities_b.c |
| $0D | $9FE0 | type13_dispatch_9FE0                      | entities_b.c |
| $0E | $A112 | type14_dispatch_A112                      | entities_b.c |
| $0F | $A222 | type15_dispatch_A222                      | entities_b.c |
| $10 | $A356 | type16_handler_A356                       | entities_c.c |
| $11 | $A43B | type17_handler_A43B                       | entities_c.c |
| $12 | $A533 | queen_handler_A533                        | entities_c.c |
| $13 | $A533 | queen_handler_A533                        | entities_c.c |
| $14 | $A6C5 | type20_handler_A6C5                       | entities_c.c |
| $15 | $A356 | type16_handler_A356                       | entities_c.c |
| $16 | $9E9C | type10_dispatch_9E9C                      | entities_b.c |
| $17 | $A8D9 | type23_handler_A8D9                       | entities_c.c |
| $18 | $A951 | type24_dispatch_A951                      | entities_d.c |
| $19 | $A9A1 | type25_dispatch_A9A1                      | entities_d.c |
| $1A | $AB0B | type26_status_panel_AB0B                  | entities_d.c |
| $1B | $AB5B | type27_dispatch_AB5B                      | entities_d.c |
| $1C | $AC3A | type28_dispatch_AC3A                      | entities_d.c |
| $1D | $AD01 | type29_dispatch_AD01                      | entities_d.c |
| $1E | $B17F | type30_dispatch_B17F                      | entities_d.c |
| $1F | $B547 | type31_cursor_hint_B547                   | entities_d.c |
| $20 | $B597 | — UNLIFTED —                              |  |
| $21 | $B68D | — UNLIFTED —                              |  |
| $22 | $B6DD | — UNLIFTED —                              |  |
| $23 | $B72D | — UNLIFTED —                              |  |
| $24 | $B77D | — UNLIFTED —                              |  |
| $25 | $B7C1 | — UNLIFTED —                              |  |
| $26 | $B7FF | — UNLIFTED —                              |  |
| $27 | $9DD5 | — UNLIFTED —                              |  |
| $28 | $9DEA | — UNLIFTED —                              |  |
| $29 | $9DFF | — UNLIFTED —                              |  |
| $2A | $9E14 | — UNLIFTED —                              |  |
| $2B | $9E29 | — UNLIFTED —                              |  |
| $2C | $B673 | — UNLIFTED —                              |  |
| $2D | $B90A | — UNLIFTED —                              |  |
| $2E | $B991 | — UNLIFTED —                              |  |
| $2F | $BA84 | — UNLIFTED —                              |  |
| $30 | $BAD4 | — UNLIFTED —                              |  |
| $31 | $BB4F | — UNLIFTED —                              |  |
| $32 | $BB74 | — UNLIFTED —                              |  |
| $33 | $BBB9 | — UNLIFTED —                              |  |
| $34 | $BC07 | — UNLIFTED —                              |  |
| $35 | $BD9B | house_screen_render_04_BD9B               | ui_menus.c |
| $36 | $BE49 | — UNLIFTED —                              |  |
| $37 | $BEEE | — UNLIFTED —                              |  |
| $38 | $BF37 | — UNLIFTED —                              |  |
| $39 | $BFB0 | — UNLIFTED —                              |  |
| $3A | $C02B | — UNLIFTED —                              |  |
| $3B | $C247 | — UNLIFTED —                              |  |
| $3C | $C300 | — UNLIFTED —                              |  |
| $3D | $C36E | — UNLIFTED —                              |  |
| $3E | $C48F | — UNLIFTED —                              |  |
| $3F | $C5C8 | — UNLIFTED —                              |  |
| $40 | $C5D7 | — UNLIFTED —                              |  |
| $41 | $C4C4 | — UNLIFTED —                              |  |
| $42 | $C599 | — UNLIFTED —                              |  |
| $43 | $C61D | — UNLIFTED —                              |  |
| $44 | $BC49 | — UNLIFTED —                              |  |
| $45 | $BC8A | — UNLIFTED —                              |  |
| $46 | $BFC6 | — UNLIFTED —                              |  |
| $47 | $C013 | — UNLIFTED —                              |  |
| $48 | $B411 | — UNLIFTED —                              |  |
| $49 | $B358 | — UNLIFTED —                              |  |
| $4A | $B3C4 | — UNLIFTED —                              |  |
| $4B | $C653 | — UNLIFTED —                              |  |
| $4C | $C73B | — UNLIFTED —                              |  |
| $4D | $C8A7 | — UNLIFTED —                              |  |
| $4E | $C91B | — UNLIFTED —                              |  |
| $4F | $C92C | — UNLIFTED —                              |  |
| $50 | $C958 | — UNLIFTED —                              |  |
| $51 | $C984 | — UNLIFTED —                              |  |
| $52 | $C9C6 | — UNLIFTED —                              |  |
| $53 | $CA08 | — UNLIFTED —                              |  |
| $54 | $CA4C | — UNLIFTED —                              |  |
| $55 | $CA93 | — UNLIFTED —                              |  |
| $56 | $CAC3 | — UNLIFTED —                              |  |
| $57 | $CB65 | — UNLIFTED —                              |  |
| $58 | $CC73 | — UNLIFTED —                              |  |
| $59 | $CB73 | — UNLIFTED —                              |  |
| $5A | $CD5B | — UNLIFTED —                              |  |
| $5B | $CE0A | — UNLIFTED —                              |  |
| $5C | $CEB9 | — UNLIFTED —                              |  |
| $5D | $CF70 | — UNLIFTED —                              |  |
| $5E | $D025 | — UNLIFTED —                              |  |
| $5F | $D08F | — UNLIFTED —                              |  |
| $60 | $C7DD | — UNLIFTED —                              |  |
| $61 | $C842 | — UNLIFTED —                              |  |
| $62 | $CB5C | — UNLIFTED —                              |  |
| $63 | $AA41 | — UNLIFTED —                              |  |
| $64 | $CB6E | — UNLIFTED —                              |  |
| $65 | $B622 | — UNLIFTED —                              |  |
| $66 | $BD4E | — UNLIFTED —                              |  |
| $67 | $BCCC | — UNLIFTED —                              |  |
| $68 | $D16F | — UNLIFTED —                              |  |
| $69 | $D19B | — UNLIFTED —                              |  |
| $6A | $D22D | — UNLIFTED —                              |  |
| $6B | $D259 | — UNLIFTED —                              |  |
| $6C | $D2D7 | — UNLIFTED —                              |  |
| $6D | $D38B | — UNLIFTED —                              |  |
| $6E | $D3F1 | — UNLIFTED —                              |  |
| $6F | $D4B8 | — UNLIFTED —                              |  |
| $70 | $D580 | — UNLIFTED —                              |  |
| $71 | $D62F | — UNLIFTED —                              |  |
| $72 | $D6DF | — UNLIFTED —                              |  |
| $73 | $B5F8 | — UNLIFTED —                              |  |
| $74 | $CCEE | — UNLIFTED —                              |  |
| $75 | $A560 | — UNLIFTED —                              |  |

## 3. Aliases (handlers shared by multiple table indices)

### Game-state aliases
| addr   | states sharing it           | meaning |
|--------|------------------------------|---------|
| $CA96  | $24, $26                     | shared setup (Behavior $24 / Caste $26 — branches on dp[$0B]) |
| $CCD0  | $25, $27                     | shared run loop (Behavior $25 / Caste $27 — branches on dp[$0B]) |

### Entity-handler aliases
| addr   | types sharing it             | meaning |
|--------|------------------------------|---------|
| $9E9C  | $0A, $16                     | type10_dispatch (Type $16 = Type $0A alias) |
| $A356  | $10, $15                     | type16_handler — Type $15 (Type 21) = alias of Type $10 |
| $A533  | $12, $13                     | queen_handler (Type $13 reuses Queen dispatcher — confirms scenarios.c snail-uses-queen-AI claim) |

## 4. Unlifted game states (STALE — see Z1 addendum at top)

Total at V4-8 time: 15 / 68 (22%) unlifted.
**Post-F4: 0 / 68 unlifted** — all entries below lifted into `states_late.c`.

| idx | addr   | notes |
|-----|--------|-------|
| $30 | $AF9A |  |
| $31 | $B060 |  |
| $32 | $AD6A |  |
| $33 | $AE33 |  |
| $34 | $B36D |  |
| $35 | $B3CB |  |
| $36 | $B535 |  |
| $37 | $B5F7 |  |
| $38 | $B612 |  |
| $39 | $B695 |  |
| $3A | $B6B0 |  |
| $3B | $B6CC |  |
| $3C | $B6E9 |  |
| $3D | $B72E |  |
| $3E | $B7AC |  |

## 5. Unlifted entity handlers (STALE — see Z1 addendum at top)

Total at V4-8 time: 86 / 118 (73%) unlifted.
**Post-F/G/H: ~8 / 118 unlifted.** Entries $20-$2B lifted by G2,
$2C-$5F dispatchers by G3, $60-$71 by G4, $2D/$2E/$2F dialog renderers
by H1. Remaining stubs are bicycle $3D, hand/cat-paw $4B, and a few
HUD widgets.

| idx | addr   | notes |
|-----|--------|-------|
| $00 | $9B1A |  |
| $20 | $B597 | mentioned in gaps.c, no body |
| $21 | $B68D | mentioned in gaps.c, no body |
| $22 | $B6DD |  |
| $23 | $B72D |  |
| $24 | $B77D |  |
| $25 | $B7C1 |  |
| $26 | $B7FF |  |
| $27 | $9DD5 | control_panels.c describes as Auto/Manual icon T1 — Behavior side (no body) |
| $28 | $9DEA | control_panels.c describes as Auto/Manual icon T2 — Caste side (no body) |
| $29 | $9DFF | control_panels.c describes as Auto/Manual icon T3 — inverted (no body) |
| $2A | $9E14 | control_panels.c describes as Auto/Manual icon T4 — Caste inverted (no body) |
| $2B | $9E29 |  |
| $2C | $B673 |  |
| $2D | $B90A |  |
| $2E | $B991 |  |
| $2F | $BA84 |  |
| $30 | $BAD4 |  |
| $31 | $BB4F |  |
| $32 | $BB74 |  |
| $33 | $BBB9 |  |
| $34 | $BC07 |  |
| $36 | $BE49 |  |
| $37 | $BEEE |  |
| $38 | $BF37 |  |
| $39 | $BFB0 |  |
| $3A | $C02B |  |
| $3B | $C247 |  |
| $3C | $C300 |  |
| $3D | $C36E | documented in gaps.c as BICYCLE handler — referenced by scenarios.c danger_bicycles_spawn; no body |
| $3E | $C48F |  |
| $3F | $C5C8 |  |
| $40 | $C5D7 |  |
| $41 | $C4C4 |  |
| $42 | $C599 |  |
| $43 | $C61D |  |
| $44 | $BC49 |  |
| $45 | $BC8A |  |
| $46 | $BFC6 |  |
| $47 | $C013 |  |
| $48 | $B411 |  |
| $49 | $B358 |  |
| $4A | $B3C4 |  |
| $4B | $C653 | documented in gaps.c as HAND / CAT-PAW handler — referenced by scenarios.c danger_cat_paws_spawn; no body |
| $4C | $C73B |  |
| $4D | $C8A7 |  |
| $4E | $C91B |  |
| $4F | $C92C |  |
| $50 | $C958 |  |
| $51 | $C984 |  |
| $52 | $C9C6 |  |
| $53 | $CA08 |  |
| $54 | $CA4C |  |
| $55 | $CA93 |  |
| $56 | $CAC3 |  |
| $57 | $CB65 |  |
| $58 | $CC73 |  |
| $59 | $CB73 |  |
| $5A | $CD5B |  |
| $5B | $CE0A |  |
| $5C | $CEB9 |  |
| $5D | $CF70 |  |
| $5E | $D025 |  |
| $5F | $D08F |  |
| $60 | $C7DD |  |
| $61 | $C842 |  |
| $62 | $CB5C |  |
| $63 | $AA41 |  |
| $64 | $CB6E |  |
| $65 | $B622 |  |
| $66 | $BD4E |  |
| $67 | $BCCC |  |
| $68 | $D16F |  |
| $69 | $D19B |  |
| $6A | $D22D |  |
| $6B | $D259 |  |
| $6C | $D2D7 |  |
| $6D | $D38B |  |
| $6E | $D3F1 |  |
| $6F | $D4B8 |  |
| $70 | $D580 |  |
| $71 | $D62F |  |
| $72 | $D6DF |  |
| $73 | $B5F8 |  |
| $74 | $CCEE |  |
| $75 | $A560 |  |

## 6. V2-D nest close-up claim — REFUTED

V2-D claimed states $24-$27 are *nest close-up views* (B.NEST setup / B.NEST run / R.NEST setup / R.NEST run) and **not** the Behavior/Caste control panels.

### Evidence

- The dispatch table aliases setup ($CA96) across $24+$26 and aliases run ($CCD0) across $25+$27. **states_gameplay.c labels them B.NEST/R.NEST**; **control_panels.c labels them Behavior/Caste**. They cannot both be right.
- Raw disassembly of $00:CA96 (file 0x4A96) shows:
  - `JSR $C318`, then `LDA #$02; STA $23`, `JSR $8D94`, `LDA #$B0; STA $19` (asset/screen setup that control_panels.c attributes to "$00:C28A panel setup helper")
  - Then a long chain of `JSL $0499C1` that spawns entity types **$27, $29, $2B** at (0x24,0x2C/0x3C/0x54) — these are the **Auto/Manual icons** documented in control_panels.c lines 36-39 ($04:9DD5/9DEA/9DFF/9E14), NOT nest ants.
  - Then types **$24, $25, $26** are spawned — these are the numeric-digit / icon entities used to render the percentage readouts on the panels (gaps.c entity table shows $24=$B77D, $25=$B7C1, $26=$B7FF, all small UI prop handlers in the $B5..$B7 range — consistent with HUD icons, not ant sprites).
  - A branch on `LDA $0B; CMP #$24; BNE ...` then loads a second template variant — exactly the Behavior-vs-Caste split documented in control_panels.c.
- $00:CCD0 starts `JSR $877D; LDA $002A; BEQ +3; JSR $A0D2; STZ $02E3; JSR $DF79; BCC +5; JSR $A3D6; ...`. Then `LDA $0B; CMP #$25; BEQ +7; CMP #$27; BEQ +3; JMP $CDAE` — i.e. `$25` and `$27` are the alive states.
- The asset chain (`LDA #$07; LDY #$B380` vs `#$B671`, BG `LDA #$16; LDY #$F76C`, sprite `#$07; #$B975`, and per-branch labels `#$FEEC + #$8F2F` vs `#$A03E + #$B1E9`, then APU cmd $0C vs $0E) is consistent with the **two-panel UI layout** (one panel per command, both reusing the same chrome + different labels), and inconsistent with two nest views which would need entirely different BG/sprite asset banks.

### Verdict
**States $24/$25 = Behavior Control Panel** (setup / run).
**States $26/$27 = Caste Control Panel** (setup / run).
control_panels.c's interpretation is correct. states_gameplay.c's "B.NEST/R.NEST CLOSE-UP" labeling for the same addresses is incorrect and should be corrected (the file has functioning code but a misleading comment block).

## 7. Danger entity mapping — VERIFIED (with one nit)

Agent J's mapping checked against raw ROM bytes of each `JSL $0499C1` spawn call in `$00:BEDA..$00:BF5E`:

| danger      | ROM addr   | spawn list (verified)               | matches J? |
|-------------|------------|-------------------------------------|------------|
| rain        | $00:BEDA   | 1× $10  + 3× $0F                   | YES — $0F=raindrop, $10=puddle |
| cat / hands | $00:BEF3   | 3× $17 + 2× $4B @ (0x3E,0x2A)      | YES — $17=spider/cat-paw, $4B=hand |
| bicycles    | $00:BF2D   | 1× $3D + **5× $1C + 2× $1B**       | mostly — scenarios.c says 6× $1C but ROM has 5 |
| feet        | $00:BF33   | 5× $1C + 2× $1B                    | YES — $1B=left, $1C=right |
| snails      | $00:BF5E   | 3× $13                             | YES — $13 = $A533 (queen handler, alias) |

Notes:
- Type $13 dispatches through `queen_handler_A533` (entities_c.c) — this confirms scenarios.c's comment that "snails share the Queen dispatcher" and explains their slow walk cycle (queen_state2_walk_A5B6 with default decay $3C).
- Type $4B (hand / cat-paw) handler at $04:C653 is documented in gaps.c but **has no body lifted** — listed in the unlifted entity table above.
- Type $3D (bicycle) handler at $04:C36E is documented in gaps.c but **has no body lifted** — also unlifted.
- The "rain" and "feet/mowers" mapping (types $0F/$10 and $1B/$1C) lines up exactly with the entity-table addresses ($9E3F type9 / $A356 type16 for rain; $AB5B type27 / $AC3A type28 for feet — these are LIFTED in entities_b.c and entities_d.c).

---

_Last updated post-Z1 (audit round, 2026-05-22). Z1 addendum at top
flags §4 and §5 as superseded by F/G/H lift rounds; raw addresses
remain accurate._

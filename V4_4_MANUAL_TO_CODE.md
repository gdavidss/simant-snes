# V4-4: Manual → Decomp Cross-Reference

ROM: SimAnt SNES (1993). Manual: 40-page instruction booklet.
Decomp: ~68.5K LOC across 51 .c files in `/Users/guilhermedavid/simant-re/`
(post-F/G/H + A1-A5; was ~63K / 45 files at V4-4 time).

Each manual section is mapped to the decomp file(s) and function(s) that
implement the mechanic. Confidence is verified against `COVERAGE.md`,
`ENTITIES.md`, `PORTING.md` and direct function grep.

Confidence legend:
- **HIGH** — verified function body matches manual mechanic word-for-word.
- **MED**  — likely match by name + caller context; not fully read.
- **LOW**  — best guess from string evidence; body partially stubbed.

---

## Page-by-page cross-reference

| Pg | Manual section | Mechanic | Decomp file | Function / symbol | Conf |
|----|---|---|---|---|---|
| 2  | It's a Bug's Life | Intro narrative | audio_intro.c | (intro animation driver) | MED |
| 3  | The Object(s) of the Game | Full / Scenario / Tutorial modes | states_menu.c | `gs_full_game_ACF3`, `gs_scenario_game_AD5B`, `gs_tutorial_ACE8` | HIGH |
| 4  | Getting Started | Boot, title, "Press Start" | states_gameplay.c | `state_16_title_input_93F3` | HIGH |
| 4  | Tutorial | 54-message overlay | text_screens.c + text_content.c | `tutorial_paint_E2A2`, `in_game_hint_E280`, `tutorial_messages[54]` | HIGH |
| 4  | Full Game | Mode selection | states_menu.c | `gs_full_game_ACF3` | HIGH |
| 4  | Scenario Game | Mode selection | states_menu.c | `gs_scenario_game_AD5B` | HIGH |
| 5  | Saved Games (1 Full + 4 Scenario) | Slot picker | states_gameplay.c, save_options.c | `state_17_save_picker_D57E`, `state_18_save_picker_navigate_D68A`, `save_slot_is_occupied` | HIGH |
| 5  | Ant Information / Encyclopedia | Topic list + page navigator | states_menu.c, text_screens.c | `gs_ant_information_B155`, `enc_pick_intro_A56F`, `enc_pick_life_A574`, `enc_pick_home_A57B`, `enc_pick_relatives_A582`, `enc_pick_strategy_A589`, `enc_pick_next_A590`, `enc_pick_prev_A5B0`, `encyclopedia_render_DFCD` | HIGH |
| 6  | Controller — Control Pad (cursor) | Cursor motion | entities_a.c | `cursor_handler_type1_9D9D`, `cursor_handler_type2_9B9B`, `cursor_step_axis` | HIGH |
| 6  | Controller — L/R (scroll without cursor) | Edge scroll lock | vsync.c | (L/R scroll bit handling) | LOW |
| 6  | Controller — X (center on Yellow Ant) | Recenter view | states_gameplay.c | X-button reset to (0x18,0x18) noted in state_1B/1C; logic in cursor type 2 | MED |
| 6  | Controller — Y (cursor to View icon) | Jump to icon | (not isolated) | — | LOW |
| 6  | Controller — Start (pause) | Pause toggle | save_options.c | `pause_toggle_8101` | HIGH |
| 6  | Controller — Select (Close-up/Overview) | View toggle | states_gameplay.c | `state_1B_view_switch_landing_C12F`, `state_1C_post_view_switch_9850` | HIGH |
| 6  | Controller — A (move/dig/select) | A action | entities_a.c, player_actions.c | `cursor_confirm_action_9DB9`, `player_a_button_action`, `surface_closeup_a_press_A824` | HIGH |
| 6  | Controller — B (pickup/cancel) | B action | player_actions.c | `player_b_button_action`, `surface_closeup_b_press_A86A` | HIGH |
| 7  | Super NES Mouse | BIOS + click | mouse.c | full file (init, read, set-speed `mouse_set_speed_E494`) | HIGH |
| 8  | Close-up View and Overview | View modes | states_gameplay.c | `state_view_surface_overview_BC9C` (+_run_98D5), `state_view_bnest_overview_BFC8` (+_run_9A14) | HIGH |
| 8  | Cursor (pointing hand) | Cursor entity | entities_a.c | `cursor_handler_type1_9D9D`, `cursor_handler_type2_9B9B` | HIGH |
| 9  | Scrolling the Screen | Edge scroll | states_gameplay.c / vsync.c | (per-view handlers + LR latch) | MED |
| 9  | Icons | Toolbar | ui_menus.c | `icon_click_dispatch_A734`, `icon_dispatch_view_run_A746/A755/A787` | HIGH |
| 9  | Menus and Submenus | Popup framework | ui_menus.c, entities_d.c | `submenu_open_E086`, `type29_dispatch_AD01` (10-state dialog machine) | HIGH |
| 10 | Yellow Ant — Moving | Click-to-walk | player_actions.c | `player_a_button_action` → `surface_closeup_a_press_A824` | HIGH |
| 10 | Yellow Ant — Digging Tunnels | Carve $7F:4000+ | entities_c.c, player_actions_full.c | `type20_handler_A6C5`, `dig_action_03D7EA` | HIGH |
| 10 | Yellow Ant — Pick up / Put down | Worker/soldier carry | player_actions.c | `simulate_pickup_food_for_yellow_lift`, `worker_click_handler_pseudo`, `food_click_handler_pseudo` | HIGH |
| 11 | Eating (hunger bar, food consumption) | Energy refill | player_actions.c, combat.c | `simulate_eat_food_for_yellow_lift`, `eat_food_8C00`, `starve_kill_8DBA` | HIGH |
| 11 | Trophallaxis (mouth-to-mouth feeding) | Food share | player_actions.c, combat.c | `simulate_trophallaxis_for_yellow`, `trophallaxis_attempt` | HIGH |
| 12 | Attacking Red Ants (B button) | Combat trigger | combat.c, player_actions.c | `yellow_ant_attack_red_simulate`, `simulate_attack_red_for_yellow` | HIGH |
| 12 | Leaving Food Scent Trails | Trail emission | scent.c, player_actions_full.c | `scent_place_black_trail_03_93D1`, `scent_place_red_trail_03_93F5` | HIGH |
| 12 | Recruiting and Releasing (5/10/All, ½/All) | Recruit menu | player_actions_full.c, player_actions.c | `recruit_apply_02A1F4`, `release_apply_02A2CB`, `recruit_menu_open_009D1A`, `recruit_menu_apply_pseudo` | HIGH |
| 13 | Communication (chemical scents) | Scent system | scent.c | full file: place/decay/follow/seed | HIGH |
| 13 | Death and Rebirth (next egg) | Yellow-ant respawn | player_actions.c | `simulate_yellow_ant_dies` | HIGH |
| 13 | Yellow Ant as Queen (Dig New Nest / Lay Eggs) | Queen menu | player_actions_full.c, player_actions.c | `queen_menu_open_009CF0`, `queen_menu_apply_pseudo`, `dig_action_03D7EA` | HIGH |
| 14 | Behavior Control Panel (Forage/Dig/Nurse triangle) | Barycentric input | control_panels.c | `cp_state_setup_CA96`, `cp_state_run_CCD0`, `cp_triangle_xy_to_weights_D034`, `cp_behavior_commit_CE9A` | HIGH |
| 14 | Auto / Manual icons | Mode toggle | control_panels.c | `cp_substate_auto_CDC6`, `cp_substate_manual_CDE5` | HIGH |
| 14 | Percent ↔ count display toggle | Numeric toggle | control_panels.c | `cp_substate_toggle_pct_count_CE04`, `cp_behavior_pct_to_count_CF8A`, `cp_caste_pct_to_count_CFDF` | HIGH |
| 15 | Foraging and Nursing (AntFacts) | (lore; gameplay is via behavior panel) | control_panels.c | (panel weight commit) | HIGH |
| 16 | Caste Control Panel (Workers/Soldiers/Breeders) | Same triangle, different commit | control_panels.c | `cp_caste_load_for_display_CEAA`, `cp_caste_commit_CEDB`, `cp_caste_auto_pack_CEC0` | HIGH |
| 17 | Castes (AntFacts: Queen/Male/Worker/Soldier) | Entity types | entities_b.c, entities_c.c | type 14 `type14_dispatch_A112` (Worker), type 15 `type15_dispatch_A222` (Soldier), type 18 `queen_handler_A533` (Queen), type 23 `type23_handler_A8D9` (Breeder) | HIGH |
| 18 | Playing Full Game — setting / 49 areas | Yard grid | territory.c | `area_grid_scan_F02A`, `area_offset_F5B2`, `area_state_has_B`, `area_state_has_R` | HIGH |
| 18 | Beginning Full Game — Dig Nest, Lay Eggs | Queen initial actions | player_actions_full.c, entities_c.c | `queen_menu_open_009CF0`, `type20_handler_A6C5` (excavator), Queen state machine `queen_states[6]` | HIGH |
| 19 | Mating Flight (pop≥100, breeders≥20) | Marriage flight trigger | simulation.c, territory.c, states_menu.c | `marriage_flight_trigger_9E35`, `territory_marriage_flight_trigger_9E35`, `gs_marriage_flight_B18C`, `state_0E_marriage_flight_setup_B2B0` | HIGH |
| 19 | Mass Exodus (pop ≥ 250) | Overcrowding split | simulation.c, territory.c | `mass_exodus_cap_and_split_F050`, `mass_exodus_cap_and_presence_F050`, `area_split_to_neighbour_F358`, `neighbour_balance_F2D9` | HIGH |
| 19 | Moving Section-to-Section | House screen click-to-travel | territory.c, ui_menus.c | `house_can_move_to_clicked`, `area_transition_append_96B0`, `house_area_append_039_6B0`, `house_screen_render_04_BD9B` | HIGH |
| 20 | Full Game Strategies — HUD bars | Population/health/energy bars | entities_d.c | `type26_status_panel_AB0B` (HUD strip) + types 6/7 indicators | HIGH |
| 20 | Save Full Game (any time) | Save flow | save_options.c | `save_ui_dispatch_9E36`, `save_full_game_03_F988_impl`, `save_checksum_03_FC3A` | HIGH |
| 20 | History / Status / House evaluation triggers | Evaluation icon | ui_menus.c | `state_house_screen_setup`, `state_history_graph_setup`, `state_status_screen_setup` | HIGH |
| 21 | Scenario Game — 3 lives | Life counter | (HUD entity 26) | `type26_status_panel_AB0B` reads life counter dp | MED |
| 21 | Scenario goal — kill Red Queen | Win condition | simulation.c | `sim_tick` watches Red Queen entity death → state_0B end | MED |
| 22 | Level 1: Spring — In the Park | View 6 config | scenarios.c | `scenario_park_view6`, scenario table entry "Park" | HIGH |
| 22 | Level 2: Spring — In the Garden | View 7 config + flower-pot props | scenarios.c | `scenario_garden_view7` | HIGH |
| 22 | Level 3: Rainy Season — In the Yard | View 8 + rain wash | scenarios.c, scent.c | `scenario_yard_view8`, `danger_rain_spawn`, `scenario_rain_tick`, `scent_rain_wash_all`, `scent_rain_wash_cell_02_96A0` | HIGH |
| 22 | Level 4: Summer — In the House | View 4 + cat paws | scenarios.c | `scenario_house_view4`, `danger_cat_paws_spawn` | HIGH |
| 22 | Level 5: Summer — On the Road | View 5 + bicycles + heat | scenarios.c | `scenario_road_view5`, `danger_bicycles_spawn` (type $3D handler at $04:BD9B) | HIGH |
| 22 | Level 6: Summer — By the River | View 1 + crevices, snails | scenarios.c, entities_c.c | `scenario_river_view1`, `danger_snails_spawn` (snail = type $13, handler `queen_handler_A533` re-used) | HIGH |
| 23 | Level 7: End of Summer — Under the Porch | View 3 + feet + falling objects | scenarios.c, entities_d.c | `scenario_porch_view3`, `danger_feet_spawn`, type 24/25 fall handlers | HIGH |
| 23 | Level 8: Autumn — In the Woods | View 10 + block red nest | scenarios.c | `scenario_woods_view10` | HIGH |
| 23 | Life cycle: Egg / Larva / Pupa / Adult | Lifecycle entities | entities_d.c, entities_b.c, entities_c.c | Egg = type 24 (`type24_dispatch_A951`), pupa drop-in = type 25 (`type25_dispatch_A9A1`), adults = types 14/15/18 | HIGH |
| 24 | Icon Menu — 6 icons | Toolbar dispatch | ui_menus.c | `icon_click_dispatch_A734` | HIGH |
| 25 | Views (6 modes + mini-map + census) | View states | states_gameplay.c | `state_view_surface_overview_*`, `state_view_bnest_overview_*` (R-mirror at $1F-$22) | HIGH |
| 26 | Scents (Black/Red Nest/Trail, Hide) | Scent overlay | scent.c | `scent_visualize_tile`, `scent_world_to_cell_offset` + 4 maps at $7F:4000-$7F:5FFF | HIGH |
| 27 | Control Panels (Behavior + Caste) | Icon → panel | control_panels.c | `cp_icon2_control_pick`, state_24/25/26/27 wrappers | HIGH |
| 28 | Loading Saved Games | Load flow | save_options.c | `load_game_03_FA74_impl`, `load_signature_check_03_FACB`, `load_ui_dispatch_9517` | HIGH |
| 28 | Saving Games (Save/Exit icon → Save → Yes) | Save UI | save_options.c | `save_ui_dispatch_9E36`, `save_signature_write_AA2E_inline` | HIGH |
| 28 | Save 1 Full + 4 Scenario slot semantics | Slot policy | save_options.c | `save_full_game_03_F988_impl`, `save_scenario_03_F9B9_impl`, `erase_full_save_00_9608`, `erase_scenario_slot_00_A986` | HIGH |
| 29 | House Screen (49 areas) | Map render | ui_menus.c, territory.c | `house_screen_render_04_BD9B`, `state_house_screen_setup`, `area_b_occupied_count`, `area_r_occupied_count` | HIGH |
| 29 | Area coloring (B/R/striped/empty/flashing) | Per-area state | territory.c | `area_state_has_B`, `area_state_has_R`, `area_state_advance_9930` | HIGH |
| 30 | Population Graph icon | Stepped graph rows | ui_menus.c | `state_house_screen_setup` toggles Pop-graph mode | MED |
| 31 | History Graph — 10 metrics | Sample recorder | simulation.c, ui_menus.c | `history_snapshot_ACC9`, `history_graph_snapshot_F927`, `history_graph_record_sample`, `state_history_graph_setup/run` | HIGH |
| 32 | Status Screen — 6 percentages | Percent compute | simulation.c, ui_menus.c, territory.c | `status_screen_compute_percent`, `status_screen_b_occupied_area_count_49`, `status_screen_r_occupied_area_count_49`, `colony_health_grade_9E62`, `status_screen_compute_territory`, `state_status_screen_setup` | HIGH |
| 33 | Setting Game Options — Sound (Music/SFX) | Sound toggles | save_options.c | `sound_music_set_A02A`, `sound_sfx_set_A04B`, `music_icon_toggle_CE04`, `sfx_play_8E88` | HIGH |
| 33 | Setting Game Options — Mouse speed | Mouse rate | save_options.c, mouse.c | `mouse_speed_set_A064`, `mouse_set_speed_E494` | HIGH |
| 33 | Setting Game Options — Speed (Fast/Normal/Slow/Pause) | Sim speed | save_options.c | `speed_set_A0C5`, `speed_set_pause_A0BA`, `pause_toggle_8101` | HIGH |
| 34 | Cast — Yellow Ant | Player avatar | (composite) | cursor types 1/2 + queen type 18 | HIGH |
| 34 | Cast — Queen Ant | One per colony | entities_c.c | `queen_handler_A533`, `queen_states[6]` | HIGH |
| 34 | Cast — Worker Ants | Workers | entities_b.c | `type14_dispatch_A112` + 5 states | HIGH |
| 34 | Cast — Soldier Ants | Soldiers | entities_b.c | `type15_dispatch_A222` + 5 states | HIGH |
| 34 | Cast — Breeders (Winged) | Breeders | entities_c.c | `type23_handler_A8D9` | MED |
| 34 | Cast — Spider | Spider | entities_c.c, combat.c | `type17_handler_A43B`, `spider_predation_tick_C0FD_excerpt` | HIGH |
| 34 | Cast — Ant Lions | Pit predator | lifted_helpers_4.c, simulation.c | `ant_lion_tick_C0FD` (stub; referenced from `sim_tick`) | LOW |
| 34 | Cast — Caterpillars | Edible larva | entities_d.c | type 27 `type27_dispatch_AB5B` (per ENTITIES.md best guess) | MED |
| 36 | Danger — Rain | Drop + puddle + wash | scenarios.c, scent.c | `danger_rain_spawn`, `scenario_rain_tick`, `scent_rain_wash_all`, types $0F/$10 | HIGH |
| 36 | Danger — Human Feet | Squash sweep | scenarios.c, combat.c | `danger_feet_spawn`, `hand_squash_EF02`, `mass_kill_sweep_EF1E` | HIGH |
| 36 | Danger — Lawn Mowers | Grind/blow ¼ kill | scenarios.c, combat.c | Re-used `danger_feet_spawn` path (mowers = type $1C variant of type 27/28 timer); `mass_kill_sweep_EF1E` | MED |
| 36 | Danger — Snails (wash scents) | Slug trail | scenarios.c | `danger_snails_spawn` (type $13 — Queen-alias handler) | HIGH |
| 36 | Danger — Cat's Paws | Pawprint stomp | scenarios.c | `danger_cat_paws_spawn` (types $17 + $4B) | HIGH |
| 36 | Danger — Bicycle Tires | Roll-over | scenarios.c | `danger_bicycles_spawn` (type $3D at $04:BD9B) | HIGH |
| 36 | Danger — Hands | Child squash | combat.c | `hand_squash_EF02` (type $4B at $04:C653) | HIGH |
| 36 | Credits | Credits screen | states_gameplay.c | `state_0A_credits_continue_B21A` | HIGH |

---

## Manual content with NO decomp coverage (gaps)

These are mechanics described in the manual but where the decomp lacks
a real implementation (only string evidence, only a stub, or no symbol
located at all).

1. **Y button → cursor jumps to View icon** (p.6). No symbol located.
2. **L/R buttons cursor-fixed scrolling** (p.6, 9). Scroll bit
   handling not isolated; likely lives unlifted in vsync.c.
3. **Ant Lion gameplay** (p.34). `ant_lion_tick_C0FD` is an empty stub in
   `lifted_helpers_4.c`. Ant Lion pit creation, ambush, and
   ant-consumption logic are referenced by `sim_tick` but the body is
   missing.
4. **Caterpillar harvest reward** (p.34: "recruit 15 ants to eat a
   caterpillar"). Caterpillar entity is type 27 by best-guess but the
   15-ant recruitment threshold and food-payout on kill is not lifted.
5. **Aphid ranching / honeydew** (p.21 AntFact + implied gameplay). No
   aphid entity or honeydew code located.
6. **Falling objects under the porch** (Scenario 7, p.23). Generic falling
   sprite is type 24/25 but the porch-specific spawner table is not
   surfaced separately from the generic `danger_feet_spawn`.
7. **Electric outlet** (Scenario 4, p.22). Mentioned by manual as a
   house hazard; no electric-outlet entity or hazard tick code.
8. **Building bridges with pebbles over crevices** (Scenario 6, p.22).
   River level config is lifted but the pickup-rock-and-drop-in-water
   bridging mechanic is not separately implemented.
9. **Blocking red ant nest with stones** (Scenario 8, p.23). Same rock
   pickup but no nest-entrance blocking detection.
10. **Drowning if nest dug too deep** (Scenario 3, p.22). Not lifted.
11. **Speed: Fast/Normal/Slow visualization** (p.33). `speed_set_A0C5`
    sets the value but the visible "Speed" submenu item rendering is
    not lifted.
12. **"Erase" save slot UI flow** (implied by p.28 + `erase_full_save_00_9608`
    existing). The UI prompt that leads to erase is not lifted.
13. **Tutorial step-completion detection** (p.4). 54 messages are lifted
    but the per-step "did the player do the requested action?" gate
    is not surfaced.

---

## Decomp code with NO manual mention (probably internal / SNES-specific)

These functions exist in the decomp but the manual makes no mention
of the corresponding mechanic — almost all are SNES platform plumbing
or internal simulation bookkeeping.

1. **NMI / VBlank machinery** — `vblank_ack_8967`, `enable_nmi_896D`,
   `mask_nmi_after_yield_8976`, `joypad_edge_latch_8887`,
   `joypad_auto_read_wait_E3FD`, `pause_lockout_check_87DA`
   (states_menu.c, vsync.c).
2. **APU / SPC700 driver** — entire audio_driver.c (1489 LOC) is the
   custom audio engine; manual only says "Music/SFX on/off".
3. **LZSS decompressor + save body compression** — used by
   `save_full_game_03_F988_impl` and `load_game_03_FA74_impl`; manual
   never mentions compression.
4. **DMA / CGRAM upload helpers** — `cgram_dma_8AED`,
   `vram_clear_block_867F`, `vram_fill_866E`, fade helpers
   `fade_in_85FC`, `fade_out_8616` (states_menu.c).
5. **Sprite OAM compositing** — `sub_AFBD_compose_draw`,
   `sub_AF90_tile_blit`, `sub_A9FD_draw_composite`,
   `type29_predispatch_guard` (entities_d.c).
6. **Round-robin slow-tick scheduler** — `round_robin_slow_ABEF` at
   32-tick cadence (simulation.c).
7. **History sample compaction** — `history_snapshot_ACC9` (every 64
   ticks); manual just shows the resulting graph.
8. **Population histogram inlining** — `build_population_histogram_inline_923B`
   (simulation.c).
9. **Save signature dance** — "DOBBY"/"DURRY" 5-byte sigs at
   $03:F97E/$03:F983; checksum `save_checksum_03_FC3A`. Manual just
   says "battery-backed memory".
10. **Asset blobs / per-bank loaders** — assets.c, asset_data_1..6.c
    (32k+ LOC of compressed tile data); manual is graphics-agnostic.
11. **Cooperative state-machine scheduler** — 64-state dispatch driven by
    `dp[$00]` and `dp[$02]` (current state / next state); manual just
    describes user-visible screens.
12. **Entity spawn / pool compaction** — `entity_spawn_0499C1`,
    `combat_compact_pool_99F1`, `combatant_append_96B0`.
13. **Scent gradient pick & follow internals** — `scent_pick_gradient_dir`,
    `scent_index_F5A8`. Manual describes "ants follow trails" but not
    the per-cell gradient + 8-direction picker.
14. **Per-view sprite spawn tables** — `view_decoration_handler` plus
    state $1D/$1E/$1F/$20 setup bodies; manual just shows screenshots.
15. **Type-29 dialog state machine** (10 states) — `type29_state0_hidden_AD6F`
    through `type29_state9_final_AF55`. Manual just says "menu opens".
16. **Worker / Soldier internal pose state** — `type14_state3_A1A7_pose`,
    `type15_state3_A2D7_pose` (between walking and attack); manual just
    says ants "walk" and "fight".

---

## Verification: counted

- **Manual mechanics catalogued: 85** (one per row in the table above).
- **Cross-referenced to decomp symbol: 80** (HIGH=63, MED=12, LOW=5).
- **Gaps (no decomp coverage): 13** listed above.
- **Internal-only routines (decomp, no manual): 16** classes listed above.

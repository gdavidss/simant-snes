# A5 — Dead Static Functions

Pure-grep static dead-code analysis across all .c files in /Users/guilhermedavid/simant-re.

**Methodology.** Every `static <type> name(` definition was extracted, then all `name(` occurrences across all .c files were counted. Definition lines and forward declarations were subtracted to get the call count. Functions with zero direct calls were re-scanned for non-call references (function-pointer table entries, casts, address-of) and classified.

## Totals

- Unique static function names defined: 617
- With zero direct calls: 307
  - CALLED INDIRECTLY: 300
  - TRULY DEAD: 7

## Per-file breakdown

### audio_driver.c (5 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | audio_driver.c:128 | `dsp_read` | audio_driver.c:1522 |
| TRULY DEAD | audio_driver.c:1520 | `__unused_anchor` | — |
| CALLED INDIRECTLY | audio_driver.c:1537 | `song_command_byte_dispatch_06CC` | audio_driver.c:1532 |
| CALLED INDIRECTLY | audio_driver.c:1538 | `driver_kick_06B6_06C4` | audio_driver.c:1533 |
| TRULY DEAD | audio_driver.c:1693 | `commit_song_y_0D34` | — |

### audio_intro.c (5 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | audio_intro.c:470 | `state_3F_winter_ending_B833` | audio_intro.c:608, audio_intro.c:660 |
| CALLED INDIRECTLY | audio_intro.c:483 | `state_40_credits_setup_B875` | audio_intro.c:219, audio_intro.c:609, audio_intro.c:661 |
| CALLED INDIRECTLY | audio_intro.c:548 | `state_41_credits_pageloop_B8B9` | audio_intro.c:610, audio_intro.c:662 |
| CALLED INDIRECTLY | audio_intro.c:557 | `state_42_credits_wrapup_B996` | audio_intro.c:611, audio_intro.c:663 |
| CALLED INDIRECTLY | audio_intro.c:579 | `state_43_post_credits_BA4D` | audio_intro.c:612, audio_intro.c:664 |

### control_panels.c (4 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | control_panels.c:530 | `cp_substate_auto_CDC6` | control_panels.c:573 |
| CALLED INDIRECTLY | control_panels.c:541 | `cp_substate_manual_CDE5` | control_panels.c:574 |
| CALLED INDIRECTLY | control_panels.c:552 | `cp_substate_click_CDA5` | control_panels.c:575 |
| CALLED INDIRECTLY | control_panels.c:559 | `cp_substate_toggle_pct_count_CE04` | control_panels.c:576, control_panels.c:577, control_panels.c:578 |

### entities_b.c (31 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | entities_b.c:295 | `type8_dispatch_9CF0` | entities_b.c:922 |
| CALLED INDIRECTLY | entities_b.c:322 | `type9_state0_9E51_spawn` | entities_b.c:359 |
| CALLED INDIRECTLY | entities_b.c:333 | `type9_state1_9E6D_alive` | entities_b.c:360 |
| CALLED INDIRECTLY | entities_b.c:351 | `type9_state2_9E8E_die` | entities_b.c:361 |
| CALLED INDIRECTLY | entities_b.c:364 | `type9_dispatch_9E3F` | entities_b.c:923 |
| CALLED INDIRECTLY | entities_b.c:374 | `type10_state0_9EAE_spawn` | entities_b.c:424 |
| CALLED INDIRECTLY | entities_b.c:390 | `type10_state1_9ECA_alive` | entities_b.c:425 |
| CALLED INDIRECTLY | entities_b.c:410 | `type10_state2_9EFD_die` | entities_b.c:426 |
| CALLED INDIRECTLY | entities_b.c:436 | `type22_dispatch_9E9C` | entities_b.c:930 |
| CALLED INDIRECTLY | entities_b.c:443 | `type11_state0_9F2F_spawn` | entities_b.c:471 |
| CALLED INDIRECTLY | entities_b.c:451 | `type11_state1_9F4B_alive` | entities_b.c:472 |
| CALLED INDIRECTLY | entities_b.c:463 | `type11_state2_9F6C_die` | entities_b.c:473 |
| CALLED INDIRECTLY | entities_b.c:476 | `type11_dispatch_9F1D` | entities_b.c:925 |
| CALLED INDIRECTLY | entities_b.c:486 | `type12_state0_9F8A_spawn` | entities_b.c:525 |
| CALLED INDIRECTLY | entities_b.c:498 | `type12_state1_9FA6_alive` | entities_b.c:526 |
| CALLED INDIRECTLY | entities_b.c:529 | `type12_dispatch_9F7A` | entities_b.c:926 |
| CALLED INDIRECTLY | entities_b.c:540 | `type13_state0_9FF0_spawn` | entities_b.c:613 |
| CALLED INDIRECTLY | entities_b.c:555 | `type13_state1_A013_banner` | entities_b.c:614 |
| CALLED INDIRECTLY | entities_b.c:617 | `type13_dispatch_9FE0` | entities_b.c:927 |
| CALLED INDIRECTLY | entities_b.c:652 | `type14_state0_A128_spawn` | entities_b.c:755 |
| CALLED INDIRECTLY | entities_b.c:671 | `type14_state1_A13E_walking` | entities_b.c:756 |
| CALLED INDIRECTLY | entities_b.c:698 | `type14_state2_A178_turning` | entities_b.c:757 |
| CALLED INDIRECTLY | entities_b.c:718 | `type14_state3_A1A7_pose` | entities_b.c:758 |
| CALLED INDIRECTLY | entities_b.c:735 | `type14_state4_A1CC_attack` | entities_b.c:759 |
| CALLED INDIRECTLY | entities_b.c:762 | `type14_dispatch_A112` | entities_b.c:928 |
| CALLED INDIRECTLY | entities_b.c:791 | `type15_state0_A238_spawn` | entities_b.c:901 |
| CALLED INDIRECTLY | entities_b.c:803 | `type15_state1_A253_walking` | entities_b.c:902 |
| CALLED INDIRECTLY | entities_b.c:830 | `type15_state2_A28D_turning` | entities_b.c:903 |
| CALLED INDIRECTLY | entities_b.c:860 | `type15_state3_A2D7_pose` | entities_b.c:904 |
| CALLED INDIRECTLY | entities_b.c:876 | `type15_state4_A2FC_attack` | entities_b.c:905 |
| CALLED INDIRECTLY | entities_b.c:908 | `type15_dispatch_A222` | entities_b.c:929 |

### entities_c.c (17 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | entities_c.c:155 | `type16_state0_init_A366` | entities_c.c:221 |
| CALLED INDIRECTLY | entities_c.c:169 | `type16_state1_step_A382` | entities_c.c:222 |
| CALLED INDIRECTLY | entities_c.c:246 | `type17_state0_init_A44B` | entities_c.c:316 |
| CALLED INDIRECTLY | entities_c.c:256 | `type17_state1_step_A467` | entities_c.c:317 |
| CALLED INDIRECTLY | entities_c.c:398 | `queen_state0_init_A54B` | entities_c.c:542 |
| CALLED INDIRECTLY | entities_c.c:409 | `queen_state1_wander_A566` | entities_c.c:543 |
| CALLED INDIRECTLY | entities_c.c:442 | `queen_state2_walk_A5B6` | entities_c.c:544 |
| CALLED INDIRECTLY | entities_c.c:484 | `queen_state3_pause_A61E` | entities_c.c:545 |
| CALLED INDIRECTLY | entities_c.c:501 | `queen_state4_face_A643` | entities_c.c:546 |
| CALLED INDIRECTLY | entities_c.c:524 | `queen_state5_stun_A682` | entities_c.c:547 |
| CALLED INDIRECTLY | entities_c.c:664 | `type20_state0_init_A6DE` | entities_c.c:750 |
| CALLED INDIRECTLY | entities_c.c:675 | `type20_state1_carve_A704` | entities_c.c:751 |
| CALLED INDIRECTLY | entities_c.c:706 | `type20_state2_pause_A758` | entities_c.c:752 |
| CALLED INDIRECTLY | entities_c.c:719 | `type20_state3_pause_A775` | entities_c.c:753 |
| CALLED INDIRECTLY | entities_c.c:732 | `type20_state4_recover_A792` | entities_c.c:754 |
| CALLED INDIRECTLY | entities_c.c:792 | `type23_state0_init_A8E9` | entities_c.c:836 |
| CALLED INDIRECTLY | entities_c.c:814 | `type23_state1_step_A8FA` | entities_c.c:837 |

### entities_d.c (8 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | entities_d.c:344 | `type24_dispatch_A951` | entities_d.c:1253 |
| CALLED INDIRECTLY | entities_d.c:399 | `type25_dispatch_A9A1` | entities_d.c:1254 |
| CALLED INDIRECTLY | entities_d.c:429 | `type26_status_panel_AB0B` | entities_d.c:1255 |
| CALLED INDIRECTLY | entities_d.c:606 | `type27_dispatch_AB5B` | entities_d.c:1256 |
| CALLED INDIRECTLY | entities_d.c:709 | `type28_dispatch_AC3A` | entities_d.c:1257 |
| CALLED INDIRECTLY | entities_d.c:1032 | `type29_dispatch_AD01` | entities_d.c:1258 |
| CALLED INDIRECTLY | entities_d.c:1190 | `type30_dispatch_B17F` | entities_d.c:1259 |
| CALLED INDIRECTLY | entities_d.c:1228 | `type31_cursor_hint_B547` | entities_d.c:1260 |

### entities_e.c (15 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | entities_e.c:214 | `type20_handler_B597` | entities_e.c:1432 |
| CALLED INDIRECTLY | entities_e.c:464 | `type27_auto_manual_icon_9DD5` | entities_e.c:1439 |
| CALLED INDIRECTLY | entities_e.c:469 | `type28_auto_manual_icon_9DEA` | entities_e.c:1440 |
| CALLED INDIRECTLY | entities_e.c:474 | `type29_auto_manual_icon_9DFF` | entities_e.c:1441 |
| CALLED INDIRECTLY | entities_e.c:479 | `type2A_auto_manual_icon_9E14` | entities_e.c:1442 |
| CALLED INDIRECTLY | entities_e.c:488 | `type2B_subnest_indicator_9E29` | entities_e.c:1443 |
| CALLED INDIRECTLY | entities_e.c:504 | `type2C_centered_prop_B673` | entities_e.c:1444 |
| CALLED INDIRECTLY | entities_e.c:812 | `type30_score_digit_BAD4` | entities_e.c:1448 |
| CALLED INDIRECTLY | entities_e.c:943 | `type34_bicycle_spawner_BC07` | entities_e.c:1452 |
| CALLED INDIRECTLY | entities_e.c:975 | `type36_burst_BE49` | entities_e.c:1454 |
| CALLED INDIRECTLY | entities_e.c:1076 | `type39_fly_spawner_BFB0` | entities_e.c:1457 |
| CALLED INDIRECTLY | entities_e.c:1180 | `type3A_dispatch_C02B` | entities_e.c:1458 |
| CALLED INDIRECTLY | entities_e.c:1262 | `type3C_hdma_setup_C300` | entities_e.c:1460 |
| CALLED INDIRECTLY | entities_e.c:1360 | `type3D_bicycle_dispatch_C36E` | entities_e.c:1461 |
| CALLED INDIRECTLY | entities_e.c:1415 | `type3F_watch_C5C8` | entities_e.c:1463 |

### entities_f.c (84 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | entities_f.c:246 | `type40_state0_init_C5E7` | entities_f.c:270 |
| CALLED INDIRECTLY | entities_f.c:254 | `type40_state1_anim_C5F2` | entities_f.c:271 |
| CALLED INDIRECTLY | entities_f.c:267 | `type40_dispatch_C5D7` | entities_f.c:1505 |
| CALLED INDIRECTLY | entities_f.c:290 | `type41_state0_init_C4E0` | entities_f.c:330 |
| CALLED INDIRECTLY | entities_f.c:296 | `type41_state1_audio_C4EB` | entities_f.c:331 |
| CALLED INDIRECTLY | entities_f.c:304 | `type41_state2_pickpos_C506` | entities_f.c:332 |
| CALLED INDIRECTLY | entities_f.c:315 | `type41_state3_draw_C526` | entities_f.c:333 |
| CALLED INDIRECTLY | entities_f.c:326 | `type41_dispatch_C4C4` | entities_f.c:1506 |
| CALLED INDIRECTLY | entities_f.c:346 | `type42_state0_init_C5A9` | entities_f.c:361 |
| CALLED INDIRECTLY | entities_f.c:352 | `type42_state1_step_C5B3` | entities_f.c:361 |
| CALLED INDIRECTLY | entities_f.c:359 | `type42_dispatch_C599` | entities_f.c:1507 |
| CALLED INDIRECTLY | entities_f.c:384 | `type43_dispatch_C61D` | entities_f.c:1508 |
| CALLED INDIRECTLY | entities_f.c:406 | `type44_state0_init_BC59` | entities_f.c:423 |
| CALLED INDIRECTLY | entities_f.c:409 | `type44_state1_step_BC63` | entities_f.c:423 |
| CALLED INDIRECTLY | entities_f.c:421 | `type44_dispatch_BC49` | entities_f.c:1509 |
| CALLED INDIRECTLY | entities_f.c:434 | `type45_dispatch_BC8A` | entities_f.c:1510 |
| CALLED INDIRECTLY | entities_f.c:458 | `type46_state0_init_BFD6` | entities_f.c:479 |
| CALLED INDIRECTLY | entities_f.c:470 | `type46_state1_step_BFFE` | entities_f.c:479 |
| CALLED INDIRECTLY | entities_f.c:477 | `type46_dispatch_BFC6` | entities_f.c:1511 |
| CALLED INDIRECTLY | entities_f.c:491 | `type47_dispatch_C013` | entities_f.c:1512 |
| CALLED INDIRECTLY | entities_f.c:508 | `type48_skipframe_B411` | entities_f.c:1513 |
| CALLED INDIRECTLY | entities_f.c:519 | `type49_skipframe_B358` | entities_f.c:1514 |
| CALLED INDIRECTLY | entities_f.c:542 | `type4A_skipframe_B3C4` | entities_f.c:1515 |
| CALLED INDIRECTLY | entities_f.c:592 | `type4B_state0_init_C667` | entities_f.c:669 |
| CALLED INDIRECTLY | entities_f.c:598 | `type4B_state1_descend_C678` | entities_f.c:670 |
| CALLED INDIRECTLY | entities_f.c:625 | `type4B_state2_sweep_C6D9` | entities_f.c:671 |
| CALLED INDIRECTLY | entities_f.c:645 | `type4B_state3_retract_C724` | entities_f.c:672 |
| CALLED INDIRECTLY | entities_f.c:652 | `type4B_state4_expire_C7BB` | entities_f.c:673 |
| CALLED INDIRECTLY | entities_f.c:666 | `type4B_dispatch_C653` | entities_f.c:1516 |
| CALLED INDIRECTLY | entities_f.c:683 | `type4C_state0_init_C74B` | entities_f.c:715 |
| CALLED INDIRECTLY | entities_f.c:693 | `type4C_state1_step_C75D` | entities_f.c:715 |
| CALLED INDIRECTLY | entities_f.c:713 | `type4C_dispatch_C73B` | entities_f.c:1517 |
| CALLED INDIRECTLY | entities_f.c:725 | `type4D_state0_init_C8BB` | entities_f.c:764 |
| CALLED INDIRECTLY | entities_f.c:732 | `type4D_state1_in_C8C8` | entities_f.c:764 |
| CALLED INDIRECTLY | entities_f.c:745 | `type4D_state2_hold_C8D8` | entities_f.c:765 |
| CALLED INDIRECTLY | entities_f.c:760 | `type4D_state3_out_C916` | entities_f.c:765 |
| CALLED INDIRECTLY | entities_f.c:761 | `type4D_dispatch_C8A7` | entities_f.c:1518 |
| CALLED INDIRECTLY | entities_f.c:780 | `type4E_scrollbias_C91B` | entities_f.c:1519 |
| CALLED INDIRECTLY | entities_f.c:804 | `type4F_walkprop_C92C` | entities_f.c:1520 |
| CALLED INDIRECTLY | entities_f.c:827 | `type50_walkprop_C958` | entities_f.c:1521 |
| CALLED INDIRECTLY | entities_f.c:847 | `type51_state1_step_C9A0` | entities_f.c:864 |
| CALLED INDIRECTLY | entities_f.c:854 | `type51_state2_step_C9AB` | entities_f.c:864 |
| CALLED INDIRECTLY | entities_f.c:861 | `type51_dispatch_C984` | entities_f.c:1522 |
| CALLED INDIRECTLY | entities_f.c:877 | `type52_state2_step_C9ED` | entities_f.c:887 |
| CALLED INDIRECTLY | entities_f.c:884 | `type52_dispatch_C9C6` | entities_f.c:1523 |
| CALLED INDIRECTLY | entities_f.c:896 | `type53_state0_init_CA1A` | entities_f.c:910 |
| CALLED INDIRECTLY | entities_f.c:900 | `type53_state2_step_CA2F` | entities_f.c:910 |
| CALLED INDIRECTLY | entities_f.c:907 | `type53_dispatch_CA08` | entities_f.c:1524 |
| CALLED INDIRECTLY | entities_f.c:925 | `type54_state2_step_CA73` | entities_f.c:935 |
| CALLED INDIRECTLY | entities_f.c:932 | `type54_dispatch_CA4C` | entities_f.c:1525 |
| CALLED INDIRECTLY | entities_f.c:962 | `type55_state0_init_CACF_real` | entities_f.c:966 |
| CALLED INDIRECTLY | entities_f.c:963 | `type55_state1_draw` | entities_f.c:966 |
| CALLED INDIRECTLY | entities_f.c:964 | `type55_compose_CA93` | entities_f.c:1526 |
| CALLED INDIRECTLY | entities_f.c:975 | `type56_state0_init_CAD3` | entities_f.c:1012 |
| CALLED INDIRECTLY | entities_f.c:997 | `type56_state1_step_CAF6` | entities_f.c:1012 |
| CALLED INDIRECTLY | entities_f.c:1010 | `type56_dispatch_CAC3` | entities_f.c:1527 |
| CALLED INDIRECTLY | entities_f.c:1023 | `type57_audio_trampoline_CB65` | entities_f.c:1528 |
| CALLED INDIRECTLY | entities_f.c:1034 | `type58_state0_init_CC83` | entities_f.c:1070 |
| CALLED INDIRECTLY | entities_f.c:1044 | `type58_state1_step_CC92` | entities_f.c:1070 |
| CALLED INDIRECTLY | entities_f.c:1068 | `type58_dispatch_CC73` | entities_f.c:1529 |
| CALLED INDIRECTLY | entities_f.c:1093 | `type59_population_readout_CB73` | entities_f.c:1530 |
| CALLED INDIRECTLY | entities_f.c:1151 | `type5A_state0_init_CD6D` | entities_f.c:1180 |
| CALLED INDIRECTLY | entities_f.c:1157 | `type5A_state1_intro_CD82` | entities_f.c:1180 |
| CALLED INDIRECTLY | entities_f.c:1168 | `type5A_state2_rest_CD9B` | entities_f.c:1180 |
| CALLED INDIRECTLY | entities_f.c:1177 | `type5A_dispatch_CD5B` | entities_f.c:1531 |
| CALLED INDIRECTLY | entities_f.c:1210 | `type5B_state0_init_CE1C` | entities_f.c:1235 |
| CALLED INDIRECTLY | entities_f.c:1216 | `type5B_state1_intro_CE31` | entities_f.c:1235 |
| CALLED INDIRECTLY | entities_f.c:1223 | `type5B_state2_rest_CE4A` | entities_f.c:1235 |
| CALLED INDIRECTLY | entities_f.c:1232 | `type5B_dispatch_CE0A` | entities_f.c:1532 |
| CALLED INDIRECTLY | entities_f.c:1259 | `type5C_state0_init_CECB` | entities_f.c:1326 |
| CALLED INDIRECTLY | entities_f.c:1305 | `type5C_state1_slide_CEDA` | entities_f.c:1326 |
| CALLED INDIRECTLY | entities_f.c:1318 | `type5C_state2_rest_CF04` | entities_f.c:1326 |
| CALLED INDIRECTLY | entities_f.c:1323 | `type5C_dispatch_CEB9` | entities_f.c:1533 |
| CALLED INDIRECTLY | entities_f.c:1363 | `type5D_state0_init_CF82` | entities_f.c:1386 |
| CALLED INDIRECTLY | entities_f.c:1371 | `type5D_state1_slide_CF91` | entities_f.c:1386 |
| CALLED INDIRECTLY | entities_f.c:1382 | `type5D_state2_rest_CFB9` | entities_f.c:1386 |
| CALLED INDIRECTLY | entities_f.c:1383 | `type5D_dispatch_CF70` | entities_f.c:1534 |
| CALLED INDIRECTLY | entities_f.c:1406 | `type5E_state0_init_D037` | entities_f.c:1447 |
| CALLED INDIRECTLY | entities_f.c:1412 | `type5E_state1_intro_D046` | entities_f.c:1447 |
| CALLED INDIRECTLY | entities_f.c:1428 | `type5E_state2_rest_D054` | entities_f.c:1447 |
| CALLED INDIRECTLY | entities_f.c:1444 | `type5E_gameover_banner_D025` | entities_f.c:1535 |
| CALLED INDIRECTLY | entities_f.c:1463 | `type5F_state0_init_D09F` | entities_f.c:1493 |
| CALLED INDIRECTLY | entities_f.c:1481 | `type5F_state1_step_D0C2` | entities_f.c:1493 |
| CALLED INDIRECTLY | entities_f.c:1491 | `type5F_dispatch_D08F` | entities_f.c:1536 |

### entities_g.c (50 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | entities_g.c:136 | `type60_state0` | entities_g.c:469 |
| CALLED INDIRECTLY | entities_g.c:145 | `type60_state1` | entities_g.c:469 |
| CALLED INDIRECTLY | entities_g.c:163 | `type61_state0` | entities_g.c:481 |
| CALLED INDIRECTLY | entities_g.c:169 | `type61_state1` | entities_g.c:481 |
| CALLED INDIRECTLY | entities_g.c:178 | `type63_state0` | entities_g.c:507 |
| CALLED INDIRECTLY | entities_g.c:187 | `type63_state1` | entities_g.c:507 |
| CALLED INDIRECTLY | entities_g.c:219 | `type65_state0` | entities_g.c:537 |
| CALLED INDIRECTLY | entities_g.c:226 | `type65_state1` | entities_g.c:537 |
| CALLED INDIRECTLY | entities_g.c:244 | `type67_state0` | entities_g.c:605 |
| CALLED INDIRECTLY | entities_g.c:256 | `type67_state1` | entities_g.c:605 |
| CALLED INDIRECTLY | entities_g.c:288 | `type69_state0` | entities_g.c:636 |
| CALLED INDIRECTLY | entities_g.c:297 | `type69_state1` | entities_g.c:636 |
| CALLED INDIRECTLY | entities_g.c:309 | `type6A_state0` | entities_g.c:646 |
| CALLED INDIRECTLY | entities_g.c:315 | `type6A_state1` | entities_g.c:646 |
| CALLED INDIRECTLY | entities_g.c:329 | `type6B_state0` | entities_g.c:660 |
| CALLED INDIRECTLY | entities_g.c:338 | `type6B_state1` | entities_g.c:660 |
| CALLED INDIRECTLY | entities_g.c:345 | `type6C_state0` | entities_g.c:672 |
| CALLED INDIRECTLY | entities_g.c:354 | `type6C_state1` | entities_g.c:672 |
| CALLED INDIRECTLY | entities_g.c:361 | `type6D_state0` | entities_g.c:685 |
| CALLED INDIRECTLY | entities_g.c:370 | `type6D_state1` | entities_g.c:685 |
| CALLED INDIRECTLY | entities_g.c:379 | `type6E_state0` | entities_g.c:696 |
| CALLED INDIRECTLY | entities_g.c:387 | `type6E_state1` | entities_g.c:696 |
| CALLED INDIRECTLY | entities_g.c:394 | `type6F_state0` | entities_g.c:709 |
| CALLED INDIRECTLY | entities_g.c:401 | `type6F_state1` | entities_g.c:709 |
| CALLED INDIRECTLY | entities_g.c:409 | `type70_state0` | entities_g.c:721 |
| CALLED INDIRECTLY | entities_g.c:419 | `type70_state1` | entities_g.c:721 |
| CALLED INDIRECTLY | entities_g.c:427 | `type71_state0` | entities_g.c:741 |
| CALLED INDIRECTLY | entities_g.c:439 | `type71_state1` | entities_g.c:741 |
| CALLED INDIRECTLY | entities_g.c:468 | `type60_dispatch_C7DD` | entities_g.c:849 |
| CALLED INDIRECTLY | entities_g.c:480 | `type61_dispatch_C842` | entities_g.c:850 |
| CALLED INDIRECTLY | entities_g.c:493 | `type62_asset_reset_CB5C` | entities_g.c:851 |
| CALLED INDIRECTLY | entities_g.c:506 | `type63_dispatch_AA41` | entities_g.c:852 |
| CALLED INDIRECTLY | entities_g.c:523 | `type64_mode7_sentinel_CB6E` | entities_g.c:853 |
| CALLED INDIRECTLY | entities_g.c:536 | `type65_dispatch_B622` | entities_g.c:854 |
| CALLED INDIRECTLY | entities_g.c:567 | `type66_decoration_iter_BD4E` | entities_g.c:855 |
| CALLED INDIRECTLY | entities_g.c:604 | `type67_dispatch_BCCC` | entities_g.c:856 |
| CALLED INDIRECTLY | entities_g.c:622 | `type68_timer_tick_D16F` | entities_g.c:857 |
| CALLED INDIRECTLY | entities_g.c:635 | `type69_dispatch_D19B` | entities_g.c:858 |
| CALLED INDIRECTLY | entities_g.c:645 | `type6A_dispatch_D22D` | entities_g.c:859 |
| CALLED INDIRECTLY | entities_g.c:657 | `type6B_dispatch_D259` | entities_g.c:860 |
| CALLED INDIRECTLY | entities_g.c:669 | `type6C_dispatch_D2D7` | entities_g.c:861 |
| CALLED INDIRECTLY | entities_g.c:682 | `type6D_dispatch_D38B` | entities_g.c:862 |
| CALLED INDIRECTLY | entities_g.c:694 | `type6E_dispatch_D3F1` | entities_g.c:863 |
| CALLED INDIRECTLY | entities_g.c:706 | `type6F_dispatch_D4B8` | entities_g.c:864 |
| CALLED INDIRECTLY | entities_g.c:718 | `type70_dispatch_D580` | entities_g.c:865 |
| CALLED INDIRECTLY | entities_g.c:734 | `type71_dispatch_D62F` | entities_g.c:866 |
| CALLED INDIRECTLY | entities_g.c:755 | `type72_static_sprite_D6DF` | entities_g.c:867 |
| CALLED INDIRECTLY | entities_g.c:783 | `type73_audio_tick_B5F8` | entities_g.c:868 |
| CALLED INDIRECTLY | entities_g.c:803 | `type74_vertical_column_CCEE` | entities_g.c:869 |
| CALLED INDIRECTLY | entities_g.c:836 | `type75_noop_marker_A560` | entities_g.c:870 |

### render_helpers.c (4 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | render_helpers.c:239 | `view_tail_select_A746` | render_helpers.c:227 |
| CALLED INDIRECTLY | render_helpers.c:253 | `view_tail_save_A755` | render_helpers.c:228 |
| CALLED INDIRECTLY | render_helpers.c:273 | `view_tail_action_A787` | render_helpers.c:229 |
| TRULY DEAD | render_helpers.c:302 | `view_tail_landing_A7AD` | — |

### save_options.c (5 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| TRULY DEAD | save_options.c:125 | `wram_at` | — |
| TRULY DEAD | save_options.c:159 | `bank3_call_save_full` | — |
| TRULY DEAD | save_options.c:161 | `bank3_call_save_scenario` | — |
| TRULY DEAD | save_options.c:164 | `bank3_call_post_load` | — |
| CALLED INDIRECTLY | save_options.c:993 | `speed_state_loop_header` | save_options.c:1100 |

### scenarios.c (2 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | scenarios.c:630 | `scenario_rain_tick` | scenarios.c:747 |
| CALLED INDIRECTLY | scenarios.c:654 | `view_decoration_handler` | scenarios.c:746 |

### simant.c (12 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | simant.c:718 | `cursor_handler_04_9D9D` | simant.c:1200 |
| CALLED INDIRECTLY | simant.c:754 | `view_switch_state_A3BD` | simant.c:1203 |
| CALLED INDIRECTLY | simant.c:763 | `play_sfx_and_fade_8611` | audio_intro.c:254, simant.c:1204 |
| CALLED INDIRECTLY | simant.c:801 | `vsync_and_input_985F` | simant.c:1190 |
| CALLED INDIRECTLY | simant.c:845 | `save_signature_write_AA2E` | simant.c:1194 |
| CALLED INDIRECTLY | simant.c:1054 | `gs_full_game` | simant.c:1191 |
| CALLED INDIRECTLY | simant.c:1069 | `gs_scenario_game` | simant.c:1191 |
| CALLED INDIRECTLY | simant.c:1076 | `gs_tutorial` | simant.c:1192 |
| CALLED INDIRECTLY | simant.c:1083 | `gs_marriage_flight` | simant.c:1192 |
| CALLED INDIRECTLY | simant.c:1091 | `gs_game_over` | simant.c:1193 |
| CALLED INDIRECTLY | simant.c:1102 | `gs_sound` | simant.c:1193 |
| CALLED INDIRECTLY | simant.c:1151 | `spc700_iram_byte_send_817A` | simant.c:1196 |

### states_gameplay.c (33 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | states_gameplay.c:198 | `state_0A_credits_continue_B21A` | states_gameplay.c:1661, states_gameplay.c:1713 |
| CALLED INDIRECTLY | states_gameplay.c:222 | `state_0B_scenario_end_celebration_B281` | states_gameplay.c:1662, states_gameplay.c:1714 |
| CALLED INDIRECTLY | states_gameplay.c:241 | `state_0C_saved_game_screen_AEAD` | states_gameplay.c:1663, states_gameplay.c:1715 |
| CALLED INDIRECTLY | states_gameplay.c:274 | `state_0D_saved_game_scroll_in_AF3F` | states_gameplay.c:1664, states_gameplay.c:1716 |
| CALLED INDIRECTLY | states_gameplay.c:303 | `state_0E_marriage_flight_setup_B2B0` | states_gameplay.c:1665, states_gameplay.c:1717 |
| CALLED INDIRECTLY | states_gameplay.c:351 | `state_0F_marriage_flight_animate_B352` | states_gameplay.c:1666, states_gameplay.c:1718 |
| CALLED INDIRECTLY | states_gameplay.c:364 | `state_10_ant_info_left_B47C` | states_gameplay.c:1667, states_gameplay.c:1719 |
| CALLED INDIRECTLY | states_gameplay.c:379 | `state_11_ant_info_text_B490` | states_gameplay.c:1668, states_gameplay.c:1720 |
| CALLED INDIRECTLY | states_gameplay.c:392 | `state_12_map_overlay_B3D8` | states_gameplay.c:1669, states_gameplay.c:1721 |
| CALLED INDIRECTLY | states_gameplay.c:426 | `state_13_map_scroll_B45D` | states_gameplay.c:1670, states_gameplay.c:1722 |
| CALLED INDIRECTLY | states_gameplay.c:444 | `state_14_bug_cutin_B4BA` | states_gameplay.c:1671, states_gameplay.c:1723 |
| CALLED INDIRECTLY | states_gameplay.c:462 | `state_15_bug_cutin_caption_B4DA` | states_gameplay.c:1672, states_gameplay.c:1724 |
| CALLED INDIRECTLY | states_gameplay.c:477 | `state_16_title_input_93F3` | states_gameplay.c:1673, states_gameplay.c:1725 |
| CALLED INDIRECTLY | states_gameplay.c:523 | `state_17_save_picker_D57E` | states_gameplay.c:1674, states_gameplay.c:1726 |
| CALLED INDIRECTLY | states_gameplay.c:539 | `state_18_save_picker_navigate_D68A` | states_gameplay.c:1675, states_gameplay.c:1727 |
| CALLED INDIRECTLY | states_gameplay.c:700 | `state_1B_view_switch_landing_C12F` | states_gameplay.c:1678, states_gameplay.c:1730 |
| CALLED INDIRECTLY | states_gameplay.c:758 | `state_1C_post_view_switch_9850` | states_gameplay.c:1679, states_gameplay.c:1731 |
| CALLED INDIRECTLY | states_gameplay.c:819 | `state_view_surface_overview_BC9C` | states_gameplay.c:1680, states_gameplay.c:1732 |
| CALLED INDIRECTLY | states_gameplay.c:956 | `state_view_surface_overview_run_98D5` | states_gameplay.c:1681, states_gameplay.c:1733 |
| CALLED INDIRECTLY | states_gameplay.c:1008 | `state_view_bnest_overview_BFC8` | states_gameplay.c:1682, states_gameplay.c:1734 |
| CALLED INDIRECTLY | states_gameplay.c:1034 | `state_view_bnest_overview_run_9A14` | states_gameplay.c:1683, states_gameplay.c:1735 |
| CALLED INDIRECTLY | states_gameplay.c:1082 | `state_view_rnest_overview_C01A` | states_gameplay.c:1684, states_gameplay.c:1736 |
| CALLED INDIRECTLY | states_gameplay.c:1108 | `state_view_rnest_overview_run_9B7D` | states_gameplay.c:1685, states_gameplay.c:1737 |
| CALLED INDIRECTLY | states_gameplay.c:1157 | `state_view_surface_closeup_A7DD` | states_gameplay.c:1686, states_gameplay.c:1738 |
| CALLED INDIRECTLY | states_gameplay.c:1216 | `state_view_nest_closeup_setup_CA96` | states_gameplay.c:1687, states_gameplay.c:1689, states_gameplay.c:1739 |
| CALLED INDIRECTLY | states_gameplay.c:1420 | `state_28_save_picker_ui_D7CE` | states_gameplay.c:1691, states_gameplay.c:1741 |
| CALLED INDIRECTLY | states_gameplay.c:1462 | `state_29_save_run_D943` | states_gameplay.c:1692, states_gameplay.c:1742 |
| CALLED INDIRECTLY | states_gameplay.c:1490 | `state_2A_sound_options_setup_D256` | states_gameplay.c:1693, states_gameplay.c:1743 |
| CALLED INDIRECTLY | states_gameplay.c:1516 | `state_2B_sound_options_input_D35A` | states_gameplay.c:1694, states_gameplay.c:1744 |
| CALLED INDIRECTLY | states_gameplay.c:1543 | `state_2C_scent_display_setup_D09E` | states_gameplay.c:1695, states_gameplay.c:1745 |
| CALLED INDIRECTLY | states_gameplay.c:1579 | `state_2D_scent_display_exit_D24C` | states_gameplay.c:1696, states_gameplay.c:1746 |
| CALLED INDIRECTLY | states_gameplay.c:1598 | `state_2E_landing_pick_setup_A3EC` | states_gameplay.c:1697, states_gameplay.c:1747 |
| CALLED INDIRECTLY | states_gameplay.c:1638 | `state_2F_landing_pick_input_A4DE` | states_gameplay.c:1698, states_gameplay.c:1748 |

### states_late.c (15 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | states_late.c:134 | `state_30_results_screen_setup_AF9A` | states_late.c:624 |
| CALLED INDIRECTLY | states_late.c:191 | `state_31_results_screen_zoom_B060` | states_late.c:625 |
| CALLED INDIRECTLY | states_late.c:215 | `state_32_queen_died_setup_AD6A` | states_late.c:626 |
| CALLED INDIRECTLY | states_late.c:272 | `state_33_queen_died_slide_in_AE33` | states_late.c:627 |
| CALLED INDIRECTLY | states_late.c:295 | `state_34_ant_info_setup_B36D` | states_late.c:628 |
| CALLED INDIRECTLY | states_late.c:317 | `state_35_ant_info_exit_B3CB` | states_late.c:629 |
| CALLED INDIRECTLY | states_late.c:339 | `state_36_credits_setup_B535` | states_late.c:630 |
| CALLED INDIRECTLY | states_late.c:374 | `state_37_credits_page1_B5F7` | states_late.c:631 |
| CALLED INDIRECTLY | states_late.c:398 | `state_38_credits_page2_setup_B612` | states_late.c:632 |
| CALLED INDIRECTLY | states_late.c:427 | `state_39_credits_page2_layout_B695` | states_late.c:633 |
| CALLED INDIRECTLY | states_late.c:467 | `state_3A_credit_card_setup_B6B0` | states_late.c:634 |
| CALLED INDIRECTLY | states_late.c:485 | `state_3B_credit_card_exit_to_40_B6CC` | states_late.c:635 |
| CALLED INDIRECTLY | states_late.c:503 | `state_3C_credit_card_with_apu_B6E9` | states_late.c:636 |
| CALLED INDIRECTLY | states_late.c:527 | `state_3D_credit_card_exit_to_16_B72E` | states_late.c:637 |
| CALLED INDIRECTLY | states_late.c:552 | `state_3E_big_credits_setup_B7AC` | states_late.c:638 |

### states_menu.c (10 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | states_menu.c:436 | `gs_full_game_ACF3` | states_menu.c:712, states_menu.c:728 |
| CALLED INDIRECTLY | states_menu.c:466 | `gs_scenario_game_AD5B` | states_menu.c:713, states_menu.c:729 |
| CALLED INDIRECTLY | states_menu.c:490 | `gs_saved_game_AC63` | states_menu.c:714, states_menu.c:730 |
| CALLED INDIRECTLY | states_menu.c:529 | `gs_tutorial_ACE8` | states_menu.c:715, states_menu.c:731 |
| CALLED INDIRECTLY | states_menu.c:549 | `gs_ant_information_B155` | audio_intro.c:217, states_menu.c:716, states_menu.c:732 |
| CALLED INDIRECTLY | states_menu.c:571 | `gs_marriage_flight_B18C` | states_menu.c:717, states_menu.c:733 |
| CALLED INDIRECTLY | states_menu.c:597 | `gs_full_end_B07B` | states_menu.c:718, states_menu.c:734 |
| CALLED INDIRECTLY | states_menu.c:646 | `gs_scenario_end_B0FC` | states_menu.c:719, states_menu.c:735 |
| CALLED INDIRECTLY | states_menu.c:681 | `gs_game_over_B19F` | states_menu.c:720, states_menu.c:736 |
| CALLED INDIRECTLY | states_menu.c:697 | `gs_sound_B1BF` | states_menu.c:721, states_menu.c:737 |

### text_screens.c (7 no-direct-call)

| Category | Location | Name | First indirect refs |
|---|---|---|---|
| CALLED INDIRECTLY | text_screens.c:247 | `enc_pick_exit_A511` | text_screens.c:294, text_screens.c:725 |
| CALLED INDIRECTLY | text_screens.c:257 | `enc_pick_next_A590` | text_screens.c:295, text_screens.c:726 |
| CALLED INDIRECTLY | text_screens.c:276 | `enc_pick_prev_A5B0` | text_screens.c:296, text_screens.c:727 |
| CALLED INDIRECTLY | text_screens.c:531 | `encyclopedia_input_loop_A4DE` | text_screens.c:712 |
| CALLED INDIRECTLY | text_screens.c:596 | `tutorial_paint_E2A2` | text_screens.c:728 |
| CALLED INDIRECTLY | text_screens.c:615 | `in_game_hint_E280` | text_screens.c:729 |
| CALLED INDIRECTLY | text_screens.c:632 | `view_change_message_E28D` | text_screens.c:730 |

## TRULY DEAD (7) — recommended deletion candidates

- `__unused_anchor` at audio_driver.c:1520 (total `name(` = 1, defs = 1, fwd = 0)
- `commit_song_y_0D34` at audio_driver.c:1693 (total `name(` = 3, defs = 1, fwd = 2)
- `view_tail_landing_A7AD` at render_helpers.c:302 (total `name(` = 2, defs = 1, fwd = 1)
- `wram_at` at save_options.c:125 (total `name(` = 1, defs = 1, fwd = 0)
- `bank3_call_save_full` at save_options.c:159 (total `name(` = 1, defs = 1, fwd = 0)
- `bank3_call_save_scenario` at save_options.c:161 (total `name(` = 1, defs = 1, fwd = 0)
- `bank3_call_post_load` at save_options.c:164 (total `name(` = 1, defs = 1, fwd = 0)

## TEST-ONLY (0)


## CALLED INDIRECTLY (300)

These have no direct calls but are referenced as function-pointer table entries, casts, or address-of. See per-file breakdown above for ref locations. Keep them.

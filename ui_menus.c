/*
 * SimAnt — ICON MENU + EVALUATION SCREENS
 *
 * WIKI: see wiki/11-house-screen-ui.md for the House Screen + Population
 *       Graph render pipeline (state $0C dispatcher, type-0x35 entity at
 *       $04:BD9B, and the iso-bar transform at $00:DCC1).
 *
 * =========================================================================
 * Faithful structural C reconstruction of the two UI subsystems that drive
 * the in-game user interface:
 *
 *   1.  The IN-GAME ICON MENU at the top of every gameplay view
 *       (manual p.9, 24): six icons — View / Scent Display / Control Panel
 *       / Save+Exit / Evaluation / Options — each opens a labelled popup
 *       submenu on click and dispatches the player's choice.
 *
 *   2.  The EVALUATION SCREENS (manual p.29-32): the three dashboards
 *       reached through the Evaluation icon — House (the 49-area map),
 *       History Graph (10 selectable time-series metrics), and Status
 *       (six aggregate colony percentages).
 *
 * Lifted from the 65816 disassembly. ROM-side specifics are documented
 * inline; the C exists for readability + porting and is intentionally
 * "structural" (no MMIO, no real entity system — it shares `wram` /
 * `dp` with the rest of the decomp).
 *
 *  - File companions:
 *      entities_d.c            type 29 (popup dispatcher) body
 *      states_gameplay.c       the per-view run loops that call the
 *                              icon-click dispatcher (sub_A734)
 *      simant.c                Entity struct + dp layout + reset/NMI
 *
 * Conventions:
 *   - extern uint8_t  wram[0x20000];
 *   - #define dp   wram   (direct page = wram[0..0xFF])
 *   - Bank $7F maps to wram[0x10000..0x1FFFF].
 *   - 16-bit reads via (*(uint16_t *)&wram[X]).
 *
 * =========================================================================
 */

#include <stdint.h>
#include <stddef.h>

extern uint8_t wram[0x20000];
#define dp wram

/* ------------------------------------------------------------------------ */
/* Convenience accessors                                                    */
/* ------------------------------------------------------------------------ */
#define BYTE(base, off)  (*(uint8_t  *)&(base)[(off)])
#define WORD(base, off)  (*(uint16_t *)&(base)[(off)])
#define LONG(base, off)  ((*(uint32_t *)&(base)[(off)]) & 0x00FFFFFF)

/* ========================================================================
 * 1. ICON MENU LAYOUT
 * ========================================================================
 *
 * The toolbar lives at the top edge of every gameplay view. It is built
 * from entity sprites spawned by `sub_C3B7` ($00:C3B7) when the player
 * enters a view.  All six icons share entity type $32 (50 decimal) — a
 * "stationary sprite" handler at $04:BB74 whose two states are:
 *
 *   state 0  init    — copy entity.pos into entity.scratch[$07..$0A],
 *                      set init_attr=$18, advance to state 1.
 *   state 1  redraw  — re-snap entity.pos = saved + BG-scroll, call the
 *                      generic 16x16 composite drawer ($04:DB52).
 *
 * Spawn coords (X, Y) and per-icon variant byte stored at entity[$0E]:
 *   sub_C3B7 (vertical column, called by state $1D Surface OV setup):
 *     (24,  24) variant $02   -- View icon
 *     (24,  40) variant $04   -- Scent Display icon
 *     (24,  56) variant $06   -- Control Panel icon
 *     (24,  72) variant $08   -- Save / Exit icon
 *     (24,  88) variant $0A   -- Evaluation Screen icon
 *     (24, 104) variant $0C   -- Options icon
 *   sub_C439 (horizontal row, alternate layout for nest closeups):
 *     (24, 16) variant $02 ... (104, 16) variant $0C
 *
 * The variant byte indexes into a palette/tile table to draw the matching
 * pictogram. The toolbar also spawns:
 *   type $2D (45)  cursor-decoration / tooltip   (entities_d.c TYPE 31?)
 *   type $1D (29)  popup framework               (entities_d.c TYPE 29)
 *   type $1E (30)  moving cursor                 (entities_d.c TYPE 30)
 * --------------------------------------------------------------------- */
struct IconSpec {
    uint8_t  x;             /* screen pixel X (0..255)             */
    uint8_t  y;             /* screen pixel Y                      */
    uint8_t  variant;       /* tile/palette selector at entity[$E] */
    uint8_t  magic;         /* opaque "menu id" passed to sub_DFCD */
    const char *label;      /* manual name                          */
};

/* Per-manual mapping of click->magic. The magic codes pass through
 * sub_DFCD ($00:DFCD), which masks LSR×3 ANDed with $FE to pick a 16-bit
 * pointer from the table at $DFDC; the pointer is JMPed.  All icon
 * menus end up at sub_E086 (the "menu open" handler); only message
 * popups go to sub_DFFD. */
static const struct IconSpec icon_menu_vertical[6] = {
    { 24,  24, 0x02, 0x25, "View"             },  /* magic→submenu @ $01:8757 */
    { 24,  40, 0x04, 0x2C, "Scent Display"    },  /* magic→submenu @ $01:87CA */
    { 24,  56, 0x06, 0x2B, "Control Panel"    },  /* magic→submenu @ $01:880A */
    { 24,  72, 0x08, 0x28, "Save / Exit"      },  /* magic→submenu @ $01:881D */
    { 24,  88, 0x0A, 0x29, "Evaluation"       },  /* magic→submenu @ $01:88F9 */
    { 24, 104, 0x0C, 0x2A, "Options"          },  /* magic→submenu @ $01:892D */
};

/* ========================================================================
 * 2. POPUP / DIALOG FRAMEWORK (entity type 29, body in entities_d.c)
 * ------------------------------------------------------------------------
 * Per Agent D's notes, the popup is a 10-state machine owned by the
 * type-29 dispatcher at $04:AD01.  Outer gating triplet:
 *
 *   POPUP_ACTIVE  ($02A7)  bitfield — which icon was clicked this frame.
 *                          Cleared by the post-popup dispatcher (sub_A734)
 *                          after the chosen item has been actioned.
 *   POPUP_LOCK    ($02E1)  inner gate — 1 while popup is opening; 2 once
 *                          the body has finished its init.
 *   POPUP_GOTO_STATE ($02E3) external "force jump" request — consumed by
 *                          the dispatcher on the next frame.
 *
 * The cursor entity (type 2 @ $04:9B9B) reads $02A7; when non-zero it
 * launches the menu render path via $00:9E81 → JSR $C91F (the text
 * renderer).  When $02A7 is zero the cursor runs the normal game flow.
 * --------------------------------------------------------------------- */
#define POPUP_ACTIVE        WORD(dp, 0x02A7)    /* icon-click bitfield */
#define POPUP_LOCK          BYTE(dp, 0x02E1)    /* sequencing latch    */
#define POPUP_GOTO_STATE    BYTE(dp, 0x02E3)    /* force-state request */
#define POPUP_SELECTION     BYTE(dp, 0x02ED)    /* submenu item idx    */
#define POPUP_LOCK_VARIANT  BYTE(dp, 0x02EF)    /* secondary lock      */
#define ICON_ACTION_MODE    BYTE(dp, 0x0299)    /* dispatcher table idx */
#define ICON_SUBSEL         BYTE(dp, 0x024A)    /* submenu post-choice  */

/* ========================================================================
 * 3. SUBMENU DATA TABLES (in ROM, bank $01)
 * ------------------------------------------------------------------------
 *
 * Submenu data has the layout
 *      [FF] [count] [ptr1] [ptr2] ... [str1\xFF] [str2\xFF] ...
 *
 * The leading $FF is a separator/sentinel. `count` is the number of items.
 * Then `count` 16-bit pointers (little-endian, bank $01) followed by each
 * item's ASCII string terminated by $FF.  The character $FE inside a
 * string is "newline" (advance to next text row in the menu box).
 *
 * Found tables (cross-referenced from sub_C91F text renderer in $00:C91F):
 * --------------------------------------------------------------------- */

/* View submenu — 6 items (manual p.8-9). String at $01:8763. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} view_submenu[6] = {
    { 0x8763, "Surface Overview" },
    { 0x8774, "B. Nest Overview" },
    { 0x8785, "R. Nest Overview" },
    { 0x8796, "Surface Close-up" },
    { 0x87A7, "B. Nest Close-up" },
    { 0x87B8, "R. Nest Close-up" },
};

/* Scent Display submenu — 5 items (manual p.26). String at $01:87D4. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} scent_submenu[5] = {
    { 0x87D4, "Hide Scent"  },
    { 0x87DF, "Black Nest"  },
    { 0x87EA, "Red Nest"    },
    { 0x87F3, "Black Trail" },
    { 0x87FF, "Red Trail"   },
};

/* Control Panel submenu — 2 items (manual p.14-16). String at $01:880E. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} control_panel_submenu[2] = {
    { 0x880E, "Behavior" },
    { 0x8817, "Caste"    },
};

/* Save / Exit submenu — 2 items (manual p.5). Strings at $01:8822/$882C. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} save_exit_submenu[2] = {
    { 0x8822, "Save Game" },
    { 0x882C, "Main Menu" },
};

/* Evaluation submenu — 3 items (manual p.29). Strings at $01:88FF/$8907/$890F. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} evaluation_submenu[3] = {
    { 0x88FF, "House"   },
    { 0x8907, "History" },
    { 0x890F, "Status"  },
};

/* Options submenu — 3 items (manual p.33). Strings at $01:8933/8939/893F. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} options_submenu[3] = {
    { 0x8933, "Sound" },
    { 0x8939, "Mouse" },
    { 0x893F, "Speed" },
};

/* Exit-confirmation dialog — 2 items. Strings at $01:88DC/$88E9. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} exit_confirm_dialog[2] = {
    { 0x88DC, "Exit to Menu"   },
    { 0x88E9, "Return to Game" },
};

/* Mouse-speed sub-options — 3 items.  Strings at $01:8968/896F/8976. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} mouse_speed_options[3] = {
    { 0x8968, "Slow"   },
    { 0x896F, "Normal" },
    { 0x8976, "Fast"   },
};

/* Game-speed sub-options — 4 items.  Strings at $01:8980/8986/898D/899B. */
static const struct {
    uint16_t rom_addr;
    const char *label;
} game_speed_options[4] = {
    { 0x8986, "Fast"   },
    { 0x898D, "Normal" },
    { 0x8994, "Slow"   },
    { 0x899B, "Pause"  },
};

/* ========================================================================
 * 4. CLICK DISPATCHER — sub_A734 ($00:A734)
 * ------------------------------------------------------------------------
 *
 * Called as the per-frame "universal tail" by every view-run state
 * (states $1E, $20, $22, $24, $26).  It is a 3-entry jump table keyed by
 * ICON_ACTION_MODE (dp[$0299]) at $00:A740:
 *
 *   [$0299=0]  → $A746   "in-view" dispatch (bit $20 → return to title)
 *   [$0299=1]  → $A755   "view" dispatch (Scent / Save / Eval / Options
 *                         + Control Panel sub-magics — opens submenus)
 *   [$0299=2]  → $A787   "submenu open" dispatch (eval, save UI etc.)
 *
 * Each dispatcher tests bits of POPUP_ACTIVE ($02A7). When a bit is set,
 * it calls sub_DFCD with the magic byte for that icon, which in turn
 * displays the labeled submenu and waits for the player's choice.
 *
 * The chosen submenu item lands in dp[$024A]; sub_A734 then re-runs and
 * decides which game state to jump to (e.g. View submenu choice "Surface
 * Overview" → state $1D + view 0).
 *
 *  Bit-to-icon table (POPUP_ACTIVE):
 *    $01    Scent Display icon clicked      magic $2C
 *    $02    Control Panel icon clicked      magic $2B
 *    $04    (reserved — possibly used in   ?)
 *    $08    Save / Exit icon clicked        magic $28 (or $2A in confirm)
 *    $10    Evaluation icon clicked         magic $25 (or $29)
 *    $20    return to title shortcut        state $16
 *    $40    Options icon clicked            magic $2A
 *    $80    ?
 * --------------------------------------------------------------------- */
extern void sub_DFCD_popup_open(uint8_t magic);
extern uint16_t sub_877D_yield(void);

/* sub_A734 ICON_ACTION_MODE=0 — title-screen / "back to title" handler. */
static int icon_dispatch_view_run_A746(void)
{
    if (POPUP_ACTIVE & 0x0020) {
        dp[0x0B] = 0x16;                /* GS_TITLE: jump back            */
        return 1;                       /* state changed                  */
    }
    return 0;
}

/* sub_A734 ICON_ACTION_MODE=1 — main in-view click dispatcher. Mirrors
 * the $A755 branch exactly. */
static int icon_dispatch_view_run_A755(void)
{
    if (POPUP_ACTIVE & 0x0008) {
        sub_DFCD_popup_open(0x2B);      /* Control Panel submenu          */
        dp[0x0B] = 0x23;                /* state $23 (close-up entry)     */
        return 1;
    }
    if (POPUP_ACTIVE & 0x0010) {
        sub_DFCD_popup_open(0x25);      /* View submenu                   */
        dp[0x0B] = 0x23;
        return 1;
    }
    if (POPUP_ACTIVE & 0x0001) {
        sub_DFCD_popup_open(0x2C);      /* Scent submenu                  */
        dp[0x0B] = 0x23;
        return 1;
    }
    return 0;
}

/* sub_A734 ICON_ACTION_MODE=2 — submenu-confirmation dispatcher. */
static int icon_dispatch_view_run_A787(void)
{
    if (POPUP_ACTIVE & 0x0010) {
        if (POPUP_SELECTION < 2) {
            sub_DFCD_popup_open(0x28);  /* Save Game submenu              */
            dp[0x0B] = 0x23;
            return 1;
        }
        /* Selection >= 2 — confirm dialog (Exit to Menu / Return to Game) */
        sub_DFCD_popup_open(0x2A);
        dp[0x0B] = 0x28;
        WORD(dp, 0x0054)         = 1;          /* "exit requested"        */
        WORD(wram, 0x1E744)      = 1;          /* persistent shadow flag  */
        POPUP_ACTIVE             = 0;
        return 1;
    }
    if (POPUP_LOCK_VARIANT == 0 && POPUP_SELECTION == 0x31) {
        sub_DFCD_popup_open(0x29);      /* Evaluation submenu             */
        dp[0x0B] = 0x23;
        return 1;
    }
    return 0;
}

/* Public entrypoint for the tail.  Re-implements the indirect-jump
 * dispatch at $A734-A73D. */
void icon_click_dispatch_A734(void)
{
    switch (ICON_ACTION_MODE) {
    case 0: icon_dispatch_view_run_A746(); break;
    case 1: icon_dispatch_view_run_A755(); break;
    case 2: icon_dispatch_view_run_A787(); break;
    default: break;
    }
}

/* sub_DFCD ($00:DFCD) — magic-to-handler dispatcher.
 *
 *  ROM at $00:DFCD:
 *     STA $72; XBA; LDA #$00; XBA      ; A 16-bit = magic & $00FF
 *     LSR LSR LSR                      ; A = magic >> 3
 *     AND #$FE                         ; X = (magic >> 3) & $FE
 *     TAX
 *     JMP ($DFDC,x)                    ; opcode $7C — indexed indirect
 *
 *  Table at $DFDC (16-bit pointers, X-stride 2 means 16 magics per entry):
 *     X=0 (magic $00-$0F) -> $E342  ("back to title" handler)
 *     X=2 (magic $10-$1F) -> $E342
 *     X=4 (magic $20-$2F) -> $DFFD  (popup w/ msg box + SFX)
 *     X=6 (magic $30-$3F) -> $DFFD
 *     X=8 (magic $40-$4F) -> $E086  (submenu opener — the actual icon menu)
 *     X=A (magic $50-$5F) -> $DFFC  ($DFFC is a tiny "RTS-only" stub)
 *     ...
 *  So icon-menu magics in $20-$2F land at $DFFD, NOT $E086. $E086 is
 *  reached only via magics $40-$4F. */
static const struct {
    uint8_t  magic_lo;     /* low end of 16-magic range */
    uint8_t  magic_hi;
    const char *role;
    uint16_t handler;      /* $00:???? */
} popup_open_table[] = {
    { 0x00, 0x1F, "back to title (E342)",            0xE342 },
    { 0x20, 0x3F, "DFFD popup (msg box + SFX)",      0xDFFD },
    { 0x40, 0x4F, "E086 submenu opener (icon menu)", 0xE086 },
    { 0x50, 0x5F, "DFFC stub",                       0xDFFC },
    { 0x60, 0x6F, "E1AA secondary popup",            0xE1AA },
    { 0x70, 0x9F, "DFFC stub",                       0xDFFC },
    { 0xA0, 0xBF, "E2A2 status popup",               0xE2A2 },
};

/* sub_E086 ($00:E086) — submenu opener.
 *
 * Steps:
 *   1. Save dp[$2E] (OAM index) into dp[$8C].
 *   2. yield (sub_877D).
 *   3. magic & $0F → dp[$004E] (which submenu within the icon).
 *   4. Read 16-bit pointer table at $E18E,y → push (SFX command).
 *   5. Read 16-bit pointer table at $E19C,y → Y register (text addr).
 *   6. JSL $008EA6 (play SFX from A).
 *   7. If dp[$004F] != 0 (asset-load path):
 *        - run sub_E0B6 sub-load (more LZSS calls)
 *        - call sub_C91F with X=$0005 / A=$16 (menu-box render)
 *      else (popup-only path):
 *        - call sub_C91F with X=$0009 / A=$0E
 *   8. After render: if dp[$0B] is in [$1D..$22] (view-run states), set
 *      dp[$09] = 5 (BG-mix counter) and call sub_C6B0 (BG mix).
 *   9. JSR $8804 (input/animate); clear dp[$004E].
 */
extern void sub_C91F_text_render(uint16_t y_textptr, uint8_t x_size, uint8_t color);
extern void sub_8804_input_animate(void);

static const uint16_t submenu_text_ptr[8] = {
    /* 0 */ 0x9E59,    /* shared count info? */
    /* 1 */ 0x9E59,
    /* 2 */ 0xB862,    /* B.Nest popup */
    /* 3 */ 0xB862,
    /* 4 */ 0xCD29,    /* R.Nest popup */
    /* 5 */ 0x0000,
    /* 6 */ 0x0000,
    /* 7 */ 0x0000,
};

void submenu_open_E086(uint8_t magic)
{
    uint8_t  sub = magic & 0x1F;
    BYTE(dp, 0x004E) = (uint8_t)(magic & 0x0F);
    (void)submenu_text_ptr;
    /* Real: read 8-bit SFX byte from $E18E[y] and play via $008EA6. */
    /* Then read 16-bit text ptr from $E19C[y] and pass to C91F. */
    sub_877D_yield();
    if (BYTE(dp, 0x004F) != 0) {
        /* Asset-load branch (real LZSS calls omitted here). */
        sub_C91F_text_render(/*ptr=*/0x0000, /*size=*/5, /*color=*/0x16);
    } else {
        sub_C91F_text_render(/*ptr=*/0x0000, /*size=*/9, /*color=*/0x0E);
    }

    /* If we landed inside a view-run state, kick the BG-mix counter. */
    if (dp[0x0B] >= 0x1D && dp[0x0B] < 0x22) {
        dp[0x09] = 5;
        /* sub_C6B0(); — bg mix */
    }
    sub_8804_input_animate();

    /* Special: if we're returning from View submenu and the choice was
     * "no change", revert dp[$0B] from $20 back to $1F. */
    if (dp[0x0B] == 0x20) {
        BYTE(dp, 0x004E) = sub;
        /* if $E19C[sub] == 0 → dp[$0B] = $1F */
    }
    BYTE(dp, 0x004E) = 0xFF;
}

/* sub_C91F ($00:C91F) — menu/text box renderer.
 *
 * Input:  Y = 16-bit pointer to text-with-control-bytes
 *         X = number of rows (size) [actually it shadows in dp[$73..$77]]
 *         A = base palette/color byte (stored in dp[$8C])
 *
 * Reads bytes from [$79..$7B] (24-bit), writing them into the VRAM update
 * queue at $0C00..  Per-byte rules:
 *   $FE  newline — advance row; bump dp[$74]; recompute base
 *   $FF  end-of-menu — store $FFFF terminator; bump dp[$2C]; return
 *   else write byte to $0C00,X then write color (dp[$8C]) to $0C00,X+1
 *
 * The renderer is the same engine used by all in-game popup boxes (View,
 * Scent, Save, Control Panel, Evaluation, Options) — only the input
 * pointer differs.  Every submenu data table starts with a leading $FF
 * count byte plus a `count`-entry 16-bit pointer table so the renderer
 * can iterate "all items" when the menu is first shown.
 */
void sub_C91F_text_render(uint16_t y_textptr, uint8_t x_size, uint8_t color)
{
    /* Documentation-only — the real call sequence is in entities_d.c. */
    (void)y_textptr; (void)x_size; (void)color;
}

/* ========================================================================
 * 5. EVALUATION SCREEN — HOUSE (manual p.30-31)
 * ------------------------------------------------------------------------
 *
 * The Full-Game map: 49 area tiles (7x7 grid) showing colony presence.
 * Manual encoding:
 *   green       empty area (no colony there)
 *   black       B-colony present
 *   red         R-colony present
 *   striped     both B and R present
 *   flashing    current area (where the player is now)
 *
 * Per Agent F's notes, the Full-Game world state lives in WRAM bank
 * $7F.  The byte-by-byte layout discovered during this lift:
 *
 *   $7F:E87E    AREA_COUNT       number of populated areas (1..49)
 *   $7F:E880    AREA_LAST_IDX    index of "current" area
 *   $7F:E882    AREA_X_TABLE[]   12 bytes — per-area screen X (icon pos)
 *   $7F:E88E    AREA_Y_TABLE[]   12 bytes — per-area screen Y
 *   $7F:E89A    AREA_FLAGS_A[]   12 bytes — per-area scratch (?)
 *   $7F:E8A6    AREA_FLAGS_B[]   12 bytes — per-area scratch (?)
 *   $7F:E8B2    AREA_STATE[]     12 bytes — per-area occupant state
 *
 * IMPORTANT: the 12-byte arrays correspond to the LIVE areas, not the
 * full 49-area grid.  The full 49-area world map is stored elsewhere
 * (likely $7F:EE8A onwards — the 78-byte buffer state $1A loads from
 * ROM at boot using the per-view pointer table at $01:81D3).
 *
 * Per-area state byte encoding (read at $04:BDC1 from $7F:E8B2):
 *   0x00     EMPTY    (green tile)
 *   0x01     BLACK    (B-colony established)
 *   0x02     RED      (R-colony established)
 *   0x03     STRIPED  (both colonies present)
 *   bit 7+   FLASHING (current area — animated by clock)
 *
 * Each area also has X/Y on the 7x7 map.  These are populated by
 * sub_03_96B0 ($03:96B0) when a new area transition happens — it appends
 * a new entry, INCing AREA_COUNT (with limit check CMP #$05 ... so the
 * runtime limit seen is 5 areas tracked simultaneously, perhaps the
 * visible 5×5 sub-region; the full 49-area master grid is elsewhere).
 *
 * The drawing entity is type 53 ($35) at $04:BD9B / $04:BDAD.  Per area:
 *   x_pixel = AREA_X_TABLE[i] * 16     (multiply ASL ASL ASL ASL)
 *   y_pixel = AREA_Y_TABLE[i] * 16
 *   tile    = rom_04_BE41[state]       (8-entry colour-coded sprite)
 *   --- draws 4 sprites per area (16x16 composite at 4 quarter-positions
 *       — tiles 0x95, 0x20, 0x42, 0x24 at base, +offset on each axis)
 * --------------------------------------------------------------------- */
#define AREA_COUNT          BYTE(wram, 0x1E87E)
#define AREA_LAST_IDX       BYTE(wram, 0x1E880)
#define AREA_X_TABLE        (&wram[0x1E882])    /* uint8_t [12] */
#define AREA_Y_TABLE        (&wram[0x1E88E])    /* uint8_t [12] */
#define AREA_FLAGS_A        (&wram[0x1E89A])    /* uint8_t [12] */
#define AREA_FLAGS_B        (&wram[0x1E8A6])    /* uint8_t [12] */
#define AREA_STATE          (&wram[0x1E8B2])    /* uint8_t [12] */

/* Per-area state byte values (manual p.30): */
enum AreaState {
    AREA_EMPTY    = 0x00,
    AREA_BLACK    = 0x01,
    AREA_RED      = 0x02,
    AREA_STRIPED  = 0x03,
    AREA_FLASH    = 0x80,    /* OR'd in for "current area"          */
};

/* sub_03_96B0 — append a new area entry. Called when player transitions
 * to a new area. dp[$F0D3]/dp[$F0D5] hold the X/Y for the new area. */
extern uint8_t rom_F0D3_new_X(void);
extern uint8_t rom_F0D5_new_Y(void);

/* WIKI: wiki/11-house-screen-ui.md §3 (live-area display slots). */
void house_area_append_039_6B0(void)
{
    uint8_t i = AREA_COUNT;
    if (i >= 5) return;                  /* limit at 5 simultaneous     */
    AREA_X_TABLE [i] = rom_F0D3_new_X(); /* new X coord                  */
    AREA_Y_TABLE [i] = rom_F0D5_new_Y(); /* new Y coord                  */
    AREA_FLAGS_A [i] = 0;
    AREA_FLAGS_B [i] = 0;
    AREA_STATE   [i] = AREA_EMPTY;       /* will be set by colony entry  */
    AREA_COUNT       = i + 1;
}

/* House Screen renderer — runs each frame while the House screen is up.
 * Iterates the 5-entry live area list IN REVERSE so the "current" area
 * (highest index) draws on top.  Each area is rendered as a 32x32-pixel
 * composite of 4 sprites at fixed sub-positions (matches the 7x7 grid
 * with 32px cells in the manual screenshot).
 *
 * Per-area color/state tile table at ROM $04:BE41 (8 entries):
 *   state 0 → tile 0x95 (green/empty)
 *   state 1 → tile 0xXX (black filled)
 *   state 2 → tile 0xXX (red filled)
 *   state 3 → tile 0xXX (striped both)
 *   state 4 → tile 0xXX (rare — maybe "newly settled")
 *   state 5..7 → animation/flash
 */
extern void sub_DB9E_sprite_draw(void);
extern const uint8_t rom_04_BE41_area_tile[8];  /* per-state base tile  */

/* WIKI: wiki/11-house-screen-ui.md §3-4 (renderer + flashing overlay). */
void house_screen_render_04_BD9B(void)
{
    int8_t i = (int8_t)AREA_COUNT - 1;
    while (i >= 0) {
        uint16_t px = (uint16_t)AREA_X_TABLE[i] << 4;
        uint16_t py = (uint16_t)AREA_Y_TABLE[i] << 4;
        uint8_t  st = AREA_STATE[i] & 7;       /* state index */

        BYTE(dp, 0x37) = (uint8_t)px;
        BYTE(dp, 0x39) = (uint8_t)py;

        /* Draw the 4-sprite composite at (px, py), (px+16, py), (px,
         * py+16), (px+16, py+16) — manual screenshot shows each area as
         * a 32x32 tile in the 7x7 map. */
        uint8_t tile;
        tile = rom_04_BE41_area_tile[st];
        if (tile != 0x42) {
            BYTE(dp, 0x3B) = tile;
            sub_DB9E_sprite_draw();
        }
        BYTE(dp, 0x3B) = 0x42; BYTE(dp, 0x3D) = 0x95;
        sub_DB9E_sprite_draw();
        BYTE(dp, 0x37) = (uint8_t)(px - 16);
        BYTE(dp, 0x39) = (uint8_t)(py - 16);
        BYTE(dp, 0x3B) = 0x20;
        sub_DB9E_sprite_draw();
        BYTE(dp, 0x3D) = 0x15;
        BYTE(dp, 0x37) = (uint8_t)(px + 16);
        BYTE(dp, 0x3B) = 0x24;
        sub_DB9E_sprite_draw();
        BYTE(dp, 0x37) = (uint8_t)(px - 16);
        BYTE(dp, 0x39) = (uint8_t)(py + 16);
        BYTE(dp, 0x3B) = 0x60;
        sub_DB9E_sprite_draw();

        i -= 2;   /* original does DEX DEX (2-byte advance — words?) */
    }
}

/* Bottom-of-screen 3-icon navigation bar (manual p.30):  House icon,
 * Population Graph icon, Exit icon. */
struct EvalNavIcon {
    uint8_t x, y;
    uint8_t variant;
    const char *label;
};
static const struct EvalNavIcon eval_nav_icons[3] = {
    {  80, 200, 0x02, "House"            },
    { 128, 200, 0x04, "Population Graph" },
    { 176, 200, 0x06, "EXIT"             },
};

/* The B.Area / R.Area tally lives at the top-right corner of the screen.
 * Computed as: count of areas with state==1 or 3 → B.Area; count with
 * state==2 or 3 → R.Area.  These numbers are 2-digit (printed via the
 * standard digit-sprite chain).
 *
 * Mating Flight trigger (manual p.19): pop_b >= 100 AND breeders_b >= 20
 *   (probably read from per-colony counters in $7F:EE??)
 * Mass Exodus trigger (manual p.20): pop_b >= 250
 */
struct HouseTally {
    uint8_t b_area;
    uint8_t r_area;
};
/* WIKI: wiki/11-house-screen-ui.md §7 ("House Tally Headers"). */
struct HouseTally house_compute_tallies(void)
{
    struct HouseTally t = { 0, 0 };
    for (uint8_t i = 0; i < AREA_COUNT; ++i) {
        uint8_t s = AREA_STATE[i] & 3;
        if (s == AREA_BLACK   || s == AREA_STRIPED) t.b_area++;
        if (s == AREA_RED     || s == AREA_STRIPED) t.r_area++;
    }
    return t;
}

/* ========================================================================
 * 6. EVALUATION SCREEN — HISTORY GRAPH (manual p.31)
 * ------------------------------------------------------------------------
 *
 * A time-series line graph over the colony's lifetime.  Up to 4 metrics
 * can be selected simultaneously.  All 10 metric labels are stored
 * contiguously in ROM at $01:9BAC:
 *
 *   $01:9BAC   "B.Pop "       Black colony total population
 *   $01:9BB3   "R.Pop "       Red colony total population
 *   $01:9BBA   "B.Food"       Black colony food held
 *   $01:9BC1   "R.Food"       Red colony food held
 *   $01:9BC8   "B.Hlth"       Black colony average health
 *   $01:9BCF   "R.Hlth"       Red colony average health
 *   $01:9BD6   "Food  "       total food in current area
 *   $01:9BDD   "Eaten "       running total food eaten
 *   $01:9BE4   "Starve"       running total ants died of starvation
 *   $01:9BEB   "Killed"       running total ants killed in combat
 *   $01:9BF9   "EXIT  "       return-to-evaluation button
 *
 * Each metric has its own circular buffer in WRAM.  Per the read pattern
 * in the graph drawing code ($02:D99D), the buffers are at:
 *
 *   $7F:EF95   16-bit current snapshot of metric "B.Pop"
 *   $7F:EF99   per-metric pointer-A (the buffer head, advanced)
 *   $7F:EF9B   per-metric pointer-B (the buffer tail, fixed)
 *   $7F:EFA3   accumulator (smoothed reading)
 *   $7F:E784   per-metric x-offset (when drawing, the next column)
 *
 * The actual sample buffer is at $00:D770 in ROM-resident space — the
 * graph drawing code does `STA $D770,X` once per game tick (probably 128
 * entries per metric × 10 metrics = 1.25 KB).  Index X is a free-running
 * sample counter, wraps modulo buffer size.
 *
 * Sampling cadence: looks like once per second (1 sample per 60 NMI
 * frames) — keyed to dp[$02] (clock-seconds rollover).
 *
 * Display layout per metric:
 *   y_pixel = metric_baseline_y - (sample * y_scale)
 *   x_pixel = (sample_index * x_scale) + graph_origin_x
 *
 * The 4 active metrics are selected via the menu (selection bytes stored
 * at $7F:EF65..EF68 perhaps).  The "EXIT" button at $9BF9 closes the
 * graph and returns to the Evaluation submenu.
 * --------------------------------------------------------------------- */
enum HistoryMetric {
    METRIC_B_POP = 0,  METRIC_R_POP = 1,
    METRIC_B_FOOD = 2, METRIC_R_FOOD = 3,
    METRIC_B_HLTH = 4, METRIC_R_HLTH = 5,
    METRIC_FOOD   = 6, METRIC_EATEN  = 7,
    METRIC_STARVE = 8, METRIC_KILLED = 9,
    METRIC_COUNT  = 10,
};

static const struct {
    uint16_t rom_label_addr;
    const char *name;
} history_metric_labels[10] = {
    { 0x9BAC, "B.Pop"  },
    { 0x9BB3, "R.Pop"  },
    { 0x9BBA, "B.Food" },
    { 0x9BC1, "R.Food" },
    { 0x9BC8, "B.Hlth" },
    { 0x9BCF, "R.Hlth" },
    { 0x9BD6, "Food"   },
    { 0x9BDD, "Eaten"  },
    { 0x9BE4, "Starve" },
    { 0x9BEB, "Killed" },
};

/* History Graph buffer addresses.
 * V3-B + final cleanup verification: long-mode reads at ROM $00:D4F1
 * (`LDA $7FF6D5`, `LDA $7FF6D3`) and `ADC #$F6D7` + `LDA #$7F STA $1E`
 * unambiguously place the History Graph buffer at $7F:F6D7 = wram offset
 * 0x1F6D7. The previous lift used 0x1D770 ($7F:D770), which is actually
 * an entity-array region (D388/D57C/D770/DB58/D964 at $02:8169). */
#define HG_SAMPLES_PER_METRIC   64
#define HG_BUF_BASE             0x1F6D7     /* $7F:F6D7 verified at $00:D4F1 */

#define HG_CUR_SNAPSHOT(m)      (*(uint16_t *)&wram[0x1EF95 + (m)*8])
#define HG_HEAD_PTR(m)          (*(uint16_t *)&wram[0x1EF99 + (m)*8])
#define HG_TAIL_PTR(m)          (*(uint16_t *)&wram[0x1EF9B + (m)*8])
#define HG_X_OFFSET(m)          BYTE(wram, 0x1E784 + (m))

/* Active-metric selection (4 of 10). Each entry holds a HistoryMetric
 * code or 0xFF for "no metric in this slot". */
#define HG_ACTIVE_SLOT(i)       BYTE(wram, 0x1EF65 + (i))

/* Append a new sample for `metric` — called once per game-tick by the
 * simulation step. */
void history_graph_record_sample(uint8_t metric, uint16_t value)
{
    if (metric >= METRIC_COUNT) return;
    uint16_t head = HG_HEAD_PTR(metric);
    uint16_t off  = HG_BUF_BASE + metric * HG_SAMPLES_PER_METRIC + head;
    /* Truncate to fit one byte per sample (the manual graph has ~64 px
     * vertical resolution). */
    wram[off] = (uint8_t)(value >> 4);
    head = (head + 1) % HG_SAMPLES_PER_METRIC;
    HG_HEAD_PTR(metric)  = head;
    HG_CUR_SNAPSHOT(metric) = value;
}

/* Render the history graph — called from the History screen state. */
extern void history_graph_render(void)
{
    for (uint8_t slot = 0; slot < 4; ++slot) {
        uint8_t m = HG_ACTIVE_SLOT(slot);
        if (m >= METRIC_COUNT) continue;
        uint16_t head = HG_HEAD_PTR(m);
        for (uint8_t i = 0; i < HG_SAMPLES_PER_METRIC; ++i) {
            uint16_t idx    = (head + i) % HG_SAMPLES_PER_METRIC;
            uint8_t  sample = wram[HG_BUF_BASE + m * HG_SAMPLES_PER_METRIC + idx];
            uint16_t px     = 32 + i * 3;                  /* x stride */
            uint16_t py     = 184 - (uint16_t)sample;      /* invert y */
            /* sub_DB9E plot tile at (px, py) with slot-coloured palette */
            (void)px; (void)py;
        }
    }
}

/* ========================================================================
 * 7. EVALUATION SCREEN — STATUS (manual p.32)
 * ------------------------------------------------------------------------
 *
 * Six percentage readouts.  Header strings at $01:A0D8:
 *   "B.Ants" / "R.Ants" / "Pupae" / "Worker" / "Soldier" / "Breeder"
 *
 * The six stats (manual p.32, computed from colony counters):
 *
 *   colony_health    %  =  100 * sum(ant.health) / (count * MAX_HLTH)
 *   foraging         %  =  100 * (ants_foraging / total_ants)
 *   eggs_hatched     %  =  100 * (eggs_hatched / eggs_laid)
 *   fights_won       %  =  100 * (combats_won / combats_total)
 *   b_ant_occupation %  =  100 * (b_area_count / 49)
 *   r_ant_occupation %  =  100 * (r_area_count / 49)
 *
 * The counters that drive these percentages are accumulated by the
 * simulation tick.  Probable storage:
 *
 *   $7F:EE...   per-colony tallies (population, breeders, food)
 *   $7F:EF...   history-buffer parking
 *
 * Specifically (best guess pending Agent I "running" lift):
 *   $7F:EE62   "world initialised" flag (set by state $1A)
 *   $7F:EE64   black population (16-bit)
 *   $7F:EE66   red population
 *   $7F:EE68   black food
 *   $7F:EE6A   red food
 *   $7F:EE6C   black queen alive flag
 *   $7F:EE6E   red queen alive flag
 *   $7F:EE70   running food-eaten total
 *   $7F:EE72   running food-starved total
 *   $7F:EE74   running ants-killed-in-combat total
 *   $7F:EE76   running combat tally
 *   $7F:EE78   running combat-won (B colony) tally
 *   $7F:EE7A   running egg-laid total
 *   $7F:EE7C   running egg-hatched total
 *   $7F:EE7E   ants-currently-foraging counter (b)
 *   $7F:EE80   ants-currently-foraging counter (r)
 *
 * NOTE: the addresses above are inferred from naming consistency with
 * the History Graph metric layout.  Agent I should reconcile.
 * --------------------------------------------------------------------- */
#define COLONY_B_POP            (*(uint16_t *)&wram[0x1EE64])
#define COLONY_R_POP            (*(uint16_t *)&wram[0x1EE66])
#define COLONY_B_FOOD           (*(uint16_t *)&wram[0x1EE68])
#define COLONY_R_FOOD           (*(uint16_t *)&wram[0x1EE6A])
#define COLONY_B_QUEEN_ALIVE     BYTE(wram, 0x1EE6C)
#define COLONY_R_QUEEN_ALIVE     BYTE(wram, 0x1EE6E)

#define STATS_FOOD_EATEN        (*(uint16_t *)&wram[0x1EE70])
#define STATS_STARVED           (*(uint16_t *)&wram[0x1EE72])
#define STATS_KILLED            (*(uint16_t *)&wram[0x1EE74])
#define STATS_COMBATS_TOTAL     (*(uint16_t *)&wram[0x1EE76])
#define STATS_COMBATS_B_WON     (*(uint16_t *)&wram[0x1EE78])
#define STATS_EGGS_LAID         (*(uint16_t *)&wram[0x1EE7A])
#define STATS_EGGS_HATCHED      (*(uint16_t *)&wram[0x1EE7C])
#define STATS_FORAGING_B         BYTE(wram, 0x1EE7E)
#define STATS_FORAGING_R         BYTE(wram, 0x1EE80)

#define COLONY_AVG_HEALTH_B     (*(uint16_t *)&wram[0x1EE82])
#define COLONY_AVG_HEALTH_R     (*(uint16_t *)&wram[0x1EE84])

struct StatusPercentages {
    uint8_t  colony_health_pct;
    uint8_t  foraging_pct;
    uint8_t  eggs_hatched_pct;
    uint8_t  fights_won_pct;
    uint8_t  b_ant_occupation_pct;
    uint8_t  r_ant_occupation_pct;
};

static uint8_t pct(uint16_t numerator, uint16_t denominator)
{
    if (denominator == 0) return 0;
    uint32_t n100 = (uint32_t)numerator * 100u;
    uint32_t q    = n100 / denominator;
    if (q > 100u) q = 100u;
    return (uint8_t)q;
}

void status_screen_compute(struct StatusPercentages *out)
{
    /* Colony Health % (manual p.32 — average health across all ants).
     * Per-colony averages are pre-computed by the per-frame tick at
     * $7F:EE82/EE84; here we average black and red. */
    uint16_t h = (uint16_t)((COLONY_AVG_HEALTH_B + COLONY_AVG_HEALTH_R) >> 1);
    out->colony_health_pct = pct(h, 0xFF);

    /* Foraging % — count of ants currently in "foraging" task vs total. */
    uint16_t total_pop = COLONY_B_POP + COLONY_R_POP;
    out->foraging_pct  = pct((uint16_t)(STATS_FORAGING_B + STATS_FORAGING_R),
                             total_pop);

    /* Eggs Hatched % */
    out->eggs_hatched_pct = pct(STATS_EGGS_HATCHED, STATS_EGGS_LAID);

    /* Fights Won % — from the perspective of the player's colony (Black). */
    out->fights_won_pct = pct(STATS_COMBATS_B_WON, STATS_COMBATS_TOTAL);

    /* B.Ant occupation % — fraction of 49 areas controlled by Black. */
    struct HouseTally tally = house_compute_tallies();
    out->b_ant_occupation_pct = pct(tally.b_area, 49);
    out->r_ant_occupation_pct = pct(tally.r_area, 49);
}

/* ========================================================================
 * 8. PER-EVALUATION-SCREEN STATE HANDLERS
 * ------------------------------------------------------------------------
 *
 * Per the dispatch table in states_gameplay.c, states $0E..$15 are
 * currently lifted as "marriage flight / ant info / map / bug cutin"
 * but their addresses ($00:B2B0, $B352, $B47C, $B490, $B3D8, $B45D,
 * $B4BA, $B4DA) are reused for the THREE evaluation screens after the
 * marriage flight cinematic is over.  This file documents the
 * Evaluation-side handlers as separate logical entries; the actual
 * state-table indices in WRAM-execution may end up being:
 *
 *   State idx       Address          Role
 *   $0E             $00:B2B0         (also House Screen setup)
 *   $0F             $00:B352         (also History Graph setup)
 *   $10             $00:B47C         (also Status Screen setup)
 *
 * Wiring confirmation pending — the dispatch table dual-uses these
 * slots based on dp[$0299] (game mode).
 *
 * --------------------------------------------------------------------- */
extern void apu_silence(void);
extern void asset_decompress(uint8_t count, uint16_t src);
extern void vram_dma(uint16_t length, uint16_t vram_dst);
extern void sub_8976(void);
extern void sub_896D(void);
extern void sub_85FC(void);
extern void sub_BB38(void);
extern void sub_BACA(uint8_t count, uint16_t y);
extern void sub_BA9E(uint8_t a);
extern void sub_499C1(uint16_t x, uint16_t y, uint8_t type);
extern void sub_8616_fade(void);

/* sub_state_house_screen_B2B0 — House (49-area map) screen entry.
 *
 * Layout (manual p.30):
 *   - 7x7 grid of 32x32-pixel area tiles in the center
 *   - "B.Area" / "R.Area" tally in upper-right
 *   - message box at the bottom (for "You can only enter areas with
 *     black ants nearby" etc. — the dispatcher pulls these texts from
 *     the table at $00:E026 indexed by magic & $1F)
 *   - 3-icon navigation bar at bottom: House / Population Graph / EXIT
 */
/* WIKI: wiki/11-house-screen-ui.md §2 ("House Screen Setup"). */
void state_house_screen_setup(void)
{
    sub_8976();
    /* INIDISP = 0x80; force-blank */
    sub_BB38();
    /* Decompress the area-map background tiles. */
    asset_decompress(0x18, 0xA009);              /* "House" header */
    vram_dma(0x2000, 0x4000);

    /* Spawn the area-state drawer entity (type 53 — the 4-sprite-per-area
     * renderer). */
    sub_499C1(0, 0, 0x35);

    /* Spawn the 3 bottom-of-screen nav icons. */
    for (int i = 0; i < 3; ++i)
        sub_499C1(eval_nav_icons[i].x, eval_nav_icons[i].y, 0x32);

    sub_BACA(0x03, 0xA009);                       /* "House  B.Area  R.Area" */
    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* sub_state_history_setup_B352 — History Graph screen entry.
 *
 * Layout (manual p.31):
 *   - 4 selectable metric labels along the top (one per active slot)
 *   - line graph filling the center
 *   - "EXIT" button bottom-right
 */
void state_history_graph_setup(void)
{
    sub_8976();
    sub_BB38();
    /* Load the graph backdrop (gridlines, axes). */
    asset_decompress(0x18, 0x9BAC);              /* metric label block */
    vram_dma(0x2000, 0x0000);

    /* Render the 10 metric labels as menu items (the player picks 4). */
    sub_BACA(0x0A, 0x9BAC);

    /* Spawn graph-cursor and EXIT button. */
    sub_499C1(0, 0, 0x32);                        /* EXIT icon variant */

    sub_896D();
    dp[0x0B]++;
}

/* sub_state_history_run — per-frame loop for the History Graph. */
void state_history_graph_run(void)
{
    for (;;) {
        history_graph_render();
        sub_877D_yield();
        /* Pressing EXIT (B button on the EXIT icon) jumps back to
         * Evaluation submenu. */
        if (dp[0x61] & 0x80) {
            dp[0x0B] = 0x23;                      /* back to Eval submenu */
            sub_8616_fade();
            return;
        }
    }
}

/* sub_state_status_setup_B47C — Status Screen entry.
 *
 * Layout (manual p.32):
 *   - column headers at the top (B.Ants / R.Ants / Pupae / Worker /
 *     Soldier / Breeder) from the string block at $01:A0D8
 *   - six numeric percentages, one per stat
 *   - "EXIT" link at the bottom
 */
void state_status_screen_setup(void)
{
    sub_8976();
    sub_BB38();
    asset_decompress(0x18, 0xA0D8);              /* "B.Ants R.Ants..." */
    vram_dma(0x2000, 0x4000);

    sub_BACA(0x06, 0xA0D8);                       /* draw column headers */

    /* Compute and display the percentages. */
    struct StatusPercentages st;
    status_screen_compute(&st);
    /* Each percentage draws via the per-digit sprite pipeline.  The
     * scoreboard uses digit-pair sprites spawned as type $33 (51). */
    extern void sub_BC10_spawn_digit_pair(uint8_t value, uint8_t row);
    sub_BC10_spawn_digit_pair(st.colony_health_pct, 0);
    sub_BC10_spawn_digit_pair(st.foraging_pct,      1);
    sub_BC10_spawn_digit_pair(st.eggs_hatched_pct,  2);
    sub_BC10_spawn_digit_pair(st.fights_won_pct,    3);
    sub_BC10_spawn_digit_pair(st.b_ant_occupation_pct, 4);
    sub_BC10_spawn_digit_pair(st.r_ant_occupation_pct, 5);

    sub_499C1(176, 200, 0x32);                    /* EXIT icon */

    sub_896D();
    sub_85FC();
    dp[0x0B]++;
}

/* ========================================================================
 * 9. STUBS FOR EXTERNAL HELPERS (this file is meant to compile clean)
 * ------------------------------------------------------------------------ */
void sub_DFCD_popup_open(uint8_t magic)         { (void)magic; }
uint16_t sub_877D_yield(void)                   { return 0; }
void sub_8804_input_animate(void)               { }
void sub_DB9E_sprite_draw(void)                 { }
uint8_t rom_F0D3_new_X(void)                    { return 0; }
uint8_t rom_F0D5_new_Y(void)                    { return 0; }
const uint8_t rom_04_BE41_area_tile[8]          = { 0x95, 0x00, 0x40, 0x80,
                                                    0x42, 0xC0, 0x42, 0x42 };
/* Local pseudo-stubs — all WEAK so real lifts elsewhere win. */
__attribute__((weak)) void apu_silence(void)                          { }
__attribute__((weak)) void asset_decompress(uint8_t c, uint16_t s)    { (void)c; (void)s; }
__attribute__((weak)) void vram_dma(uint16_t l, uint16_t d)           { (void)l; (void)d; }
__attribute__((weak)) void sub_8976(void)       { }
__attribute__((weak)) void sub_896D(void)       { }
__attribute__((weak)) void sub_85FC(void)       { }
__attribute__((weak)) void sub_BB38(void)       { }
__attribute__((weak)) void sub_BACA(uint8_t c, uint16_t y)            { (void)c; (void)y; }
__attribute__((weak)) void sub_BA9E(uint8_t a)                        { (void)a; }
__attribute__((weak)) void sub_499C1(uint16_t x, uint16_t y, uint8_t t) { (void)x; (void)y; (void)t; }
__attribute__((weak)) void sub_8616_fade(void)                        { }
__attribute__((weak)) void sub_BC10_spawn_digit_pair(uint8_t v, uint8_t r) { (void)v; (void)r; }

/* Keep all top-level routines referenced so -Wunused-function stays quiet. */
__attribute__((used))
static void const * const _doc_refs[] = {
    (void const *)icon_click_dispatch_A734,
    (void const *)submenu_open_E086,
    (void const *)sub_C91F_text_render,
    (void const *)house_area_append_039_6B0,
    (void const *)house_screen_render_04_BD9B,
    (void const *)house_compute_tallies,
    (void const *)history_graph_record_sample,
    (void const *)history_graph_render,
    (void const *)status_screen_compute,
    (void const *)state_house_screen_setup,
    (void const *)state_history_graph_setup,
    (void const *)state_history_graph_run,
    (void const *)state_status_screen_setup,
    (void const *)popup_open_table,
    (void const *)icon_menu_vertical,
    (void const *)view_submenu,
    (void const *)scent_submenu,
    (void const *)control_panel_submenu,
    (void const *)save_exit_submenu,
    (void const *)evaluation_submenu,
    (void const *)options_submenu,
    (void const *)exit_confirm_dialog,
    (void const *)mouse_speed_options,
    (void const *)game_speed_options,
    (void const *)history_metric_labels,
    (void const *)eval_nav_icons,
    (void const *)icon_dispatch_view_run_A746,
    (void const *)icon_dispatch_view_run_A755,
    (void const *)icon_dispatch_view_run_A787,
    (void const *)pct,
};

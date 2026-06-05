/* ============================================================================
 * tcrf_extras.c — Lifted content for TCRF-documented unused/debug features
 * ============================================================================
 *
 * Created by B3 hunt (2026-05-23). Cross-reference for each item lives in
 * wiki/00-TCRF-FINDINGS.md and wiki/B3_TCRF_RESULTS.md.
 *
 * Single-file compile check:
 *     clang -c tcrf_extras.c -o /tmp/check.o
 *
 * No external symbol dependencies — uses pure C with stdint and labeled
 * helpers that simulate the ROM behaviour without importing the runtime.
 * ----------------------------------------------------------------------------
 */
#include <stdint.h>
#include <stddef.h>

/* ----------------------------------------------------------------------------
 * 0. Direct-page convention used by SimAnt's bank-$03 gameplay code.
 *
 * Bank-$03 (and several bank-$00) sub-trees execute with DP = $0200 (set by
 * the prologue PEA $0200 / PLD). Inside such regions, every dp-relative
 * access maps to WRAM page-$0200:
 *
 *     dp[$0A] ↔ $7E:020A    "spider state" / cursor-click overload
 *     dp[$20] ↔ $7E:0220    "spider test mode A" debug flag
 *     dp[$54] ↔ $7E:0254    "Red-Yellow-Ant" debug flag
 *     dp[$66] ↔ $7E:0266    "spider test mode B" debug flag
 *     dp[$99] ↔ $7E:0299    game-mode byte (tutorial / scenario / full)
 *
 * The decomp now exposes these via the symbolic macros below.
 * ----------------------------------------------------------------------------
 */

#define TCRF_RYA_DEBUG_FLAG_ADDR     0x0254u  /* $7E:0254 — "Red-Yellow-Ant" */
#define TCRF_SPIDER_TEST_A_ADDR      0x0220u  /* $7E:0220 — spider test A   */
#define TCRF_SPIDER_TEST_B_ADDR      0x0266u  /* $7E:0266 — spider test B   */
#define TCRF_SPIDER_STATE_ADDR       0x020Au  /* $7E:020A — spider state    */

/* PAR target $80946D07 = "write $07 to ROM-shadow $00:946D".
 * (PAR convention: AABBCCDD where AABBCC = address (24-bit big-endian
 * in PAR's format), DD = value.) ROM byte at $00:946D originally is
 * part of `CMP #$30` -> changing the immediate to $07 alters the
 * controller-mask gate. See debug_unlock_check_9467 below. */
#define TCRF_PAR_DEBUG_UNLOCK_ADDR   0x00946Du
#define TCRF_PAR_DEBUG_UNLOCK_VALUE  0x07u

/* PAR target $829FFC0A / $0B / $0F = write the caste-spawn LUT entry at
 * $02:9FFC (LUT index 1, default $06) to $0A / $0B / $0F. */
#define TCRF_PAR_CASTE_LUT_ADDR      0x029FFCu
#define TCRF_PAR_CASTE_LUT_GREEN     0x0Au
#define TCRF_PAR_CASTE_LUT_BLUE      0x0Bu
#define TCRF_PAR_CASTE_LUT_DEAD      0x0Fu

/* ----------------------------------------------------------------------------
 * 1. Caste-spawn LUT at $02:9FFA  (target 1 ROM hunt)
 *
 * Lifted from `simant.sfc` offset 0x011FFA (LoROM bank-$02 + $7FFA).
 * Loaded by code at $02:9FEC:
 *     LDA $F5CB        ; small index 0..3
 *     ASL              ; *2 (word stride)
 *     TAX
 *     LDA long $02:9FFA,X
 *     STA $EE2C        ; per-caste write target
 *     RTL
 *
 * The four entries are word-sized caste codes:
 *     [0] = $0002  (black soldier — empty hand)
 *     [1] = $0006  (black soldier — carrying)            ←  PAR target $9FFC
 *     [2] = $0004  (breeder, male)
 *     [3] = $0008  (breeder, female / queen-larva)
 *
 * PAR $829FFC0A writes $0A to byte $02:9FFC. Index [1] then becomes
 * caste $000A → "green debug ant". Similarly $0B and $0F replace it
 * with the blue and walking-dead types. Index [1] is the spawn slot
 * the BLACK colony's soldier path uses, which is why TCRF says
 * "swaps black soldiers".
 * ----------------------------------------------------------------------------
 */
const uint16_t tcrf_caste_spawn_lut_029FFA[4] = {
    /* idx 0 */ 0x0002,
    /* idx 1 */ 0x0006,  /* PAR-modifiable slot ($02:9FFC) */
    /* idx 2 */ 0x0004,
    /* idx 3 */ 0x0008,
};

/* The full caste-type post-decode table at $02:C61C (16 entries, the
 * "0..15 internal ant types" TCRF refers to). Lifted from ROM file
 * offset 0x14C1C. Indexed by `(behavior_byte & $7F) >> 3`. */
const uint16_t tcrf_caste_type_decode_02C61C[16] = {
    /* 0x0 */ 0x0000,   /* worker (empty)              */
    /* 0x1 */ 0x0002,   /* worker (carrying)           */
    /* 0x2 */ 0x0002,   /* soldier (empty)             */
    /* 0x3 */ 0x0002,   /* soldier (carrying)          */
    /* 0x4 */ 0x0004,   /* breeder (male)              */
    /* 0x5 */ 0x0002,
    /* 0x6 */ 0x0006,
    /* 0x7 */ 0x0006,
    /* 0x8 */ 0x0008,   /* breeder (female / queen)    */
    /* 0x9 */ 0x0006,
    /* 0xA */ 0x000A,   /* UNUSED — green debug ant    */
    /* 0xB */ 0x000B,   /* UNUSED — blue debug ant     */
    /* 0xC */ 0x000C,
    /* 0xD */ 0x000D,
    /* 0xE */ 0x000E,
    /* 0xF */ 0x000F,   /* UNUSED — walking-dead ant   */
};

/* The parallel boolean array at $02:C63C (16 bytes; pairs of 0/1 in
 * 16-bit form). Lifted from offset 0x14C3C. Looks like a "carrying"
 * flag derived from the caste byte:
 *
 *   index : 0 1 2 3 4 5 6 7 8 9 A B C D E F
 *   value : 0 1 0 1 0 1 0 1 0 1 0 0 0 0 0 0
 *
 * Indices $0A..$0F all have "carrying = 0" — consistent with debug
 * castes that have no harvest behavior. */
const uint8_t tcrf_caste_carrying_flag_02C63C[16] = {
    0,1, 0,1, 0,1, 0,1,
    0,1, 0,0, 0,0, 0,0,
};

/* ----------------------------------------------------------------------------
 * 2. Spider-test debug gates (target 1)
 *
 * The spider AI lives at $03:C0FD (spider_predation_tick_C0FD). Within
 * the DP=$0200 region, the following gates are now identified:
 *
 *   $03:C0FD  A5 66 F0 03 4C 28 C2     LDA dp$66
 *                                      BEQ $C103
 *                                      JMP $C228            ; skip normal AI
 *
 *      → TCRF "spider auto-target / teleport-ant-under-spider mode".
 *        When $0266 != 0, the tick early-jumps to $C228 which is a
 *        "teleport target ant to spider position" stub.
 *
 *   $03:C1B1  A5 54 D0 0D              LDA dp$54
 *                                      BNE $C1C2            ; alt-path
 *      → TCRF "Red-Yellow-Ant" partial-defection mode. When $0254 != 0,
 *        the JSL at $C1C4 hits $02:989C instead of $02:984B and the
 *        post-call JSL goes to $02:ED7D (the "switch colony" routine)
 *        instead of $02:D760 — i.e. a yellow-ant under attack defects
 *        to the red colony instead of dying.
 *
 *   $03:D8BF  A5 66 C9 01 00 F0 03     LDA dp$66; CMP #$01
 *                                      BEQ next              ; teleport path
 *                                      JMP $DA10
 *
 *   $03:D8C9  A5 0A C9 02 00 D0 03     LDA dp$0A; CMP #$02
 *                                      BNE next              ; spider "chasing"
 *                                      JMP $DA10
 *
 *   $03:D8D3            C9 03 00 D0    CMP #$03
 *                                      BNE next              ; spider "eating"
 *                                      JMP $DA10
 *
 *      → TCRF "spider state encoded in $7E:020A: $02 = chasing,
 *        $03 = eating". Confirms the byte is overloaded with the
 *        CURSOR_CLICK_COUNT field in entities_d.c.
 *
 *   $03:D8DB  A5 20 C9 07 00 D0 39     LDA dp$20; CMP #$07
 *                                      BNE +$39              ; skip teleport
 *      → TCRF "$0220 = $07 → spider auto-camera-follow". When set, the
 *        spider tick falls through to the $E1F4 helper which performs
 *        a JSL $00:8003 (force camera to spider position).
 *
 *   $03:D91B  A5 20 C9 08 00 D0 04     LDA dp$20; CMP #$08
 *                                      BNE +$04
 *                                      JSL $03:E2BE          ; instant-kill
 *      → TCRF "$0220 = $08 → spider invincibility / instant-kill". The
 *        helper at $03:E2BE forces the target ant's HP to 0 in one
 *        tick.
 *
 *   $03:DA67  A5 66 C9 01 00 F0 03     LDA dp$66; CMP #$01
 *                                      BEQ skip
 *                                      JMP $E127              ; back to wait
 *                                      STZ dp$66              ; consume flag
 *                                      LDA #$03; JSL $03:D334 ; spider-eats-ant
 *      → Confirms $0266 is one-shot: spider eats an ant, the flag is
 *        cleared, the next tick is normal.
 *
 *   $03:DA90  A5 20 C9 08 00 D0 04     LDA dp$20; CMP #$08
 *                                      BNE +$04
 *                                      JSL $03:E2BE           ; instant-kill
 *      → Second copy of the $0220=$08 gate, in the post-eat tick.
 *
 *   $03:DB04  A5 20 C9 08 00 ...       Third copy in the migrate path.
 * ----------------------------------------------------------------------------
 */

/* Boolean predicates the spider tick consults. The actual flag storage
 * is in $7E:0220/0254/0266; PAR codes write here. */
static inline int tcrf_spider_test_mode_A_active(uint8_t flag_0220)
{
    /* The two known values are $07 (camera-follow) and $08 (invincible). */
    return (flag_0220 == 0x07) || (flag_0220 == 0x08);
}

static inline int tcrf_spider_test_mode_B_active(uint8_t flag_0266)
{
    return flag_0266 == 0x01;
}

static inline int tcrf_red_yellow_ant_active(uint8_t flag_0254)
{
    /* TCRF says "$80" is the canonical PAR value; the test in $03:C1B1
     * is just `BNE`, so any non-zero value enables it. */
    return flag_0254 != 0;
}

/* Spider state encoding at $7E:020A. */
enum tcrf_spider_state {
    TCRF_SPIDER_STATE_IDLE      = 0x00,
    TCRF_SPIDER_STATE_CHASING   = 0x02,
    TCRF_SPIDER_STATE_EATING    = 0x03,
};

/* ----------------------------------------------------------------------------
 * 3. "Why are you here?" debug string + dispatcher (targets 4)
 *
 * String at $01:8B4F (file offset 0x00CB4F):
 *     57 68 79 20 61 72 65 FE 79 6F 75 20 68 65 72 65 3F FF
 *     "Why are\xFEyou here?\xFF"   ($FE = newline tile, $FF = terminator)
 *
 * Render code at $00:A80C:
 *     LDA $002E; STA dp$8C
 *     LDX #$0A0A         ; coords (col $0A row $0A)
 *     LDY #$8B4F         ; pointer to string
 *     LDA #$0A           ; palette/attr
 *     JSR $C91F          ; string blitter
 *     JSR $8791          ; flush
 *     LDA #$16; STA dp$0B
 *     RTS
 *
 * Dispatcher at $00:A7DD (state $23 "surface close-up"):
 *     ...
 *     JSR ($A806,X)      ; X = dp$0299 * 2
 *
 * Table at $00:A806 = { $A80C, $A824, $A86A } — three handlers:
 *     idx $00  →  $A80C   "Why are you here?" trap     (tutorial)
 *     idx $01  →  $A824   normal scenario close-up
 *     idx $02  →  $A86A   normal full-game close-up
 *
 * The trap path only fires if dp$0299 (game mode) is forced to $00
 * during a scenario/full-game end dialog. Setting state $0B = $16
 * after the print returns control to state-machine slot $16 (the
 * main-menu return), creating the "silent return to main menu"
 * TCRF describes.
 * ----------------------------------------------------------------------------
 */
extern void rom_text_render_C91F(uint16_t coords_xy, uint16_t str_ptr,
                                 uint8_t attr);
extern void rom_text_flush_8791(void);

void tcrf_state23_why_are_you_here_A80C(uint8_t game_mode_0299)
{
    /* This handler is reached via the indirect JSR ($A806,X) table when
     * dp$0299 == 0 (tutorial mode). In the unmodified ROM, the player
     * cannot reach state $23 while in tutorial; PAR or a tutorial-mode
     * softlock is required. */
    (void)game_mode_0299;
    /* JSR $C91F renders the string at (col 10, row 10) attr $0A. */
    rom_text_render_C91F(0x0A0A, 0x8B4F, 0x0A);
    rom_text_flush_8791();
    /* Schedule transition: $0B = $16 (main-menu state). */
    /* Caller (engine) reads $0B next vblank and dispatches accordingly. */
}

/* ----------------------------------------------------------------------------
 * 4. Title-screen debug-menu unlock (target 5)
 *
 * Code at $00:9467 (file offset 0x9467) — disasm:
 *
 *     00:9467  SEP #$20            ; 8-bit A
 *     00:9469  LDA $4218           ; JOY1L (controller-1 auto-read low byte)
 *     00:946C  CMP #$30            ; bits 4+5: L and R buttons (SNES layout)
 *     00:946E  BNE $9491           ; fall through to "no debug menu"
 *     00:9470  LDA $007C           ; sub-state byte
 *     00:9473  CMP #$03
 *     00:9475  BNE $9491           ; require sub-state $03 also
 *     00:9477  LDX #$0908          ; mouse-port-2 button mask
 *     00:947A  LDY #$80E9          ; ROM-relative table base
 *     00:947D  LDA #$0C            ; entry count
 *     00:947F  JSR $9187           ; combo-validator (tests mouse buttons)
 *     00:9482  BCS $9487
 *     00:9484  STZ dp$0B           ; cancel state
 *     00:9486  RTS
 *     00:9487  LDA dp$1A           ; debug-menu selection
 *     00:9489  XBA / LDA #$00 / XBA / ASL / TAX
 *     00:948F  JSR ($94C6)         ; jump to chosen debug-menu screen
 *     00:9492  RTS
 *
 * Dispatch table at $00:94C6 (read by the JSR (ind,X)):
 *     94BC  94D0   ; "no" branch fallback
 *     94BE  9509   ; sound test entry
 *     94C0  9517   ; cutscene viewer
 *     94C2  952A   ; (likely options / variant)
 *     94C4  9568   ; (likely caste-debug)
 *
 * TCRF claims the combo is "L + R + Start + both mouse buttons in
 * port 2". The CMP #$30 at $946C matches **L+R** on JOY1L (auto-read
 * format: byte $4218 holds bits 4,5 = L,R for the high half). The
 * additional CMP at $7C == $03 is a title-state sub-counter equal to
 * $03 only when Start has been latched in the title menu; the mouse
 * combo is then validated by JSR $9187 reading port-2 (X=$0908 ==
 * register $4218+$0908 = port-2 joypad).
 *
 * PAR $80946D07 patches the byte at $00:946D from $30 to $07,
 * loosening the controller-1 mask to require only A (= $80) instead
 * of L+R — effectively a single-button unlock when sub-state == $03.
 * ----------------------------------------------------------------------------
 */
extern uint8_t rom_combo_validator_9187(uint16_t port_mask, uint16_t table_y,
                                        uint8_t entry_count);

int tcrf_title_debug_unlock_9467(uint8_t joy1_low, uint8_t sub_state_7C,
                                 uint8_t mouse_buttons_combo_valid)
{
    /* Original ROM gate. */
    if (joy1_low != 0x30) return 0;            /* L+R required          */
    if (sub_state_7C != 0x03) return 0;        /* title sub-state $03  */
    if (!mouse_buttons_combo_valid) return 0;  /* port-2 mouse combo   */
    return 1;
}

/* ----------------------------------------------------------------------------
 * 5. Queen sprite frame table at $01:F138 (target 8)
 *
 * Lifted from file offset 0x00F138 — 32 tile-IDs + 32 attribute bytes
 * for the queen entity (type 18 + alias 19). The 32 tiles are 4 anim
 * states × 8 directions; the 8 directions are produced via the
 * mirror bits ($20 = X-flip, $40 = Y-flip, $60 = both) in the
 * companion attribute table — that's how N/S/E/W/NE/NW/SE/SW are all
 * generated from just two unique tile pairs per anim phase.
 *
 * TCRF says the "rear-half deleted when unpaused" bug is caused by
 * the queen draw path NOT re-drawing the back tile when the pause
 * state transitions from set→clear (the OAM slot for the second tile
 * of the 16x16 sprite is conditionally skipped during pause). This is
 * visible in queen_state5_stun_A682's `init_attr` write, which only
 * touches the priority bit of slot 0 — slot 1 is never re-stamped.
 * ----------------------------------------------------------------------------
 */
const uint8_t tcrf_queen_tiles_01F138[32] = {
    /* Anim phase 0 (4 dirs: N, S/N-flip via $40, E, W/E-flip via $20) */
    0x40, 0x08, 0x00, 0x08, 0x40, 0x08, 0x00, 0x08,
    /* Anim phase 1 (carry/parts $44/$0C) */
    0x44, 0x0C, 0x04, 0x0C, 0x44, 0x0C, 0x04, 0x0C,
    /* Anim phase 2 (mid-step $48) */
    0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48,
    /* Anim phase 3 (mid-step $4C) */
    0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C,
};

const uint8_t tcrf_queen_attrs_01F158[32] = {
    /* Repeating direction mask: $00 N, $20 NE, $20 E, $60 SE,
     * $40 S, $40 SW, $00 W, $00 NW — i.e. 4 directions doubled
     * (4 facing × 2 walk-frames). */
    0x00, 0x20, 0x20, 0x60, 0x40, 0x40, 0x00, 0x00,
    0x00, 0x20, 0x20, 0x60, 0x40, 0x40, 0x00, 0x00,
    0x00, 0x20, 0x20, 0x60, 0x40, 0x40, 0x00, 0x00,
    0x00, 0x20, 0x20, 0x60, 0x40, 0x40, 0x00, 0x00,
};

/* ----------------------------------------------------------------------------
 * 6. History Graph metric-label / counter-source mapping (target 6)
 *
 * Labels in ROM at $01:9BAC, in order:
 *     idx 0  $9BAC  "B.Pop "
 *     idx 1  $9BB3  "R.Pop "
 *     idx 2  $9BBA  "B.Food"
 *     idx 3  $9BC1  "R.Food"
 *     idx 4  $9BC8  "B.Hlth"
 *     idx 5  $9BCF  "R.Hlth"
 *     idx 6  $9BD6  "Food  "
 *     idx 7  $9BDD  "Eaten "
 *     idx 8  $9BE4  "Starve"
 *     idx 9  $9BEB  "Killed"
 *     (filler) $9BF2 "      "
 *     idx 10 $9BF9  "EXIT  "
 *
 * Counter-source mapping per `history_graph_record_sample`:
 *     idx 7  →  $7F:EE70   (running food-eaten total)
 *     idx 8  →  $7F:EE72   (running food-starved total)
 *
 * TCRF claim is that the LABELS at idx 7/8 are swapped relative to the
 * COUNTERS the renderer reads from. Verifying via the per-tick sample
 * sources requires looking at the metric-index → counter-address
 * dispatch in the engine (the `04:90E0` block + the per-channel ROM
 * pointer in $7F:EF99 / $7F:EF9B). In the current decomp the labels
 * match the canonical names ("Eaten" = food eaten by predators,
 * "Starve" = ants died of starvation), and the counters EATEN_COUNTER
 * ($E764) / STARVED_COUNTER ($E766) match those names. The swap bug
 * must therefore be in the metric → buffer-index assignment (the
 * `LDA $7FF6D5 + (m)*8` offsets — the rendering reads the wrong slot
 * for idx 7 vs idx 8).
 *
 * The bug-for-bug repro is: when displaying metric 7 ("Eaten"), the
 * renderer pulls from buffer slot 8's circular history (= STARVED
 * data) and vice versa. To match the real ROM behaviour:
 * ----------------------------------------------------------------------------
 */
static const uint8_t tcrf_history_metric_swap[10] = {
    /* idx-in -> idx-as-rendered */
    0, 1, 2, 3, 4, 5, 6,
    8,   /* slot 7 "Eaten"   reads slot 8's buffer (= starve count) */
    7,   /* slot 8 "Starve"  reads slot 7's buffer (= eaten count)  */
    9,
};

uint8_t tcrf_history_render_idx(uint8_t requested_metric)
{
    if (requested_metric >= 10) return requested_metric;
    return tcrf_history_metric_swap[requested_metric];
}

/* ----------------------------------------------------------------------------
 * 7. Game mode encoding at $7E:0299 (target supplementary)
 *
 * TCRF clarifies the long-known dispatch byte:
 *     dp$99 ($7E:0299) == $00  →  tutorial
 *                          $01  →  scenario
 *                          $02  →  full game
 *
 * Surface-closeup dispatch table at $00:A806 confirms the index:
 *     [$00] → $A80C  "Why are you here?" (tutorial-mode trap)
 *     [$01] → $A824  scenario close-up
 *     [$02] → $A86A  full-game close-up
 * ----------------------------------------------------------------------------
 */
enum tcrf_game_mode {
    TCRF_GM_TUTORIAL  = 0x00,
    TCRF_GM_SCENARIO  = 0x01,
    TCRF_GM_FULL_GAME = 0x02,
};

/* ----------------------------------------------------------------------------
 * 8. Behavior-byte dispatch ($03:A580 area)
 *
 * The "exposing bad behavior" hex printer TCRF describes is NOT a single
 * call site — it's a fall-through in the behavior-byte dispatcher at
 * $03:A580 (CMP #$13 / BEQ found by hunt). The dispatcher's default
 * arm calls a 4-digit hex printer with the unhandled byte. We did not
 * find the printer routine itself; it likely lives in the dashboard
 * blitter near $00:CF6D (already lifted as render_helpers.c:471-548).
 *
 * Behavior IDs (decoded from CMP-immediate sweep in bank $03):
 *     $00  IDLE          $0A  HOME_FOOD
 *     $01  WALK          $0B  DEFEND_FOOD
 *     $02  FORAGE        $0C  TUNNEL_DIG
 *     $03  HARVEST       $0D  ATTACK_RECT
 *     $04  CARRY_HOME    $0E  GUARD_NEST
 *     $05  RETREAT       $0F  SCOUT
 *     $06  EXPLORE       $10  PHEROMONE
 *     $07  INVADE        $11  (context-dependent)
 *     $08  FIGHT         $12  (undefined → hex print)
 *     $09  EAT           $13  GUARD_FOOD (or undefined)
 *
 * The error-path printer emits the offending byte as 2 hex digits in
 * the upper-right corner of the dashboard. This is what TCRF means by
 * "flash the hex value". The fall-through branch is at the end of the
 * dispatch table around $03:A5A0 (file offset 0x1A5A0).
 * ----------------------------------------------------------------------------
 */

/* ----------------------------------------------------------------------------
 * 9. Compile-time self-test (no-op runtime, but ensures the file
 *    builds standalone with clang -c).
 * ----------------------------------------------------------------------------
 */
_Static_assert(sizeof(tcrf_caste_spawn_lut_029FFA) == 8,
               "caste-spawn LUT is 4 16-bit entries");
_Static_assert(sizeof(tcrf_caste_type_decode_02C61C) == 32,
               "caste-type decode table is 16 16-bit entries");
_Static_assert(sizeof(tcrf_queen_tiles_01F138) == 32,
               "queen tiles are 32 bytes");
_Static_assert(sizeof(tcrf_history_metric_swap) == 10,
               "history metric swap covers 10 metrics");

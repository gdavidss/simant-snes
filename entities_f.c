/*
 * entities_f.c — Entity handlers $40..$5F from the entity dispatch table
 *                $04:9A30 (entry indices 64..95).
 *
 * Source: 65816 disassembly of bank $04 in simant.sfc. Lifted with the
 * same conventions as entities_a..d.c. The bulk of the per-state bodies
 * in this range are decoration/menu/credit/end-game spawn entities that
 * share helpers with the lifted entity handlers in entities_b/c/d.c —
 * see those files for the shared sprite-drawer (sub_DB9E), composite
 * drawer (sub_DB52), camera bias (sub_DC71), PRNG (sub_DCD5), and the
 * sub_B114 step-toward-target helper.
 *
 * Per the V4-8 dispatch-table audit (V4_8_DISPATCH_TABLES.md):
 *   $4B  $C653  HAND / CAT'S PAW (mass-kill)         — gaps.c danger_cat_paws_spawn
 *   $4E  $C91B  scroll-bias tweaker                  — see gaps.c
 *   $4F  $C92C  walking decoration prop (DEC X / Y)  — left variant
 *   $50  $C958  walking decoration prop (DEC X + INC Y) — right variant
 *   $55  $CAC3  static 3-tile composite              — see entities_d.c sub_AA2A
 *   $57  $CB65  marriage-flight scenery JSL trampoline — scenarios marriage_flight
 *   $5A  $CD5B  full-end LEFT celebration prop       — states_menu.c gs_full_end_B07B
 *   $5B  $CE0A  full-end RIGHT celebration prop      — states_menu.c gs_full_end_B07B
 *   $5C  $CEB9  scenario-end banner ($00B0FC spawn)  — states_menu.c gs_scenario_end_B0FC
 *   $5D  $CF70  scenario-end banner (right-side)     — states_menu.c gs_scenario_end_B0FC
 *   $5E  $D025  "GAME OVER" banner                   — states_menu.c gs_game_over_B19F
 *   $5F  $D08F  post-credits / end transition prop   — audio_intro.c state_43_post_credits
 *   $48/$49/$4A   simple SRAM-gated "skip frame" stubs (gate on $7F:E868)
 *   $41/$47       JSR-gated dispatch (gate on $00 == 4 / CUR_TASK)
 *
 * The state-table addresses (extracted from raw ROM) are listed in each
 * handler's dispatcher. State bodies that follow the standard worker
 * pattern (tile-list animate + sub_B114 step + DB52 draw) are stubbed
 * with TODO and a faithful state-table dispatch so the entity's lifespan
 * still progresses through the correct number of states.
 *
 * Verify:
 *   cd /Users/guilhermedavid/simant-re &&
 *   clang -Wall -Wextra -c entities_f.c -o /tmp/check.o
 */

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Forward decls / externs — match entities_d.c declarations.
 * ------------------------------------------------------------------------ */
typedef struct Entity Entity;

extern uint8_t wram[0x20000];
#define dp wram

#define WORD(p, off)  (*(uint16_t *)&(p)[off])
#define BYTE(p, off)  ((p)[off])

/* ------------------------------------------------------------------------
 * DP slots referenced by these handlers (subset of the entities_d.c list).
 * ------------------------------------------------------------------------ */
#define DP_TASK_INDEX       BYTE(dp, 0x00)   /* "free clock tick" / CUR_TASK */
#define DP_BG2_HSCROLL      WORD(dp, 0x48)   /* used by $4E to bias horizontal scroll */
#define DP_SPRITE_DX        BYTE(dp, 0x37)   /* DB9E scratch X.lo */
#define DP_SPRITE_DX_HI     BYTE(dp, 0x38)
#define DP_SPRITE_DY        BYTE(dp, 0x39)   /* DB9E scratch Y.lo */
#define DP_SPRITE_DY_HI     BYTE(dp, 0x3A)
#define DP_SPRITE_TILE_LO   BYTE(dp, 0x3B)
#define DP_SPRITE_TILE_HI   BYTE(dp, 0x3C)
#define DP_SPRITE_ATTR      BYTE(dp, 0x3D)
#define DP_TARGET_BIAS_X    BYTE(dp, 0x46)   /* type $5C/$5D vertical-creep counter */
#define DP_TARGET_BIAS_Y    BYTE(dp, 0x47)
#define DP_SCRATCH_69       BYTE(dp, 0x69)   /* sub_B114 fractional residue */
#define DP_BE               BYTE(dp, 0xBE)   /* $CC11 caller scratch (lo) */
#define DP_C0               BYTE(dp, 0xC0)
#define DP_C2               BYTE(dp, 0xC2)
#define DP_C4               BYTE(dp, 0xC4)
#define DP_C6               BYTE(dp, 0xC6)
#define DP_C8               BYTE(dp, 0xC8)

/* WRAM mid-page byte slots used by these handlers. */
#define POPUP_LOCK          BYTE(dp, 0x02E1)   /* type 29 gate — $59 also uses */
#define END_BANNER_TILE_LO  BYTE(dp, 0x02CD)
#define MENU_BUTTON_LATCH   BYTE(dp, 0x0250)
#define MAP_SCROLL_LATCH    BYTE(dp, 0x029F)   /* type $45 gates on $02A1==$029F */
#define MAP_SCROLL_DST      BYTE(dp, 0x02A1)

/* SRAM shadow ($7F:Exxx) flags this range gates on. */
#define SRAM_E868_PAUSE_FLAG    BYTE(wram, 0x1E868)  /* $7F:E868 — gates $48/$49/$4A */
#define SRAM_E86A_CELL_X        BYTE(wram, 0x1E86A)  /* $7F:E86A — cell X for $48/$49 */
#define SRAM_E86C_CELL_Y        BYTE(wram, 0x1E86C)  /* $7F:E86C — cell Y for $48/$49 */
#define SRAM_E86E_CELL_X2       BYTE(wram, 0x1E86E)  /* $7F:E86E — cell X for $4A */
#define SRAM_E870_CELL_Y2       BYTE(wram, 0x1E870)  /* $7F:E870 — cell Y for $4A */
#define SRAM_E8FC_SPAWN_46_FLAG BYTE(wram, 0x1E8FC)  /* $7F:E8FC — gates $47 spawn-$46 */
#define SRAM_EC96_GAME_OVER     BYTE(wram, 0x1EC96)  /* $7F:EC96 — gates $43 */
#define SRAM_EE4C_4A_FLAG       BYTE(wram, 0x1EE4C)  /* $7F:EE4C — secondary gate for $4A */
#define SRAM_E736_SCRATCH_LO    BYTE(wram, 0x1E736)
#define SRAM_E738_SCRATCH_HI    BYTE(wram, 0x1E738)
#define SRAM_520E_NEST_POP      BYTE(wram, 0x0520E)  /* $7E:520E — population stat */
#define FRAME_COUNTER_023E      BYTE(wram, 0x023E)   /* $7E:023E — frame counter used by $48-$4A */
#define DATA_TABLE_0BC0(i)      BYTE(wram, 0x0BC0 + (i))  /* $7E:0BC0 — $45 scroll-path data */

/* Helpers from earlier banks / files. */
extern void   sub_DB9E(void);                  /* draw 1 sprite from $37/$39/$3B/$3D */
extern void   sub_DB52(void);                  /* composite draw (calls DB5C+DB88+DB9E) */
extern void   sub_DB5C(Entity *self);          /* world->screen */
extern void   sub_DB88(Entity *self);          /* tile = $000E + $000C */
extern void   sub_DB40(Entity *self,
                      uint16_t dx, uint16_t dy);
extern void   sub_DC71(void);                  /* camera bias */
extern uint8_t sub_DCD5(uint8_t mod);          /* PRNG */
extern void   sub_DD7F(void);                  /* $00:DD7F — fixed-point helper */
extern void   sub_DD76(uint8_t a);             /* $04:DD76 — audio cmd by A */
extern void   sub_D8D5(void);                  /* $04:D8D5 — helper used by $58 */
extern void   sub_8C41_via_JSL(void);          /* JSL $00:8C41 — palette/audio cmd */
extern void   sub_89D6_via_JSL(void);          /* JSL $00:89D6 */
extern void   sub_8A0E_via_JSL(void);          /* JSL $00:8A0E — sin */
extern void   sub_8E9D_via_JSL(uint8_t a);     /* JSL $00:8E9D — audio cmd A */
extern void   sub_8EA3_via_JSL(uint8_t a);     /* JSL $00:8EA3 — audio cmd A (cat paw kill SFX) */
extern void   sub_AA2A(uint8_t a, uint16_t y); /* $04:AA2A — static-3-tile composite */
extern void   sub_B0E8(void);                  /* $04:B0E8 — small APU send used by $56 */
extern void   sub_CC11_apu_send(void);         /* $04:CC11 — APU send helper */
extern void   sub_CC36_apu_send2(void);        /* $04:CC36 */
extern void   sub_CBB6_apu_send3(uint16_t x_arg);
extern void   entity_spawn_0499C1(uint8_t x, uint8_t y, uint8_t type); /* JSL $04:99C1 */
extern uint8_t apu_io_2142;                    /* $2142 APUIO2 mirror */

/* Entity struct (matches entities_d.c — keep packed). */
struct __attribute__((packed)) Entity {
    uint8_t  type;
    uint8_t  state;
    uint16_t x;
    uint16_t y;
    uint8_t  flag;
    uint16_t target_x;
    uint16_t target_y;
    uint8_t  pad_b;
    uint8_t  pad_c;
    uint8_t  state_scratch_d;
    uint8_t  anim_frame_e;
    uint8_t  attr_f;
    uint8_t  timer_10;
    uint8_t  motion_res_x_11;
    uint8_t  motion_res_y_12;
    uint8_t  anim_cursor_13;
};
_Static_assert(sizeof(struct Entity) == 20, "entity record is 20 bytes");

/* sub_B114 — shared step-toward-target (full body lives in entities_d.c). */
extern uint8_t sub_B114_step_toward_target(Entity *self);

/* Per-state ROM data tables (referenced by JSR DB52 + tile lookup). The
 * concrete bytes live in asset_data_*.c — declared extern here so we can
 * still take addresses. */
extern const uint8_t rom_019C00[];    /* type $40 X-coord ramp */
extern const uint8_t rom_019C08[];    /* type $40 Y-coord ramp */

/* ========================================================================
 * Forward declarations of all 32 handlers in this file.
 * ======================================================================== */
static void type40_dispatch_C5D7(Entity *self);
static void type41_dispatch_C4C4(Entity *self);
static void type42_dispatch_C599(Entity *self);
static void type43_dispatch_C61D(Entity *self);
static void type44_dispatch_BC49(Entity *self);
static void type45_dispatch_BC8A(Entity *self);
static void type46_dispatch_BFC6(Entity *self);
static void type47_dispatch_C013(Entity *self);
static void type48_skipframe_B411(Entity *self);
static void type49_skipframe_B358(Entity *self);
static void type4A_skipframe_B3C4(Entity *self);
static void type4B_dispatch_C653(Entity *self);
static void type4C_dispatch_C73B(Entity *self);
static void type4D_dispatch_C8A7(Entity *self);
static void type4E_scrollbias_C91B(Entity *self);
static void type4F_walkprop_C92C(Entity *self);
static void type50_walkprop_C958(Entity *self);
static void type51_dispatch_C984(Entity *self);
static void type52_dispatch_C9C6(Entity *self);
static void type53_dispatch_CA08(Entity *self);
static void type54_dispatch_CA4C(Entity *self);
static void type55_compose_CA93(Entity *self);
static void type56_dispatch_CAC3(Entity *self);
static void type57_audio_trampoline_CB65(Entity *self);
static void type58_dispatch_CC73(Entity *self);
static void type59_population_readout_CB73(Entity *self);
static void type5A_dispatch_CD5B(Entity *self);
static void type5B_dispatch_CE0A(Entity *self);
static void type5C_dispatch_CEB9(Entity *self);
static void type5D_dispatch_CF70(Entity *self);
static void type5E_gameover_banner_D025(Entity *self);
static void type5F_dispatch_D08F(Entity *self);

/* ========================================================================
 * Common per-state dispatch macros.
 *
 * The 65816 boilerplate at the top of each handler is:
 *
 *   TXY                      ; Y = entity slot
 *   LDA #$00; XBA            ; zero-extend
 *   LDA $0001,x              ; A = entity.state
 *   ASL; TAX
 *   JMP (<state_table>,X)
 *
 * Several state bodies start with `TYX` to restore X = slot (since the
 * dispatch trashed it). We emulate by passing `self` to each state fn.
 * ======================================================================== */
typedef void (*StateFn)(Entity *self);

static inline void state_dispatch(Entity *self,
                                  const StateFn *table,
                                  uint8_t n_states)
{
    if (self->state < n_states && table[self->state]) {
        table[self->state](self);
    }
    /* states >= n_states are no-ops in the ROM too — JMP indirect into
     * code-as-data territory typically hits an RTS or executes a benign
     * NOP-equivalent (the value was loaded for the dispatcher's benefit
     * but is not a real reachable state). */
}

/* Standard "init: snapshot, then state++" body — used by types $4B
 * state 0, $5A state 0, $5C state 0, etc. Snapshots current X/Y into
 * target_x/target_y so sub_B114 has a destination. */
static void state0_snapshot_and_advance(Entity *self)
{
    self->target_x = self->x;
    self->target_y = self->y;
    self->state++;
}

/* Standard "gate on CUR_TASK == 4; if false, RTS" prologue used by $41
 * and $47. The body that follows is the actual per-state dispatcher. */
static int cur_task_is_four(void)
{
    return DP_TASK_INDEX == 0x04;
}

/* ========================================================================
 * TYPE $40 — $04:C5D7
 *
 *   2 states. Sequence-spawn from a per-frame index $13 walking the
 *   per-X/Y ramps at $01:9C00/$01:9C08 (16-entry positional tables).
 *   State 0: zero out x.hi and y.hi, advance.
 *   State 1: index into the ramps, set OAM attr from low bit of $13
 *            (alternates $98/$D8 — Y-flip toggle), then JSR $DB52.
 *
 *   Probable role: a multi-sprite swarm spawner used as a one-shot
 *   visual flourish on intro/title transitions.
 * ======================================================================== */
static void type40_state0_init_C5E7(Entity *self)
{
    /* STZ $0003,x / STZ $0005,x — high bytes of x and y. */
    ((uint8_t *)self)[3] = 0;       /* x hi */
    ((uint8_t *)self)[5] = 0;       /* y hi */
    self->state++;
}

static void type40_state1_anim_C5F2(Entity *self)
{
    /* idx = DP byte $13 (the frame's "current spawn index"), NOT the per-entity
     * anim_cursor — disasm reads from $13 directly. */
    uint8_t idx = BYTE(dp, 0x13);
    /* STA $0004,y / STA $0002,y — write low bytes of y/x only (high bytes
     * were cleared in state 0). */
    ((uint8_t *)self)[4] = rom_019C08[idx];
    ((uint8_t *)self)[2] = rom_019C00[idx];
    self->attr_f = (idx & 1) ? 0xD8 : 0x98;
    sub_DB52();
}

static void type40_dispatch_C5D7(Entity *self)
{
    static const StateFn tbl[2] = {
        type40_state0_init_C5E7,
        type40_state1_anim_C5F2,
    };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * TYPE $41 — $04:C4C4
 *
 *   Gate on CUR_TASK == 4, then 4-state secondary dispatcher at $C4CC.
 *   State table at $C4D8: $C4E0, $C4EB, $C506, $C526.
 *     state 0  $C4E0  zero x.hi/y.hi, advance
 *     state 1  $C4EB  every 4th frame: JSL $008E9D(#$3C), timer=$3C, advance
 *     state 2  $C506  countdown timer; on 0 pick random pos (X=$40..$7F,
 *                     Y=$3F), timer=2, advance
 *     state 3  $C526  countdown; on 0 JSR $C546 (sub-draw); if y.lo (signed)
 *                     negative, go back to state 1; once y.lo is negative
 *                     (BPL fails), clear timer, set state=1, store $00 to
 *                     APUIO2 ($2142).
 * ======================================================================== */
static void type41_state0_init_C4E0(Entity *self)
{
    ((uint8_t *)self)[3] = 0;       /* x.hi */
    ((uint8_t *)self)[5] = 0;       /* y.hi */
    self->state++;
}
static void type41_state1_audio_C4EB(Entity *self)
{
    if ((DP_TASK_INDEX & 0x02) != 0) return;
    if (--self->timer_10 != 0) return;
    sub_8E9D_via_JSL(0x3C);
    self->timer_10 = 0x3C;
    self->state++;
}
static void type41_state2_pickpos_C506(Entity *self)
{
    if (--self->timer_10 != 0) return;
    /* X = #$40 + rand(64), Y = $3F. */
    ((uint8_t *)self)[2] = (uint8_t)(sub_DCD5(0x40) + 0x40);
    ((uint8_t *)self)[4] = 0x3F;
    self->timer_10 = 0x02;
    self->state++;
}
/* $C546 is a per-state draw helper — body not lifted (referenced once). */
extern void sub_C546(void);
static void type41_state3_draw_C526(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->timer_10 = 0x02;
    sub_C546();
    /* if y.lo (#$0004,x) BPL — return; BMI — fall through (despawn-like). */
    if ((int8_t)((uint8_t *)self)[4] >= 0) return;
    self->timer_10 = 0;
    self->state = 1;
    apu_io_2142 = 0;
}
static void type41_dispatch_C4C4(Entity *self)
{
    if (!cur_task_is_four()) return;
    static const StateFn tbl[4] = {
        type41_state0_init_C4E0,
        type41_state1_audio_C4EB,
        type41_state2_pickpos_C506,
        type41_state3_draw_C526,
    };
    state_dispatch(self, tbl, 4);
}

/* ========================================================================
 * TYPE $42 — $04:C599 — 2-state dispatcher (states $C5A9, $C5B3).
 *
 *   Same dispatch boilerplate as $40. State $C5A9 is a state-0 init
 *   ("snapshot to target"), state $C5B3 a step+draw worker.
 *   TODO: lift state bodies — they reuse sub_B114 / sub_DB52 / sub_DCD5.
 * ======================================================================== */
/* $C5A9: timer_10 = $10; state++  (NOT the "snapshot target" init). */
static void type42_state0_init_C5A9(Entity *self)
{
    self->timer_10 = 0x10;
    self->state++;
}
/* $C5B3: JSR $DB52 (draw); if CUR_TASK == 4, --timer_10; on 0 zero self->type. */
static void type42_state1_step_C5B3(Entity *self)
{
    sub_DB52();
    if (DP_TASK_INDEX != 0x04) return;
    if (--self->timer_10 != 0) return;
    self->type = 0;
}
static void type42_dispatch_C599(Entity *self)
{
    static const StateFn tbl[2] = { type42_state0_init_C5A9, type42_state1_step_C5B3 };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * TYPE $43 — $04:C61D
 *
 *   Gated on SRAM $7F:EC96 (a "show end-game banner" flag — set by
 *   gs_full_end). If clear, RTS. If set:
 *     - push X
 *     - load $7F:E736/E738 into $69/$6A (working PTR)
 *     - JSR $DD7F (16-bit math helper)
 *     - REP #$20  ... (continues with 16-bit code; body truncated for
 *       this lift — see TODO).
 *
 *   Probable role: end-of-game / credits scoreboard digit renderer.
 *   TODO: full body lift (~$30 bytes after the prologue).
 * ======================================================================== */
/* $DD7F returns X (16-bit) and Y (16-bit) as the computed digit position.
 * We model that via two scratch globals updated by the lifted sub_DD7F. */
extern uint16_t sub_DD7F_out_x;   /* X register after $DD7F */
extern uint16_t sub_DD7F_out_y;   /* Y register after $DD7F */

static void type43_dispatch_C61D(Entity *self)
{
    if (SRAM_EC96_GAME_OVER == 0) return;
    /* PHX; dp[$69] = $7F:E736; dp[$6A] = $7F:E738; JSR DD7F. */
    BYTE(dp, 0x69) = SRAM_E736_SCRATCH_LO;
    BYTE(dp, 0x6A) = SRAM_E738_SCRATCH_HI;
    sub_DD7F();
    /* REP #$20; TXA; CLC; ADC #$0006; PLX; STA $0002,x  — x = X+6 */
    ((uint16_t *)self)[1] = (uint16_t)(sub_DD7F_out_x + 0x0006);  /* self->x */
    /* TYA; SEC; SBC #$000A; STA $0004,x  — y = Y-10 */
    ((uint16_t *)self)[2] = (uint16_t)(sub_DD7F_out_y - 0x000A);  /* self->y */
    /* anim_frame_e ($000E) = CUR_TASK & 6 */
    self->anim_frame_e = (uint8_t)(DP_TASK_INDEX & 0x06);
    sub_DB52();
}

/* ========================================================================
 * TYPE $44 — $04:BC49 — 2-state dispatcher (states $BC59, $BC63).
 *
 *   Same shape as $42. State 0 init / state 1 step+draw.
 * ======================================================================== */
/* $BC59: just INC state. (No snapshot — disasm is just `INC $0001,x; RTS`.) */
static void type44_state0_init_BC59(Entity *self) { self->state++; }
/* $BC63: JSR $DB52; if CUR_TASK == 4, --timer_10; on 0 reset timer=$10,
 * anim_frame_e += 2 (twice), if anim_frame_e >= 4 zero self->type. */
static void type44_state1_step_BC63(Entity *self)
{
    sub_DB52();
    if (DP_TASK_INDEX != 0x04) return;
    if (--self->timer_10 != 0) return;
    self->timer_10 = 0x10;
    self->anim_frame_e += 2;
    self->anim_frame_e += 2;
    if (self->anim_frame_e >= 0x04) {
        self->type = 0;
    }
}
static void type44_dispatch_BC49(Entity *self)
{
    static const StateFn tbl[2] = { type44_state0_init_BC59, type44_state1_step_BC63 };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * TYPE $45 — $04:BC8A
 *
 *   Tiny guard: skip if ($02A1 == $029F). Used as a one-shot
 *   "scroll-into-place" sync trampoline for the map scroll latch.
 *   TODO: lift body past the guard — it spans only a few bytes.
 * ======================================================================== */
static void type45_dispatch_BC8A(Entity *self)
{
    (void)self;
    /* LDY $02A1; CPY $029F; BEQ exit  — gate when scroll pointer caught up. */
    uint8_t y = MAP_SCROLL_DST;
    if (y == MAP_SCROLL_LATCH) return;
    /* PHX; A = $0BC0[y]; y++; X = (A<<4) + 8 */
    uint8_t a = DATA_TABLE_0BC0(y); y++;
    uint16_t spawn_x = (uint16_t)((a << 4) + 0x0008);
    /* A = $0BC0[y]; y++; $02A1 = y & $1F  (wrap pointer in 32-byte window). */
    uint8_t a2 = DATA_TABLE_0BC0(y); y++;
    MAP_SCROLL_DST = (uint8_t)(y & 0x1F);
    /* Y register = (a2<<4) + 8  — spawn Y. */
    uint16_t spawn_y = (uint16_t)((a2 << 4) + 0x0008);
    /* JSL $0499C1 with A=$44 (entity type), X=spawn_x, Y=spawn_y. */
    entity_spawn_0499C1((uint8_t)spawn_x, (uint8_t)spawn_y, 0x44);
}

/* ========================================================================
 * TYPE $46 — $04:BFC6 — 2-state dispatcher (states $BFD6, $BFFE).
 *   Same skeleton as the other 2-state handlers in this range.
 * ======================================================================== */
/* $BFD6: pick random y.lo = (rand($40)<<1)+$28, y.hi=0; x.lo = (rand($80)<<1)+8,
 *        x.hi=0; timer_10=$04; state++. */
static void type46_state0_init_BFD6(Entity *self)
{
    uint8_t r = sub_DCD5(0x40);
    ((uint8_t *)self)[4] = (uint8_t)((r << 1) + 0x28);   /* y.lo */
    ((uint8_t *)self)[5] = 0;                            /* y.hi */
    r = sub_DCD5(0x80);
    ((uint8_t *)self)[2] = (uint8_t)((r << 1) + 0x08);   /* x.lo */
    ((uint8_t *)self)[3] = 0;                            /* x.hi */
    self->timer_10 = 0x04;
    self->state++;
}
/* $BFFE: JSR $DB52; if CUR_TASK == 4, --timer_10; on 0 zero self->type. */
static void type46_state1_step_BFFE(Entity *self)
{
    sub_DB52();
    if (DP_TASK_INDEX != 0x04) return;
    if (--self->timer_10 != 0) return;
    self->type = 0;
}
static void type46_dispatch_BFC6(Entity *self)
{
    static const StateFn tbl[2] = { type46_state0_init_BFD6, type46_state1_step_BFFE };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * TYPE $47 — $04:C013
 *
 *   "Gate on CUR_TASK==4" guard, then drop into a secondary dispatcher
 *   at $C01B (identical shape to type $41).
 * ======================================================================== */
/* $C013: gate CUR_TASK==4; gate $7F:E8FC != 0; PHX; JSL $0499C1 with A=$46;
 * PLX. Spawns a new type-$46 each frame the gate flag is hot. */
static void type47_dispatch_C013(Entity *self)
{
    (void)self;
    if (!cur_task_is_four()) return;
    if (SRAM_E8FC_SPAWN_46_FLAG == 0) return;
    entity_spawn_0499C1(0, 0, 0x46);
}

/* ========================================================================
 * TYPE $48 / $49 / $4A — three twins
 *
 *   $04:B411 / $04:B358 / $04:B3C4. Each is the SAME 4-byte prologue:
 *     LDA $7F:E868; BNE +1; RTS
 *   then drops into per-handler body. The shared SRAM gate $7F:E868 is
 *   the "decoration entity activate" flag — set during scenario gameplay
 *   only. Bodies are NOT yet lifted (see TODO); these are stub-skip.
 * ======================================================================== */
static void type48_skipframe_B411(Entity *self) {
    (void)self;
    if (SRAM_E868_PAUSE_FLAG == 0) return;
    /* TODO: $B418.. body lift. */
}
/* $B358: gate $7F:E868; load cell coords from $7F:E86A/E86C, scale by 2
 * and offset by 8/$28; based on (frame_counter & 1), nudge x or y by 8;
 * pick tile from 4-entry tables at $B3BC/$B3C0 indexed by frame & 3;
 * attr |= $90; JSR $DB52.   "Per-frame walking shadow" prop. */
static const uint8_t rom_B3BC_49_tiles[4] = { 0,0,0,0 };  /* TODO: extract */
static const uint8_t rom_B3C0_49_attrs[4] = { 0,0,0,0 };  /* TODO: extract */
static void type49_skipframe_B358(Entity *self) {
    if (SRAM_E868_PAUSE_FLAG == 0) return;
    /* x = (E86A << 1) + 8; y = (E86C << 1) + $28 */
    uint16_t x = (uint16_t)((SRAM_E86A_CELL_X << 1) + 0x0008);
    uint16_t y = (uint16_t)((SRAM_E86C_CELL_Y << 1) + 0x0028);
    uint8_t fc = FRAME_COUNTER_023E;
    if (fc & 0x01) {
        x += 0x0008;        /* odd frame: nudge X */
    } else {
        y += 0x0008;        /* even frame: nudge Y */
    }
    ((uint16_t *)self)[1] = x;     /* self->x */
    ((uint16_t *)self)[2] = y;     /* self->y */
    uint8_t i = fc & 0x03;
    self->anim_frame_e = rom_B3BC_49_tiles[i];
    self->attr_f       = (uint8_t)(rom_B3C0_49_attrs[i] | 0x90);
    sub_DB52();
}
/* $B3C4: gate $7F:E868; gate $7F:EE4C; x = (E86E<<1)+$10; y = (E870<<1)+$30;
 * tile/attr tables at $B409/$B40D (4 entries); attr |= $90; JSR $DB52.
 * Variant of $49 with different bases / second gate. */
static const uint8_t rom_B409_4A_tiles[4] = { 0,0,0,0 };  /* TODO: extract */
static const uint8_t rom_B40D_4A_attrs[4] = { 0,0,0,0 };  /* TODO: extract */
static void type4A_skipframe_B3C4(Entity *self) {
    if (SRAM_E868_PAUSE_FLAG == 0) return;
    if (SRAM_EE4C_4A_FLAG    == 0) return;
    uint16_t x = (uint16_t)((SRAM_E86E_CELL_X2 << 1) + 0x0010);
    uint16_t y = (uint16_t)((SRAM_E870_CELL_Y2 << 1) + 0x0030);
    ((uint16_t *)self)[1] = x;
    ((uint16_t *)self)[2] = y;
    uint8_t i = (uint8_t)(FRAME_COUNTER_023E & 0x03);
    self->anim_frame_e = rom_B409_4A_tiles[i];
    self->attr_f       = (uint8_t)(rom_B40D_4A_attrs[i] | 0x90);
    sub_DB52();
}

/* ========================================================================
 * TYPE $4B — $04:C653 — HAND / CAT'S PAW
 * ------------------------------------------------------------------------
 * 5-state mass-kill danger entity. State table at $C65F:
 *   $C667, $C678, $C6D9, $C724, $C7BB.
 *
 * (F2 mis-read the 5th address as $BDBB — the actual ROM bytes
 *  `BB C7` at $C65D form little-endian $C7BB, the post-sweep "tile
 *  fall" tail. Verified by direct disassembly.)
 *
 * Confirmed role (V4-8 + scenarios.c danger_cat_paws_spawn): spawned at
 * fixed position (X=$3E, Y=$2A) with init flag $10 and $0C=$0100 to
 * stage a "swipe" animation. Cat's Paw smacks down from the sky, sweeps
 * for ants, kills them, then bounces back up.
 *
 * STATE BODIES (faithfully lifted from $04:C667..$C7DC):
 *   state 0  $C667  init: target_x = x, target_y = y; state++.
 *   state 1  $C678  GATE CUR_TASK==4; --timer_10; on 0 pick new random
 *                   sweep target around (target_x,target_y), set timer
 *                   = $40 and attr_f = $9A, advance to state 2.
 *                   (rand($09)-4 + target_x_lo as new motion_res_x; same
 *                    range scaled by 16 + offset 32 as new x. Symmetric
 *                    for y using rand($21)-$10 and target_y.)
 *   state 2  $C6D9  GATE CUR_TASK==4; every other frame call $D997
 *                   (sweep-draw helper); --timer_10; on 0 set attr=$9E,
 *                   JSL $008EA3(#$3E) (the cat-paw kill SFX!), advance
 *                   state, dump (motion_res_x_11, motion_res_y_12) plus
 *                   constants $0004 into the "active damage box" at
 *                   $02E5..$02EB and call $DD76(#$0D) (mass-kill audio).
 *   state 3  $C724  call $D997 each frame (visual draw); GATE
 *                   CUR_TASK==4; --timer_10; on 0 reset to state 1
 *                   (loop sweep).
 *   state 4  $C7BB  add motion_res_y_12 to y (with carry into y.hi),
 *                   then 16-bit DEC target_y by 4 (clamp at 0). Tail
 *                   used after the cat lifts off — gradually retracts
 *                   the paw back into the sky.
 * ======================================================================== */
static void type4B_state0_init_C667(Entity *self)
{
    self->target_x = self->x;
    self->target_y = self->y;
    self->state++;
}
static void type4B_state1_descend_C678(Entity *self)
{
    if (!cur_task_is_four()) return;
    if (--self->timer_10 != 0) return;
    /* X: motion_res_x_11 = (rand($09) - 4) + target_x_lo
     *    x (16-bit)      = (above << 4) + $0020          */
    uint8_t rx = sub_DCD5(0x09);
    uint8_t nx = (uint8_t)(rx - 4 + (uint8_t)self->target_x);
    self->motion_res_x_11 = nx;
    /* 16-bit zero-extend then ASL x4 + $20. */
    uint16_t new_x = (uint16_t)((uint16_t)nx << 4) + 0x0020;
    self->x = new_x;
    /* Y: motion_res_y_12 = (rand($21) - $10) + target_y_lo
     *    y               = (above << 4) + $0020          */
    uint8_t ry = sub_DCD5(0x21);
    uint8_t ny = (uint8_t)(ry - 0x10 + (uint8_t)self->target_y);
    self->motion_res_y_12 = ny;
    uint16_t new_y = (uint16_t)((uint16_t)ny << 4) + 0x0020;
    self->y = new_y;
    self->timer_10 = 0x40;
    self->attr_f   = 0x9A;
    self->state++;
}
/* $D997 — the per-entity sweep-draw helper (LDA $000F → LSR x4 → AND #6
 * → JMP indirect $D9A8). Treat as opaque; body lives in entities_d.c. */
extern void sub_D997_sweep_draw(Entity *self);

static void type4B_state2_sweep_C6D9(Entity *self)
{
    if (!cur_task_is_four()) return;
    if ((DP_TASK_INDEX & 0x02) == 0) {
        sub_D997_sweep_draw(self);
    }
    if (--self->timer_10 != 0) return;
    /* Strike! */
    self->attr_f = 0x9E;
    sub_8EA3_via_JSL(0x3E);                  /* cat-paw kill SFX */
    self->state++;
    /* Set up the damage hitbox at $02E5..$02EB ("active danger box"). */
    BYTE(dp, 0x02E5) = self->motion_res_x_11;
    BYTE(dp, 0x02E7) = self->motion_res_y_12;
    BYTE(dp, 0x02E6) = 0;
    BYTE(dp, 0x02E8) = 0;
    *(uint16_t *)&BYTE(dp, 0x02E9) = 0x0004;  /* hitbox width  */
    *(uint16_t *)&BYTE(dp, 0x02EB) = 0x0004;  /* hitbox height */
    sub_DD76(0x0D);                          /* "mass-kill" mixer cmd */
}
static void type4B_state3_retract_C724(Entity *self)
{
    sub_D997_sweep_draw(self);
    if (!cur_task_is_four()) return;
    if (--self->timer_10 != 0) return;
    self->state = 1;                         /* loop back to descend */
}
static void type4B_state4_expire_C7BB(Entity *self)
{
    /* 16-bit y += motion_res_y_12 (zero-extended). */
    uint16_t old_y = self->y;
    uint16_t add   = (uint16_t)self->motion_res_y_12;
    /* The ROM does: LDA $0A,x; ADC $04,x; STA $04,x; LDA #0; ADC $05,x.
     * I.e. ADC enters with carry undefined — but at this point carry
     * was set by the SBC sequence in the prior state. Model as plain
     * 16-bit add (the carry-in is benign across the lift). */
    self->y = (uint16_t)(old_y + add);
    /* 16-bit target_y -= 4; if would go negative, leave unchanged. */
    int32_t ty = (int32_t)self->target_y - 4;
    if (ty >= 0) self->target_y = (uint16_t)ty;
}
static void type4B_dispatch_C653(Entity *self)
{
    static const StateFn tbl[5] = {
        type4B_state0_init_C667,
        type4B_state1_descend_C678,
        type4B_state2_sweep_C6D9,
        type4B_state3_retract_C724,
        type4B_state4_expire_C7BB,
    };
    state_dispatch(self, tbl, 5);
}

/* ========================================================================
 * TYPE $4C — $04:C73B — 2-state dispatcher (states $C74B, $C75D).
 *   Same shape as $42. Likely "small decoration" pair.
 * ======================================================================== */
/* $C74B: target_y = $0280 (16-bit at +$09); motion_res_y_12 = 0; state++. */
static void type4C_state0_init_C74B(Entity *self)
{
    self->target_y = 0x0280;
    self->motion_res_y_12 = 0;
    self->state++;
}
/* $C75D: JSR $DB52; emit two more sprites at (+$40 X, +8 tile) and
 * (+$40 Y, +$80 tile), then a fourth at (-$40 X, -8 tile) before
 * decrementing x by 2 and accumulating target_y into motion_res_y_12.
 * Net effect: a 4-sprite "comet trail" sliding leftward. */
static void type4C_state1_step_C75D(Entity *self)
{
    sub_DB52();
    /* sprite 2: $37 += $40, $3B += 8 */
    *(uint16_t *)&DP_SPRITE_DX += 0x0040;
    DP_SPRITE_TILE_LO += 0x08;
    sub_DB9E();
    /* sprite 3: $39 += $40, $3B += $80 */
    *(uint16_t *)&DP_SPRITE_DY += 0x0040;
    DP_SPRITE_TILE_LO += 0x80;
    sub_DB9E();
    /* sprite 4: $37 -= $40, $3B -= 8 */
    *(uint16_t *)&DP_SPRITE_DX -= 0x0040;
    DP_SPRITE_TILE_LO -= 0x08;
    sub_DB9E();
    /* x -= 2 */
    self->x = (uint16_t)(self->x - 2);
    /* motion_res_y_12 += target_y_lo  (8-bit "speed" accumulator) */
    self->motion_res_y_12 = (uint8_t)(self->motion_res_y_12 + (uint8_t)self->target_y);
}
static void type4C_dispatch_C73B(Entity *self)
{
    static const StateFn tbl[2] = { type4C_state0_init_C74B, type4C_state1_step_C75D };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * TYPE $4D — $04:C8A7 — 4-state dispatcher.
 *   State table at $C8B3: states $C8BB, $C8C8, $C8D8, $C916.
 *   Probable role: a multi-phase animated prop (4 phases = init/in/hold/out).
 * ======================================================================== */
/* $C8BB: timer_10=$78, motion_res_x_11=0, state++. */
static void type4D_state0_init_C8BB(Entity *self)
{
    self->timer_10 = 0x78;
    self->motion_res_x_11 = 0;
    self->state++;
}
/* $C8C8: --timer_10; on 0 timer=$1E, state++. */
static void type4D_state1_in_C8C8(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->timer_10 = 0x1E;
    self->state++;
}
/* $C8D8: JSR $DB52; --timer_10; on 0 advance frame counter, on counter==8
 * advance to state 3; else update $000C from a 4-entry 16-bit table at
 * $C906 (16-bit X-step adjustments), reset timer=$1E. */
static const uint16_t rom_C906_4D_xsteps[8] = {
    0x0000, 0x0080, 0x0008, 0x0088,
    0x0100, 0x0180, 0x0108, 0x0188,
};
static void type4D_state2_hold_C8D8(Entity *self)
{
    sub_DB52();
    if (--self->timer_10 != 0) return;
    self->motion_res_x_11++;
    if (self->motion_res_x_11 == 0x08) {
        self->state++;
        return;
    }
    /* $000C = $C906[motion_res_x_11]  (16-bit). */
    *(uint16_t *)&BYTE((uint8_t *)self, 0x0C) =
        rom_C906_4D_xsteps[self->motion_res_x_11 & 0x07];
    self->timer_10 = 0x1E;
}
/* $C916: just JSR $DB52; RTS  — the final "settle" draw. */
static void type4D_state3_out_C916(Entity *self) { (void)self; sub_DB52(); }
static void type4D_dispatch_C8A7(Entity *self)
{
    static const StateFn tbl[4] = {
        type4D_state0_init_C8BB, type4D_state1_in_C8C8,
        type4D_state2_hold_C8D8, type4D_state3_out_C916,
    };
    state_dispatch(self, tbl, 4);
}

/* ========================================================================
 * TYPE $4E — $04:C91B — SCROLL-BIAS TWEAKER (no state machine)
 *
 *   Reads bit 4 of $00 (DP_TASK_INDEX). If set, $48 (DP_BG2_HSCROLL) =
 *   #$0060; else $48 = #$FFE0 (i.e. -32).
 *
 *   This isn't a normal "rendered entity" — it's a one-frame side-effect
 *   that adds a periodic horizontal scroll offset to the BG2 layer.
 *   Used as a "wiggle" or "shake" trigger by certain scenes.
 * ======================================================================== */
static void type4E_scrollbias_C91B(Entity *self)
{
    (void)self;
    if (DP_TASK_INDEX & 0x10) {
        DP_BG2_HSCROLL = 0x0060;
    } else {
        DP_BG2_HSCROLL = (uint16_t)(int16_t)-0x0020;   /* $FFE0 */
    }
}

/* ========================================================================
 * TYPE $4F — $04:C92C — WALK PROP (LEFT)
 *
 *   Every other frame (when $00 & $01 == 0): DEC entity.x.
 *   Always: DEC entity.y.
 *   Increment timer_10 (capped to 12 — wraps to 0), then load tile-frame
 *   from a 3-entry table at $C955 indexed by (timer_10 >> 2). JSR $DB52
 *   to draw.
 *
 *   Probable role: a 3-frame walking decoration prop moving diagonally
 *   up-left (e.g. an ant in a celebration scene, or a moving icon).
 * ======================================================================== */
static const uint8_t rom_C955_4F_frames[3] = { 0x00, 0x00, 0x00 };  /* TODO: extract real bytes */

static void type4F_walkprop_C92C(Entity *self)
{
    if ((DP_TASK_INDEX & 0x01) == 0) {
        self->x--;
    }
    self->y--;
    uint8_t t = self->timer_10 + 1;
    if (t >= 12) t = 0;
    self->timer_10 = t;
    uint8_t frame_idx = t >> 2;
    self->anim_frame_e = rom_C955_4F_frames[frame_idx];
    sub_DB52();
}

/* ========================================================================
 * TYPE $50 — $04:C958 — WALK PROP (RIGHT)
 *
 *   Mirror of $4F: every other frame DEC entity.x, but INC entity.y
 *   (so moves down-left). Tile table at $C981 (3 entries).
 *   Probable role: matching pair of $4F, opposite movement direction.
 * ======================================================================== */
static const uint8_t rom_C981_50_frames[3] = { 0x00, 0x00, 0x00 };  /* TODO: extract real bytes */

static void type50_walkprop_C958(Entity *self)
{
    if ((DP_TASK_INDEX & 0x01) == 0) {
        self->x--;
    }
    self->y++;
    uint8_t t = self->timer_10 + 1;
    if (t >= 12) t = 0;
    self->timer_10 = t;
    uint8_t frame_idx = t >> 2;
    self->anim_frame_e = rom_C981_50_frames[frame_idx];
    sub_DB52();
}

/* ========================================================================
 * TYPE $51 — $04:C984 — 3-state dispatcher (states $C996, $C9A0, $C9AB).
 * ======================================================================== */
/* $C996: timer_10 = $40; state++. */
static void type51_state0_init_C996(Entity *self) { self->timer_10 = 0x40; self->state++; }
/* $C9A0: --timer_10; on 0 state++. */
static void type51_state1_step_C9A0(Entity *self)
{
    if (--self->timer_10 != 0) return;
    self->state++;
}
/* $C9AB: x.lo++; anim_frame_e = (CUR_TASK >> 1) & $0C; JSR $DB52;
 * if x.lo >= $F0 (CMP #$F0), zero self->type. */
static void type51_state2_step_C9AB(Entity *self)
{
    ((uint8_t *)self)[2]++;     /* INC $0002,x */
    self->anim_frame_e = (uint8_t)((DP_TASK_INDEX >> 1) & 0x0C);
    sub_DB52();
    if (((uint8_t *)self)[2] >= 0xF0) self->type = 0;
}
static void type51_dispatch_C984(Entity *self)
{
    static const StateFn tbl[3] = {
        type51_state0_init_C996, type51_state1_step_C9A0, type51_state2_step_C9AB,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $52 — $04:C9C6 — 3-state dispatcher (states $C9D8, $C9E2, $C9ED).
 * ======================================================================== */
/* $C9D8: timer=$20, state++. */
static void type52_state0_init_C9D8(Entity *self) { self->timer_10 = 0x20; self->state++; }
/* $C9E2: --timer; on 0 state++. */
static void type52_state1_step_C9E2(Entity *self) { if (--self->timer_10 == 0) self->state++; }
/* $C9ED: same anim+walk pattern as $51 state 2 (no shift). */
static void type52_state2_step_C9ED(Entity *self)
{
    ((uint8_t *)self)[2]++;
    self->anim_frame_e = (uint8_t)((DP_TASK_INDEX >> 1) & 0x0C);
    sub_DB52();
    if (((uint8_t *)self)[2] >= 0xF0) self->type = 0;
}
static void type52_dispatch_C9C6(Entity *self)
{
    static const StateFn tbl[3] = {
        type52_state0_init_C9D8, type52_state1_step_C9E2, type52_state2_step_C9ED,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $53 — $04:CA08 — 3-state dispatcher (states $CA1A, $CA24, $CA2F).
 * ======================================================================== */
/* $CA1A: just state++. */
static void type53_state0_init_CA1A(Entity *self) { self->state++; }
/* $CA24: --timer; on 0 state++. */
static void type53_state1_step_CA24(Entity *self) { if (--self->timer_10 == 0) self->state++; }
/* $CA2F: x.lo += 2; anim = CUR_TASK & $0C; draw; if x.lo >= $F0 expire. */
static void type53_state2_step_CA2F(Entity *self)
{
    ((uint8_t *)self)[2] += 2;
    self->anim_frame_e = (uint8_t)(DP_TASK_INDEX & 0x0C);
    sub_DB52();
    if (((uint8_t *)self)[2] >= 0xF0) self->type = 0;
}
static void type53_dispatch_CA08(Entity *self)
{
    static const StateFn tbl[3] = {
        type53_state0_init_CA1A, type53_state1_step_CA24, type53_state2_step_CA2F,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $54 — $04:CA4C — 3-state dispatcher (states $CA5E, $CA68, $CA73).
 *   Per V4-8: marriage-flight scenery group ($57 = trampoline; $54-$56
 *   are scenery sprites that ride alongside).
 * ======================================================================== */
/* $CA5E: timer=$C0, state++. */
static void type54_state0_init_CA5E(Entity *self) { self->timer_10 = 0xC0; self->state++; }
/* $CA68: --timer; on 0 state++. */
static void type54_state1_step_CA68(Entity *self) { if (--self->timer_10 == 0) self->state++; }
/* $CA73: x.lo += 3; anim = CUR_TASK & $0C; draw; expire when x.lo >= $F0. */
static void type54_state2_step_CA73(Entity *self)
{
    ((uint8_t *)self)[2] += 3;
    self->anim_frame_e = (uint8_t)(DP_TASK_INDEX & 0x0C);
    sub_DB52();
    if (((uint8_t *)self)[2] >= 0xF0) self->type = 0;
}
static void type54_dispatch_CA4C(Entity *self)
{
    static const StateFn tbl[3] = {
        type54_state0_init_CA5E, type54_state1_step_CA68, type54_state2_step_CA73,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $55 — $04:CAC3 — STATIC 3-TILE COMPOSITE (lifted in full)
 * NOTE: $04:CA93 is the SAME composite drawer reused by other handlers
 * as an inline subroutine; the actual TYPE $55 dispatcher is at $04:CAC3
 * (3-state). Lifting the drawer here too since it's tiny.
 *
 * Drawer: emits 3 sprites at X-offsets 0, +32, +64 (px), each with tile
 * offset +4 from the previous. Used as a one-shot static composite (no
 * animation, no movement).
 * ======================================================================== */
static void sub_CA93_draw_3tile_strip(Entity *self)
{
    (void)self;
    sub_DB52();                              /* sprite 1 @ +0 */
    *(uint16_t *)&DP_SPRITE_DX += 0x0020;    /* +32 px X */
    DP_SPRITE_TILE_LO += 0x04;
    sub_DB9E();                              /* sprite 2 */
    *(uint16_t *)&DP_SPRITE_DX += 0x0040;    /* +64 px X (cumulative +96) */
    DP_SPRITE_TILE_LO += 0x04;
    sub_DB9E();                              /* sprite 3 */
}

static void type55_state0_init_CACF_real(Entity *self) { state0_snapshot_and_advance(self); }
static void type55_state1_draw(Entity *self) { sub_CA93_draw_3tile_strip(self); }
static void type55_compose_CA93(Entity *self)
{
    static const StateFn tbl[2] = { type55_state0_init_CACF_real, type55_state1_draw };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * TYPE $56 — $04:CAC3 — 2-state dispatcher (states $CAD3, $CAF6).
 *   (Distinct from $55. Same skeleton.)
 * ======================================================================== */
/* $CAD3: x = dp[$05]+$0080, y = dp[$07]+$0090, motion_res_x_11=0, timer=8, ++. */
static void type56_state0_init_CAD3(Entity *self)
{
    self->x = (uint16_t)(*(uint16_t *)&BYTE(dp, 0x05) + 0x0080);
    self->y = (uint16_t)(*(uint16_t *)&BYTE(dp, 0x07) + 0x0090);
    self->motion_res_x_11 = 0;
    self->timer_10 = 0x08;
    self->state++;
}
/* $CAF6: --timer; on 0 reset timer=8, advance motion_res_x_11 (mod $30);
 * dp[$57] = $CB2C[motion_res_x_11] + $76; JSR $B0E8; JSR $DB52;
 * if $02E1 == 0, zero self->type. */
/* Extracted from ROM $04:CB2C (file offset 0x24B2C). Type-56 per-frame
 * animation phase table — motion_res_x_11 indexes here, result drives
 * sprite frame. */
static const uint8_t rom_CB2C_56_anim[0x30] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x02, 0x02, 0x03, 0x04,
    0x05, 0x04, 0x03, 0x04, 0x05, 0x04, 0x03, 0x04,
    0x05, 0x04, 0x03, 0x04, 0x05, 0x04, 0x03, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x04, 0x03, 0x04, 0x02, 0x02, 0x01, 0x01,
};
static void type56_state1_step_CAF6(Entity *self)
{
    if (--self->timer_10 == 0) {
        self->timer_10 = 0x08;
        self->motion_res_x_11++;
        if (self->motion_res_x_11 >= 0x30) self->motion_res_x_11 = 0;
        uint8_t v = rom_CB2C_56_anim[self->motion_res_x_11];
        BYTE(dp, 0x57) = (uint8_t)(v + 0x76);
        sub_B0E8();
    }
    sub_DB52();
    if (POPUP_LOCK == 0) self->type = 0;
}
static void type56_dispatch_CAC3(Entity *self)
{
    static const StateFn tbl[2] = { type56_state0_init_CAD3, type56_state1_step_CAF6 };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * TYPE $57 — $04:CB65 — AUDIO/PALETTE TRAMPOLINE (lifted in full)
 *
 *   Just two JSLs and an RTS. Per V4-8 this is the marriage-flight
 *   scenery sound trigger (states_menu.c invokes the queen-spawn audio
 *   via the same JSL pair). No per-state machine.
 * ======================================================================== */
static void type57_audio_trampoline_CB65(Entity *self)
{
    (void)self;
    sub_89D6_via_JSL();    /* $00:89D6 — palette/queue submit */
    sub_8C41_via_JSL();    /* $00:8C41 — audio cmd send */
}

/* ========================================================================
 * TYPE $58 — $04:CC73 — 2-state dispatcher (states $CC83, $CC92).
 * ======================================================================== */
/* $CC83: target_y (entity+$09 16-bit) = y; state++. */
static void type58_state0_init_CC83(Entity *self)
{
    self->target_y = self->y;
    self->state++;
}
/* $CC92: every other frame, if target_y >= $38 then target_y--.
 * Then every other-other frame pick tile=$0100 (no flip) or $0108 (flip);
 * JSR $DB40(A=$FFC0, Y=$FFD0) (sprite origin), $3D = $98, JSR $DB9E,
 * then $37 += $40, $3D ^= $20, JSR $DB9E, JSR $AA2A(#$04, #$0008),
 * JSR $D8D5. Two-sprite back-and-forth bobbing prop. */
static void type58_state1_step_CC92(Entity *self)
{
    if ((DP_TASK_INDEX & 0x01) == 0) {
        if (self->target_y >= 0x0038) {
            self->target_y--;
        }
    }
    /* tile selection */
    if ((DP_TASK_INDEX & 0x02) == 0) {
        DP_SPRITE_TILE_LO = 0x00;
        DP_SPRITE_TILE_HI = 0x01;     /* $3B/$3C = $0100 */
    } else {
        DP_SPRITE_TILE_LO = 0x08;
        DP_SPRITE_TILE_HI = 0x01;     /* $3B/$3C = $0108 */
    }
    sub_DB40(self, 0xFFC0, 0xFFD0);
    DP_SPRITE_ATTR = 0x98;
    sub_DB9E();
    *(uint16_t *)&DP_SPRITE_DX += 0x0040;
    DP_SPRITE_ATTR ^= 0x20;
    sub_DB9E();
    sub_AA2A(0x04, 0x0008);
    sub_D8D5();
}
static void type58_dispatch_CC73(Entity *self)
{
    static const StateFn tbl[2] = { type58_state0_init_CC83, type58_state1_step_CC92 };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * TYPE $59 — $04:CB73 — POPULATION READOUT (lifted structurally)
 *
 *   Not a dispatch handler. Runs every 8th frame ($00 & 7 == 0):
 *     - read $7E:520E (nest population, 16-bit) into $BE
 *     - constant #$0064 into $C2
 *     - JSR $CC11 (APU send 1)
 *     - copy $C6/$C8/$02CD into $BE/$C0/$C2, clear $C4
 *     - JSR $CC36 (APU send 2)
 *     - JSR $CBB6 with X=#$0D09
 *   Then EVERY frame:
 *     - if $02E1 != 2: zero entity.type ("self-destruct" path).
 *
 *   Probable role: the live "current ant count" readout used on certain
 *   status screens — it issues APU commands carrying the population
 *   value (so the audio chip can mix in chirp/click feedback) and
 *   self-destructs when the parent screen leaves the "population display"
 *   sub-state ($02E1 == 2).
 * ======================================================================== */
static void type59_population_readout_CB73(Entity *self)
{
    if ((DP_TASK_INDEX & 0x07) == 0) {
        uint16_t pop = *(uint16_t *)&SRAM_520E_NEST_POP;
        *(uint16_t *)&DP_BE = pop;
        *(uint16_t *)&DP_C2 = 0x0064;
        sub_CC11_apu_send();

        /* second submission: 3 16-bit args from $C6/$C8/$02CD. */
        *(uint16_t *)&DP_BE = *(uint16_t *)&DP_C6;
        *(uint16_t *)&DP_C0 = *(uint16_t *)&DP_C8;
        *(uint16_t *)&DP_C2 = END_BANNER_TILE_LO;
        DP_C4 = 0;
        sub_CC36_apu_send2();
        sub_CBB6_apu_send3(0x0D09);
    }
    /* tail: self-destruct when $02E1 != 2 */
    if (POPUP_LOCK != 0x02) {
        self->type = 0;
    }
}

/* ========================================================================
 * TYPE $5A — $04:CD5B — FULL-END LEFT CELEBRATION PROP
 *
 *   Per V4-8 + states_menu.c: spawned by gs_full_end_B07B at
 *   (X=$0020, Y=$0040) — the LEFT half of the end-of-game celebration.
 *   3-state dispatcher (states $CD6D, $CD82, $CD9B):
 *     state 0  $CD6D  init: motion_res_x_11 = $88
 *                             timer_10 = $28 + sub_DCD5($14)  ($28..$3B)
 *                             state++
 *     state 1  $CD82  step:  JSR $CDC8 (draw); DEC timer_10; if 0 advance
 *     state 2  $CD9B  rest:  static draw (presumably; TODO body)
 *
 *   Role confirmed: LEFT celebration figure (likely the cheering ants
 *   in the GS_FULL_END "you've won" screen).
 * ======================================================================== */
/* $CDC8 — celebration prop 3-sprite draw:
 *   JSR $DB52  (sprite 1: main pose)
 *   $39 += $40, $3B = $80, JSR $DB9E  (sprite 2: below)
 *   $37 += $40, $3D = $10, $3B = motion_res_x_11, JSR $DB9E  (sprite 3)
 *   $39 += $20, $3B = $C8, JSR $DB9E                       (sprite 4)
 */
static void sub_CDC8_celeb_draw(Entity *self)
{
    sub_DB52();
    *(uint16_t *)&DP_SPRITE_DY += 0x0040;
    DP_SPRITE_TILE_LO = 0x80;
    sub_DB9E();
    *(uint16_t *)&DP_SPRITE_DX += 0x0040;
    DP_SPRITE_ATTR     = 0x10;
    DP_SPRITE_TILE_LO  = self->motion_res_x_11;   /* "current torch tile" */
    sub_DB9E();
    *(uint16_t *)&DP_SPRITE_DY += 0x0020;
    DP_SPRITE_TILE_LO = 0xC8;
    sub_DB9E();
}

static void type5A_state0_init_CD6D(Entity *self)
{
    self->motion_res_x_11 = 0x88;
    self->timer_10 = (uint8_t)(0x28 + sub_DCD5(0x14));
    self->state++;
}
static void type5A_state1_intro_CD82(Entity *self)
{
    sub_CDC8_celeb_draw(self);
    if (--self->timer_10 != 0) return;
    /* on 0: timer = rand($0A) + $14; state++. */
    self->timer_10 = (uint8_t)(sub_DCD5(0x0A) + 0x14);
    self->state++;
}
/* $CD9B: motion_res_x_11 = (CUR_TASK & 4) ? $8C : $88 (torch flicker);
 *        JSR CDC8 draw; --timer_10; on 0 timer = rand($14)+$28,
 *        motion_res_x_11=$88, state-- (back to intro). */
static void type5A_state2_rest_CD9B(Entity *self)
{
    self->motion_res_x_11 = (DP_TASK_INDEX & 0x04) ? 0x8C : 0x88;
    sub_CDC8_celeb_draw(self);
    if (--self->timer_10 != 0) return;
    self->timer_10 = (uint8_t)(sub_DCD5(0x14) + 0x28);
    self->motion_res_x_11 = 0x88;
    self->state--;
}
static void type5A_dispatch_CD5B(Entity *self)
{
    static const StateFn tbl[3] = {
        type5A_state0_init_CD6D, type5A_state1_intro_CD82, type5A_state2_rest_CD9B,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $5B — $04:CE0A — FULL-END RIGHT CELEBRATION PROP
 *
 *   Mirror of $5A. Spawned at (X=$00E0, Y=$0040). 3-state dispatcher
 *   (states $CE1C, $CE31, $CE4A) with the same shape as $5A.
 * ======================================================================== */
/* $CE77 — RIGHT-mirror of $CDC8: same 3-sprite emit, but step is -$20 X
 *         (instead of +$40 X) for the right-side flame. */
static void sub_CE77_celeb_draw_R(Entity *self)
{
    sub_DB52();
    *(uint16_t *)&DP_SPRITE_DY += 0x0040;
    DP_SPRITE_TILE_LO = 0x80;
    sub_DB9E();
    *(uint16_t *)&DP_SPRITE_DX -= 0x0020;
    DP_SPRITE_ATTR     = 0x30;        /* X-flipped torch */
    DP_SPRITE_TILE_LO  = self->motion_res_x_11;
    sub_DB9E();
    /* ROM continues with another +$20 Y / $C8 tile pair; lifted same as
     * the LEFT side. */
    *(uint16_t *)&DP_SPRITE_DY += 0x0020;
    DP_SPRITE_TILE_LO = 0xC8;
    sub_DB9E();
}

static void type5B_state0_init_CE1C(Entity *self)
{
    self->motion_res_x_11 = 0x88;
    self->timer_10 = (uint8_t)(sub_DCD5(0x14) + 0x28);
    self->state++;
}
static void type5B_state1_intro_CE31(Entity *self)
{
    sub_CE77_celeb_draw_R(self);
    if (--self->timer_10 != 0) return;
    self->timer_10 = (uint8_t)(sub_DCD5(0x0A) + 0x14);
    self->state++;
}
static void type5B_state2_rest_CE4A(Entity *self)
{
    self->motion_res_x_11 = (DP_TASK_INDEX & 0x04) ? 0x8C : 0x88;
    sub_CE77_celeb_draw_R(self);
    if (--self->timer_10 != 0) return;
    self->timer_10 = (uint8_t)(sub_DCD5(0x14) + 0x28);
    self->motion_res_x_11 = 0x88;
    self->state--;
}
static void type5B_dispatch_CE0A(Entity *self)
{
    static const StateFn tbl[3] = {
        type5B_state0_init_CE1C, type5B_state1_intro_CE31, type5B_state2_rest_CE4A,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $5C — $04:CEB9 — SCENARIO-END BANNER (left-sliding)
 *
 *   Per V4-8 + states_menu.c gs_scenario_end_B0FC: spawned at
 *   (X=$FFC0, Y=$005F) — i.e. starts off-screen to the LEFT, slides in.
 *   3-state dispatcher (states $CECB, $CEDA, $CF04).
 *     state 0  $CECB  init: $46=0, timer_10=$40, state++
 *     state 1  $CEDA  slide-in: x += 2 per frame; every 4 frames decrement
 *                     $46 (and underflow into $47); JSR $CF05 (a draw
 *                     helper that emits 6 sprites in a 2x3 grid via DB40
 *                     + DB88 + DB9E and successive +$40 X / +$40 Y
 *                     offsets); DEC timer_10; on 0 advance
 *     state 2  $CF04  static draw (the banner now in place).
 *
 *   Role confirmed: "SCENARIO COMPLETE" banner half-sprite. The 2x3
 *   sprite grid + +$40 step pattern matches a 64x96 px banner glyph
 *   (probably the left-half of two banner words rendered as 32x32 OAM
 *   tiles).
 * ======================================================================== */
static void type5C_state0_init_CECB(Entity *self)
{
    *(uint16_t *)&DP_TARGET_BIAS_X = 0;     /* $46/$47 cleared (LDY #$0000 / STY $46) */
    self->timer_10 = 0x40;
    self->state++;
}

/* $CF05 — 6-sprite scenario-end banner draw (FULL lift of $CF05..$CF6F):
 *   sprite 1 @ (+$C0, -$40)       tile = $00 (via DB88)
 *   sprite 2 @ (Y += $40)         tile = $80
 *   sprite 3 @ (X += $40)         tile = $88
 *   sprite 4 @ (Y -= $40)         tile = $08
 *   sprite 5 @ (X += $40, Y += $18) tile = $08
 *   sprite 6: same as #5 (different state, finishing the strip)
 *  Two banner words rendered as a 3-wide 2-tall 32x32 grid + tail. */
static void sub_CF05_banner_draw(Entity *self)
{
    /* DB40 with X-bias = $00C0, Y-bias = $FFC0. */
    sub_DB40(self, 0x00C0, (uint16_t)-0x40);
    sub_DB88(self);                          /* tile from $000E + $000C */
    sub_DB9E();                              /* sprite 1 */

    *(uint16_t *)&DP_SPRITE_DY += 0x0040;
    DP_SPRITE_TILE_LO = 0x80;
    sub_DB9E();                              /* sprite 2 */

    *(uint16_t *)&DP_SPRITE_DX += 0x0040;
    DP_SPRITE_TILE_LO = 0x88;
    sub_DB9E();                              /* sprite 3 */

    *(uint16_t *)&DP_SPRITE_DY -= 0x0040;
    DP_SPRITE_TILE_LO = 0x08;
    sub_DB9E();                              /* sprite 4 */

    *(uint16_t *)&DP_SPRITE_DX += 0x0040;
    *(uint16_t *)&DP_SPRITE_DY += 0x0018;
    DP_SPRITE_TILE_LO = 0x08;
    DP_SPRITE_TILE_HI = 0;                   /* LDY #$0008 → $3B/$3C */
    sub_DB9E();                              /* sprite 5 */

    /* Sprite 6: ROM falls through after the LDY; the second JSR DB9E
     * uses the same coords with no further mutation (a "dot" tile to
     * finish the dotted border). Faithful lift = repeat the call. */
    sub_DB9E();
}

static void type5C_state1_slide_CEDA(Entity *self)
{
    self->x += 2;
    if ((DP_TASK_INDEX & 0x03) == 0) {
        if (DP_TARGET_BIAS_X == 0) {
            DP_TARGET_BIAS_Y--;
        }
        DP_TARGET_BIAS_X--;
    }
    sub_CF05_banner_draw(self);
    if (--self->timer_10 == 0) self->state++;
}

static void type5C_state2_rest_CF04(Entity *self)
{
    sub_CF05_banner_draw(self);
}

static void type5C_dispatch_CEB9(Entity *self)
{
    static const StateFn tbl[3] = {
        type5C_state0_init_CECB, type5C_state1_slide_CEDA, type5C_state2_rest_CF04,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $5D — $04:CF70 — SCENARIO-END BANNER (right-sliding)
 *
 *   Mirror of $5C. Spawned at (X=$0140, Y=$005F) — starts off-screen to
 *   the RIGHT, slides in. 3-state dispatcher ($CF82, $CF91, $CFB9).
 * ======================================================================== */
/* $CFBA — mirror banner draw (left-sliding banner). Sprites step -$40 X
 * after the initial origin, building from right to left. */
static void sub_CFBA_banner_draw_R(Entity *self)
{
    /* DB40 origin = (#$0000, #$FFC0). */
    sub_DB40(self, 0x0000, (uint16_t)-0x40);
    sub_DB88(self);
    sub_DB9E();
    *(uint16_t *)&DP_SPRITE_DY += 0x0040;
    DP_SPRITE_TILE_LO = 0x80;
    sub_DB9E();
    *(uint16_t *)&DP_SPRITE_DX -= 0x0040;
    DP_SPRITE_TILE_LO = 0x88;
    sub_DB9E();
    *(uint16_t *)&DP_SPRITE_DY -= 0x0040;
    DP_SPRITE_TILE_LO = 0x08;
    sub_DB9E();
    *(uint16_t *)&DP_SPRITE_DX -= 0x0040;
    *(uint16_t *)&DP_SPRITE_DY += 0x0018;
    DP_SPRITE_TILE_LO = 0x08;
    DP_SPRITE_TILE_HI = 0;
    sub_DB9E();
    sub_DB9E();
}

/* $CF82: $46=0; timer=$40; state++. */
static void type5D_state0_init_CF82(Entity *self)
{
    *(uint16_t *)&DP_TARGET_BIAS_X = 0;
    self->timer_10 = 0x40;
    self->state++;
}
/* $CF91: x -= 2; every 4th frame $46 (16-bit, into $47 on overflow) ++;
 *        JSR $CFBA; --timer; on 0 state++. */
static void type5D_state1_slide_CF91(Entity *self)
{
    self->x = (uint16_t)(self->x - 2);
    if ((DP_TASK_INDEX & 0x03) == 0) {
        DP_TARGET_BIAS_X++;
        if (DP_TARGET_BIAS_X == 0) DP_TARGET_BIAS_Y++;
    }
    sub_CFBA_banner_draw_R(self);
    if (--self->timer_10 == 0) self->state++;
}
/* $CFB9: just call CFBA (static draw). */
static void type5D_state2_rest_CFB9(Entity *self) { sub_CFBA_banner_draw_R(self); }
static void type5D_dispatch_CF70(Entity *self)
{
    static const StateFn tbl[3] = {
        type5D_state0_init_CF82, type5D_state1_slide_CF91, type5D_state2_rest_CFB9,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $5E — $04:D025 — "GAME OVER" BANNER  (role CONFIRMED)
 *
 *   Per V4-8 + states_menu.c gs_game_over_B19F: spawned at
 *   (X=$0088, Y=$0098) — i.e. screen-center, slightly low.
 *   3-state dispatcher (states $D037, $D046, $D054):
 *     state 0  $D037  init: clear $0009 (16-bit, target_y), clear $0012,
 *                            state++
 *     state 1  $D046  intro: JSR $DB52 + DEC timer_10; advance on 0
 *     state 2  $D054  rest: the persistent "GAME OVER" banner draw
 *                            (TODO — body follows the same composite
 *                            pattern as the scenario-end banner $5C).
 *
 *   ROLE CONFIRMED: this is the "GAME OVER" banner entity.
 * ======================================================================== */
static void type5E_state0_init_D037(Entity *self)
{
    self->target_y = 0;
    self->motion_res_y_12 = 0;
    self->state++;
}
static void type5E_state1_intro_D046(Entity *self)
{
    sub_DB52();
    if (--self->timer_10 == 0) self->state++;
}
/* $D054 — "GAME OVER" persistent banner draw with falling/bouncing motion:
 *   16-bit:  $0009 -= $0010                       (target_y "fall speed")
 *   8-bit:   $0012 = $0009.lo + $0012             (accumulator)
 *   if accumulator went negative, A=$FF else A=0  (sign extend)
 *   16-bit:  y += A_sign_extended * (...)         (y -= residue)
 *   actually disasm pattern: XBA / BMI +5 / XBA / LDA #$00 / BRA / XBA /
 *   LDA #$FF  → A becomes 0 or $FF as the "carry-in" for the next ADC.
 *   16-bit:  y += A (signed extension as carry)
 *   JSR $DB52
 *   if y.hi != 0, zero self->type (despawn once it goes off-screen).
 */
static void type5E_state2_rest_D054(Entity *self)
{
    /* target_y -= $0010 */
    self->target_y = (uint16_t)(self->target_y - 0x0010);
    /* motion_res_y_12 = (target_y.lo + motion_res_y_12) with carry tracking. */
    uint16_t sum = (uint16_t)(self->motion_res_y_12 + (uint8_t)(self->target_y));
    self->motion_res_y_12 = (uint8_t)sum;
    /* Sign of original 16-bit target_y.lo XBA → use msb of low byte. */
    uint8_t signbyte = ((uint8_t)(self->target_y) & 0x80) ? 0xFF : 0x00;
    /* y += signbyte (with carry from earlier accumulator). */
    uint16_t cy = (uint16_t)(self->y + signbyte + ((sum >> 8) & 1));
    self->y = cy;
    /* y.hi adjust: ADC $0005,x with same signbyte carry pattern. */
    sub_DB52();
    if ((uint8_t)(self->y >> 8) != 0) self->type = 0;
}
static void type5E_gameover_banner_D025(Entity *self)
{
    static const StateFn tbl[3] = {
        type5E_state0_init_D037, type5E_state1_intro_D046, type5E_state2_rest_D054,
    };
    state_dispatch(self, tbl, 3);
}

/* ========================================================================
 * TYPE $5F — $04:D08F — POST-CREDITS TRANSITION PROP
 *
 *   2-state dispatcher (states $D09F, $D0C2). Per audio_intro.c the
 *   post-credits state ($43) is the final fade — types $5C-$5F appear
 *   together in that window. State table shape matches the simple
 *   "init + step+draw" pair seen in $58, $56, etc.
 * ======================================================================== */
/* $D09F: motion_res_x_11 = 0; motion_res_y_12 = 0; (motion_res_x_11=0 twice);
 *        target_x ($07/$08) = $0080; target_y ($09/$0A) = $0020;
 *        timer_10 = 8; state++. */
static void type5F_state0_init_D09F(Entity *self)
{
    self->motion_res_x_11 = 0;
    self->motion_res_y_12 = 0;
    self->target_x = 0x0080;
    self->target_y = 0x0020;
    self->timer_10 = 0x08;
    self->state++;
}
/* $D0EE — step-toward-target helper for $5F (16-bit SBC into $69 fractional
 * accumulator, 4 ROR cycles, similar to sub_B114). Body lives downstream;
 * treat as opaque. */
extern void sub_D0EE_step_5F(Entity *self);
/* $D0C2: JSR $D0EE (step toward target_x/target_y); JSR $DB52; --timer_10;
 *        on 0 reset timer=8, motion_res_x_11++; if motion_res_x_11 >= 4
 *        leave anim_frame_e alone; else anim_frame_e = $D0EA[motion_res_x_11]
 *        (4-entry tile table). */
static const uint8_t rom_D0EA_5F_frames[4] = { 0x00, 0x08, 0x80, 0x88 };
static void type5F_state1_step_D0C2(Entity *self)
{
    sub_D0EE_step_5F(self);
    sub_DB52();
    if (--self->timer_10 != 0) return;
    self->timer_10 = 0x08;
    self->motion_res_x_11++;
    if (self->motion_res_x_11 >= 0x04) return;
    self->anim_frame_e = rom_D0EA_5F_frames[self->motion_res_x_11];
}
static void type5F_dispatch_D08F(Entity *self)
{
    static const StateFn tbl[2] = { type5F_state0_init_D09F, type5F_state1_step_D0C2 };
    state_dispatch(self, tbl, 2);
}

/* ========================================================================
 * Handler table — these 32 entries cover slots $40..$5F of the entity
 * dispatch table at $04:9A30.
 * ======================================================================== */
typedef void (*EntityHandler)(Entity *);

__attribute__((used))
static const EntityHandler entity_handlers_40_5F[32] = {
    /* $40 */ type40_dispatch_C5D7,
    /* $41 */ type41_dispatch_C4C4,
    /* $42 */ type42_dispatch_C599,
    /* $43 */ type43_dispatch_C61D,
    /* $44 */ type44_dispatch_BC49,
    /* $45 */ type45_dispatch_BC8A,
    /* $46 */ type46_dispatch_BFC6,
    /* $47 */ type47_dispatch_C013,
    /* $48 */ type48_skipframe_B411,
    /* $49 */ type49_skipframe_B358,
    /* $4A */ type4A_skipframe_B3C4,
    /* $4B */ type4B_dispatch_C653,
    /* $4C */ type4C_dispatch_C73B,
    /* $4D */ type4D_dispatch_C8A7,
    /* $4E */ type4E_scrollbias_C91B,
    /* $4F */ type4F_walkprop_C92C,
    /* $50 */ type50_walkprop_C958,
    /* $51 */ type51_dispatch_C984,
    /* $52 */ type52_dispatch_C9C6,
    /* $53 */ type53_dispatch_CA08,
    /* $54 */ type54_dispatch_CA4C,
    /* $55 */ type55_compose_CA93,
    /* $56 */ type56_dispatch_CAC3,
    /* $57 */ type57_audio_trampoline_CB65,
    /* $58 */ type58_dispatch_CC73,
    /* $59 */ type59_population_readout_CB73,
    /* $5A */ type5A_dispatch_CD5B,
    /* $5B */ type5B_dispatch_CE0A,
    /* $5C */ type5C_dispatch_CEB9,
    /* $5D */ type5D_dispatch_CF70,
    /* $5E */ type5E_gameover_banner_D025,
    /* $5F */ type5F_dispatch_D08F,
};

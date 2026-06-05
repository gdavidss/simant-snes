/*
 * misc_helpers.c — small, fully-decoded common routines that don't fit
 * elsewhere. These are foundational helpers used by many lifted handlers
 * but are simple enough to capture in a single faithful C body.
 */

#include <stdint.h>

extern uint8_t           wram[0x20000];
extern volatile uint8_t  mmio[0x10000];
#define dp wram
#define MMIO8(addr)  (*(volatile uint8_t *)&mmio[(addr) & 0xFFFF])
#define MMIO16(addr) (*(volatile uint16_t*)&mmio[(addr) & 0xFFFF])

/* MMIO registers we actually touch. */
#define INIDISP    MMIO8 (0x2100)
#define APUIO0     MMIO8 (0x2140)
#define APUIO3     MMIO8 (0x2143)
#define JOY1L      MMIO8 (0x4218)
#define JOY1H      MMIO8 (0x4219)

/* ------------------------------------------------------------------------
 * PPU register-init table interpreter @ $00:BC7F
 *
 * The PPU is initialized from a table of (addr_lo, addr_hi, value) triples
 * starting at $01:98A3. For each triple:
 *   - addr_hi == 0 -> table terminator, return
 *   - else: store `value` at the address (addr_hi:addr_lo) — for the PPU
 *           addresses ($2100-$213F), this directly writes the register
 *
 * The table at $01:98A3 (decoded contents):
 *
 *   [0]  INIDISP = $8F             (force blank, brightness 15)
 *   [1]  OBSEL   = $62             (sprite tiles: 8x8 + 32x32, base $2000)
 *   [4]  BGMODE  = $02             (mode 2, BG3 priority)
 *   [6]  BG1SC   = $70             (BG1 tilemap @ VRAM $7000, 32x32)
 *   [7]  BG2SC   = $74             (BG2 tilemap @ VRAM $7400)
 *   [8]  BG3SC   = $78             (BG3 tilemap @ VRAM $7800)
 *   [9]  BG4SC   = $7C             (BG4 tilemap @ VRAM $7C00)
 *   [10] BG12NBA = $30             (BG1 chars @ $3000, BG2 chars @ $0000)
 *   [11] BG34NBA = $66             (BG3 chars @ $6000, BG4 chars @ $6000)
 *   [12..23] BG1/BG2/BG3 HOFS/VOFS scroll registers — each cleared via
 *            the "write-twice" protocol so it takes 2 entries per
 *            register; that's 12 triples covering the 6 scroll regs.
 *   [24] VMAIN   = $80             (VRAM auto-increment by 1 word)
 *   [25] M7SEL   = $00             (mode 7 off at boot)
 *   [26] TM      = $17             (main screen: BG1+BG2+BG3+OBJ enabled)
 *   [27] TS      = $00             (sub screen: all disabled)
 *   [28] TMW     = $00             (no main-screen window)
 *   END: hi==0
 *
 * The lifted code uses indirect-DP store so each triple writes through a
 * fresh pointer.  In C we just read the table and dispatch.
 */
/* Lifted contents of the table at ROM $01:98A3 (zero-terminated triples). */
static const uint8_t ppu_init_table_0198A3[] = {
    0x00,0x21, 0x8F,   /* INIDISP = $8F (force blank, brightness 15)        */
    0x01,0x21, 0x62,   /* OBSEL   = $62 (sprite: 8x8/32x32, base $2000)     */
    0x02,0x21, 0x00,   /* OAMADDL = 0                                       */
    0x03,0x21, 0x00,   /* OAMADDH = 0                                       */
    0x05,0x21, 0x02,   /* BGMODE  = mode 2, BG3 priority                    */
    0x06,0x21, 0x00,   /* MOSAIC  = off                                     */
    0x07,0x21, 0x70,   /* BG1SC   = tilemap @ $7000, 32x32                  */
    0x08,0x21, 0x74,   /* BG2SC   = tilemap @ $7400                         */
    0x09,0x21, 0x78,   /* BG3SC   = tilemap @ $7800                         */
    0x0A,0x21, 0x7C,   /* BG4SC   = tilemap @ $7C00                         */
    0x0B,0x21, 0x30,   /* BG12NBA = BG1 chars $3000, BG2 chars $0000        */
    0x0C,0x21, 0x66,   /* BG34NBA = BG3 chars $6000, BG4 chars $6000        */
    0x0D,0x21, 0x00, 0x0D,0x21, 0x00,    /* BG1HOFS clear (×2 for the
                                            write-twice register protocol) */
    0x0E,0x21, 0x00, 0x0E,0x21, 0x00,    /* BG1VOFS clear                  */
    0x0F,0x21, 0x00, 0x0F,0x21, 0x00,    /* BG2HOFS clear                  */
    0x10,0x21, 0x00, 0x10,0x21, 0x00,    /* BG2VOFS clear                  */
    0x11,0x21, 0x00, 0x11,0x21, 0x00,    /* BG3HOFS clear                  */
    0x12,0x21, 0x00, 0x12,0x21, 0x00,    /* BG3VOFS clear                  */
    0x15,0x21, 0x80,   /* VMAIN   = $80 (increment by 1 word per write)     */
    0x1A,0x21, 0x00,   /* M7SEL   = mode 7 disabled at boot                 */
    0x2C,0x21, 0x17,   /* TM      = main screen: BG1+BG2+BG3+OBJ enabled    */
    0x2D,0x21, 0x00,   /* TS      = sub screen: all off                     */
    0x2E,0x21, 0x00,   /* TMW     = no main-screen window                   */
    0x00,0x00, 0x00,   /* terminator (hi == 0)                              */
};

void ppu_init_apply_BC7F(void)
{
    const uint8_t *t = ppu_init_table_0198A3;
    for (unsigned i = 0; ; ++i) {
        uint8_t lo = t[i*3 + 0];
        uint8_t hi = t[i*3 + 1];
        if (hi == 0) return;
        uint8_t val = t[i*3 + 2];
        MMIO8((hi << 8) | lo) = val;
    }
}

/* ------------------------------------------------------------------------
 * Music command dispatcher @ $00:8E88 (JSL target)
 *
 * Game logic calls this with A=<command byte>. If music is enabled
 * (dp[$33] != 0), the command is forwarded to APUIO0 for the SPC700 to
 * pick up. Otherwise silently ignored.
 *
 * Observed command bytes (from state-handler code):
 *   $02 — encyclopedia background music (state $04 GS_ANT_INFORMATION)
 *   $08 — main menu music                (state $16)
 *   $30 — pause music                   (Agent F's sub_A0D2)
 *   $00 — silence (preceded by various transitions)
 * ------------------------------------------------------------------------ */
void apu_send_music_8E88(uint8_t cmd)
{
    dp[0x0037] = cmd;
    if (dp[0x0033] == 0) return;          /* music disabled */
    APUIO0 = dp[0x0037];
}
/* Alias for sites that reference the long-form symbol name. */
void apu_send_if_enabled_008E88(uint8_t cmd) { apu_send_music_8E88(cmd); }

/* ------------------------------------------------------------------------
 * SFX command dispatcher @ $00:8EA3
 *
 * Channel 3 is the SFX channel. Each SFX play increments a per-channel
 * alternation counter at dp[$3E] and packs it into bit 0 of the command
 * sent to APUIO3 — that way the SPC700 always sees a state change even
 * if the same SFX is retriggered.
 *
 * Observed command bytes:
 *   $C4 — view-switch confirmation     (sub_8611)
 *   $4E — entity collision / "ouch"    (Queen handler)
 *   ... most are documented per-call-site in the lifted handlers
 * ------------------------------------------------------------------------ */
void apu_play_sfx_8EA3(uint8_t cmd)
{
    dp[0x003A] = cmd;
    if (dp[0x0036] == 0) return;          /* SFX disabled */
    dp[0x003E] = (dp[0x003E] + 1) & 0xFF;
    APUIO3 = (dp[0x003E] & 0x01) | dp[0x003A];
}
/* Alias for sites that reference the long-form symbol name. */
void apu_play_sfx_008EA3(uint8_t cmd) { apu_play_sfx_8EA3(cmd); }

/* ------------------------------------------------------------------------
 * Screen fade-in @ $00:85FC and fade-out @ $00:8616
 *
 * INIDISP's low nibble is brightness (0-15). Both routines step the
 * brightness up or down in 16 frames, calling sub_8841 to wait between
 * steps.
 *
 *   85FC: brightness 0 -> 15 (fade in)
 *   8616: brightness 15 -> 0 (fade out)
 * ------------------------------------------------------------------------ */
extern void wait_frames_8841(uint8_t n);

/* Verified $00:85FC:
 *   STZ $6C
 * loop:
 *   LDA $6C / STA INIDISP    (= dp[$6C] -> $2100)
 *   LDA #$02 / JSR $8841     (wait 2 frames)
 *   INC $6C                   (so dp[$6C] advances 0,1,...,15,16)
 *   LDA $6C / CMP #$10 / BNE loop
 *   RTS                       (exits with dp[$6C] == $10)  */
void fade_in_85FC(void)
{
    dp[0x6C] = 0;
    while (dp[0x6C] != 0x10) {
        INIDISP = dp[0x6C];
        wait_frames_8841(2);
        dp[0x6C]++;
    }
}

/* Verified $00:8616:
 *   LDA #$0F / STA $6C
 * loop:
 *   LDA $6C / STA INIDISP
 *   LDA #$02 / JSR $8841
 *   DEC $6C                   (15 -> 14 -> ... -> 0 -> $FF)
 *   BPL loop                  (exits when N=1, i.e., dp[$6C] = $FF)
 *   RTS                       (exits with dp[$6C] == $FF) */
void fade_out_8616(void)
{
    dp[0x6C] = 0x0F;
    do {
        INIDISP = dp[0x6C];
        wait_frames_8841(2);
        dp[0x6C]--;
    } while ((dp[0x6C] & 0x80) == 0);
}

/* ------------------------------------------------------------------------
 * Wait N frames @ $00:8841
 *
 * Cooperative wait: calls sub_877D (the cooperative-yield point) N times,
 * each yielding to the scheduler so other tasks (mainly the NMI) keep
 * running. Used by every "do something then wait" sequence.
 * ------------------------------------------------------------------------ */
extern void coop_yield_877D(void);

void wait_frames_8841(uint8_t n)
{
    while (n--) coop_yield_877D();
}

/* ------------------------------------------------------------------------
 * Cooperative yield @ $00:877D — THE per-task yield in the scheduler.
 *
 *   sub_877D:
 *     LDA $00            ; A = "my" task index (the value that was dp[$00]
 *                        ;  when this task last ran)
 *     spin:
 *       CMP $00           ; spin while dp[$00] is still us
 *       BEQ spin
 *     PHX / PHY
 *     JSR $8887           ; (per-frame helper; not yet lifted)
 *     JSR joypad_read_E3FD   ; refresh JOY1/JOY2/mouse shadows
 *     JSR $87DA           ; (per-frame helper)
 *     PLY / PLX
 *     RTS
 *
 * The spin works because the NMI handler:
 *   1. INCs dp[$00] (the clock tick at $809A) — this is what BREAKS the
 *      BEQ-spin: A now differs from dp[$00].
 *   2. Runs the SP-swap scheduler ($80D0+), which sets dp[$00] = the
 *      next task slot index it picks.
 *   3. RTIs into that task.
 *
 * Net: the task BLOCKS until NMI fires (one frame), then on return it
 * re-reads inputs and continues. Perfect cooperative 60Hz timing.
 *
 * Used everywhere by `wait_frames_8841` and any "show prompt, wait for
 * user response" sequence.
 *
 * Porting note: on a host platform, replace the spin with "wait for next
 * frame" (vsync / 60Hz tick / coroutine yield).
 * ------------------------------------------------------------------------ */
extern uint8_t cur_task_index;  /* alias for dp[$00]; some other file owns it */

void coop_yield_877D(void)
{
    uint8_t my_id = dp[0x00];
    while (dp[0x00] == my_id) {
        /* in real hardware, NMI eventually changes dp[$00] for us */
    }
    /* Per-frame work: read joypad/mouse, plus two helpers not yet lifted. */
    extern void sub_8887(void);
    extern void joypad_read_E3FD(void);
    extern void sub_87DA(void);
    sub_8887();
    joypad_read_E3FD();
    sub_87DA();
}

/* ------------------------------------------------------------------------
 * "Wait for button release" @ $00:87BC
 *
 *   yield once;
 *   if not pause_locked:
 *      while (JOY1L | JOY1H) has any high-bit set, yield;
 *   else:
 *      while (dp[0x007B], i.e. mouse current button, != 0), yield;
 *   dp[0x28] = dp[0x29] = 0xFF;
 *
 * Used after a button press to ensure the player has released before the
 * next event fires (prevents auto-repeat).
 * ------------------------------------------------------------------------ */
void wait_button_release_87BC(void)
{
    coop_yield_877D();
    if (!dp[0x0071]) {
        while ((JOY1L | JOY1H) & 0x80) coop_yield_877D();
    } else {
        while (dp[0x007B] != 0) coop_yield_877D();
    }
    dp[0x28] = 0xFF;
    dp[0x29] = 0xFF;
}

/* ------------------------------------------------------------------------
 * Per-view DPAD scroller — Surface @ $00:A106 (template; B.Nest is $A18D)
 *
 * Two-part routine:
 *
 *  1. EDGE-AUTOSCROLL: keep cursor inside a visible rectangle. When the
 *     cursor's X (dp[$14]) or Y (dp[$15]) hits an edge, snap it back one
 *     pixel and bump the world position (dp[$0C/$0D] for Surface,
 *     dp[$0E/$0F] for B.Nest, dp[$10/$11] for R.Nest), then call the
 *     view's redraw helper. This is what makes the camera follow the
 *     cursor as you push to a screen edge.
 *
 *  2. L/R + DPAD VIEW SCROLL (manual p.6 "leave the cursor where it is,
 *     but make the screen scroll"):
 *       if !pause_locked
 *          && (JOY1L & $30) != 0       (L or R held; bits 5,4)
 *          && (JOY1H & $0F) != 0       (dpad pressed; bits 3..0)
 *       then dpad_nibble = JOY1H & $0F
 *            view_x += dx_table[dpad_nibble]   ; small +1/-1 step
 *            view_y += dy_table[dpad_nibble]
 *            redraw_view()
 *
 * The dx/dy tables encode the standard 8-direction nibble:
 *   bit 0 = right, bit 1 = left, bit 2 = down, bit 3 = up
 *   opposite combinations (L+R, U+D) yield (0, 0)
 * ------------------------------------------------------------------------ */
static const int8_t dpad_dx[16] = {
    /* 0000  0001  0010  0011  0100  0101  0110  0111
       0000R  0001L 0010R+L 0011D 0100D+R 0101D+L  ...   */
    [0x0]=0, [0x1]=+1, [0x2]=-1, [0x3]=0,
    [0x4]=0, [0x5]=+1, [0x6]=-1, [0x7]=0,
    [0x8]=0, [0x9]=+1, [0xA]=-1, [0xB]=0,
    [0xC]=0, [0xD]=+1, [0xE]=-1, [0xF]=0,
};
static const int8_t dpad_dy[16] = {
    [0x0]=0, [0x1]=0, [0x2]=0, [0x3]=0,
    [0x4]=+1,[0x5]=+1,[0x6]=+1,[0x7]=+1,
    [0x8]=-1,[0x9]=-1,[0xA]=-1,[0xB]=-1,
    [0xC]=0, [0xD]=0, [0xE]=0, [0xF]=0,
};

extern void sub_871A(void);     /* per-view tilemap refresh */
extern void sub_86BD(void);     /* post-scroll surface housekeeping */

void scroll_surface_view_A106(void)
{
    /* Edge clamp + autoscroll on cursor */
    if (dp[0x15] <= 0x08) {
        dp[0x15] = 0x09;
        dp[0x0D]--;
        sub_871A();
    }
    if (dp[0x15] >= 0xA8) {
        dp[0x15] = 0xA7;
        dp[0x0D]++;
        sub_871A();
    }
    if (dp[0x14] <= 0x08) {
        dp[0x14] = 0x09;
        dp[0x0C]--;
        sub_871A();
    }
    if (dp[0x14] >= 0xE8) {
        dp[0x14] = 0xE7;
        dp[0x0C]++;
        sub_871A();
    }
    /* L/R + DPAD: scroll view without moving cursor */
    if (dp[0x0071]) goto done;
    if ((JOY1L & 0x30) == 0) goto done;          /* L or R must be held */
    uint8_t dpad = JOY1H & 0x0F;
    if (dpad == 0) goto done;
    dp[0x0C] += dpad_dx[dpad];
    dp[0x0D] += dpad_dy[dpad];
    sub_871A();
done:
    sub_86BD();
}

/* ------------------------------------------------------------------------
 * Disable NMI @ $00:8976  (cooperative-yield then STZ NMITIMEN)
 *
 * Verified asm @ $00:8976:
 *   JSR $877D        ; yield ONE frame first (so any pending NMI work
 *                    ;   like the entity walker completes cleanly)
 *   STZ $4200        ; NMITIMEN = 0 (NMI off, joypad auto-read off)
 *   RTS
 *
 * Used during screen transitions where new tilemap data is uploaded;
 * the yield-first pattern guarantees we won't STZ between a JSR $877D
 * spin and the NMI that's supposed to wake the spinning task — that
 * would deadlock the cooperative scheduler.
 * ------------------------------------------------------------------------ */
void disable_nmi_8976(void)
{
    coop_yield_877D();
    MMIO8(0x4200) = 0;
}

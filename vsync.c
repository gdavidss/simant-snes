/*
 * vsync.c — vblank-sync + scrolling tilemap maintenance helpers.
 *
 * These routines run from the GAME-LOOP TASK (task #1, the dispatcher at
 * $00:935C). They're paired by the vsync-and-input function at $00:985F:
 *
 *   for (;;) {
 *      NMITIMEN = 0;                       // freeze NMI briefly
 *      sub_E527();                          // VRAM stream advance + tile
 *                                           //   row queue (full row update)
 *      NMITIMEN = dp[$0A];                  // re-enable NMI ($81)
 *      sub_DEEE();                          // view-state guard
 *      if (!sub_DF79()) break;              // BCC: scroll position settled
 *      sub_A3D6();                          // retry path (advance state)
 *   }
 *   sub_A354();                              // A-button handling
 *   ... // SELECT-button check + view switch
 *
 * Don't try to make this directly runnable on a host — it documents the
 * per-frame foreground work the SNES original does while waiting for
 * each vblank.
 */

#include <stdint.h>

/* Externals — same WRAM model as simant.c. */
extern uint8_t  wram[0x20000];
extern volatile uint8_t mmio[0x10000];
#define dp wram
#define MMIO8(addr)  (*(volatile uint8_t *)&mmio[(addr) & 0xFFFF])
#define JOY1L      MMIO8(0x4218)

extern void sub_877D(void);          /* called from GS_SCENARIO_GAME too */
extern void sub_A0F4(void);
extern void sub_87BC(void);
extern void sub_E201(void);
extern uint16_t jsl_E79B(void);
extern uint16_t jsl_E7B7(void);
extern uint16_t jsl_E7A8(void);
extern uint8_t  sub_E680(void);      /* returns the next tile attr byte */
extern void     sub_E66A(uint16_t y); /* writes attr to [$B1]+y         */
extern void     handle_button_A_A36B(void); /* A-button press handler   */

/* ------------------------------------------------------------------------
 * sub_E527 — per-frame VRAM-stream + tilemap-row queue
 *
 * Advances the global VRAM streamer cursor and queues up enough VRAM
 * writes to refresh one full tile row of the BG layer being scrolled.
 *
 *   dp[$B0]    = streamer cycle (0..7); used as the source-block selector
 *                for sub_814F (the NMI VRAM streamer)
 *   dp[$88]    = (dp[$B0] + 1) — the value sub_814F reads as its cursor
 *   dp[$94]    = dp[$B0] * 8     — sub-cycle indexer
 *   dp[$B2]    = dp[$94] + 0x30  — VRAM row base (in tiles) for this cycle
 *   dp[$B1.B3] = 24-bit src ptr to the working tile-row buffer in WRAM
 *                ($7E:xxxx)
 *   dp[$B4-$B5]  destination word for sub_E66A (auto-incremented)
 *   dp[$B6-$B7]  another VRAM ptr  (auto-incremented)
 *   dp[$B8-$B9]  yet another ptr   (auto-incremented; sometimes wraps -$3F
 *                                   when dp[$BA] equals $10 — likely a
 *                                   tilemap-row wrap)
 *   dp[$BA]    = "rows remaining" countdown (starts at $1F = 31 rows)
 *   dp[$BB]    = $03  (unknown)
 *   dp[$BC]    = $01  (unknown)
 *   dp[$BD]    = scratch for the tile attribute being written this iter
 *   dp[$92]    = $C0 (initial attr-byte selector), then >>= 2 each step
 *
 * The inner sequence
 *     JSR $E680  -> A = next tile-attribute byte
 *     STA $BD
 *     LDY #$0002 / JSR $E66A  -> write to [$B1]+2
 *     LDY #$0012 / JSR $E66A  -> write to [$B1]+0x12
 * is repeated four times with progressively-shifted attr bytes. Then the
 * pointer is bumped by $20 (one tilemap row stride) and dp[$BA] is
 * decremented. The whole thing repeats until $BA reaches 0.
 *
 * Net effect: write 32 tilemap rows of fresh tile attributes into a WRAM
 * buffer that the NMI VRAM streamer will later DMA into PPU memory.
 * This is what scrolls the BG layers as the player moves.
 * ------------------------------------------------------------------------ */
void sub_E527(void)
{
    /* Advance cycle counter 0..7. */
    dp[0xB0] = (dp[0xB0] + 1) & 0x07;
    dp[0x88] =  dp[0xB0] + 1;
    dp[0x94] =  dp[0xB0] << 3;
    dp[0xB2] =  dp[0x94] + 0x30;
    dp[0xB1] =  0;
    dp[0xB3] =  0x7E;
    dp[0x93] =  0;

    /* Three "look up the active stream pointers" calls; results go into
     * dp[$B4..$B9] for the writer loop below. */
    *(uint16_t *)&dp[0xB4] = jsl_E79B();
    *(uint16_t *)&dp[0xB6] = jsl_E7B7();
    *(uint16_t *)&dp[0xB8] = jsl_E7A8();

    dp[0xBC] = 0x01;
    dp[0xBB] = 0x03;
    dp[0xBA] = 0x1F;                                 /* 31 rows         */

    /* Zero two 16-bit slots at [$B1.B3]+0x02 and +0x12. */
    *(uint16_t *)&wram[(dp[0xB3] << 16) | (dp[0xB2] << 8) | (dp[0xB1] + 2)]  = 0;
    *(uint16_t *)&wram[(dp[0xB3] << 16) | (dp[0xB2] << 8) | (dp[0xB1] + 0x12)] = 0;

    dp[0x92] = 0xC0;

    while (dp[0xBA] != 0) {
        /* 4 sub-steps, each writes a tile byte via sub_E680/sub_E66A and
         * shifts the attr selector right by 2. */
        for (unsigned s = 0; s < 4; ++s) {
            dp[0xBD] = sub_E680();
            sub_E66A(0x0002);
            sub_E66A(0x0012);
            ++*(uint16_t *)&dp[0xB4];
            /* Only even iters (s==0, s==2) bump $B6 — verified inversion
             * fix vs an earlier draft. */
            if (s == 0 || s == 2) ++*(uint16_t *)&dp[0xB6];
            if (dp[0xBA] == 0x10) {
                *(uint16_t *)&dp[0xB8] -= 0x3F;     /* wrap a row     */
            } else {
                ++*(uint16_t *)&dp[0xB8];
            }
            dp[0x92] >>= 2;
        }
        *(uint16_t *)&dp[0xB1] += 0x20;             /* next tile row  */
        dp[0xBA]--;
    }
}

/* ------------------------------------------------------------------------
 * sub_DEEE — view-state guard.   *** PORT STUB ***
 *   Only the early-return guard below is implemented; the $DEF9 body is
 *   narrated for reference but not yet lifted.
 *
 * Verified disassembly @ $00:DEEE:
 *   LDA $02B1
 *   BNE $DEF9        ; if dp[$02B1] != 0, run body
 *   LDA $02B4
 *   BEQ $DEF9        ; if dp[$02B4] == 0, run body
 *   RTS              ; otherwise (dp[$02B1]==0 AND dp[$02B4]!=0): skip
 *
 * So the body at $DEF9 runs when:  dp[$02B1] != 0  OR  dp[$02B4] == 0.
 * (The previous lift had the boolean inverted.) The body composes a
 * tile-row update for the current view's overlay using dp[$00] (frame
 * counter) to index into the per-view tile-attribute tables at
 * $7F:E796 etc.
 * ------------------------------------------------------------------------ */
void sub_DEEE(void)
{
    if (dp[0x02B1] == 0 && dp[0x02B4] != 0) return;  /* skip body */
    /* TODO: body at $00:DEF9 not yet fully lifted. Reads dp[$00] for
     * the frame phase, builds a 4-bit attribute selector at dp[$6C/$6D],
     * fetches per-view tile pointers from $7F:E796 + index*8, runs
     * sub_C5CF / sub_C7A7 to update the tilemap row. */
}

/* ------------------------------------------------------------------------
 * sub_DF79 — scroll-position settled probe.   *** PARTIAL PORT ***
 *   Only the equality compare + early return is implemented; the SEC-path
 *   body at $DF86 is narrated for reference but not yet lifted.
 *
 * Verified asm @ $00:DF79:
 *   JSR $E201                  ; helper that may update dp[$02BD]
 *   LDX $02BD / CPX $02BB
 *   BNE $DF86                  ; not equal -> "needs work" body
 *   CLC / RTS                  ; equal -> return carry CLEAR (settled)
 * $DF86 body:
 *   STZ $26 / build next BD value / animate / ...
 *   SEC / RTS                  ; return carry SET (caller re-loops)
 *
 * Caller uses BCC $9877 (= carry clear -> exit the vsync loop). To make
 * the C model match, we return NONZERO when settled (so the caller can
 * write `if (sub_DF79()) break;`).
 * ------------------------------------------------------------------------ */
int sub_DF79(void)
{
    sub_E201();
    if ((*(uint16_t *)&dp[0x02BD]) == (*(uint16_t *)&dp[0x02BB]))
        return 1;                  /* settled (CLC in asm)              */
    /* TODO: body at $00:DF86 — updates dp[$02BD], handles the streamer
     * animation slot at $0FE0, may dispatch view-specific sub-tasks. */
    return 0;                      /* not settled (SEC in asm)           */
}

/* ------------------------------------------------------------------------
 * sub_A3D6 — scroll-not-settled retry.
 *
 * Bookkeeping done when the scroll position hasn't yet matched its
 * target: spawn-or-process a subtask, run two helpers, copy
 * dp[$0016] -> dp[$001E], bump dp[$0026] (which was zeroed during
 * SELECT-press to mark "view changed"). The two JSRs to $877D bracket
 * the helpers, ensuring task spawning is up-to-date.
 * ------------------------------------------------------------------------ */
void sub_A3D6(void)
{
    sub_877D();
    sub_A0F4();
    sub_87BC();
    dp[0x001E] = dp[0x0016];
    dp[0x0026]++;
    sub_877D();
}

/* ------------------------------------------------------------------------
 * sub_A354 — A-button handler.
 *
 *   if (dp[$0071]) return;          // pause-locked? skip
 *   if (JOY1L & $80) handle_A();    // A button (bit 7 of JOY1L)
 *
 * Note this reads $4218 (JOY1L) DIRECTLY, not the dp[$0160] shadow.
 * The shadow is meant for the just-pressed edge ($60/$61 versus current
 * $0160/$0161); this routine just wants "is A held right now".
 * ------------------------------------------------------------------------ */
void sub_A354(void)
{
    if (dp[0x0071]) return;
    if (JOY1L & 0x80) handle_button_A_A36B();
}

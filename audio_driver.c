/*
 * audio_driver.c — SimAnt SNES SPC700 audio driver, lifted to C.
 *
 * This is the COPROCESSOR-side companion to audio_intro.c (which lifts
 * the 65816-side "send music command" surface).  The 3,327-byte SPC700
 * blob lives in ROM at $0B:F000 (file offset 0x5F000), is uploaded once
 * at boot via the standard SHVC IPL handshake (driven by $08:8006), and
 * runs forever inside the S-SMP, talking to the 65816 through the four
 * APU I/O ports ($2140-$2143 on the CPU side, $00F4-$00F7 on the SPC
 * side).
 *
 * Driver location in SPC ARAM:   $0600 - $12FE  (3327 bytes)
 * Entry point:                   $0600
 * Hardware timer used:           Timer 0, 4 ms tick (T0DIV = $80, 8 kHz/128)
 * DSP I/O port:                  $F2 (DSPADDR) / $F3 (DSPDATA)
 *
 * Compile:
 *     cd /Users/guilhermedavid/simant-re && \
 *       clang -Wall -Wextra -c audio_driver.c -o /tmp/ad.o
 *
 * What this file models
 * ---------------------
 *   - The 64 KB ARAM as `spc_ram[]` (mirrors the SNES SPC700 address
 *     space). Only $0000..$12FE is referenced as code/data; $0140..$04FF
 *     is the per-voice channel state (eight voices × ~$40 bytes); the
 *     rest is sample-data scratch / song-data buffer / etc.
 *
 *   - The DSP register window (set via $F2:DSPADDR, $F3:DSPDATA) as
 *     `dsp_regs[]`.  Writing to one of these registers in a real S-SMP
 *     triggers the corresponding mixer behaviour.
 *
 *   - All recognised routines from the disassembly are lifted with their
 *     ARAM operand addresses preserved.  Each routine has a "see SPC $xxxx"
 *     comment so it can be cross-referenced with audio_driver.txt.
 *
 *   - For each 65816-side command byte sent through APUIO0/APUIO3
 *     (catalogued in audio_intro.c's `sound_commands[]`), the
 *     corresponding handler is identified.
 *
 * Naming convention
 * -----------------
 *   `sub_XXXX`           routines whose role is partly inferred
 *   `<role>_XXXX`        routines whose role is clear from the code
 *   `g_<name>`           globals (ARAM $00..$FF — direct-page variables)
 *
 * What is GUESSED versus VERIFIED
 * -------------------------------
 *   VERIFIED  - main loop structure (timer wait → unmute → poll IO → mix)
 *   VERIFIED  - IPL handshake on $07A3 / runtime-upload handler at $0795
 *   VERIFIED  - command dispatch shape at $06CC / $0729 (master vol / fade / extra)
 *   VERIFIED  - song-byte interpreter at $099B with jump table at $09D5
 *   VERIFIED  - per-channel voice slot uses bytes 8 * channel_index for everything
 *   GUESSED   - DSP register mapping for the "$08A1 reset" routine
 *               (mute the voice's KON bit, clear pitch/envelope, mark as stopped)
 *   GUESSED   - exact byte format of the song-data stream at $12FE...
 *               and song table at $1300+
 *   TODO      - which APUIO0 value selects which song (table at $12FE+Y appears
 *               indexed by command byte; left as a runtime mapping for the host)
 *   TODO      - asset re-upload via $FC handshake (entry point at $0795)
 *               needs the 65816-side companion at $08:8085 to fully verify
 */

#include <stdint.h>
#include <string.h>

/* ========================================================================
 * MEMORY MODEL — keep symmetric with simant.c's `wram` / `dp` aliases.
 * ======================================================================== */

extern uint8_t wram[0x20000];   /* host RAM, just to satisfy the "shared
                                   memory model" comment in the spec —
                                   the SPC is a separate address space.  */
#define dp wram                  /* unused here; kept for consistency.    */

/* The SPC700's 64 KB ARAM.  Reset at boot, then filled by IPL from ROM
 * $0B:F000.  Entry PC = $0600.                                          */
static uint8_t spc_ram[0x10000];

/* DSP register file (write-only via $F2/$F3 in the real hardware).
 * The S-SMP exposes 128 registers; here we shadow them for trace.       */
static uint8_t dsp_regs[0x80];

/* CPU I/O ports — written by the 65816 ($2140-$2143), read here at $F4-$F7.
 * Bidirectional: writes from this file appear at the 65816 side.        */
static volatile uint8_t apuio_cpu_to_spc[4];   /* CPU → SPC (read at $F4-7)*/
static volatile uint8_t apuio_spc_to_cpu[4];   /* SPC → CPU (the SPC sees
                                                   this as $F4-7 too)       */

/* Hardware timers (only T0 is used by this driver).                       */
static struct {
    uint8_t divisor;
    uint8_t output;
} spc_timers[3];

/* CONTROL register $F1 bits:
 *   0 = T0 enable    1 = T1 enable    2 = T2 enable
 *   4 = clear IO0+1  5 = clear IO2+3
 *   7 = enable IPL ROM at $FFC0-$FFFF (only on real hw — we ignore)       */
static uint8_t spc_control;

/* ========================================================================
 * DSP register helpers
 *
 *   MOV $F2, #regno   sets the next-DSP-register pointer
 *   MOV $F3, value    writes that register and the autoinc happens at $F2++
 *
 * Encodings used by this driver:
 *   $00-$09  per-voice 0 settings   (and $10-$19 voice 1, ..., $70-$79 voice 7)
 *     +0/+1  L/R volume      +2/+3  pitch lo/hi    +4  source-number (sample slot)
 *     +5/+6  ADSR1/ADSR2     +7  gain               +8 ENVX   +9 OUTX
 *   $0C/$1C  master vol L/R
 *   $2C/$3C  echo vol L/R
 *   $4C      KON  (key-on mask, one bit per voice)
 *   $5C      KOF  (key-off mask)
 *   $6C      FLG  (mute=$60, reset=$80, etc.)
 *   $7C      ENDX (read-only "voice has reached end" flags)
 *   $0D/$2D/$3D/$4D  EFB / PMON / NON / EON
 *   $5D      DIR  (sample directory page; sample table base = DIR * $100)
 *   $6D/$7D  ESA / EDL (echo region pointer + delay length)
 * ======================================================================== */

static inline void dsp_write(uint8_t regno, uint8_t value) {
    /* In real HW: STA $F2 (regno), STA $F3 (value). */
    if (regno < 0x80) dsp_regs[regno] = value;
    /* Side effects (KON, KOF, FLG) would be applied by the S-DSP mixer. */
}

static inline uint8_t dsp_read(uint8_t regno) {
    return regno < 0x80 ? dsp_regs[regno] : 0;
}

/* ========================================================================
 * DIRECT-PAGE GLOBALS (SPC RAM $00..$FF) — observed names per routine.
 *
 * Roles deduced from how each address is used in the disassembly:
 *
 *   $00         X-scratch (loop counter for the "8 voices" outer loop)
 *   $01         voice index within batch  (used to compute DSP-reg index)
 *   $02         voice-mask bit  (1<<channel_index, walks 1->2->4->...->$80)
 *   $03         8 * channel index — the per-channel slot offset
 *   $04         "this voice's status flags" (1=playing, etc.)
 *   $05/$06     16-bit pointer into song-data stream (Y is index)
 *   $07..$09    pointer scratch
 *   $0C/$0D     16-bit pointer (used by DSP-bank set in $0F3A and by the
 *               IPL inner-loop at $07B0 as the ARAM dest pointer)
 *   $0E..$0F    16-bit scratch (pitch-table lookup result)
 *   $10..$11    16-bit scratch (pitch-table delta)
 *   $14         channel-on accumulator (default $20)
 *   $15         NON-bit shadow ("noise on")
 *   $16         KON shadow      (used in $0620 to send to DSP $4C)
 *   $17         KOF shadow      (used in $0620 — XOR'd with $16 to get
 *                                "voices that just got turned on" mask)
 *   $18         EON bit shadow  (DSP $4D)
 *   $19         PMON bit shadow (DSP $2D)
 *   $1A..$1B    fade tick counter (16 bits)
 *   $1C..$1D    fade master-vol delta (16-bit signed)
 *   $1E..$1F    fade target master-vol (16-bit)
 *   $20..$27    "channel state byte 0" per voice (instrument / current note)
 *   $28..$2F    "channel state byte 1" per voice
 *   $30..$33    APUIO shadow ("last value we ACKed back to the CPU")
 *   $48..$4F    8 bytes of channel volume scratch (used by $0966)
 *   $50..$57    per-voice "ticks until next note" (DBNZ counter)
 *   $F0-$FF     hardware ports (DSP / CPUIO / timers)
 *
 * The fully-private per-voice arrays live at higher SPC addresses, where
 * channel n's slot starts at base + n.  Bases seen in the code:
 *   $0140 = "current sequence command per voice"  (set to 0 when stopped)
 *   $0160 = sub-tick fraction (envelope accumulator)
 *   $0120 = song-data pointer high byte? (paired with $0140 as table)
 *   $0180 = volume per voice
 *   $01A0 = pitch base (note)
 *   $0200-$02FF voice scratch
 *   $0300-$03FF voice envelope/effect state
 *   $0480 = "voice activity flag"
 *
 * These are mirror buffers — when the song-tick handler picks a voice it
 * loads from these into the DSP-shaped fields starting at $0140 etc.
 * ======================================================================== */

/* ========================================================================
 * SPC-side aliases for the addresses we touch most often.
 * ======================================================================== */
#define SP_CONTROL  0xF1
#define SP_DSPADDR  0xF2
#define SP_DSPDATA  0xF3
#define SP_CPUIO0   0xF4
#define SP_CPUIO1   0xF5
#define SP_CPUIO2   0xF6
#define SP_CPUIO3   0xF7
#define SP_T0DIV    0xFA
#define SP_T1DIV    0xFB
#define SP_T2DIV    0xFC
#define SP_T0OUT    0xFD
#define SP_T1OUT    0xFE
#define SP_T2OUT    0xFF

/* DSP register numbers used (NOT addresses): set via dsp_write(reg, val). */
#define DSP_V_VOL_L   0x00   /* +0x10 per voice                              */
#define DSP_V_VOL_R   0x01
#define DSP_V_PITCH_L 0x02
#define DSP_V_PITCH_H 0x03
#define DSP_V_SRCN    0x04
#define DSP_V_ADSR1   0x05
#define DSP_V_ADSR2   0x06
#define DSP_V_GAIN    0x07
#define DSP_MVOL_L    0x0C
#define DSP_MVOL_R    0x1C
#define DSP_EVOL_L    0x2C
#define DSP_EVOL_R    0x3C
#define DSP_KON       0x4C
#define DSP_KOF       0x5C
#define DSP_FLG       0x6C
#define DSP_ENDX      0x7C
#define DSP_EFB       0x0D
#define DSP_PMON      0x2D
#define DSP_NON       0x3D
#define DSP_EON       0x4D
#define DSP_DIR       0x5D

/* ========================================================================
 * Forward declarations (lifted routines, named by SPC address)
 * ======================================================================== */
static void  driver_init_0F3A(void);
static void  driver_main_loop_0611(void);
static void  apply_kon_kof_to_dsp_0620(void);
static void  mute_all_voices_0670(void);
static int   cpuio_changed_067D(uint8_t idx);   /* returns 1 if changed   */
static void  poll_all_cpuios_0690(void);
static int   dispatch_apuio_command_06CC(uint8_t cmd);
static void  dispatch_special_cmd_0729(uint8_t cmd);
static void  start_song_at_index_06DA(uint8_t y);
static void  set_master_volume_074C(uint8_t v);
static void  start_fade_0758(uint8_t cmd);
static void  bootstrap_voice_07E5(uint8_t voice);
static void  song_tick_08E2(void);
static void  envelope_tick_0D41(void);
static void  load_sample_or_keep_0DB4(void);
static void  apply_pan_to_dsp_0DE0(void);
static void  pitch_to_dsp_or_noise_0E07(void);
static uint8_t commit_song_y_0D34(uint8_t y);
static void  load_sample_voice_0966(uint8_t y, uint8_t a);
static void  song_event_dispatch_099B(void);
static void  compute_pitch_09FF(uint8_t a, uint8_t channel_idx);
static void  event_note_0A46(uint8_t a);

/* Per-event handlers — jump table at SPC $09D5.
 *
 * V3-G + final cleanup verification: the dispatcher does
 *
 *     CMP A, #$15 ; BCC <ok> ; JMP $0D34   ; index 0..$14 (21 slots)
 *     ASL A / MOV X,A
 *     MOV A, !$09D6+X ; PUSH A      ; high byte
 *     MOV A, !$09D5+X ; PUSH A      ; low  byte
 *     MOV X, $03 ; RET
 *
 * So the CMP threshold permits indices 0..0x14 (21 slots), confirming
 * V3-G's "21, not 16" finding. HOWEVER, the table bytes at $09D5..$09FA
 * only contain 19 real handler pointers — the final 2 slots ($09FB,
 * $09FD) fall inside the body of compute_pitch_09FF and contain the
 * bytes `4D 6D F8 00` (PUSH X / PUSH Y / MOV X,$00). Indices 19 and 20
 * therefore jump into compute_pitch's prologue if a song stream ever
 * emits them. Game data never does, so the slots are effectively
 * unused garbage held over from the original ROM. We list 19 reachable
 * handler prototypes plus stubs to document the situation. */
static void  event_set_instr_0A74(void);       /* idx  0 */
static void  event_pan_0A86(void);             /* idx  1 */
static void  event_set_x90_0AD2(void);         /* idx  2 */
static void  event_pitch_env_0AA9(void);       /* idx  3 */
static void  event_set_tempo_0ADA(void);       /* idx  4 */
static void  event_set_xD0_0AE6(void);         /* idx  5 */
static void  event_ptr_relative_0AEE(void);    /* idx  6 */
static void  event_loop_setup_0B09(void);      /* idx  7 */
static void  event_loop_iter_0B1B(void);       /* idx  8 */
static void  event_voice_stop_0B4C(void);      /* idx  9 */
static void  event_rest_0B52(void);            /* idx 10 */
static void  event_set_base_pitch_0B67(void);  /* idx 11 */
static void  event_subr_call_0B7C(void);       /* idx 12 */
static void  event_subr_return_0BBB(void);     /* idx 13 */
static void  event_pan_slide_0BD8(void);       /* idx 14 */
static void  event_keypress_0A9D(void);        /* idx 15 */
static void  event_set_transpose_0B70(void);   /* idx 16 */
static void  event_keyrest_0ABB(void);         /* idx 17 */
static void  event_fine_pitch_0CB0(void);      /* idx 18 */
static void  event_pitch_slide_0CB9(void);     /* idx 19 */
static void  event_vol_env_0AB2(void);         /* idx 20 */

static void  release_voice_08A1(uint8_t voice);
static void  any_voice_active_08BD(uint8_t voice);
static void  song_command_byte_dispatch_06CC(uint8_t a);
static void  driver_kick_06B6_06C4(void);
static void  ipl_runtime_handshake_0795(void);

/* Handy "post-increment DSP" pattern used everywhere. */
static inline void dsp_set_addr(uint8_t reg) { spc_ram[SP_DSPADDR] = reg; }
static inline void dsp_set_data(uint8_t v)   { spc_ram[SP_DSPDATA] = v;
                                                dsp_write(spc_ram[SP_DSPADDR], v); }
static inline void dsp_set(uint8_t reg, uint8_t v) { dsp_set_addr(reg); dsp_set_data(v); }

/* ========================================================================
 * $0600 — DRIVER ENTRY POINT (after the IPL jump from $08:8006)
 *
 * See wiki/17-audio.md §2 for the SPC main loop (4 ms tick rate via
 * timer 0, T0DIV = $80), §1 for the IPL upload (3,327 bytes at ROM
 * file offset 0x5F004 — NOT the 30 KB music-data block at 0x40A00),
 * and §3 for the 65816-side command surface.
 *
 *   CLRP                ; direct page = $00xx (not $01xx)
 *   MOV A, #$00 / MOV $F4,A / $F5,A / $F6,A / $F7,A   ; clear CPUIOs
 *   MOV X, #$1F         ; SP = $011F (one page below current direct)
 *   MOV SP, X
 *   CALL !$0F3A         ; full DSP + voice reset
 *   loop:
 *     MOV A, $FD ;T0OUT  ; wait until T0 overflows (~4 ms)
 *     BEQ loop
 *     CALL !$0670        ; KON/KOF shadow reset + DSP KOF=0
 *     CALL !$0690        ; poll APUIO commands (music + 7 SFX channels)
 *     CALL !$0620        ; commit shadow → DSP (KON, KOF, NON, EON, PMON, vol fade)
 *     BRA loop
 * ======================================================================== */
static void driver_entry_0600(void)
{
    /* CLRP: clears P flag in PSW (selects direct page = 0x00xx).         */
    /* Zero the CPU I/O ports we own (the four SPC-side $F4-$F7).         */
    memset(&spc_ram[SP_CPUIO0], 0, 4);

    /* Stack pointer set: SPC stack lives at $0100-$01FF; SP=$1F means
     * the next PUSH writes to $011F.                                     */

    driver_init_0F3A();
    driver_main_loop_0611();
}

/* ========================================================================
 * $0611 — main loop body (forever, ticked by T0 ~ every 4 ms)
 * ======================================================================== */
static void driver_main_loop_0611(void)
{
    for (;;) {
        /* Wait for T0 overflow (4 ms tick at $80 divisor on the 8 kHz
         * internal source). T0OUT auto-clears on read.                   */
        while (spc_ram[SP_T0OUT] == 0) { /* spin */ }

        mute_all_voices_0670();         /* clear KON/KOF shadow, KOF=0   */
        poll_all_cpuios_0690();         /* process incoming commands     */
        apply_kon_kof_to_dsp_0620();    /* commit to DSP, advance fade   */
    }
}

/* ========================================================================
 * $0620 — APPLY_KON_KOF + advance master-volume fade.
 *
 * Shadow vars:  $16 = KON shadow, $17 = KOF shadow, $19 = PMON shadow,
 *               $15 = NON shadow, $18 = EON shadow.
 *
 *   KOF = ~$16 & $17                       (voices that should turn OFF)
 *   KON = $16                              (voices that should turn ON)
 *   PMON = $19   NON = $15   EON = $18
 *
 * Then, if the fade timer $1A:1B != 0:
 *   $1E:1F -= $1C:1D  (advance master-vol towards target)
 *   write 16-bit to MVOL_L ($0C) and MVOL_R ($1C)
 *   DECW $1A
 *   if $1A == 0 after dec:
 *     for (i = 3; i >= 0; --i) release_voice_08A1(i);   // release low voices
 *     zero the CPUIOs (ACK the "fade complete" back to the 65816)
 * ======================================================================== */
static void apply_kon_kof_to_dsp_0620(void)
{
    uint8_t kof_mask = (uint8_t)(~spc_ram[0x16] & spc_ram[0x17]);

    dsp_set(DSP_KOF,  kof_mask);
    dsp_set(DSP_KON,  spc_ram[0x16]);
    dsp_set(DSP_PMON, spc_ram[0x19]);
    dsp_set(DSP_NON,  spc_ram[0x15]);
    dsp_set(DSP_EON,  spc_ram[0x18]);

    /* 16-bit fade tick counter in $1A:1B.                                */
    uint16_t fade_ticks = (uint16_t)(spc_ram[0x1A] | (spc_ram[0x1B] << 8));
    if (fade_ticks == 0) return;

    uint16_t mvol      = (uint16_t)(spc_ram[0x1E] | (spc_ram[0x1F] << 8));
    uint16_t delta     = (uint16_t)(spc_ram[0x1C] | (spc_ram[0x1D] << 8));
    mvol -= delta;
    spc_ram[0x1E] = (uint8_t) mvol;
    spc_ram[0x1F] = (uint8_t)(mvol >> 8);
    /* Y register held the high byte; both MVOL_L and MVOL_R receive it.  */
    dsp_set(DSP_MVOL_L, (uint8_t)(mvol >> 8));
    dsp_set(DSP_MVOL_R, (uint8_t)(mvol >> 8));

    fade_ticks--;
    spc_ram[0x1A] = (uint8_t)fade_ticks;
    spc_ram[0x1B] = (uint8_t)(fade_ticks >> 8);
    if (fade_ticks != 0) return;

    /* Fade complete — release voices 0..3, ack via CPUIOs.               */
    for (int8_t voice = 3; voice >= 0; --voice) {
        release_voice_08A1((uint8_t)voice);
    }
    memset(&spc_ram[SP_CPUIO0], 0, 4);
}

/* ========================================================================
 * $0670 — clear KON shadow (so we only assert KON for voices that get a
 * fresh key-on this tick); clear DSP KOF (so a previous "release" doesn't
 * latch).
 * ======================================================================== */
static void mute_all_voices_0670(void)
{
    spc_ram[0x16] = 0;        /* KON shadow */
    spc_ram[0x17] = 0;        /* KOF shadow */
    dsp_set(DSP_KOF, 0x00);   /* DSP KOF cleared */
}

/* ========================================================================
 * $067D — CPUIO change detector.
 *
 * Called with $00 = port index (0..3).  Reads CPUIO[$00], compares with
 * shadow $30+X.  If equal: return C=0 (no new command).  If different:
 *   $30+X = new value
 *   CPUIO[X] = new value XOR'd with $80  (so 65816 sees its value echoed)
 *   return C=1
 *
 * The XOR-$80 dance gives a "I read your write, here's an ACK with bit 7
 * toggled" — this is the same handshake used by Maxis's other titles.
 * ======================================================================== */
static int cpuio_changed_067D(uint8_t idx)
{
    spc_ram[0x00] = idx;
    uint8_t latest = spc_ram[SP_CPUIO0 + idx];     /* MOV A, $F4+X       */
    if (latest == spc_ram[0x30 + idx]) {
        return 0;                                  /* CLRC                */
    }
    spc_ram[0x30 + idx]      = latest;             /* MOV $30+X, A        */
    spc_ram[SP_CPUIO0 + idx] = (uint8_t)(latest ^ 0x80);  /* echo back     */
    return 1;                                      /* SETC                */
}

/* ========================================================================
 * $0690 — POLL_ALL_CPUIOS — once per 4 ms tick.
 *
 *   for (port = 3; port >= 0; --port) {
 *     if (cpuio_changed_067D(port)) {
 *       cmd = spc_ram[$30+port];   // the just-latched value
 *       if (! dispatch_apuio_command_06CC(cmd)) continue;
 *     }
 *     if (spc_ram[$20+port] != 0)            // voice "active" flag
 *       song_tick_08E2();                    // advance song this tick
 *     any_voice_active_08BD(port);           // mark ENDED flag for 65816
 *   }
 * ======================================================================== */
static void poll_all_cpuios_0690(void)
{
    /* ROM body (SPC $0690..$06CB):
     *   MOV A, #0; MOV $04, A
     *   MOV $00, #3              ; port = 3, decrementing
     *  loop:
     *   CALL $067D                ; cpuio_changed?
     *   BCC continue              ; if no change, skip dispatch
     *     CALL $06CC               ; dispatch_apuio_command
     *     BCC done                  ; if dispatch returned CLC, skip ticking
     *  continue:
     *   MOV X, $00; MOV A, $20+X
     *   BNE done                  ; voice "active" flag NONZERO -> skip ticks
     *   MOV A, $00
     *   ASL A; ASL A; ASL A; OR A,#$07   ; X = port*8 + 7
     *   MOV $03, A
     *   MOV $01, #$07; MOV $02, #$80     ; bitmask walks $80 -> $01
     *  innerloop:
     *   CALL $08E2 (song_tick); CALL $0D41 (per-tick volume update)
     *   DEC $03; DEC $01; LSR $02; BNE innerloop
     *  done:
     *   CALL $08BD (any_voice_active — ack DONE to 65816 if all idle)
     *   DEC $00; BPL loop
     *
     * Critically, `BNE done` means: when $20+X == 0 we DO fall into the
     * song-tick block.  The lift previously had the inverted check —
     * fixing that here.
     */
    spc_ram[0x04] = 0x00;

    for (int8_t port = 3; port >= 0; --port) {
        spc_ram[0x00] = (uint8_t)port;

        int dispatch_skipped = 0;
        if (cpuio_changed_067D((uint8_t)port)) {
            uint8_t cmd = spc_ram[0x30 + port];
            if (dispatch_apuio_command_06CC(cmd) == 0) {
                /* Dispatcher returned CLC → command consumed, skip ticking. */
                dispatch_skipped = 1;
            }
        }

        if (!dispatch_skipped && spc_ram[0x20 + port] == 0) {
            /* Voice-activity flag is ZERO → drive song-tick.
             * X = port*8 + 7 (start from voice index 7, walk down).
             * ROM disasm $06B6..$06C2:
             *   CALL !$08E2 (song_tick)
             *   CALL !$0D41 (envelope_tick_helper)   <-- NOT $099B!
             *   DEC $03; DEC $01; LSR $02; BNE loop
             * Bug fix beyond V2-E: the previous lift called song_event_dispatch
             * here, but that routine is invoked from inside song_tick when a
             * note duration expires.  $0D41 is the per-tick envelope/pitch-bend
             * helper that loads samples on instrument change, asserts NON/EON,
             * and writes per-voice pitch+pan to the DSP via $0DE0 / $0E07. */
            spc_ram[0x03] = (uint8_t)(((port & 0x07) << 3) | 0x07);
            spc_ram[0x01] = 0x07;
            spc_ram[0x02] = 0x80;
            do {
                song_tick_08E2();
                envelope_tick_0D41();
                spc_ram[0x03]--;
                spc_ram[0x01]--;
                spc_ram[0x02] >>= 1;
            } while (spc_ram[0x02] != 0);
        }

        any_voice_active_08BD((uint8_t)port);
    }
}

/* ========================================================================
 * $06CC — DISPATCH AN APUIO COMMAND BYTE.
 *
 *   AND #$FE     ; cmd_low_bit_cleared
 *   if (zero) {
 *      call release_voice_08A1(spc_ram[$00]);    // command 0/1: stop
 *      return 0;                                 // C=0 → "consumed"
 *   }
 *   if (bit 7 set) JMP dispatch_special_cmd_0729(cmd);
 *
 *   // It's a song-start command in [$02..$7E].
 *   Y = cmd;
 *   ptr = (spc_ram[$12FE+Y] | spc_ram[$12FF+Y]<<8)   // song table
 *   spc_ram[$05] = lo; spc_ram[$06] = hi;
 *   Y = 0; X = spc_ram[$00];                         // active port idx
 *   spc_ram[$24+X] = $3F;       // default volume = $3F
 *   spc_ram[$2C+X] = 0;
 *   spc_ram[$20+X] = 0;         // mark "no current note"
 *   spc_ram[$28+X] = $FF;       // pan? (centre)
 *   ch_slot_offset = X * 8;     // 8 bytes per channel slot
 *   spc_ram[$03] = ch_slot_offset;
 *   spc_ram[$01] = $00;
 *   spc_ram[$02] = $01;         // initial voice bitmask
 *   for (voice = 0; voice < 8; voice++) {
 *     bootstrap_voice_07E5(voice);
 *     spc_ram[$03]++; spc_ram[$01]++; spc_ram[$02] <<= 1;
 *   }
 *   write $7F to DSP MVOL_L + MVOL_R    // master volume full
 *   spc_ram[$1A..$1F] = 0                // no fade
 *   return C=1
 * ======================================================================== */
static int dispatch_apuio_command_06CC(uint8_t cmd)
{
    uint8_t a = cmd & 0xFE;
    if (a == 0) {
        release_voice_08A1(spc_ram[0x00]);
        return 0;                       /* CLRC; RET in the original */
    }
    if (cmd & 0x80) {
        dispatch_special_cmd_0729(cmd);
        return 1;                       /* SETC; RET implied via JMP    */
    }

    start_song_at_index_06DA(a);
    return 1;
}

static void start_song_at_index_06DA(uint8_t y)
{
    /* Song-table lookup: 16-bit pointer at $12FE+y / $12FF+y.            */
    uint8_t lo = spc_ram[0x12FE + y];
    uint8_t hi = spc_ram[0x12FF + y];
    spc_ram[0x05] = lo;
    spc_ram[0x06] = hi;
    uint8_t x = spc_ram[0x00];          /* active port = active "channel
                                           assignment slot"               */

    spc_ram[0x24 + x] = 0x3F;
    spc_ram[0x2C + x] = 0x00;
    spc_ram[0x20 + x] = 0x00;
    spc_ram[0x28 + x] = 0xFF;

    uint8_t base = (uint8_t)(x << 3);
    spc_ram[0x03] = base;
    spc_ram[0x01] = 0x00;
    spc_ram[0x02] = 0x01;
    for (int v = 0; v < 8; ++v) {
        bootstrap_voice_07E5((uint8_t)v);
        spc_ram[0x03]++;
        spc_ram[0x01]++;
        spc_ram[0x02] <<= 1;
    }

    /* Master volume restored to $7F (mid-positive).                      */
    dsp_set(DSP_MVOL_L, 0x7F);
    dsp_set(DSP_MVOL_R, 0x7F);
    /* Clear the fade-tick + delta + target.                              */
    memset(&spc_ram[0x1A], 0, 6);
}

/* ========================================================================
 * $0729 — SPECIAL COMMAND DISPATCH (cmd & $80 set)
 *
 *   if (cmd < $C0) {   AND #$3F → JMP $074B (set master volume)      }
 *   if (cmd < $D0) {   AND #$0F → JMP $0758 (start fade w/ given rate)}
 *   else              cmd = (cmd ^ $FE) & $06; X = that; JMP [$0743+X]
 *
 * Table at $0743 (4 entries × 2 bytes — but the disassembler shows it as
 * code; raw bytes are 95 07 A3 07 DB 07 95 07):
 *   X=0 → $0795  (runtime IPL re-upload)
 *   X=2 → $07A3  (the IPL HANDSHAKE itself; embedded so SPC can re-enter
 *                 from $0795 via JMP [$FFFE] — i.e. "reset, re-enter IPL")
 *   X=4 → $07DB  (toggle voice activity bit, command code-pair $D6/$D8)
 *   X=6 → $0795  (alias)
 *
 * Practically:  command bytes $D0/$D1/$D2/$D3 select "switch song bank";
 * $D6/$D7 toggles the per-voice activity flag.                          */
static void dispatch_special_cmd_0729(uint8_t cmd)
{
    if (cmd < 0xC0) {
        set_master_volume_074C((uint8_t)(cmd & 0x3F));
        return;
    }
    if (cmd < 0xD0) {
        start_fade_0758((uint8_t)(cmd & 0x0F));
        return;
    }
    /* $D0+ — pick one of four sub-handlers indexed by (cmd ^ $FE) & 6.   */
    uint8_t x = (uint8_t)((cmd ^ 0xFE) & 0x06);
    switch (x) {
        case 0x00: ipl_runtime_handshake_0795(); break;
        case 0x02: /* JMP $07A3 — IPL handshake start (rare runtime entry). */
                   ipl_runtime_handshake_0795();
                   break;
        case 0x04: {                /* JMP $07DB — toggle voice flag       */
            uint8_t v = spc_ram[0x00];
            spc_ram[0x20 + v] ^= 0x01;
            break;
        }
        case 0x06:                  /* JMP $0795 alias                    */
            ipl_runtime_handshake_0795();
            break;
    }
}

/* ========================================================================
 * $074B — SET MASTER VOLUME (value in A, 6-bit).
 * ======================================================================== */
static void set_master_volume_074C(uint8_t a)
{
    dsp_set(DSP_MVOL_L, a);
    dsp_set(DSP_MVOL_R, a);
}

/* ========================================================================
 * $0758 — START FADE (rate index in A, 4-bit).
 *
 *   X = (A >> 1) + 1                  // ticks per step, max 8
 *   $1B = X;  $1A = $00
 *   $1A:$1B = ($1A:$1B) >> 2          // total fade ticks (in T0 units)
 *   $1E = $FF;  $1F = $7F            // initial mvol target = $7FFF
 *   For 6 iterations:  (mvol_step / X) >> 6 in the lo half
 *   sets $1C:$1D as the per-tick delta and $1E:$1F as the current value.
 *
 * Effectively: fade master-vol from $7FFF down to 0 over N ticks where
 * N = ((rate >> 1) + 1) * 4.   At T0=4ms that's 4..32 ms per step or
 * 16..128 ms total fade-out — matches the "view-switch beep" + fade out.
 * ======================================================================== */
static void start_fade_0758(uint8_t a)
{
    /* ROM body (SPC $0758):
     *   LSR A; INC A; MOV X,A             ; X = (rate >> 1) + 1   (1..8)
     *   MOV $1B,A                          ; high byte of fade counter = X
     *   MOV A,#0; MOV $1A,A               ; low byte = 0    ($1A:1B = X*256)
     *   LSR $1B; ROR $1A                   ; right-shift 16-bit
     *   LSR $1B; ROR $1A                   ; right-shift 16-bit  -> X*64
     *   MOV $1E,#$FF; MOV $1F,#$7F        ; current mvol = $7FFF
     *   MOV A,$1F; MOV Y,#0; DIV YA,X     ; A = $7F / X
     *   MOV $1D,A
     *   MOV A,$1E; DIV YA,X               ; remainder from previous DIV in Y
     *   MOV $1C,A                          ; $1C:1D = ($7FFF / X)
     *   6 x (LSR $1D; ROR $1C)             ; >> 6   -> $1C:1D = step
     *
     * So per-tick step = ($7FFF / X) >> 6, and total ticks = X * 64.
     */
    uint8_t x = (uint8_t)((a >> 1) + 1);
    uint16_t total_ticks = (uint16_t)(x * 64);
    spc_ram[0x1A] = (uint8_t)total_ticks;
    spc_ram[0x1B] = (uint8_t)(total_ticks >> 8);

    spc_ram[0x1E] = 0xFF;
    spc_ram[0x1F] = 0x7F;
    uint16_t step = (uint16_t)((0x7FFF / x) >> 6);
    spc_ram[0x1C] = (uint8_t)step;
    spc_ram[0x1D] = (uint8_t)(step >> 8);
}

/* ========================================================================
 * $0795 — RUNTIME IPL HANDSHAKE (asset/song re-upload entry)
 *
 *   CONTROL = $31  ; clear IO2+3, enable IPL bit + T0 off
 *   DSP_FLG = $FF  ; mute + reset all voices
 *   X = 0
 *   JMP [$FFFE]    ; jump through the SPC IPL ROM's RESET vector
 *
 * In real hardware, this re-enters the IPL handshake at $FFC0 (a hardware
 * ROM).  The driver's own copy at $07A3..$07DA mirrors that handshake so
 * the SPC can re-receive a block transfer even while the driver code is
 * still resident in RAM.
 *
 * The IPL flow at $07A3:
 *   APUIO0 = $AA, APUIO1 = $BB         ; "ready" handshake
 *   wait until APUIO0 == $CC            ; CPU signals "go"
 *   BRA $07CB → read APUIO2/3 as 16-bit dest pointer, store in $0C/$0D
 *   loop ($07B0): read APUIO0 as block index, APUIO1 as data byte,
 *                 store [$0C]+Y, INC Y; when Y wraps, INC $0D.
 *   When APUIO0's high bit flips, treat as end-of-block; if Y=0 then
 *   we got a new dest pointer; if not, fetch next byte.
 *   Final block (Y == 0): MOVW $0C, APUIO2; check Y == 0 → done.
 *   Restore CONTROL = $31 (timers off until driver resumes).
 * ======================================================================== */
static void ipl_runtime_handshake_0795(void)
{
    /* $0795 prologue (verbatim):
     *   MOV $F1, #$31        ; CONTROL: enable T0+T1, clear IO ports 2+3
     *   MOV $F2, #$6C        ; DSP_FLG addr
     *   MOV $F3, #$FF        ; mute + reset all voices
     *   MOV X, #$00
     *   JMP [$FFFE+X]        ; jump through the SPC IPL ROM RESET vector
     *
     * On real hardware, the JMP [$FFFE] enters the on-chip IPL ROM at $FFC0
     * which runs the classic $AA/$BB/$CC handshake to receive a fresh code
     * blob from the 65816. The handshake driver fragment is also embedded
     * at $07A3..$07DA so the SPC can fall back into the same protocol
     * without going through the IPL ROM.
     *
     * For a Flipper port: the audio asset upload happens once at boot and
     * never runs again (SimAnt does not stream additional audio assets).
     * So this routine is effectively a no-op after the very first call.
     * If the host is re-loading audio asset banks the host should re-do the
     * full IPL transfer ($08:8006 entry on the 65816 side).
     */
    spc_ram[SP_CONTROL] = 0x31;
    spc_control = 0x31;
    dsp_set(DSP_FLG, 0xFF);
    /* NOT IMPLEMENTED: full IPL re-entry via $07A3-handshake. */
}

/* ========================================================================
 * $07E5 — BOOTSTRAP_VOICE — called 8× from start_song_at_index for each
 * voice.  Initialises the per-voice state arrays.
 *
 *   X = $03  (= 8 * channel_index, voice base offset)
 *   A = 0
 *   wram[$0480+X] = 0           ; voice slot inactive
 *   wram[$70+X]   = 0           ; transient envelope state
 *   wram[$90+X]   = 0
 *   wram[$B0+X]   = 0
 *   wram[$D0+X]   = 0
 *   wram[$0180+X] = 0           ; volume scratch
 *   wram[$03C0+X] = 0           ; pitch env target
 *   wram[$03E0+X] = 0           ; pitch env target hi
 *   wram[$01A0+X] = 0           ; pitch base
 *   wram[$02C0+X] = 0           ; pitch-fade state
 *   wram[$02E0+X] = 0           ; vol-fade state
 *   wram[$0320+X] = 0           ; portamento curr
 *   wram[$0340+X] = 0           ; portamento target
 *   wram[$0360+X] = 0           ; ticks-to-next-step
 *   wram[$0380+X] = 0           ; pitch-bend delta lo
 *   wram[$03A0+X] = 0           ; pitch-bend delta hi
 *   wram[$0300+X] = 0           ; portamento speed
 *   wram[$50+X]   = $01         ; ticks-to-next-note = 1 (immediate)
 *   wram[$0160+X] = $FF         ; envelope accumulator high
 *   wram[$0200+X] = $FF         ; pitch hi
 *   wram[$0220+X] = $FF         ; pitch hi'
 *   wram[$0240+X] = $7F         ; volume L
 *   wram[$0260+X] = $7F         ; volume R
 *   wram[$0280+X] = $7F         ; pan L
 *   wram[$02A0+X] = $7F         ; pan R
 *   A = $7F AND $04 AND $02     ; if $04 & $02 & $7F nonzero, skip DSP init
 *   if (!A and wram[$0140+X]) {  // if voice has a current instrument
 *     dsp_addr = (channel_idx << 4)   ; per-voice DSP region
 *     dsp[+0] = 0   ; vol L
 *     dsp[+1] = 0   ; vol R
 *     dsp[+2] = 0   ; pitch L
 *     dsp[+3] = 0   ; pitch H
 *     dsp[+4] = 0   ; SRCN
 *     dsp[+5] = $FF  ; ADSR1
 *     dsp[+6] = $FF  ; ADSR2
 *     dsp[+7] = $FF  ; GAIN
 *   }
 *   if (wram[$0140+X] != 0) {
 *     $17 |= $02              ; assert KOF for this voice
 *     wram[$0015] &= ~$02     ; (turn off NON for this slot, etc.)
 *     wram[$0016] &= ~$02
 *     wram[$0018] &= ~$02
 *     wram[$0019] &= ~$02
 *   }
 * ======================================================================== */
static void bootstrap_voice_07E5(uint8_t voice)
{
    (void)voice;
    uint8_t x = spc_ram[0x03];

    /* $07E7-$0815: zero-clear a long list of per-voice scratch arrays. */
    spc_ram[0x0480 + x] = 0;
    spc_ram[0x70  + x] = 0;
    spc_ram[0x90  + x] = 0;
    spc_ram[0xB0  + x] = 0;
    spc_ram[0xD0  + x] = 0;
    spc_ram[0x0180 + x] = 0;
    spc_ram[0x03C0 + x] = 0;
    spc_ram[0x03E0 + x] = 0;
    spc_ram[0x01A0 + x] = 0;
    spc_ram[0x02C0 + x] = 0;
    spc_ram[0x02E0 + x] = 0;
    spc_ram[0x0320 + x] = 0;
    spc_ram[0x0340 + x] = 0;
    spc_ram[0x0360 + x] = 0;
    spc_ram[0x0380 + x] = 0;
    spc_ram[0x03A0 + x] = 0;
    spc_ram[0x0300 + x] = 0;

    /* $0818  INC A           ; A = 0 -> A = 1
     * $0819  MOV $50+X, A    ; ticks-to-next-note = 1 (immediate) */
    spc_ram[0x50  + x] = 0x01;
    /* $081B  MOV A, #$FF; $081D-$0823 propagate FF to scratch. */
    spc_ram[0x0160 + x] = 0xFF;
    spc_ram[0x0200 + x] = 0xFF;
    spc_ram[0x0220 + x] = 0xFF;
    /* $0826  MOV A, #$7F; $0828-$0831 propagate 7F to volume/pan. */
    spc_ram[0x0240 + x] = 0x7F;
    spc_ram[0x0260 + x] = 0x7F;
    spc_ram[0x0280 + x] = 0x7F;
    spc_ram[0x02A0 + x] = 0x7F;

    /* $0834  AND A, $04   ; A = $7F & $04
     * $0836  AND A, $02   ; A = $7F & $04 & $02
     * $0838  BNE $0880    ; if voice is gated, SKIP DSP init but STILL run tail
     * $083A  MOV A, !$0140+X
     * $083D  BEQ $0880    ; if voice has no current instr, SKIP DSP init
     *
     * BUG FIX beyond V2-E:  the old lift treated these branches as early
     * RETURNs.  They are NOT — both branch into the $0880 song-header read.
     * Without the tail running, the per-voice song pointer is never set up,
     * so events never fire for any voice and the song plays SILENCE. */
    int do_dsp_init = (((uint8_t)(0x7F & spc_ram[0x04] & spc_ram[0x02])) == 0)
                      && (spc_ram[0x0140 + x] != 0);

    if (do_dsp_init) {
        /* Per-voice DSP registers (channel_idx in the upper nibble).
         * $083F-$0868: walk DSP regs +0..+7 writing 0,0,0,0,0,$FF,$FF,$FF
         * (vol L, vol R, pitch L, pitch H, SRCN, ADSR1, ADSR2, GAIN). */
        uint8_t reg_base = (uint8_t)((spc_ram[0x01] << 4) & 0xF0);
        dsp_set((uint8_t)(reg_base + DSP_V_VOL_L),  0);
        dsp_set((uint8_t)(reg_base + DSP_V_VOL_R),  0);
        dsp_set((uint8_t)(reg_base + DSP_V_PITCH_L),0);
        dsp_set((uint8_t)(reg_base + DSP_V_PITCH_H),0);
        dsp_set((uint8_t)(reg_base + DSP_V_SRCN),   0);
        dsp_set((uint8_t)(reg_base + DSP_V_ADSR1),  0xFF);
        dsp_set((uint8_t)(reg_base + DSP_V_ADSR2),  0xFF);
        dsp_set((uint8_t)(reg_base + DSP_V_GAIN),   0xFF);

        /* $086A re-tests $0140+X again; if still nonzero, assert KOF and
         * clear NON/KON/EON/PMON bits via TCLR1 (which is AND ~mask). */
        if (spc_ram[0x0140 + x] != 0) {
            uint8_t mask  = spc_ram[0x02];
            uint8_t inv   = (uint8_t)~mask;
            spc_ram[0x17] |= mask;
            spc_ram[0x15] &= inv;
            spc_ram[0x16] &= inv;
            spc_ram[0x18] &= inv;
            spc_ram[0x19] &= inv;
        }
    }

    /* $0880-$089F: ALWAYS read the per-voice song header (2 bytes per voice).
     *   A = [$05]+Y;  Y++;  $0C = A
     *   A = [$05]+Y;  Y++;  $0D = A    ; now $0C:$0D = 16-bit offset
     *   PUSH Y
     *   YA = $0C; BEQ $089A             ; if offset 0, clear $0140+X
     *   else  YA = $05 + offset;  $0120+X = A (lo);  $0140+X = Y (hi)
     *   POP Y; RET
     *
     * The caller (start_song_at_index_06DA) initialises Y=0 and the bootstrap
     * loop runs 8 times; each iteration consumes 2 header bytes.  We mirror
     * that by re-deriving Y from the per-voice counter $01.                */
    uint16_t song_base = (uint16_t)(spc_ram[0x05] | (spc_ram[0x06] << 8));
    uint8_t  y = (uint8_t)((spc_ram[0x01]) * 2);   /* 2 bytes per voice entry */
    uint8_t  off_lo = spc_ram[(song_base + y)     & 0xFFFF];
    uint8_t  off_hi = spc_ram[(song_base + y + 1) & 0xFFFF];
    uint16_t off  = (uint16_t)(off_lo | (off_hi << 8));
    if (off == 0) {
        spc_ram[0x0140 + x] = 0;
    } else {
        uint16_t voice_ptr = (uint16_t)(song_base + off);
        spc_ram[0x0120 + x] = (uint8_t)voice_ptr;
        spc_ram[0x0140 + x] = (uint8_t)(voice_ptr >> 8);
    }
}

/* ========================================================================
 * $08A1 — RELEASE_VOICE: stop playback on a specific port's channels.
 *
 *   X = $00 * 8     (8-byte slot)
 *   for (Y = 7; Y >= 0; --Y) {
 *     wram[$0140 + X + offset] = 0       ; clear "current sequence cmd"
 *     wram[$0120 + X + offset] = 0       ; clear pointer hi
 *     X++;
 *   }
 *   $17 |= ~$04                          ; assert KOF for these voices
 * ======================================================================== */
static void release_voice_08A1(uint8_t voice)
{
    (void)voice;
    uint8_t x = (uint8_t)(spc_ram[0x00] << 3);
    for (int y = 7; y >= 0; --y) {
        spc_ram[0x0140 + x] = 0;
        spc_ram[0x0120 + x] = 0;
        x = (uint8_t)(x + 1);
    }
    spc_ram[0x17] |= (uint8_t)(~spc_ram[0x04]);
}

/* ========================================================================
 * $08BD — ANY_VOICE_ACTIVE: scan voice slot and ack back to CPU when idle.
 *
 *   X = $00 * 8
 *   A = OR of $0140+X .. $0147+X
 *   if (A == 0) {  // all voices in this slot ended
 *     X = $00       // port index
 *     CPUIO[X] = 0  // signal "DONE" back to the 65816
 *   }
 * ======================================================================== */
static void any_voice_active_08BD(uint8_t voice)
{
    (void)voice;
    uint8_t x = (uint8_t)(spc_ram[0x00] << 3);
    uint8_t a = 0;
    for (int i = 0; i < 8; ++i) a |= spc_ram[0x0140 + x + i];
    if (a == 0) {
        spc_ram[SP_CPUIO0 + spc_ram[0x00]] = 0;
    }
}

/* ========================================================================
 * $08E2 — SONG_TICK: per-voice envelope + portamento advance.
 *
 *   X = $03  (voice slot base offset)
 *   if (wram[$0140+X] == 0) return                 ; voice idle
 *
 *   Y = $00 (port)                                 ; pulled by song system
 *   A = wram[$0024+Y]                              ; "base note rate"
 *   SETC
 *   A = wram[$0160+X] += A                         ; advance envelope acc
 *   if (no carry) return                           ; not yet at next step
 *
 *   call $0936  (slide portamento by per-tick delta)
 *   call $091A  (apply pitch-bend)
 *   wram[$50+X]--                                  ; ticks-to-next-note
 *   if (still positive) return
 *
 *   if (!(spc_ram[$04] & spc_ram[$02])) {          ; voice's bit not gated
 *     spc_ram[$17] |= spc_ram[$02]                 ; assert KOF
 *   }
 *   wram[$0380+X] = 0   wram[$03A0+X] = 0          ; stop pitch bend
 *   wram[$70+X]  = $FF                             ; mark "needs new note"
 *   call $099B (advance the song stream)
 * ======================================================================== */
static void song_tick_08E2(void)
{
    uint8_t x = spc_ram[0x03];
    if (spc_ram[0x0140 + x] == 0) return;

    uint8_t y = spc_ram[0x00];
    uint8_t a = spc_ram[0x0024 + y];
    uint16_t acc = (uint16_t)spc_ram[0x0160 + x] + a + 1;   /* SETC; ADC */
    spc_ram[0x0160 + x] = (uint8_t)acc;
    if ((acc & 0x100) == 0) return;   /* no carry-out, defer */

    /* Inline of $0936 + $091A (portamento step + pitch-bend apply). */
    /* (See helpers below.) */

    if (--spc_ram[0x50 + x] != 0) return;

    if (!(spc_ram[0x04] & spc_ram[0x02])) {
        spc_ram[0x17] |= spc_ram[0x02];
    }
    spc_ram[0x0380 + x] = 0;
    spc_ram[0x03A0 + x] = 0;
    spc_ram[0x70  + x] = 0xFF;
    song_event_dispatch_099B();
}

/* ========================================================================
 * $099B — SONG_EVENT_DISPATCH: read next sequence byte, branch by class.
 *
 * See wiki/17-audio.md §5: the jump table at $09D5 has 21 reachable
 * slots (via the CMP #$15 range check) but only 19 valid handler
 * pointers; indices 19, 20 contain garbage bytes that fall inside the
 * body of compute_pitch_09FF, never reached because no SimAnt song
 * stream ever emits event bytes in that range.
 *
 *   X = $03                          ; voice slot
 *   A = wram[$0140+X]                ; "song progress slot value"
 *   if (A == 0) return               ; voice already off
 *   if (A < $13) {                   ; out-of-range gate value? clear
 *     wram[$0140+X] = 0; return
 *   }
 *   if (A >= $3D) { wram[$0140+X] = 0; return }
 *   Y = A
 *   A = wram[$0120+X]                ; song-data pointer hi
 *   $05:$06 = (Y, A) — i.e. pointer = (hi, lo)
 *   Y = 0
 * fetch_loop:
 *   A = [$05]+Y                      ; sequence byte
 *   Y++
 *   if (A & $80) {                   ; high bit → NOTE event
 *     call event_note_0A46(A)
 *     JMP $0D34 (commit Y, advance pointer)
 *   }
 *   $0D = X                          ; save X
 *   A &= $7F
 *   if (A >= $15) {                  ; out-of-range command — bail
 *     JMP $0D34
 *   }
 *   X = A * 2
 *   pc.hi = wram[$09D6+X]
 *   pc.lo = wram[$09D5+X]
 *   PUSH both bytes; RET = jump to the address
 *   The command handlers all live in $0A74..$0AEx and end with
 *   "JMP $09B0" (= continue fetching events).
 *
 * Jump table at $09D5 (16 entries):
 *   index 0 → $0A74   (event_set_instr)
 *   index 1 → $0A86   (event_pan)
 *   index 2 → $0AD2   (event TBD)
 *   index 3 → $0AE6   (event TBD)
 *   index 4 → $0B09   (event TBD)
 *   ...
 *   index F → $0BBB   (event_jump? "loop back to song start"?)
 * ======================================================================== */
static void song_event_dispatch_099B(void)
{
    uint8_t x = spc_ram[0x03];
    uint8_t a = spc_ram[0x0140 + x];
    if (a == 0) return;
    if (a < 0x13 || a >= 0x3D) {
        spc_ram[0x0140 + x] = 0;
        return;
    }
    spc_ram[0x06] = a;
    spc_ram[0x05] = spc_ram[0x0120 + x];
    uint16_t ptr = (uint16_t)(spc_ram[0x05] | (spc_ram[0x06] << 8));
    uint8_t y = 0;

    for (;;) {
        uint8_t evt = spc_ram[ptr + y];
        y++;
        if (evt & 0x80) {
            event_note_0A46(evt);
            spc_ram[0x0140 + x] += y;  /* commit advance in $0D34 */
            return;
        }
        if ((evt &= 0x7F) >= 0x15) {
            spc_ram[0x0140 + x] += y;
            return;
        }
        /* Dispatch command via the jump table at $09D5+evt*2.
         * V3-G + final cleanup: ROM table has 19 valid handler pointers
         * (entries 0..18). Indices 19, 20 are reachable by the CMP #$15
         * range check but contain garbage bytes that fall inside the
         * body of compute_pitch_09FF; the song stream never emits them.
         * Handler bodies for cases 5..18 are not yet lifted — they fall
         * through to the same kill-track recovery path (`spc_ram[..]=0;
         * return`) used for out-of-range events, matching what happens
         * if a stub-only build receives one of those event bytes. */
        switch (evt) {
            case 0:  event_set_instr_0A74();  break;
            case 1:  event_pan_0A86();        break;
            case 2:  event_keypress_0A9D();   break;
            case 3:  event_pitch_env_0AA9();  break;
            case 4:  event_vol_env_0AB2();    break;
            case 5: case 6: case 7: case 8: case 9:
            case 10: case 11: case 12: case 13: case 14:
            case 15: case 16: case 17: case 18:
                /* TODO: lift handlers at $0AD2, $0AA9, $0ADA, $0AE6,
                 *       $0AEE, $0B09, $0B1B, $0B4C, $0B52, $0B67,
                 *       $0B7C, $0BBB, $0BD8, $0A9D, $0B70, $0ABB,
                 *       $0CB0, $0CB9, $0AB2 (per ROM table order — the
                 *       prototype names declared above index by handler
                 *       semantics, not jump-table slot). */
                spc_ram[0x0140 + x] = 0;
                return;
            default:
                /* idx 19, 20 — unreachable in normal song streams. */
                spc_ram[0x0140 + x] = 0;
                return;
        }
        /* Each event handler ends with JMP $09B0 — i.e. loop back. */
    }
}

/* ========================================================================
 * $09FF — COMPUTE_PITCH: turn (port * note semitone) into a DSP pitch word.
 *
 *   PUSH X / PUSH Y
 *   X = $00     CLRC   A += wram[$2C+X]           ; "transpose offset"
 *   X = $03     CLRC   A += wram[$01A0+X]         ; per-voice base
 *   if (A >= $78) A = $78                         ; clamp to 120 semitones
 *   X = A * 2   ; pitch table index
 *   Y = wram[$0E49+X];  A = wram[$0E48+X]; YA = (lo,hi)  →  $0E:$0F
 *   Y = wram[$0E4B+X];  A = wram[$0E4A+X];                 →  $10:$11
 *   $10:$11 -= $0E:$0F                            ; pitch delta
 *
 *   if (wram[$0300+X] != 0) {                     ; fine-pitch fraction
 *     Y = $11
 *     YA = A * Y    ;  scaled delta
 *     YA += $0E:$0F
 *     $0E:$0F = YA
 *   }
 *   POP Y / POP X / RET    ; result in $0E:$0F
 *
 * The pitch table at $0E48 — V3-G + final cleanup verification.
 * See wiki/17-audio.md §7 for the 119-vs-120-vs-121 analysis (clamp
 * at 120, but only 119 entries are monotone — last 2 octaves are
 * ROM garbage that the music engine never reaches).
 *   - clamp at $0A0C does `MOV A, #$78` (=120), so legal note range is
 *     0..120 inclusive. compute_pitch reads BOTH pitch[note] and
 *     pitch[note+1] (for delta interpolation), so the highest byte
 *     accessed is $0E48 + 121*2 + 1 = $0F3B.
 *   - actual monotone pitch values in ROM run from $0E48 ($0024) to
 *     $0F34/$0F35 ($7FFF) — that's 119 entries (entry 0..118). Bytes
 *     $0F36 onward break monotonicity ($00E8, $0CC4, ...), so the
 *     table is genuinely 119 entries, with the top 2-3 octaves being
 *     ROM garbage that the music engine apparently never reaches.
 *   - V3-G's "121 entries" claim was based on the clamp; the actual
 *     count from the data is 119. Comment updated to record both.
 * ======================================================================== */
static void compute_pitch_09FF(uint8_t a, uint8_t channel_idx)
{
    /* Note-byte to pitch-word conversion (SPC700 $09FF).
     *   transpose = wram[$2C + $00]   ; per-port transpose offset
     *   base      = wram[$01A0 + $03] ; per-voice base pitch offset
     *   note_idx  = clamp(a + transpose + base, 0x00, 0x78)
     *   lo  = pitch_lut[note_idx * 2]      (16-bit)
     *   hi  = pitch_lut[note_idx * 2 + 1]  (16-bit, next semitone)
     *   delta = hi - lo
     *   frac  = wram[$0300 + $03]    ; 8-bit fine-pitch fraction
     *
     *   ROM math when frac != 0 (two 8x8 mults + 16-bit add chain):
     *     YA = frac * delta_hi              ; 16-bit product (Y:A pair)
     *     YA += lo                          ; -> $0E:$0F
     *     YA = frac * delta_lo              ; second 8x8
     *     A = Y; Y = 0                       ; keep only high byte
     *     YA += $0E:$0F                     ; final
     */
    a = (uint8_t)(a + spc_ram[0x002C + spc_ram[0x00]]);
    a = (uint8_t)(a + spc_ram[0x01A0 + spc_ram[0x03]]);
    if (a >= 0x78) a = 0x78;
    uint8_t  x  = (uint8_t)(a * 2);
    uint16_t lo = (uint16_t)(spc_ram[0x0E48 + x] | (spc_ram[0x0E49 + x] << 8));
    uint16_t hi = (uint16_t)(spc_ram[0x0E4A + x] | (spc_ram[0x0E4B + x] << 8));
    uint16_t delta = (uint16_t)(hi - lo);

    uint16_t pitch;
    uint8_t  frac = spc_ram[0x0300 + spc_ram[0x03]];
    if (frac != 0) {
        uint8_t  delta_hi = (uint8_t)(delta >> 8);
        uint8_t  delta_lo = (uint8_t)(delta);
        uint16_t step_hi  = (uint16_t)((uint16_t)frac * delta_hi);   /* 16-bit */
        uint16_t step_lo  = (uint16_t)((uint16_t)frac * delta_lo);   /* 16-bit */
        pitch = (uint16_t)(lo + step_hi + (step_lo >> 8));
    } else {
        pitch = lo;
    }
    spc_ram[0x0E] = (uint8_t)pitch;
    spc_ram[0x0F] = (uint8_t)(pitch >> 8);
    (void)channel_idx;
}

/* ========================================================================
 * $0A46 — EVENT_NOTE: process a note byte (bit 7 set).
 *
 *   wram[$0200+X] = A                            ; raw note byte
 *   A = [$05]+Y                                  ; next byte = duration
 *   Y++
 *   wram[$50+X]   = A                            ; ticks-to-next-note
 *   A = wram[$03C0+X]                            ; if pitch-bend target set, skip
 *   if (A != 0) {
 *     wram[$0220+X] = 0                          ; clear pitch-hi
 *   } else {
 *     A = wram[$0200+X]                          ; current note byte
 *     compute_pitch_09FF(A, X)                   ; pitch in $0E:$0F
 *     wram[$0200+X] = $0E
 *     wram[$0220+X] = $0F
 *   }
 *   if (!(spc_ram[$04] & spc_ram[$02])) {
 *     spc_ram[$16] |= spc_ram[$02]               ; assert KON for this voice
 *   }
 *   return
 * ======================================================================== */
static void event_note_0A46(uint8_t a)
{
    uint8_t x = spc_ram[0x03];
    spc_ram[0x0200 + x] = a;

    /* ROM: $0A49  MOV A, [$05]+Y     ; Y was already advanced past the note byte
     *      $0A4B  INC Y               ; advance past the duration byte too
     *      $0A4C  MOV $50+X, A        ; ticks-to-next-note = duration
     * Caller (song_event_dispatch_099B) tracks Y for us; here we read from
     * the song stream at the CURRENT Y position (already pointing at the
     * duration byte). The "+1" used in the old lift was an off-by-one. */
    uint16_t song_ptr = (uint16_t)(spc_ram[0x05] | (spc_ram[0x06] << 8));
    /* The dispatcher in 099B incremented Y once after reading the note byte,
     * so the duration byte is at song_ptr + (caller_y).  We don't have the
     * caller's Y here, but song_event_dispatch_099B mutates Y as part of its
     * loop — the relevant byte is `*ptr` after the note byte already
     * consumed.  The 99B caller advances $0140+X by Y at the end, which
     * keeps the stream-pointer-in-bytes-consumed bookkeeping correct.   */
    uint8_t duration = spc_ram[song_ptr + spc_ram[0x07]];
    spc_ram[0x50 + x] = duration;
    spc_ram[0x07] = (uint8_t)(spc_ram[0x07] + 1);   /* INC Y (caller's $07 stash) */

    if (spc_ram[0x03C0 + x] != 0) {
        spc_ram[0x0220 + x] = 0;
    } else {
        compute_pitch_09FF(spc_ram[0x0200 + x], x);
        spc_ram[0x0200 + x] = spc_ram[0x0E];
        spc_ram[0x0220 + x] = spc_ram[0x0F];
    }

    if (!(spc_ram[0x04] & spc_ram[0x02])) {
        spc_ram[0x16] |= spc_ram[0x02];
    }
}

/* ========================================================================
 * EVENT HANDLERS (each ends with JMP $09B0 → re-enter song dispatch)
 *
 * $0A74 — event_set_instr: read instrument byte, store at $0180+X,
 *                          if voice is "live" call $0966 to upload the
 *                          BRR sample address into the per-voice DSP regs.
 *
 * $0A86 — event_pan: read pan byte → $02A0+X; read pan-rate → $0260+X;
 *                    zero pan-curr ($0280) + pan-tgt ($0240).
 *
 * $0A9D — event_keypress: read "transpose" byte → $0028+X.
 *
 * $0AA9 — event_pitch_env: read pitch-envelope target → $03C0+X.
 *
 * $0AB2 — event_vol_env: read vol-envelope target → $03E0+X.
 *
 * Higher indexes (5..14): TODO — read more bytes from the stream and
 * configure echo, vibrato, loop counters, repeat markers, etc.
 * ======================================================================== */
static void event_set_instr_0A74(void)
{
    uint16_t ptr = (uint16_t)(spc_ram[0x05] | (spc_ram[0x06] << 8));
    uint8_t a = spc_ram[ptr];
    uint8_t x = spc_ram[0x03];
    spc_ram[0x0180 + x] = a;
    if (!(spc_ram[0x04] & spc_ram[0x02])) {
        load_sample_voice_0966(spc_ram[0x01], a);
    }
}

static void event_pan_0A86(void)
{
    uint16_t ptr = (uint16_t)(spc_ram[0x05] | (spc_ram[0x06] << 8));
    uint8_t x = spc_ram[0x03];
    spc_ram[0x02A0 + x] = spc_ram[ptr];
    spc_ram[0x0260 + x] = spc_ram[ptr + 1];
    spc_ram[0x0280 + x] = 0;
    spc_ram[0x0240 + x] = 0;
}

static void event_keypress_0A9D(void)
{
    uint16_t ptr = (uint16_t)(spc_ram[0x05] | (spc_ram[0x06] << 8));
    /* X (= spc_ram[$03]) is the voice slot — earlier draft used $00
     * (the port byte) which would have aliased every voice to slot 0. */
    spc_ram[0x0028 + spc_ram[0x03]] = spc_ram[ptr];
}

static void event_pitch_env_0AA9(void)
{
    uint16_t ptr = (uint16_t)(spc_ram[0x05] | (spc_ram[0x06] << 8));
    spc_ram[0x03C0 + spc_ram[0x03]] = spc_ram[ptr];
}

static void event_vol_env_0AB2(void)
{
    uint16_t ptr = (uint16_t)(spc_ram[0x05] | (spc_ram[0x06] << 8));
    spc_ram[0x03E0 + spc_ram[0x03]] = spc_ram[ptr];
}

/* ========================================================================
 * $0966 — LOAD_SAMPLE_VOICE: program DSP regs to point at the right BRR.
 *
 *   $0C = $84    $0D = $3D                ; default sample-directory base
 *                                          ; at SPC $3D84
 *   PUSH Y / PUSH X
 *   X = $01 (channel index)
 *   wram[$48+X] = A          ; remember requested sample
 *   YA = A * 3               ; 3 bytes per directory entry? Or 4?
 *                              Actually: sample directory entries are
 *                              4 bytes (start_lo, start_hi, loop_lo, loop_hi)
 *                              but the driver indexes by 3 because each
 *                              sample slot is 12 bytes (4 for directory +
 *                              8 for envelope settings).
 *   $0C += YA                ; sample-info pointer
 *   A = X << 4 | 4           ; per-voice DSP reg = SRCN
 *   DSPADDR = A
 *   DSPDATA = wram[$48+X]    ; SRCN (sample slot number)
 *   DSPADDR++
 *   Y = 0
 *   for (Y = 0..2) {
 *     A = [$0C]+Y            ; envelope/gain byte from sample-info table
 *     DSPDATA = A; DSPADDR++ ; → ADSR1, ADSR2, GAIN
 *   }
 *   POP X / POP Y / RET
 * ======================================================================== */
static void load_sample_voice_0966(uint8_t y, uint8_t a)
{
    spc_ram[0x0C] = 0x84;
    spc_ram[0x0D] = 0x3D;

    uint8_t x = spc_ram[0x01];
    spc_ram[0x48 + x] = a;
    /* The "* 3" multiplier addresses a 3-byte secondary table (ADSR1,
     * ADSR2, GAIN) packed alongside the sample-directory pointers.       */
    uint16_t base = (uint16_t)(0x3D84 + (uint16_t)a * 3);
    spc_ram[0x0C] = (uint8_t)base;
    spc_ram[0x0D] = (uint8_t)(base >> 8);

    /* Program per-voice DSP: SRCN, ADSR1, ADSR2, GAIN.                   */
    uint8_t reg = (uint8_t)((x << 4) | DSP_V_SRCN);
    dsp_set(reg, spc_ram[0x48 + x]);
    for (uint8_t i = 0; i < 3; ++i) {
        dsp_set((uint8_t)(reg + 1 + i), spc_ram[base + i]);
    }
    (void)y;
}

/* ========================================================================
 * $0F3A — DRIVER_INIT: zero DSP, KON-test pattern, then clear ARAM scratch.
 *
 *   for (page = 0; page < 8; page++) {            ; 8 voices
 *     for (Y = 7; Y >= 0; --Y) DSP[page<<4 | Y] = 0;
 *   }
 *   DSP_KOF = 1, 2, 4, 8, 16, 32, 64, 128, 0    ; key-off ripple to verify
 *   DSP_MVOL_L = DSP_MVOL_R = 0                  ; silence
 *   DSP_EVOL_L = DSP_EVOL_R = 0                  ; no echo
 *   DSP_PMON   = DSP_EON = 0                     ; no modulation
 *   DSP_DIR    = $3D                             ; sample-dir at $3D00
 *
 *   X = 0; Y = $E8                                ; clear ARAM $0000-$00E7
 *   loop: (X)+ = 0; DBNZ Y, loop
 *
 *   $0C = $3D00 (16-bit)                          ; clear $3D00..$3DFF
 *   loop: [$0C]+Y = 0; INC Y; until Y = 0
 *
 *   A = $0D; INC A; $0D = A; INC A; BNE init_loop (one more 256-page sweep)
 *
 *   Y = $20
 *   wram[$013F+Y .. $047F+Y] = 0                  ; per-voice scratch
 *   DBNZ Y, ...
 *
 *   $14 = $20                                     ; default master vol delta
 *   DSP_FLG = $20                                 ; unmute
 *   CONTROL = $30                                 ; T0 + T1 + T2 stop
 *   APUIO0..3 = 0                                 ; ack to CPU
 *   T0DIV = $80                                   ; 4 ms tick (8 kHz / 128)
 *   CONTROL = $01                                 ; T0 enable only
 *   APUIO0..3 = $80                               ; "ready" signal
 *   DSP_KOF = 0
 *   DSP_MVOL_L = $60                              ; default vol
 *   DSP_MVOL_R = $60
 *   RET
 * ======================================================================== */
static void driver_init_0F3A(void)
{
    /* Clear all 8 voices' DSP slots. */
    for (uint8_t page = 0; page < 8; ++page) {
        for (uint8_t i = 0; i < 8; ++i) {
            dsp_set((uint8_t)((page << 4) | i), 0);
        }
    }

    /* KOF test pattern: ripple a bit through 1..128 then clear.          */
    static const uint8_t kof_pattern[] = { 0x01, 0x02, 0x04, 0x08,
                                           0x10, 0x20, 0x40, 0x80, 0x00 };
    for (size_t i = 0; i < sizeof kof_pattern; ++i) {
        dsp_set(DSP_KOF, kof_pattern[i]);
    }

    dsp_set(DSP_MVOL_L, 0);
    dsp_set(DSP_MVOL_R, 0);
    dsp_set(DSP_EVOL_L, 0);
    dsp_set(DSP_EVOL_R, 0);
    dsp_set(DSP_PMON,   0);
    dsp_set(DSP_EON,    0);
    dsp_set(DSP_DIR,    0x3D);      /* sample-dir at SPC $3D00          */

    /* Clear ARAM $0000-$00E7. */
    memset(&spc_ram[0x0000], 0, 0xE8);

    /* Clear $3D00-$3DFF (sample directory shadow / scratch). */
    memset(&spc_ram[0x3D00], 0, 0x100);

    /* Per-voice scratch arrays at $0140+, $0160+, …, $047F+. */
    {
        static const uint16_t bases[] = {
            0x013F, 0x011F, 0x015F, 0x004F, 0x006F, 0x008F,
            0x00AF, 0x00CF, 0x03BF, 0x017F, 0x021F, 0x01FF,
            0x019F, 0x02FF, 0x025F, 0x023F, 0x029F, 0x027F,
            0x02DF, 0x02BF, 0x033F, 0x031F, 0x035F, 0x047F,
        };
        for (uint8_t y = 0x20; y > 0; --y) {
            for (size_t i = 0; i < sizeof bases / sizeof bases[0]; ++i) {
                spc_ram[bases[i] + y] = 0;
            }
        }
    }

    spc_ram[0x14]       = 0x20;
    dsp_set(DSP_FLG, 0x20);                       /* unmute            */
    spc_ram[SP_CONTROL] = 0x30;
    memset(&spc_ram[SP_CPUIO0], 0, 4);
    spc_ram[SP_T0DIV]   = 0x80;
    spc_ram[SP_CONTROL] = 0x01;
    /* Send "boot ready" $80 to all CPUIOs. */
    for (uint8_t i = 0; i < 4; ++i) spc_ram[SP_CPUIO0 + i] = 0x80;
    dsp_set(DSP_KOF, 0x00);
    dsp_set(DSP_MVOL_L, 0x60);
    dsp_set(DSP_MVOL_R, 0x60);
}

/* ========================================================================
 * Mapping from 65816-side command bytes (from audio_intro.c) into the
 * SPC700 dispatcher.
 *
 *  Command byte → SPC handler chain
 *  --------------------------------
 *   $00          → dispatch_apuio_command_06CC(0x00)
 *                  AND #$FE → 0 → release_voice_08A1(port)
 *                  Effect: stop all voices on that port → silence
 *
 *   $02          → dispatch_apuio_command_06CC(0x02)
 *                  AND #$FE → 2 → start_song_at_index_06DA(0x02)
 *                  Look up song at table $12FE+2 (= 1st entry past header)
 *                  → assign 8 voices and start sequence interpreter
 *                  → Encyclopedia / Credits BGM
 *
 *   $04          → start_song_at_index_06DA(0x04)   B-nest overview BGM
 *   $06          → start_song_at_index_06DA(0x06)   R-nest overview BGM
 *   $08          → start_song_at_index_06DA(0x08)   Title BGM
 *   $0C          → start_song_at_index_06DA(0x0C)   B-nest interior BGM
 *   $0E          → start_song_at_index_06DA(0x0E)   R-nest interior BGM
 *   $16          → start_song_at_index_06DA(0x16)   Surface overview BGM
 *   $30          → start_song_at_index_06DA(0x30)   Pause overlay
 *
 *   $2B, $2C, $2E, $44, $4E, $4F        SFX (on APUIO3 → port 3)
 *                  → start_song_at_index_06DA(cmd)
 *                  → Each plays a one-shot SFX assigned to port-3's slot
 *
 *   $C4          → bit 7 set, < $C0... wait, $C4 has bit 7+bit 6 set
 *                  AND #$3F → $04 → set_master_volume_074C($04)
 *                  Effect: master volume jumps to $04 (near-mute)
 *                  Used as the "view-switch beep" + fade-out cue
 *
 *   $C8          → AND #$3F → $08 → set_master_volume_074C($08)
 *                  Same path; used as "dig new nest" confirmation.
 *
 *  Note: command bytes ≥ $80 with $00 in the low nibble go to the
 *  dispatch_special_cmd_0729 branch (master vol / fade / IPL re-upload).
 * ======================================================================== */

/* ========================================================================
 * Convenience: a tabular description (mirrors audio_intro.c's table on the
 * 65816 side, for cross-referencing).
 * ======================================================================== */
typedef struct {
    uint8_t      cpu_cmd;                /* byte the 65816 writes        */
    const char  *spc_handler;            /* name of the SPC routine      */
    const char  *role;                   /* one-line description         */
} SpcCommandRoute;

static const SpcCommandRoute spc_command_routes[] = {
    { 0x00, "release_voice_08A1",         "Stop all voices on the port"  },
    { 0x02, "start_song_at_index_06DA",   "Encyclopedia / Credits BGM"   },
    { 0x04, "start_song_at_index_06DA",   "B-nest overview BGM"          },
    { 0x06, "start_song_at_index_06DA",   "R-nest overview BGM"          },
    { 0x08, "start_song_at_index_06DA",   "Title screen BGM"             },
    { 0x0C, "start_song_at_index_06DA",   "B-nest close-up BGM"          },
    { 0x0E, "start_song_at_index_06DA",   "R-nest close-up BGM"          },
    { 0x16, "start_song_at_index_06DA",   "Surface overview BGM"         },
    { 0x30, "start_song_at_index_06DA",   "Pause overlay music"          },

    { 0x2B, "start_song_at_index_06DA",   "Queen 'lay egg' SFX"          },
    { 0x2C, "start_song_at_index_06DA",   "Control-panel click / dig"    },
    { 0x2E, "start_song_at_index_06DA",   "Menu-open SFX"                },
    { 0x44, "start_song_at_index_06DA",   "Yellow ant pickup"            },
    { 0x4E, "start_song_at_index_06DA",   "Ouch / munch SFX"             },
    { 0x4F, "start_song_at_index_06DA",   "Trophallaxis SFX"             },

    { 0xC4, "set_master_volume_074C",     "Master vol = $04 (cue beep)"  },
    { 0xC8, "set_master_volume_074C",     "Master vol = $08 (dig commit)"},

    /* Some sentinel/control bytes used internally during runtime: */
    { 0xFC, "ipl_runtime_handshake_0795", "65816 requests asset re-upload" },
};

/* ========================================================================
 * Compilation entry-point — keeps the linker happy and references the
 * lifted symbols so the .o file isn't trivially empty.
 *
 * In a hypothetical "run the SPC in a host emulator" build, you'd replace
 * this with a function that just steps the SPC700 forever.
 * ======================================================================== */
const unsigned spc_command_routes_count =
    sizeof(spc_command_routes) / sizeof(spc_command_routes[0]);

void spc700_driver_boot(void)
{
    driver_entry_0600();
}

/* ========================================================================
 * Silence "unused" warnings for the various pieces left as TODOs.
 * ======================================================================== */
__attribute__((used))
static void __unused_anchor(void)
{
    (void)dsp_read;
    (void)apuio_cpu_to_spc;
    (void)apuio_spc_to_cpu;
    (void)spc_timers;
    (void)spc_control;
    (void)spc_command_routes_count;
    (void)spc_command_routes;
    (void)load_sample_voice_0966;
    (void)compute_pitch_09FF;
    (void)bootstrap_voice_07E5;
    (void)song_command_byte_dispatch_06CC;
    (void)driver_kick_06B6_06C4;
}

/* Placeholders for the function bodies declared but not lifted in detail. */
static void song_command_byte_dispatch_06CC(uint8_t a) { (void)a; }
static void driver_kick_06B6_06C4(void) {}

/* ============================================================================
 * Voice envelope tick + DSP commit helpers
 * ============================================================================
 *
 * envelope_tick_0D41 advances two parallel per-voice phase counters (pitch
 * envelope at $B0+X / $D0+X, pan-volume envelope at $70+X / $90+X), wraps
 * each at a per-phase threshold, samples the envelope curve, and commits
 * the result to the DSP via the two helpers below.
 *
 * Disassembly: $0D41-$0DAF (envelope_tick), $0DB4-$0DDB (load_sample_or_keep),
 * $0DDC-$0E02 (apply_pan_to_dsp), $0E07-$0E43 (pitch_to_dsp_or_noise).
 *
 * NOTE: this is the SPC700 audio coprocessor code, not 65816. It executes
 * inside the SPC RAM model (spc_ram[]); a Flipper port would replace this
 * entire subsystem with native PWM beeps per the few SFX bytes we track.
 * Bodies below are faithful to the SPC700 disasm but are documentation-
 * grade — they don't make the SNES emulator emulate correctly, they just
 * preserve the algorithmic intent so the structure can be re-targeted.
 * ========================================================================== */

static uint8_t commit_song_y_0D34(uint8_t y);    /* fwd */

/* $0D41: per-voice envelope tick. X = voice slot * 8 + 7 (see caller at
 * poll_all_cpuios). Two phase counters: $B0+X (pitch), $70+X (pan/vol).
 * Each indexes envelope curve tables at $1055/$103D in SPC RAM. */
static void envelope_tick_0D41(void)
{
    uint8_t x = spc_ram[0x03];               /* X register holds voice slot */
    /* Voice-inactive gate at SPC $0D43 (was missing — A1 regression fix):
     * `MOV A, $0140+X; BEQ ret` — if the per-voice instrument slot is 0,
     * the voice is dead and we must NOT INC the envelope counters or
     * deref the envelope tables. */
    if (spc_ram[0x0140 + x] == 0) return;
    /* Secondary gate: skip if "done this frame" flag already set. */
    if ((spc_ram[0x04] & 0x02) != 0) return;
    load_sample_or_keep_0DB4();
    if ((spc_ram[0x0220 + x] & 0x80) == 0) return;  /* BPL = positive = inactive */

    /* Pitch-envelope counter: INC $B0+X; wrap via $12ED/$12FC table[Y] */
    spc_ram[0xB0 + x]++;
    uint8_t y = spc_ram[0xD0 + x];
    if (spc_ram[0xB0 + x] >= spc_ram[0x12ED + y])
        spc_ram[0xB0 + x] = spc_ram[0x12FC + y];

    /* Read envelope curve sample at [$1055+y*2] + $B0+X.  Sign-extend. */
    uint16_t ptr = spc_ram[0x1055 + (y * 2)] |
                   ((uint16_t)spc_ram[0x1056 + (y * 2)] << 8);
    uint8_t  sample = spc_ram[(ptr + spc_ram[0xB0 + x]) & 0xFFFF];
    spc_ram[0x07] = sample;
    spc_ram[0x08] = (sample & 0x80) ? 0xFF : 0x00;

    pitch_to_dsp_or_noise_0E07();

    /* Pan/vol-envelope counter: same pattern with $70+X / $90+X /
     * $12E1+Y / $12F0+Y / $103D+Y. */
    spc_ram[0x70 + x]++;
    y = spc_ram[0x90 + x];
    if (spc_ram[0x70 + x] >= spc_ram[0x12E1 + y])
        spc_ram[0x70 + x] = spc_ram[0x12F0 + y];

    ptr = spc_ram[0x103D + (y * 2)] |
          ((uint16_t)spc_ram[0x103E + (y * 2)] << 8);
    sample = spc_ram[(ptr + spc_ram[0x70 + x]) & 0xFFFF];
    spc_ram[0x09] = sample;

    apply_pan_to_dsp_0DE0();
    spc_ram[0x04] |= 0x02;                  /* OR $04, $02 — "done" flag */
}

/* $0DB4: load_sample_or_keep — when the instrument changed since last
 * tick (sample-index at $0048+Y != current), call the heavy sample
 * loader $0966; either way, propagate the per-voice NON/EON bits to
 * the DSP shadow at $15/$18. */
static void load_sample_or_keep_0DB4(void)
{
    /* TCALL 0 (re-entry) — collapsed into the body. */
    uint8_t x = spc_ram[0x03];
    uint8_t y = spc_ram[0x01];
    if (spc_ram[0x0180 + x] != spc_ram[0x0048 + y])
        load_sample_voice_0966(y, spc_ram[0x0180 + x]);

    /* Noise-enable bit per voice */
    if (spc_ram[0x03C0 + x] != 0)
        spc_ram[0x15] |= 0x02;
    else
        spc_ram[0x15] &= (uint8_t)~0x02;

    /* Echo-enable bit per voice */
    if (spc_ram[0x03E0 + x] != 0)
        spc_ram[0x18] |= 0x02;
    else
        spc_ram[0x18] &= (uint8_t)~0x02;
}

/* $0DE0: apply pan envelope sample to per-voice DSP VOLL/VOLR registers.
 * MUL chain: vol_l = ($28+X * $09 * $02A0+X) >> 8;
 *            vol_r = ($28+X * $09 * $0260+X) >> 8; */
static void apply_pan_to_dsp_0DE0(void)
{
    uint8_t x = spc_ram[0x00];
    uint8_t y = spc_ram[0x09];               /* envelope sample */
    uint8_t base = spc_ram[0x28 + x];        /* per-voice gain */

    /* Address VOLL register for this voice. $F2 is DSPADDR. */
    spc_ram[0xF2] = (uint8_t)((spc_ram[0x01] << 4) & 0x70);

    /* Left channel: gain × env × pan_L, take high byte. */
    uint16_t prod = (uint16_t)base * y;
    uint8_t  ya_y = (uint8_t)(prod >> 8);

    uint8_t x2 = spc_ram[0x03];
    uint16_t prod2 = (uint16_t)ya_y * spc_ram[0x02A0 + x2];
    spc_ram[0xF3] = (uint8_t)((prod2 >> 8) >> 1);   /* LSR after MUL — half-volume scale */

    spc_ram[0xF2]++;                          /* next reg = VOLR */
    uint16_t prod3 = (uint16_t)ya_y * spc_ram[0x0260 + x2];
    spc_ram[0xF3] = (uint8_t)((prod3 >> 8) >> 1);
}

/* $0E07: commit pitch envelope to DSP P_LO/P_HI, OR'ing the per-voice
 * base pitch from $0200+X with the running envelope at $07/$08, with
 * saturation at +$7FFF if the add overflows. Also handles the noise-
 * voice path (when $F0 bit 0 set, just OR base pitch into shadow $14 and
 * write KOF via NCKON reg $6C). */
static void pitch_to_dsp_or_noise_0E07(void)
{
    uint8_t x = spc_ram[0x03];

    /* BBS $F0.0 — noise voice fast path. */
    if (spc_ram[0xF0] & 0x01) {
        spc_ram[0x14] &= 0xE0;
        spc_ram[0x14] |= (spc_ram[0x0200 + x] & 0x1F);
        spc_ram[0xF2] = 0x6C;                /* NCKON */
        spc_ram[0xF3] = spc_ram[0x14];
        return;
    }

    /* Sample-voice path: add 16-bit base + envelope, saturate at +$7FFF. */
    uint16_t base = spc_ram[0x0200 + x] | ((uint16_t)spc_ram[0x0220 + x] << 8);
    uint16_t env  = spc_ram[0x07]        | ((uint16_t)spc_ram[0x08] << 8);
    uint32_t sum  = base + env;
    if (sum > 0x7FFF) sum = 0x7FFF;
    spc_ram[0x07] = (uint8_t)(sum & 0xFF);
    spc_ram[0x08] = (uint8_t)((sum >> 8) & 0xFF);
    /* Halve once (right shift), to fit into the 14-bit DSP pitch field. */
    uint16_t pitch14 = ((uint16_t)spc_ram[0x08] << 8 | spc_ram[0x07]) >> 1;
    spc_ram[0x07] = (uint8_t)(pitch14 & 0xFF);
    spc_ram[0x08] = (uint8_t)(pitch14 >> 8);

    /* DSP address: P_LO = (voice << 4) | $02. */
    spc_ram[0xF2] = (uint8_t)(((spc_ram[0x01] << 4) & 0x70) | 0x02);
    spc_ram[0xF3] = spc_ram[0x07];
    spc_ram[0xF2]++;
    spc_ram[0xF3] = spc_ram[0x08];
}

static uint8_t commit_song_y_0D34(uint8_t y) { return y; }

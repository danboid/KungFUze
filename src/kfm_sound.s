/*
 * update_sound for the game with sound (make SOUND=1): every scanline, the sound effects'
 * sample from the KungFuMasterAudio mixer's 256-byte queue (filled in the foreground,
 * src/kfm_mixrt.c) plus the floor's music, generated here: a square-wave melody and noise
 * drums (the arcade's AY channels B and A), their pitch and levels set once a frame by the
 * music player (src/kfm_mixrt.c, mus_frame). Replaces the stock inline mixer's update_sound,
 * which is renamed out of the way in its object (see the Makefile).
 *
 * Contract: the stock inline mixer's, measured on its code as built for this game, so Mode 748
 * and the kernel's sync code need no change:
 *   ZL bit 0 (pre/post-equalising): the SYNC rising edge's sbi completes 62 cycles after entry
 *   ZL bit 1 (normal HSYNC):        the sbi completes 129 cycles after entry
 *   total, including ret: 189 cycles with either bit set (188 with neither)
 * Every branch below is balanced. Destroys Z and r0 (as the stock one; r1 is left alone).
 * An empty queue repeats the last sample played.
 */
#include <avr/io.h>
#include <defines.h>
#ifdef KFM_DEBUG
#define KFM_CRASH_LOG (KFM_QUEUE - 0x20)   /* src/main.c: KFM_SRAM_TOP (with sound) */
#else
#define KFM_CRASH_LOG kfm_crash            /* src/main.c: an ordinary variable (sts/lds: the same cycles) */
#endif

.macro WAIT cnt
.rept \cnt
	nop
.endr
.endm

.global update_sound
.section .text.kfm_update_sound

update_sound:
	push  r18                          ; 2   (2)
	push  r19                          ; 2   (4)
	mov   r18,     ZL                  ; 1   (5)
	; ---- the effects' sample from the queue ----
	lds   ZL,      MixerReadIndex      ; 2   (7)
	lds   r0,      MixerWriteIndex     ; 2   (9)
	cp    ZL,      r0                  ; 1   (10)
	breq  1f                           ; 1 / 2
	ldi   ZH,      hi8(MixerSamples)   ; 1
	ld    r0,      Z                   ; 2
	inc   ZL                           ; 1
	rjmp  2f                           ; 2   (17) a sample is queued
1:	dec   ZL                           ; 1        empty: repeat the last one
	ldi   ZH,      hi8(MixerSamples)   ; 1
	ld    r0,      Z                   ; 2
	inc   ZL                           ; 1   (17)
2:	sts   MixerReadIndex, ZL           ; 2   (19)
	; ---- plus the music (prepared on the previous line), saturated ----
	lds   ZL,      mus_next            ; 2   (21) signed
	mov   ZH,      r0                  ; 1
	subi  ZH,      0x80                ; 1   (23) the sample, signed
	add   ZH,      ZL                  ; 1   (24)
	brvs  3f                           ; 1 / 2
	nop                                ; 1
	nop                                ; 1
	rjmp  4f                           ; 2   (29)
3:	ldi   ZH,      0x7F                ; 1        overflow: to the rail of the music's sign
	sbrc  ZL,      7                   ; 1 / 2
	ldi   ZH,      0x80                ; 1   (29)
4:	subi  ZH,      0x80                ; 1   (30) back to unsigned
	sts   _SFR_MEM_ADDR(OCR2A), ZH     ; 2   (32) out
	WAIT  27                           ;     (59)
	sbrc  r18,     0                   ; --- SYNC, 68-cycle pulse (equalising) ---
	sbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN   ; completes at 62 (bit 0 set; 61 if not)
	; ---- the next line's music value (65 cycles to the next sync) ----
	lds   r0,      mus_phase           ; 2   melody: phase += step
	lds   ZL,      mus_step            ; 2
	add   r0,      ZL                  ; 1
	sts   mus_phase, r0                ; 2
	lds   r0,      mus_phase + 1       ; 2
	lds   ZL,      mus_step + 1        ; 2
	adc   r0,      ZL                  ; 1
	sts   mus_phase + 1, r0            ; 2   (14)
	lds   ZL,      mus_tvol            ; 2   square: +level / -level
	sbrs  r0,      7                   ; 1 / 2
	neg   ZL                           ; 1   (18)
	lds   r0,      mus_phase2          ; 2   channel A's tone (the jingles): phase += step
	lds   ZH,      mus_step2           ; 2
	add   r0,      ZH                  ; 1
	sts   mus_phase2, r0               ; 2
	lds   r0,      mus_phase2 + 1      ; 2
	lds   ZH,      mus_step2 + 1       ; 2
	adc   r0,      ZH                  ; 1
	sts   mus_phase2 + 1, r0           ; 2
	lds   r19,     mus_tvol2           ; 2
	sbrs  r0,      7                   ; 1 / 2
	neg   r19                          ; 1
	add   ZL,      r19                 ; 1   (37)
	lds   r0,      mus_lfsr            ; 2   drums: a 16-bit LFSR (x^16+x^5+x^3+x^2+1)
	lds   ZH,      mus_lfsr + 1        ; 2
	ldi   r19,     0x2D                ; 1
	lsl   r0                           ; 1
	rol   ZH                           ; 1
	brcc  5f                           ; 1 / 2
	eor   r0,      r19                 ; 1
5:	sts   mus_lfsr, r0                 ; 2
	sts   mus_lfsr + 1, ZH             ; 2   (31)
	lds   r19,     mus_dvol            ; 2   noise: +level / -level
	sbrs  r0,      0                   ; 1 / 2
	neg   r19                          ; 1
	add   ZL,      r19                 ; 1   (three levels <= 30: no overflow)
	sts   mus_next, ZL                 ; 2   (57)
	WAIT  8                            ;     (65)
	sbrc  r18,     1                   ; --- SYNC, 136-cycle pulse (HSYNC) ---
	sbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN   ; completes at 129 (bit 1 set)
	; ---- the crash log's trail (the game's crash log at KFM_CRASH_LOG, see src/main.c) ----
	sts   KFM_CRASH_LOG + 12, r18      ; 2   this call's flags (ZL)
	lds   r19,     sync_pulse          ; 2
	sts   KFM_CRASH_LOG + 13, r19      ; 2   the kernel's sync position
	lds   r19,     KFM_CRASH_LOG + 14  ; 2
	lds   r18,     KFM_CRASH_LOG + 15  ; 2
	subi  r19,     0xFF                ; 1   lines since the main loop's last frame: + 1
	sbci  r18,     0xFF                ; 1
	sts   KFM_CRASH_LOG + 14, r19      ; 2
	sts   KFM_CRASH_LOG + 15, r18      ; 2   (16)
	pop   r19                          ; 2
	pop   r18                          ; 2
	WAIT  36
	ret                                ; 4   (189; 188 with neither bit)

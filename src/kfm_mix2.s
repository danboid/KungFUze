/*
 * uint8_t kfm_mix2(uint8_t n): the 2-voice 2-bit ADPCM mixer's per-sample work in assembly
 * (src/kfm_mixvar.c has the C around it). The same values as MixerSample() in C:
 *   sample = clamp(128 + voice0 + voice1, 0, 255), a voice being (acc - 128) * gain >> 7 after
 *   its 2-bit step through the 90-state table, and 0 once its sample count has run out.
 * In batches: the batch is as long as both voices can go without a buffer refill and the queue
 * has room (at most n); voice 0 runs it with its state in registers, writing 128 + its sample
 * (always 0..255), then voice 1 runs it adding its sample with the clamp. State is loaded and
 * stored once a batch. Returns the batch's length: 0 when a refill is due (the C does it).
 * CodecVoice offsets are checked in src/kfm_mixvar.c.
 */
#include <avr/io.h>

#define V_SLO   8
#define V_SHI   10
#define V_PACK  12
#define V_BITS  14
#define V_ACC   15
#define V_STATE 16
#define V_GAIN  17
#define V_INDEX 18
#define V_COUNT 19
#define V_ACT   20
#define V_BUF   21
#define V_SIZE  57

.section .text.kfm_mix2
.global kfm_mix2

/* r16 = min(r16, the samples the voice at Y can make before a refill: bits/2 + 4 * (count - index)) */
.macro LIMIT
	ldd   r18,     Y + V_ACT
	tst   r18
	breq  1f
	ldd   r18,     Y + V_COUNT
	ldd   r19,     Y + V_INDEX
	sub   r18,     r19
	lsl   r18
	lsl   r18
	ldd   r19,     Y + V_BITS
	lsr   r19
	add   r18,     r19
	cp    r18,     r16
	brsh  1f
	mov   r16,     r18
1:
.endm

/* one voice (Y) through the batch (r16 samples), queue at X: \add 0 writes 128 + sample, 1 adds
** its sample (clamped). Registers: r18 packed, r19 bits, r20 acc, r21 state, r22 gain, r24:r25
** samplesLo, r12:r13 the buffer pointer, r17 = 8, r15 the count, r23/r0/r1 scratch. */
.macro PASS add
	mov   r15,     r16
	ldd   r18,     Y + V_ACT
	tst   r18
	brne  2f
	rjmp  8f                         ; inactive: 0s (pass 0 writes 128s)
2:	ldd   r18,     Y + V_PACK
	ldd   r19,     Y + V_BITS
	ldd   r20,     Y + V_ACC
	ldd   r21,     Y + V_STATE
	ldd   r22,     Y + V_GAIN
	ldd   r24,     Y + V_SLO
	ldd   r25,     Y + V_SLO + 1
	ldd   r23,     Y + V_INDEX        ; the buffer pointer: Y + V_BUF + index
	movw  r12,     YL
	subi  r23,     -(V_BUF)
	add   r12,     r23
	adc   r13,     r1
3:	tst   r19                         ; (every 4 samples) a new byte
	brne  4f
	movw  ZL,      r12
	ld    r18,     Z+
	movw  r12,     ZL
	ldi   r19,     8
4:	mov   r23,     r18                ; code = packed & 3; packed >>= 2; bits -= 2
	andi  r23,     3
	lsr   r18
	lsr   r18
	subi  r19,     2
	mul   r21,     r17                ; Z = table + state * 8 + code * 2
	movw  ZL,      r0
	clr   r1
	subi  ZL,      lo8(-(kfm_adpcm2_tab))
	sbci  ZH,      hi8(-(kfm_adpcm2_tab))
	lsl   r23
	add   ZL,      r23
	adc   ZH,      r1
	lpm   r23,     Z+
	add   r20,     r23                ; acc += step
	lpm   r21,     Z                  ; the next state
	mov   r23,     r20                ; (acc ^ 128) * gain, << 1: the sample in r1
	subi  r23,     0x80
	mulsu r23,     r22
	lsl   r0
	rol   r1
.if \add
	ld    r23,     X                  ; queue + sample, clamped to 0..255
	mov   r0,      r1
	clr   r1
	add   r23,     r0
	sbrc  r0,      7
	rjmp  5f
	brcc  6f                          ; (+: carry = above 255)
	ldi   r23,     255
	rjmp  6f
5:	brcs  6f                          ; (-: no carry = below 0)
	clr   r23
6:
.else
	mov   r23,     r1                 ; 128 + sample (always 0..255)
	clr   r1
	subi  r23,     0x80
.endif
	st    X,       r23
	inc   XL
	sbiw  r24,     1                  ; the sample count
	brcs  7f                          ; (it was 0: the high word)
	brne  9f
	ldd   r23,     Y + V_SHI          ; 0 now: and the high word 0 too, the voice ends
	ldd   r0,      Y + V_SHI + 1
	or    r23,     r0
	brne  9f
	std   Y + V_ACT, r1
	dec   r15
	rjmp  10f                         ; the rest of the batch: as inactive
7:	ldd   r23,     Y + V_SHI          ; samplesLo was 0: 0xFFFF now, samplesHi - 1
	subi  r23,     1
	std   Y + V_SHI, r23
	ldd   r23,     Y + V_SHI + 1
	sbci  r23,     0
	std   Y + V_SHI + 1, r23
9:	dec   r15
	breq  10f
	rjmp  3b
10:	std   Y + V_PACK, r18             ; the state back
	std   Y + V_BITS, r19
	std   Y + V_ACC, r20
	std   Y + V_STATE, r21
	std   Y + V_SLO, r24
	std   Y + V_SLO + 1, r25
	movw  r24,     r12                ; index = pointer - (Y + V_BUF)
	sub   r24,     YL
	subi  r24,     V_BUF
	std   Y + V_INDEX, r24
8:
.if \add == 0
	tst   r15                         ; (pass 0: the samples left as inactive are 128)
	breq  11f
	ldi   r23,     128
12:	st    X,       r23
	inc   XL
	dec   r15
	brne  12b
11:
.endif
.endm

kfm_mix2:
	push  r12
	push  r13
	push  r15
	push  r16
	push  r17
	push  r28
	push  r29
	mov   r16,     r24                ; the batch: at most n,
	lds   r24,     MixerWriteIndex    ; the queue's room (240 - queued),
	lds   r25,     MixerReadIndex
	sub   r24,     r25
	ldi   r25,     240
	sub   r25,     r24
	brcc  1f
	clr   r25
1:	cp    r25,     r16
	brsh  2f
	mov   r16,     r25
2:	ldi   YL,      lo8(kfm_voices)    ; and what both voices can make without a refill
	ldi   YH,      hi8(kfm_voices)
	LIMIT
	adiw  YL,      V_SIZE
	LIMIT
	tst   r16
	brne  3f
	rjmp  done
3:	ldi   r17,     8
	lds   XL,      MixerWriteIndex
	ldi   XH,      hi8(MixerSamples)
	sbiw  YL,      V_SIZE
	PASS  0
	lds   XL,      MixerWriteIndex
	adiw  YL,      V_SIZE
	PASS  1
	lds   r24,     MixerWriteIndex    ; the batch is in the queue (whatever voice 1 did)
	add   r24,     r16
	sts   MixerWriteIndex, r24
done:
	mov   r24,     r16
	pop   r29
	pop   r28
	pop   r17
	pop   r16
	pop   r15
	pop   r13
	pop   r12
	ret
.size kfm_mix2, . - kfm_mix2

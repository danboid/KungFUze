/*
 * Kung-Fu Master prototype: specialised sprite blitter for Mode 748, inner loops.
 *
 * A strip is a 16 pixel wide sprite column, stored in SPI RAM as one header byte
 * per row ((start << 4) | len, in bytes) followed by the rows' trimmed 4bpp bytes,
 * colours already final, nybble 0 transparent. kfb_rows() consumes the data part
 * of one strip from an SPI RAM read stream the caller has started
 * (SpiRamSeqReadStart: the first byte is already being clocked in) and ORs it into
 * RAM tiles the caller has allocated.
 *
 * Per data byte: 18 cycles, overlapping the 16 cycle SPI transfer
 * of the next byte. Cells the strip may not draw to (clipped, off screen,
 * foreground tiles, RAM tiles exhausted) point at a scratch tile, so the loops
 * never test for clipping.
 */

#include <avr/io.h>

#define SR_DR _SFR_IO_ADDR(SPDR)

/* Extra cycles before every SPI RAM read, on top of the kernel's 17-cycle minimum
** (out to in). The minimum works in cuzebox-ex; real boards may need slack.
** Build with EXTRA=-DKFB_SPI_PAD=n to change it (each cycle costs about 6% of the
** pose streaming time). */
#ifndef KFB_SPI_PAD
#define KFB_SPI_PAD 4
#endif
.macro spi_pad
	.rept KFB_SPI_PAD
	nop
	.endr
.endm

.section .text.kfb_mask
.balign 256
/* nybble transparency: mask[b] keeps the destination where b's nybble is 0 */
.global kfb_mask
kfb_mask:
	.irp hi, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.irp lo, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.byte ((-(\hi == 0)) * 0xF0) | ((-(\lo == 0)) * 0x0F)
	.endr
	.endr

/*
 * void kfb_rows(void);
 *
 * Streams one pose from an SPI RAM read the caller has started: per row a start
 * byte and a length byte (in bytes), then the row's trimmed 4bpp bytes.
 *
 * Context (global kfb_ctx):
 *   +0     rows:    number of rows (>= 1)
 *   +1     line4:   (first row's line within its tile row) * 4
 *   +2     xbase:   destination byte column of data byte 0 relative to the grid's
 *                   first tile column * 4 (flipped: of the rightmost byte); mod 256
 *   +3     flip:    0 or 1
 *   +4,+5  ptr:     the pose's grid of RAM tile addresses (u16), first band
 *   +6     stride2: the grid's row length in bytes
 *   +7     dmax:    last valid destination byte column (grid width * 4 - 1)
 *
 * A row whose header would write outside the grid (bad data read from SPI RAM)
 * aborts the pose and counts in kfb_bad.
 *
 * SPI RAM: every read (in SPDR) at least 17 cycles after the previous out
 * (16 is not enough: the read returns the previous byte).
 */
.section .text.kfb_rows
.global kfb_rows
kfb_rows:
	push  r16
	push  r17
	push  YL
	push  YH
	lds   r24,     kfb_ctx + 0
	lds   r23,     kfb_ctx + 1
	lds   r25,     kfb_ctx + 2
	lds   YL,      kfb_ctx + 4
	lds   YH,      kfb_ctx + 5
	lds   r16,     kfb_ctx + 6
	lds   r0,      kfb_ctx + 3
	sbrc  r0,      0
	rjmp  frow

/* ---- not flipped ------------------------------------------------------------- */
row:
	spi_pad
	in    r18,     SR_DR   ; ( 1) start
	out   SR_DR,   r1      ; ( 2)
	add   r18,     r25     ; ( 3) destination byte column
	mov   r17,     r18     ;      kept for the bounds check
	mov   r22,     r18     ; ( 4)
	andi  r22,     3       ; ( 5) offset in the tile row
	lsr   r18              ; ( 6)
	andi  r18,     0xFE    ; ( 7) k * 2
	movw  ZL,      YL      ; ( 8)
	add   ZL,      r18     ; ( 9)
	adc   ZH,      r1      ; (10)
	ld    XL,      Z+      ; (12)
	ld    XH,      Z       ; (14)
	add   XL,      r23     ; (15)
	adc   XH,      r1      ; (16)
	add   XL,      r22     ; (17)
	adc   XH,      r1      ; (18)
	spi_pad
	in    r19,     SR_DR   ; (19) len
	out   SR_DR,   r1      ; (20)
	neg   r22
	subi  r22,     0xFC    ; count to the tile boundary: 4 - offset
	ldi   ZH,      hi8(kfb_mask)
	tst   r19
	brne  first
	rjmp  .                ; empty row: 16+ cycles to the next read
	rjmp  .
	rjmp  nrow
first:
	lds   r20,     kfb_ctx + 7
	cp    r20,     r17
	brcs  nbad             ; first byte beyond the grid
	mov   r0,      r17
	add   r0,      r19
	dec   r0
	cp    r20,     r0
	brcs  nbad             ; last byte beyond the grid
	rjmp  .
byte:
	spi_pad
	in    r0,      SR_DR   ; ( 1)
	out   SR_DR,   r1      ; ( 2)
	mov   ZL,      r0      ; ( 3)
	lpm   r20,     Z       ; ( 6)
	ld    r21,     X       ; ( 8)
	and   r21,     r20     ; ( 9)
	or    r21,     r0      ; (10)
	st    X+,      r21     ; (12)
	nop                    ; (13) the next read must come 17+ cycles after the out
	dec   r19              ; (14)
	breq  nrow             ; (15)
	dec   r22              ; (16)
	brne  byte             ; (18)
	subi  r18,     0xFE    ; next tile column
	movw  ZL,      YL
	add   ZL,      r18
	adc   ZH,      r1
	ld    XL,      Z+
	ld    XH,      Z
	add   XL,      r23
	adc   XH,      r1
	ldi   r22,     4
	ldi   ZH,      hi8(kfb_mask)
	rjmp  byte
nrow:
	subi  r23,     0xFC    ; line4 += 4
	cpi   r23,     32
	brne  .+6
	clr   r23
	add   YL,      r16     ; next band
	adc   YH,      r1
	dec   r24
	breq  .+2
	rjmp  row
	rjmp  done
nbad:
	rjmp  bad

/* ---- flipped: right to left, nybbles swapped ------------------------------- */
frow:
	spi_pad
	in    r18,     SR_DR   ; ( 1) start
	out   SR_DR,   r1      ; ( 2)
	mov   r0,      r25     ; ( 3)
	sub   r0,      r18     ; ( 4) destination byte column of the first byte
	mov   r18,     r0      ; ( 5)
	mov   r17,     r0
	mov   r22,     r18     ; ( 6)
	andi  r22,     3       ; ( 7)
	lsr   r18              ; ( 8)
	andi  r18,     0xFE    ; ( 9)
	movw  ZL,      YL      ; (10)
	add   ZL,      r18     ; (11)
	adc   ZH,      r1      ; (12)
	ld    XL,      Z+      ; (14)
	ld    XH,      Z       ; (16)
	add   XL,      r23     ; (17)
	adc   XH,      r1      ; (18)
	spi_pad
	in    r19,     SR_DR   ; (19) len
	out   SR_DR,   r1      ; (20)
	add   XL,      r22
	adc   XH,      r1
	adiw  XL,      1       ; pre-decrement addressing
	inc   r22              ; count to the boundary: offset + 1
	ldi   ZH,      hi8(kfb_mask)
	tst   r19
	brne  ffirst
	rjmp  .
	rjmp  fnrow
ffirst:
	lds   r20,     kfb_ctx + 7
	cp    r20,     r17
	brcs  fbad             ; first (rightmost) byte beyond the grid
	mov   r0,      r17
	inc   r0
	cp    r0,      r19
	brcs  fbad             ; would run left of the grid
fbyte:
	spi_pad
	in    r0,      SR_DR   ; ( 1)
	out   SR_DR,   r1      ; ( 2)
	swap  r0               ; ( 3)
	mov   ZL,      r0      ; ( 4)
	lpm   r20,     Z       ; ( 7)
	ld    r21,     -X      ; ( 9)
	and   r21,     r20     ; (10)
	or    r21,     r0      ; (11)
	st    X,       r21     ; (13)
	dec   r19              ; (14)
	breq  fnrow            ; (15)
	dec   r22              ; (16)
	brne  fbyte            ; (18)
	subi  r18,     2       ; previous tile column
	movw  ZL,      YL
	add   ZL,      r18
	adc   ZH,      r1
	ld    XL,      Z+
	ld    XH,      Z
	add   XL,      r23
	adc   XH,      r1
	adiw  XL,      4
	ldi   r22,     4
	ldi   ZH,      hi8(kfb_mask)
	rjmp  fbyte
	rjmp  fnrow
fbad:
	rjmp  bad
fnrow:
	subi  r23,     0xFC
	cpi   r23,     32
	brne  .+6
	clr   r23
	add   YL,      r16
	adc   YH,      r1
	dec   r24
	breq  .+2
	rjmp  frow

done:
	pop   YH
	pop   YL
	pop   r17
	pop   r16
	ret
bad:
	lds   r24,     kfb_bad
	lds   r25,     kfb_bad + 1
	adiw  r24,     1
	sts   kfb_bad, r24
	sts   kfb_bad + 1, r25
	rjmp  done

/*
 * void kfb_copy32(u8* dst, const u8* src_flash);
 * Copies a 32 byte ROM tile into a RAM tile (about 165 cycles).
 */
.section .text.kfb_copy32
.global kfb_copy32
kfb_copy32:
	movw  XL,      r24
	movw  ZL,      r22
	.rept 32
	lpm   r0,      Z+
	st    X+,      r0
	.endr
	ret

/*
 * void kfb_alloc(void);
 *
 * Allocates the RAM tiles of one pose's cell grid (replaces the C allocator).
 *
 * Context (global kfb_actx):
 *   +0      tr0:   tile row of the grid's first band
 *   +1      nb:    bands
 *   +2      w:     grid width in tile columns
 *   +3      c0:    grid's first tile column (signed)
 *   +4..13  bmin:  per band, first tile column with opaque pixels (signed)
 *   +14..23 bmax:  per band, last (bmin > bmax: band empty)
 *   +24     trmax: last tile row a sprite may be drawn on (Mode 0 rows from 4)
 *
 * Fills kfb_grid (nb x w u16) with RAM tile addresses, scratch elsewhere. Per
 * band: cells that already are sprite RAM tiles are reused; for the rest one SPI
 * RAM burst reads their background indices while each new RAM tile is filled
 * from its ROM tile (the next index is clocked in during the copy). Cells on
 * foreground tiles (mask index 0xFF) stay scratch; when the RAM tiles run out,
 * kfb_rt_drop counts the cells left out.
 *
 * Registers: r2 column, r3 last column, r4 tile row, r5 band, r6 nb, r7 w,
 * r8 c0, r9 RAM tiles allocated, r10 rtmax, r11 rtbase, r12:r13 scratch save,
 * r14:r15 dropped cells, Y VRAM row, X grid entry, r24:r25 VRAM data pointer.
 */
#define SR_PORT _SFR_IO_ADDR(PORTA)
#define SR_PIN  PA4

/* 16 cycles: with the out that follows, consecutive outs / an out and the next in are 17+ apart */
.macro w16
	ldi   r23,     5
	dec   r23
	brne  .-4              ; 15
	nop                    ; 16
	spi_pad                ; + slack (see KFB_SPI_PAD)
.endm

.section .text.kfb_alloc
.global kfb_alloc
kfb_alloc:
	push  r2
	push  r3
	push  r4
	push  r5
	push  r6
	push  r7
	push  r8
	push  r9
	push  r10
	push  r11
	push  r12
	push  r13
	push  r14
	push  r15
	push  r16
	push  r17
	push  YL
	push  YH
	lds   r9,      kfb_rtno
	lds   r10,     m74_rtmax
	lds   r11,     m74_rtbase
	clr   r14
	clr   r15
	lds   r6,      kfb_actx + 1
	lds   r7,      kfb_actx + 2
	lds   r8,      kfb_actx + 3

	/* the grid starts as scratch */
	mul   r6,      r7
	mov   r16,     r0
	clr   r1
	ldi   ZL,      lo8(kfb_grid)
	ldi   ZH,      hi8(kfb_grid)
	ldi   r18,     lo8(kfb_scratch)
	ldi   r19,     hi8(kfb_scratch)
	tst   r16
	breq  ainit_done
ainit:
	st    Z+,      r18
	st    Z+,      r19
	dec   r16
	brne  ainit
ainit_done:
	clr   r5
	lds   r4,      kfb_actx + 0

aband:
	cp    r5,      r6
	brcs  .+2
	rjmp  adone
	ldi   r16,     4
	cp    r4,      r16
	brcc  .+2
	rjmp  anextb   ; above the playfield rows
	lds   r16,     kfb_actx + 24
	cp    r16,     r4
	brcc  .+2
	rjmp  anextb   ; below
	ldi   ZL,      lo8(kfb_actx + 4)
	ldi   ZH,      hi8(kfb_actx + 4)
	add   ZL,      r5
	adc   ZH,      r1
	ld    r2,      Z       ; first column
	ldd   r3,      Z + 10  ; last column
	cp    r3,      r2
	brge  .+2
	rjmp  anextb   ; empty band
	mul   r5,      r7      ; grid entry: b * w + (first - c0)
	mov   r16,     r2
	sub   r16,     r8
	add   r16,     r0
	clr   r1
	lsl   r16
	ldi   XL,      lo8(kfb_grid)
	ldi   XH,      hi8(kfb_grid)
	add   XL,      r16
	adc   XH,      r1
	tst   r2
	brpl  aclip1
	mov   r16,     r2      ; columns left of the screen: skip their entries
	neg   r16
	lsl   r16
	add   XL,      r16
	adc   XH,      r1
	clr   r2
aclip1:
	ldi   r16,     24
	cp    r16,     r3
	brge  aclip2
	mov   r3,      r16
aclip2:
	cp    r3,      r2
	brge  .+2
	rjmp  anextb
	mov   r16,     r4      ; VRAM row: pfrow[tr - 4]
	subi  r16,     4
	ldi   r17,     30
	mul   r16,     r17
	movw  YL,      r0
	clr   r1
	subi  YL,      lo8(-(pfrow))
	sbci  YH,      hi8(-(pfrow))

	/* pass 1: reuse cells that already are sprite RAM tiles */
	movw  r12,     XL
	clr   r17
	mov   r16,     r2
	movw  ZL,      YL
	adiw  ZL,      5
	add   ZL,      r2
	adc   ZH,      r1
ap1:
	ld    r18,     Z+
	sbrs  r18,     7
	rjmp  ap1need
	andi  r18,     0x7F
	ldi   r19,     32
	mul   r18,     r19
	inc   r1
	st    X+,      r0
	st    X+,      r1
	clr   r1
	rjmp  ap1next
ap1need:
	ldi   r17,     1
	adiw  XL,      2
ap1next:
	inc   r16
	cp    r3,      r16
	brge  ap1
	tst   r17
	brne  .+2
	rjmp  anextb

	/* pass 2: one SPI burst of background indices, allocating while it runs */
	movw  XL,      r12
	ldd   r18,     Y + 1
	ldd   r19,     Y + 2
	add   r18,     r2
	adc   r19,     r1      ; SPI address of the first column's index
	cbi   SR_PORT, SR_PIN
	ldi   r16,     0x03
	out   SR_DR,   r16     ; read
	w16
	out   SR_DR,   r1      ; address 23-16 (bank 0)
	w16
	out   SR_DR,   r19
	w16
	out   SR_DR,   r18
	w16
	out   SR_DR,   r1      ; clock in the first index
	mov   r16,     r2
	movw  r24,     YL
	adiw  r24,     5
	add   r24,     r2
	adc   r25,     r1      ; &data[first]
	w16
ap2:
	spi_pad
	in    r20,     SR_DR   ; background tile index of column r16
	cp    r16,     r3
	breq  .+2
	out   SR_DR,   r1      ; clock in the next one (not after the last)
	movw  ZL,      r24
	ld    r18,     Z
	sbrc  r18,     7
	rjmp  ap2skip          ; already a RAM tile (set in pass 1)
	mov   r23,     r20     ; ROM tile address: t0/t1 high + (index & 0x7F) * 32
	andi  r23,     0x7F
	lsr   r23
	lsr   r23
	lsr   r23
	ldd   r21,     Y + 3
	sbrc  r20,     7
	ldd   r21,     Y + 4
	add   r23,     r21     ; high byte
	mov   r22,     r20
	andi  r22,     7
	swap  r22
	lsl   r22              ; low byte
	mov   ZL,      r23     ; its mask index: M74_ROMMASKIDX_OFF + address / 32
	clr   ZH
	lsl   ZL
	rol   ZH
	lsl   ZL
	rol   ZH
	lsl   ZL
	rol   ZH
	mov   r21,     r20
	andi  r21,     7
	add   ZL,      r21
	adc   ZH,      r1
	subi  ZL,      lo8(-(M74_ROMMASKIDX_OFF))
	sbci  ZH,      hi8(-(M74_ROMMASKIDX_OFF))
	lpm   r21,     Z
	cpi   r21,     0xFF
	brne  .+2
	rjmp  ap2skip          ; foreground tile: drawn over sprites
	subi  ZL,      lo8(-(KFB_FILL_OFF - M74_ROMMASKIDX_OFF))
	sbci  ZH,      hi8(-(KFB_FILL_OFF - M74_ROMMASKIDX_OFF))
	lpm   r20,     Z       ; fill byte (the index in r20 is no longer needed)
	cp    r9,      r10
	brcs  ap2alloc
	ldi   r21,     1       ; out of RAM tiles
	add   r14,     r21
	adc   r15,     r1
	rjmp  ap2skip
ap2alloc:
	mov   r21,     r11
	add   r21,     r9      ; RAM tile number
	inc   r9
	mov   r17,     r21
	ori   r17,     0x80
	movw  ZL,      r24
	st    Z,       r17     ; VRAM: use the RAM tile
	ldi   r17,     32
	mul   r21,     r17
	inc   r1               ; its address: 0x100 + 32 * number
	st    X+,      r0
	st    X+,      r1
	movw  r12,     XL
	movw  XL,      r0
	clr   r1
	cpi   r20,     0x01    ; flat-colour tile? (r20: fill byte, 0x01 if not)
	breq  ap2copy
	.rept 32
	st    X+,      r20     ; 64 cycles
	.endr
	movw  XL,      r12
	rjmp  ap2next
ap2copy:
	movw  ZL,      r22
	.rept 32
	lpm   r0,      Z+      ; 160 cycles
	st    X+,      r0
	.endr
	movw  XL,      r12
	rjmp  ap2next
ap2skip:
	adiw  XL,      2
	ldi   r21,     3       ; keep the next in 17+ cycles after the out
	dec   r21
	brne  .-4
ap2next:
	adiw  r24,     1
	inc   r16
	cp    r3,      r16
	brlt  .+2
	rjmp  ap2
	sbi   SR_PORT, SR_PIN  ; no transfer pending: the last index was not followed by an out

anextb:
	inc   r4
	inc   r5
	rjmp  aband

adone:
	sts   kfb_rtno, r9
	lds   r16,     kfb_rt_drop
	lds   r17,     kfb_rt_drop + 1
	add   r16,     r14
	adc   r17,     r15
	sts   kfb_rt_drop,     r16
	sts   kfb_rt_drop + 1, r17
	pop   YH
	pop   YL
	pop   r17
	pop   r16
	pop   r15
	pop   r14
	pop   r13
	pop   r12
	pop   r11
	pop   r10
	pop   r9
	pop   r8
	pop   r7
	pop   r6
	pop   r5
	pop   r4
	pop   r3
	pop   r2
	ret

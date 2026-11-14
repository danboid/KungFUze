/*
 * Padded SPI RAM access for everything that is not the sprite blitter (src/kfb.s pads its own).
 *
 * The kernel's spiram.s clocks bytes 17 cycles apart, the SPI's minimum; on at least one Omega
 * board that misses now and then: reads came back wrong, and writes (the HUD's, during play) went
 * to the wrong address, leaving a row of the floor map garbled for the rest of the game. These
 * wait about 31 cycles after every byte, and are not pipelined: each byte's transfer completes
 * before the next starts.
 *
 *   void kfs_begin(u8 cmd, u8 bank, u16 addr)   cmd 0x03 read / 0x02 write, then the address
 *   u8   kfs_read(void)                         one byte of a read
 *   void kfs_write(u8 v)                        one byte of a write
 *   void kfs_end(void)                          deselect
 *   void kfs_stream(u8 bank, u16 addr)          a read for kfb_rows: the first byte already clocking
 *   u8   kfs_read1(u8 bank, u16 addr)           a single byte
 *   void kfs_mode(void)                         re-assert sequential mode (write mode register 0x40)
 */
#include <avr/io.h>

#define SR_DR   _SFR_IO_ADDR(SPDR)
#define SR_PORT _SFR_IO_ADDR(PORTA)
#define SR_PIN  PA4

.section .text.kfs

; about 24 cycles, plus the rcall (3) and ret (4): 31 after the out that precedes the call
kfs_wait:
	ldi   r25,     8
1:	dec   r25
	brne  1b
	ret

.global kfs_begin
kfs_begin:                         ; r24 cmd, r22 bank, r21:r20 addr
	cbi   SR_PORT, SR_PIN
	out   SR_DR,   r24
	rcall kfs_wait
	out   SR_DR,   r22
	rcall kfs_wait
	out   SR_DR,   r21
	rcall kfs_wait
	out   SR_DR,   r20
	rjmp  kfs_wait

.global kfs_read
kfs_read:
	out   SR_DR,   r1               ; clock a byte in (r1 = 0)
	rcall kfs_wait
	in    r24,     SR_DR
	ret

.global kfs_write
kfs_write:
	out   SR_DR,   r24
	rjmp  kfs_wait

.global kfs_end
kfs_end:
	rcall kfs_wait                  ; (kfs_stream's last transfer may still be running)
	sbi   SR_PORT, SR_PIN
	ret

.global kfs_stream
kfs_stream:                        ; r24 bank, r23:r22 addr
	mov   r20,     r22
	mov   r21,     r23
	mov   r22,     r24
	ldi   r24,     0x03
	rcall kfs_begin
	ldi   r25,     0xFF
	out   SR_DR,   r25              ; the first byte clocking: kfb_rows reads it (after its prologue)
	ret

.global kfs_read1
kfs_read1:                         ; r24 bank, r23:r22 addr
	mov   r20,     r22
	mov   r21,     r23
	mov   r22,     r24
	ldi   r24,     0x03
	rcall kfs_begin
	rcall kfs_read
	sbi   SR_PORT, SR_PIN
	ret

.global kfs_mode
kfs_mode:
	cbi   SR_PORT, SR_PIN
	ldi   r24,     0x01             ; WRMR
	out   SR_DR,   r24
	rcall kfs_wait
	ldi   r24,     0x40             ; sequential mode
	out   SR_DR,   r24
	rcall kfs_wait
	sbi   SR_PORT, SR_PIN
	ret

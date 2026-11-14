/*
 * Kung-Fu Master, Uzebox Mode 748 prototype
 *
 * Floor 1 scrolls under joypad (or autoplay) control; Thomas and up to six
 * grippers are drawn through the Mode 748 sprite blitter. The program measures
 * how much of each vertical blank the frame's work uses.
 *
 * Screen (216 lines):
 *   lines   0- 15  HUD: VRAM rows 0-1, HUD palette (loaded at frame start)
 *   line       16  VRAM row 2, Mode 2 separator: loads the playfield palette
 *   lines  17-215  playfield: logical lines 33-231 = arcade lines 57-255,
 *                  VRAM rows 4-28 (world rows 1-25), 192 px = arcade x 32-223
 *
 * The assets (gen/, KFM.DAT) are produced by tools/kfm_extract.py and
 * tools/kfm_build.py from the user's own ROM set.
 */

#include <stdbool.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/crc16.h>
#include <uzebox.h>
#include <spiram.h>
#include <bootlib.h>
#include "kfm_rom.h"

/* Padded SPI RAM access (src/kfs.s): the kernel's minimum-timing routines miss on some boards */
extern void kfs_begin(u8 cmd, u8 bank, u16 addr);
extern u8   kfs_read(void);
extern void kfs_write(u8 v);
extern void kfs_end(void);
extern void kfs_stream(u8 bank, u16 addr);
extern u8   kfs_read1(u8 bank, u16 addr);
extern void kfs_mode(void);
#define SpiRamSeqWriteStart(b, a) kfs_begin(0x02U, (b), (a))
#define SpiRamSeqWriteU8(v)       kfs_write(v)
#define SpiRamSeqWriteEnd()       kfs_end()
#define SpiRamSeqReadStart(b, a)  kfs_begin(0x03U, (b), (a))
#define SpiRamSeqReadU8()         kfs_read()
#define SpiRamSeqReadEnd()        kfs_end()
#define SpiRamReadU8(b, a)        kfs_read1((b), (a))
#ifdef KFM_SOUND
#include "mixer.h"
#include "kfm_audio.h"
#ifdef KFM_DEBUG
#define KFM_SRAM_TOP (KFM_QUEUE - 0x20U)   /* the sound queue's page (make QUEUE=, default 0x0E00); the crash log below it */
#else
#define KFM_SRAM_TOP KFM_QUEUE             /* (the crash log is an ordinary variable: one more RAM tile) */
#endif
#else
#ifdef KFM_DEBUG
#define KFM_SRAM_TOP 0x0EE0U
#else
#define KFM_SRAM_TOP 0x0F00U
#endif
#endif
#ifdef KFM_GAME
#include "kfm_game.h"
#include "kfm_intro.h"
#define KFM_LOAD_SIZE KFG_SPI_SIZE
#else
#define KFM_LOAD_SIZE KFM_SPI_SIZE
#endif

/* ---- display geometry -------------------------------------------------- */

#ifndef KFM_LINES
#define KFM_LINES    216U      /* 216: full playfield; fewer crop its bottom, for more blank time */
#endif
#ifndef KFM_FIRST
#if KFM_LINES == 192
#define KFM_FIRST    32U       /* IK748's geometry (known good on an Omega) */
#else
#define KFM_FIRST    (20U + ((224U - KFM_LINES) / 2U))   /* centred */
#endif
#endif
#define FIRST_LINE   KFM_FIRST
#define HSYNC_LINES  253U      /* SYNC_HSYNC_PULSES */
#define VSYNC_LINES  9U        /* 18 half-line equalising / sync pulses */
#define RENDER_TOP   (HSYNC_LINES - FIRST_LINE)              /* sync_pulse at display start */
#define RENDER_END   (HSYNC_LINES - FIRST_LINE - KFM_LINES - 2U) /* sync_pulse after display (+2, as IK748) */
#ifndef KFM_SAFETY
#define KFM_SAFETY   4U        /* blank-line estimates are made this much more pessimistic */
#endif
#define SAFETY_LINES KFM_SAFETY
#define VBLANK_LINES (262U - KFM_LINES)

#define PF_LOG_TOP   33        /* first logical playfield line shown */
#define PF_LOG_BOT   (PF_LOG_TOP + KFM_LINES - 17)       /* one past the last */
#define SCREEN_W     192
#define PF_ROWS      (((PF_LOG_BOT - 1) >> 3) - 3)  /* VRAM rows 4.. that are displayed (25 at 216 lines, 22 at 192) */

#define THOMAS_X     (120 - KFM_CROP_X)   /* Thomas's anchor on screen */
#define THOMAS_Y     (193 + KFM_LOG_Y)    /* logical line */
#define GRIP_Y       (145 + KFM_LOG_Y)    /* gripper head, logical line */
#define CAM_MAX      ((KFM_WORLD_W - 25) * 8)
#define CAM_START    (1663 - 96)          /* the arcade's first scroll value, cropped */

extern volatile u8 sync_pulse;
extern volatile u8 sync_phase;
extern u8 __bss_end;
extern const unsigned char kfm_rom[];

/* ---- VRAM ---------------------------------------------------------------- */

static m74_mode0_vram_t hudrow[2];
m74_mode0_vram_t pfrow[PF_ROWS];               /* also used by src/kfb.s */
static m74_mode2_vram_t seprow   = { 2U | M74_CFG_PAL_SRC_ROM,  KFM_PAL_PF, 0x00U };
static m74_mode2_vram_t dummyrow = { 2U | M74_CFG_PAL_SRC_NONE, 0U,         0x00U };
static u8  rowsel[] = { 0U, 17U, PF_LOG_TOP, 255U };

#ifdef KFM_GAME
/* The intro: 23 rows of picture from line 0 (the HUD rows and the playfield rows, pointed at the
** intro's maps and tiles), no separator; the playfield palette from the top of the frame. */
#define IR(n) (((n) < 2) ? (u16)(&hudrow[(n) < 2 ? (n) : 0]) : (((n) - 2) < PF_ROWS ? (u16)(&pfrow[((n) >= 2 && ((n) - 2) < PF_ROWS) ? (n) - 2 : 0]) : (u16)(&dummyrow)))
static const u16 vrow_intro[32] PROGMEM = {
	IR(0),  IR(1),  IR(2),  IR(3),  IR(4),  IR(5),  IR(6),  IR(7),  IR(8),  IR(9),  IR(10), IR(11),
	IR(12), IR(13), IR(14), IR(15), IR(16), IR(17), IR(18), IR(19), IR(20), IR(21), IR(22),
	(u16)(&dummyrow), (u16)(&dummyrow), (u16)(&dummyrow), (u16)(&dummyrow), (u16)(&dummyrow),
	(u16)(&dummyrow), (u16)(&dummyrow), (u16)(&dummyrow), (u16)(&dummyrow)
};
static u8 rowsel_intro[] = { 0U, 255U };
static u8 intro_on;
#endif

/* the 32 VRAM row pointers, in flash (saves 64 bytes of SRAM = 2 RAM tiles) */
#define PFR(n) (((n) < PF_ROWS) ? (u16)(&pfrow[(n) < PF_ROWS ? (n) : 0]) : (u16)(&dummyrow))
static const u16 vrow_rom[32] PROGMEM = {
	(u16)(&hudrow[0]), (u16)(&hudrow[1]), (u16)(&seprow), (u16)(&dummyrow),
	PFR(0),  PFR(1),  PFR(2),  PFR(3),  PFR(4),  PFR(5),  PFR(6),  PFR(7),
	PFR(8),  PFR(9),  PFR(10), PFR(11), PFR(12), PFR(13), PFR(14), PFR(15),
	PFR(16), PFR(17), PFR(18), PFR(19), PFR(20), PFR(21), PFR(22), PFR(23), PFR(24),
	(u16)(&dummyrow), (u16)(&dummyrow), (u16)(&dummyrow)
};

/* ---- game state ------------------------------------------------------------ */

#define NGRIP 6U
typedef struct {
	s16 x;        /* world x of the head */
	u8  state;    /* 0 off, 1 walking, 2 holding */
	u8  right;    /* 1: comes from the right (mirrored art) */
	u8  kind;     /* 0: walk poses 0-2, 1: walk poses 3-5 */
	u8  anim;
	u8  timer;
	u8  slot;     /* stacking slot while holding */
} grip_t;

#ifndef KFM_GAME
static grip_t grip[NGRIP];
#endif
static s16 cam = CAM_START;
static u8  facing_left = 1U;
static u8  sub;
#ifndef KFM_GAME
static u8  walk_phase;
static u8  held;
#endif

/* ---- benchmark interface (read / written through the emulator API) --------- */

volatile u8  bench_ngrip = NGRIP;   /* grippers active */
volatile u8  bench_auto  = 1U;      /* 1: autoplay (walk, turn at the ends) */
volatile u8  bench_draw  = 1U;      /* 0: skip the sprite blits */
volatile u8  bench_mask  = 0U;      /* Mode 748 blitter masks (needs M74_MSK_ENABLE, off in m74cfg.h) */
volatile u8  bench_thomas = 1U;     /* 0: no Thomas */
volatile u8  bench_budget = 0U;     /* max 8x8 blits an update (0: no limit); objects rotate in priority */
volatile u8  bench_engine = 1U;     /* 0: Mode 748 sprite blitter, 1: custom strip blitter */
volatile u8  bench_deadline = 0U;   /* custom blitter: stop drawing when fewer blank lines than this remain */
volatile u8  bench_calloc = 0U;     /* 1: the C allocator instead of kfb_alloc() (for comparison) */
volatile u8  bench_delay = 0U;      /* calibration: burn bench_delay x 10000 cycles in the update */

typedef struct {
	u16 updates;      /* game updates (pictures) done */
	u16 fields;       /* video fields elapsed */
	u8  cpu_lines;    /* last update: scanlines of CPU time it took (display lines excluded) */
	u8  cpu_max;
	u32 cpu_sum;      /* sum of cpu_lines, for averages */
	u8  late;         /* last update: displays it ran over (0: fitted in one blank) */
	u16 late_count;   /* updates that ran over at least one display */
	u8  blits;        /* 8x8 blits issued last update */
	u8  blits_max;
	u8  rtbase;
	u8  rtmax;
	u8  vblank_lines;
	u8  err;          /* 0 ok, 1 SD init, 2 file missing, 3 bad data, 4 ROM blob misplaced, 6 no SPI RAM */
	u8  rt_used;      /* RAM tiles allocated by the last update */
	u8  rt_max;
	u16 rt_full;      /* updates that ran out of RAM tiles */
} stats_t;
volatile stats_t kfm_stats;

/* ---- loading ------------------------------------------------------------------ */

static void fail(u8 code, u8 colour)
{
	kfm_stats.err = code;
	m74_discol = colour;
	m74_config = 0U;
	while (1) { WaitVsync(1); }
}

/* SPI RAM self-check: the 8-bit sum of every 64-byte chunk of the loaded image is
** kept in SPI RAM bank 1 (unused otherwise); spare blank time re-reads one chunk a
** frame and compares, counting read errors (diagnostics HUD page). */
#define CHK_CHUNK 64U
#define CHK_BASE  0xF000U                 /* the sums: bank 1 from 0xF000 (the game's poses end below) */
static u16 chk_chunks, chk_next;
volatile u16 chk_bad;
volatile u8  chk_pass;

static void load_spiram(u8* secbuf)
{
	sdc_struct_t sds;
	u32 cl;
	u32 addr;
	u16 i;
	u8  j;
#ifdef KFM_GAME
	u16 crc = 0U, crc_want = 0U;
	u32 crc_len = 0U;
#endif

	sds.bufp = secbuf;
	if (FS_Init(&sds) != 0U) { fail(1U, 0x07U); }
	if (SpiRamInit() == 0U) { fail(6U, 0xC7U); }  /* no SPI RAM answered: magenta */
#ifdef KFM_GAME
	/* The data is packed inside the game's own KUNGFUM.UZE, as in IK748 and stuntcar-uzebox: after
	** packrom's 512-byte header and the program (its size: header bytes 8-11), from the next whole
	** sector. If the .uze is not there (renamed) or not a .uze: a separate KFMG.DAT. */
	cl = FS_Find(&sds,
	    ((u16)('K') << 8) | 'U', ((u16)('N') << 8) | 'G', ((u16)('F') << 8) | 'U', ((u16)('M') << 8) | ' ',
	    ((u16)('U') << 8) | 'Z', ((u16)('E') << 8) | 0U);
	if (cl != 0U) {
		FS_Select_Cluster(&sds, cl);
		FS_Read_Sector(&sds);
		if (secbuf[0] == 'U' && secbuf[1] == 'Z' && secbuf[2] == 'E' && secbuf[5] == 'X') {
			u16 skip = (u16)((512UL + ((u32)secbuf[8] | ((u32)secbuf[9] << 8) | ((u32)secbuf[10] << 16)) + 511UL) / 512UL);
			while (skip != 0U) { FS_Next_Sector(&sds); skip--; }
		} else {
			cl = 0U;
		}
	}
	if (cl == 0U) {
		cl = FS_Find(&sds,
		    ((u16)('K') << 8) | 'F', ((u16)('M') << 8) | 'G', ((u16)(' ') << 8) | ' ', ((u16)(' ') << 8) | ' ',
		    ((u16)('D') << 8) | 'A', ((u16)('T') << 8) | 0U);
		if (cl == 0U) { fail(2U, 0x07U); }
		FS_Select_Cluster(&sds, cl);
	}
#else
	cl = FS_Find(&sds,
	    ((u16)('K') << 8) | 'F', ((u16)('M') << 8) | ' ',
	    ((u16)(' ') << 8) | ' ', ((u16)(' ') << 8) | ' ',
	    ((u16)('D') << 8) | 'A', ((u16)('T') << 8) | 0U);
	if (cl == 0U) { fail(2U, 0x07U); }
	FS_Select_Cluster(&sds, cl);
#endif
	for (addr = 0U; addr < KFM_LOAD_SIZE; addr += 512U) {
		FS_Read_Sector(&sds);
		SDC_Release();                          /* the card must not drive the bus for SPI RAM */
#ifdef KFM_GAME
		if (addr == 0U) {                       /* the data's length and CRC (tools/kfm_pack.py) */
			crc_want = (u16)secbuf[16] | ((u16)secbuf[17] << 8);
			crc_len  = (u32)secbuf[18] | ((u32)secbuf[19] << 8) | ((u32)secbuf[20] << 16) | ((u32)secbuf[21] << 24);
			if (crc_len == 0U || crc_len > KFM_LOAD_SIZE) { fail(5U, 0xFFU); }
			secbuf[16] = 0U; secbuf[17] = 0U;   /* (the CRC was made with these zero; nothing reads them) */
		}
		{	/* this sector into SPI RAM, and its bytes within the data's length into the CRC. The
			** kernel's SpiRamSeqWriteU8 waits a fixed delay (18 cycles from the last byte) and writes;
			** here the CRC of each byte (over 18 cycles) is computed while the next byte goes out
			** instead, so the check costs almost nothing. The first byte, after the address, is the
			** kernel's (its delay); the bytes past the data's end too. */
			u16 n = (crc_len - addr >= 512U) ? 512U : (u16)(crc_len - addr);
			SpiRamSeqWriteStart((u8)(addr >> 16), (u16)addr);
			SpiRamSeqWriteU8(secbuf[0]);
			if (n != 0U) { crc = _crc_xmodem_update(crc, secbuf[0]); }
			for (i = 1U; i < n; i++) {
				u8 v = secbuf[i];
				SPDR = v;
				crc = _crc_xmodem_update(crc, v);   /* (asm volatile: not moved before the write) */
			}
			for (; i < 512U; i++) { SpiRamSeqWriteU8(secbuf[i]); }
		}
#else
		SpiRamSeqWriteStart((u8)(addr >> 16), (u16)addr);
		for (i = 0U; i < 512U; i++) { SpiRamSeqWriteU8(secbuf[i]); }
#endif
		SpiRamSeqWriteEnd();
		SpiRamSeqWriteStart(1U, CHK_BASE + (u16)(addr / CHK_CHUNK));
		for (j = 0U; j < (u8)(512U / CHK_CHUNK); j++) {
			u8 sum = 0U;
			for (i = 0U; i < CHK_CHUNK; i++) { sum += secbuf[j * CHK_CHUNK + i]; }
			SpiRamSeqWriteU8(sum);
		}
		SpiRamSeqWriteEnd();
		FS_Next_Sector(&sds);
	}
#ifdef KFM_GAME
	if (crc != crc_want) { fail(5U, 0xFFU); }   /* the data is damaged: white */
#endif
	SDC_Release();
	SPCR = (1U << SPE) | (1U << MSTR);          /* SPI back to SPI RAM speed (SpiRamInit would */
	SPSR = (1U << SPI2X);                       /* overwrite RAM with its presence test)        */
	PORTA |= (1U << PA4);
	chk_chunks = (u16)(((KFM_LOAD_SIZE + 511U) / 512U) * (512U / CHK_CHUNK));
	kfs_mode();
	if (SpiRamReadU8(0U, 0U) != 'K' || SpiRamReadU8(0U, 3U) != '7') { fail(3U, 0x3FU); }
#ifdef KFM_GAME
	{	/* KFMG.DAT must come from the same asset build as this firmware: else stop (cyan) */
		u8 k;
		for (k = 0U; k < 4U; k++) {
			if (SpiRamReadU8(0U, 4U + k) != (u8)(KFG_BUILD_ID >> (8U * k))) { fail(4U, 0xF8U); }
		}
	}
#endif
}

/* one chunk of the self-check (the HUD rows, which the program rewrites, are skipped) */
static __attribute__((unused)) void chk_step(void)
{
	u32 a = (u32)chk_next * CHK_CHUNK;
	if (a < KFM_SPI_HUD || a >= KFM_SPI_HUD + 0x100U) {
		u8 i, sum = 0U;
		SpiRamSeqReadStart((u8)(a >> 16), (u16)a);
		for (i = 0U; i < CHK_CHUNK; i++) { sum += SpiRamSeqReadU8(); }
		SpiRamSeqReadEnd();
		if (sum != SpiRamReadU8(1U, CHK_BASE + chk_next)) { chk_bad++; }
	}
	if (++chk_next >= chk_chunks) { chk_next = 0U; chk_pass++; }
}

/* ---- persistent settings and boot counter (EEPROM block KFB_EE_ID) ------------------- */
#define KFB_EE_ID 0x4B46U                       /* 'KF' */
static u16 boots;

#ifdef KFM_GAME
static u16 fix_count, fix_addr;    /* display structures found damaged (and repaired), the last address */
#endif

/* Crash log: a few bytes at a fixed address the next start-up reads back (if the bootloader
** has not overwritten them; the magic word tells). */
typedef struct { u16 magic; u8 phase; u8 mode; u16 secs; u8 stk; u8 spare; u16 pad1; u16 pad2;
                 u8 zl; u8 pulse; u16 lines; } crash_t;   /* zl..lines: written by update_sound every line */
#ifdef KFM_DEBUG
#define CRASH_LOG ((volatile crash_t*)KFM_SRAM_TOP)   /* fixed, above the RAM tiles: it survives a reset */
#else
volatile crash_t kfm_crash;                         /* an ordinary variable (kfm_sound.s writes its trail) */
#define CRASH_LOG (&kfm_crash)
#endif

/* Unexpected interrupts: avr-libc's default jumps to address 0 (a restart). With a Uzenet ESP8266 on
** the board, its serial output arrives on the AVR's UART: if the bootloader leaves the UART's receive
** interrupt on, every byte would restart the game. The game uses only the kernel's timer interrupt:
** anything else is switched off again here and counted (diagnostics page: I), and the game carries on. */
static volatile u8 bad_irqs;
ISR(BADISR_vect)
{
	UCSR0B = 0U;                        /* the UART (the ESP8266's serial output) */
	EIMSK  = 0U;                        /* external interrupts */
	PCICR  = 0U;                        /* pin-change interrupts */
	SPCR  &= (u8)~_BV(SPIE);            /* the SPI's own interrupt (the game polls) */
	TWCR   = 0U;                        /* two-wire */
	if (bad_irqs < 255U) { bad_irqs++; }
	((volatile u8*)CRASH_LOG)[7] = bad_irqs;      /* the crash log's spare byte */
}
static u8  last_ok, last_phase, last_stk, stk_prev;
static u16 last_secs, last_pad1, last_pad2, last_lines;
static u8  last_zl, last_pulse;
static u16 combo_frames;   /* frames a controller read exactly Start+Select+Y+B */
#ifdef KFM_GAME
static u8  run_secs_sub;
#endif
#define PHASE(n) (CRASH_LOG->phase = (n))
/* stack watermark: the free part of the main stack is painted at start-up; the diagnostics
** page shows the fewest bytes that ever stayed unused above the Mode 748 area (0x1020) */
#define STK_LO 0x1020U
static u8 stk_free(void)
{
	const volatile u8* p = (const volatile u8*)STK_LO;
	u8 n = 0U;
	while (n < 200U && p[n] == 0xA5U) { n++; }
	return n;
}
static u8  ee_ok, ee_timer, mcusr_boot;

static void ee_save(void)
{
	struct EepromBlockStruct b;
	u8 i;
	if (!ee_ok) { return; }
	for (i = 0U; i < 30U; i++) { b.data[i] = 0U; }
	b.id = KFB_EE_ID;
	b.data[0] = (u8)boots;
	b.data[1] = (u8)(boots >> 8);
	b.data[2] = (bench_engine == 0U) ? 0U : ((bench_calloc != 0U) ? 1U : 2U);
	b.data[3] = bench_ngrip;
	b.data[4] = bench_auto;
	b.data[5] = bench_deadline;
	b.data[6] = 0xA5U;                          /* valid marker */
	EepromWriteBlock(&b);
}

static void ee_boot(void)
{
	struct EepromBlockStruct b;
	ee_ok = isEepromFormatted() ? 1U : 0U;      /* never format: that would erase other games' saves */
	if (!ee_ok) { return; }
	if (EepromReadBlock(KFB_EE_ID, &b) == 0 && b.data[6] == 0xA5U) {
		boots = (u16)b.data[0] | ((u16)b.data[1] << 8);
		bench_engine = (b.data[2] != 0U) ? 1U : 0U;
		bench_calloc = (b.data[2] == 1U) ? 1U : 0U;
		if (b.data[3] <= NGRIP) { bench_ngrip = b.data[3]; }
		bench_auto = b.data[4] ? 1U : 0U;
		bench_deadline = b.data[5];
	}
	boots++;
	ee_save();
}

/* ---- rendering ------------------------------------------------------------------ */

/* The display's row structures are written once, at start-up; on an Omega a row of the screen was
** seen to switch to the wrong tiles for the rest of the game. Check them every frame, repair what
** differs, and count it (the diagnostics page shows the count and the last damaged address). */
#ifdef KFM_GAME
static void fix_byte(volatile u8* p, u8 v)
{
	if (*p != v) { *p = v; fix_count++; fix_addr = (u16)p; }
}

static void vram_heal(void)
{
	u8 i;
	for (i = 0U; i < PF_ROWS; i++) {
		u8 t = pgm_read_byte(&kfm_row_t0[i]);
		fix_byte(&pfrow[i].t0_addr_h, t);
		fix_byte(&pfrow[i].t1_addr_h, (u8)(t + 0x10U));
	}
	for (i = 0U; i < 2U; i++) {
		fix_byte(&hudrow[i].config, 0U);
		fix_byte(&hudrow[i].t0_addr_h, KFM_HUD_TILES >> 8);
		fix_byte(&hudrow[i].t1_addr_h, (KFM_HUD_TILES >> 8) + 0x10U);
	}
	fix_byte(&rowsel[0], 0U);
	fix_byte(&rowsel[1], 17U);
	fix_byte(&rowsel[2], PF_LOG_TOP);
	fix_byte(&rowsel[3], 255U);
	fix_byte((volatile u8*)&seprow, 2U | M74_CFG_PAL_SRC_ROM);
	fix_byte((volatile u8*)&dummyrow, 2U | M74_CFG_PAL_SRC_NONE);
}
#endif

static void bg_move(s16 x)
{
	u8 i;
	u8 xshift = (u8)x & 7U;
	u16 xtile = (u16)x >> 3;
	for (i = 0U; i < PF_ROWS; i++) {
		pfrow[i].config  = 0U | (xshift << 4);
		pfrow[i].bg_addr = KFM_SPI_MAP + (i * KFM_WORLD_W) + xtile;
	}
}

static u8 blit_count;
volatile u16 kfm_dropped; /* objects / strips left out by the budget or the deadline */
#ifndef KFM_GAME
static u8 rot;            /* round-robin start for the gripper priority */
#endif

#ifndef KFM_GAME
static void kfb_pose(u8 pose, s16 x, s16 y);
#endif

#ifndef KFM_GAME   /* benchmark only */
static void draw_pose(u8 pose, s16 x, s16 y)
{
	if (bench_engine) { kfb_pose(pose, x, y); return; }
	{
	u8 n = pgm_read_byte(&kfm_pose_n[pose]);
	if (bench_budget != 0U && (u16)blit_count + n > bench_budget) { kfm_dropped++; return; }
	const s8* p = kfm_blits + 6U * pgm_read_word(&kfm_pose_ofs[pose]);
	while (n--) {
		s16 bx = x + (s8)pgm_read_byte(p + 2);
		s16 by = y + (s8)pgm_read_byte(p + 3);
		if (bx > -8 && bx < SCREEN_W + 8 && by > PF_LOG_TOP - 8 && by < PF_LOG_BOT) {
			u16 t = pgm_read_byte(p) | ((u16)pgm_read_byte(p + 1) << 8);
			M74_BlitSpriteCol((u16)(KFM_SPI_SPR + (t * 32U)), (u8)(bx + 8), (u8)(by + 8),
			                  pgm_read_byte(p + 4) | (bench_mask ? M74_SPR_MASK : 0U), pgm_read_byte(p + 5));
			blit_count++;
		}
		p += 6;
	}
	}
}
#endif

/* ---- custom strip blitter (see src/kfb.s) ------------------------------------- */

typedef struct {
	u8  rows;
	u8  line4;
	u8  xbase;
	u8  flip;
	u16 ptr;
	u8  stride2;
	u8  dmax;
} kfb_ctx_t;

kfb_ctx_t kfb_ctx;                 /* read by kfb_rows() */
volatile u16 kfb_bad;              /* pose rows rejected by kfb_rows() (bad SPI RAM data) */
extern const u8 kfb_mask[];
extern void kfb_rows(void);
extern void kfb_copy32(u8* dst, u16 src);
extern void kfb_alloc(void);

typedef struct {
	u8 tr0;
	u8 nb;
	u8 w;
	s8 c0;
	s8 bmin[10];
	s8 bmax[10];
	u8 trmax;
} kfb_actx_t;
kfb_actx_t kfb_actx;               /* read by kfb_alloc() */

#define KFB_MAXB 10U               /* bands a pose may span (72 rows at any phase) */
#define KFB_MAXW 9U                /* tile columns a pose may span */
u16 kfb_grid[KFB_MAXB * KFB_MAXW];               /* pose cell grid (src/kfb.s); set to scratch at start-up */
u8  kfb_scratch[32];
u8  kfb_rtno;
volatile u16 kfb_rt_drop;          /* cells left undrawn: no RAM tile left */
volatile u16 kfb_wait_lines;       /* blank lines spent waiting for a display to pass */
#ifndef KFB_ALLOC_LINES
#define KFB_ALLOC_LINES 9U
#endif
#define KFB_ALLOC_LINES_ KFB_ALLOC_LINES        /* more than a pose's allocation phase takes */
#ifndef KFB_STRIP_LINES
#define KFB_STRIP_LINES 12U
#endif
#define KFB_STRIP_LINES_ KFB_STRIP_LINES        /* more than one strip's stream takes */
typedef struct { u8 v; u16 l; } when_t;
static when_t now(void);
static u16 cpu_lines_between(when_t a, when_t b, u8* displays);
#ifdef KFB_PROFILE
volatile u32 prof[5];   /* est alloc, actual alloc, est stream, actual stream (lines), poses */
#endif
static u8  lines_left(void);
static bool post_display(void);

/* SPI RAM work must not straddle the display start (the display takes the bus over
** without restoring it): wait for the display to pass if fewer lines than `need` remain. */
static void kfb_safe(u8 need)
{
	u8 left = lines_left();
	if (left < need) {
		kfb_wait_lines += left;
		while (!post_display()) {}
	}
}

/* RAM tile addresses for columns c0 .. c1 of tile row tr (others stay scratch) */
#ifdef KFB_PROFILE
#define KFB_NOINLINE __attribute__((noinline))
#else
#define KFB_NOINLINE
#endif

static KFB_NOINLINE void kfb_band(u8 tr, s8 c0, s8 c1, u16* out)
{
	m74_mode0_vram_t* row;
	s8  c;
	u8  bg[KFB_MAXW], need = 0U;
	if (tr < 4U || tr >= 4U + PF_ROWS) { return; }
	row = &pfrow[tr - 4U];
	if (c0 < 0) { out -= c0; c0 = 0; }
	if (c1 > 24) { c1 = 24; }
	if (c0 > c1) { return; }
	for (c = c0; c <= c1; c++) {           /* cells already sprite RAM tiles need no background */
		u8 v = row->data[(u8)c];
		if (v & 0x80U) { out[c - c0] = 0x100U + ((u16)(v & 0x7FU) << 5); }
		else { need = 1U; }
	}
	if (!need) { return; }
	SpiRamSeqReadStart(0U, row->bg_addr + (u8)c0);
	for (c = c0; c <= c1; c++) { bg[c - c0] = SpiRamSeqReadU8(); }
	SpiRamSeqReadEnd();
	for (c = c0; c <= c1; c++) {
		u8  idx, rt;
		u16 taddr;
		if (row->data[(u8)c] & 0x80U) { continue; }
		idx = bg[c - c0];
		taddr = (idx < 128U) ? (((u16)row->t0_addr_h << 8) + ((u16)idx << 5))
		                     : (((u16)row->t1_addr_h << 8) + ((u16)(idx - 128U) << 5));
		if (pgm_read_byte(M74_ROMMASKIDX_OFF + (taddr >> 5)) == 0xFFU) { continue; }   /* drawn over sprites */
		if (kfb_rtno >= m74_rtmax) { kfb_rt_drop++; continue; }
		rt = m74_rtbase + kfb_rtno;
		kfb_rtno++;
		out[c - c0] = 0x100U + ((u16)rt << 5);
		kfb_copy32((u8*)out[c - c0], taddr);
		row->data[(u8)c] = 0x80U | rt;
	}
}

/* Pose metadata, filled from flash tables by the front-ends below. */
typedef struct {
	s8  top;        /* first row, relative to the anchor's y */
	u8  rows;
	u8  bank;       /* SPI RAM bank of the streams (0 or 1) */
	u16 a0, a1;     /* stream address for an even / odd anchor x */
	s8  off0, off1; /* pixel offset of the stream's byte 0, relative to the anchor */
	u16 n0, n1;     /* stream lengths */
} kfb_meta_t;
static kfb_meta_t kfb_md;
static u8 kfb_snb;               /* bands of the current vertical phase */
static s8 kfb_sa[2U * 10U];      /* their opaque pixel spans (x min, x max) relative to the anchor */

static void kfb_draw(u8 flip, s16 x, s16 y);

/* benchmark poses: spans and streams in flash / SPI RAM bank 0 */
#ifndef KFM_GAME   /* benchmark only */
static KFB_NOINLINE void kfb_pose(u8 pose, s16 x, s16 y)
{
	u8  base = pgm_read_byte(&kfb_pose_base[pose]);
	const kfb_span_t* si = &kfb_span_info[base];
	const kfb_pstream_t* ps = &kfb_pstreams[base];
	const s8* sp;
	u8 b;
	kfb_md.top  = (s8)pgm_read_byte(&si->top);
	kfb_md.rows = pgm_read_byte(&si->rows);
	kfb_md.bank = 0U;
	kfb_md.a0 = pgm_read_word(&ps->a0);  kfb_md.a1 = pgm_read_word(&ps->a1);
	kfb_md.off0 = (s8)pgm_read_byte(&ps->off0); kfb_md.off1 = (s8)pgm_read_byte(&ps->off1);
	kfb_md.n0 = pgm_read_word(&ps->n0);  kfb_md.n1 = pgm_read_word(&ps->n1);
	sp = kfb_spans + 2U * pgm_read_word(&si->ofs[(u8)(y + kfb_md.top) & 7U]);
	kfb_snb = pgm_read_byte(sp);
	if (kfb_snb > 10U) { kfm_dropped++; return; }
	for (b = 0U; b < 2U * kfb_snb; b++) { kfb_sa[b] = (s8)pgm_read_byte(sp + 2U + b); }
	kfb_draw(pgm_read_byte(&kfb_pose_flip[pose]), x, y);
}
#endif

#ifdef KFM_GAME
/* game poses: metadata in flash, spans and streams in SPI RAM bank 1 */
static KFB_NOINLINE void kfg_pose_m(const kfg_meta_t* g, u8 flip, s16 x, s16 y)
{
	u8 b;
	kfb_md.top  = (s8)pgm_read_byte(&g->top);
	kfb_md.rows = pgm_read_byte(&g->rows);
	kfb_md.bank = pgm_read_byte(&g->bank);
	kfb_md.a0 = pgm_read_word(&g->a0);  kfb_md.a1 = pgm_read_word(&g->a1);
	kfb_md.off0 = (s8)pgm_read_byte(&g->off0); kfb_md.off1 = (s8)pgm_read_byte(&g->off1);
	kfb_md.n0 = pgm_read_word(&g->n0);  kfb_md.n1 = pgm_read_word(&g->n1);
	{
		s16 ytop = y + kfb_md.top;
		if (ytop >= (s16)PF_LOG_BOT || ytop + kfb_md.rows <= (s16)PF_LOG_TOP - 8) { return; }
		kfb_safe(2U);
		SpiRamSeqReadStart(kfb_md.bank, pgm_read_word(&g->spans) + KFG_SPAN_BLOCK * ((u8)ytop & 7U));
		kfb_snb = SpiRamSeqReadU8();
		/* the band count must match the pose's rows at this phase: a misread one would make the
		** stream walk grid entries never set up for this pose (null pointers: a crash) */
		if (kfb_snb != (u8)((((u8)ytop & 7U) + kfb_md.rows - 1U) / 8U + 1U)) {
			SpiRamSeqReadEnd(); kfm_dropped++; kfb_bad++; return;
		}
		for (b = 0U; b < 2U * kfb_snb; b++) { kfb_sa[b] = (s8)SpiRamSeqReadU8(); }
		SpiRamSeqReadEnd();
	}
	kfb_draw(flip, x, y);
}

static void kfg_pose(u8 id, u8 flip, s16 x, s16 y) { kfg_pose_m(&kfg_meta[id], flip, x, y); }
#endif

/* the drawing core: the pose in kfb_md, its spans for this vertical phase in kfb_sa */
static KFB_NOINLINE void kfb_draw(u8 flip, s16 x, s16 y)
{
	u8  rows = kfb_md.rows;
	s16 ytop = y + kfb_md.top;
	s16 vx   = x + (cam & 7);                  /* anchor in VRAM pixels */
	s16 tr0;
	s8  c0 = 127, c1 = -128;
	u8  est_alloc, est_stream;
	s8* bmin = kfb_actx.bmin;
	s8* bmax = kfb_actx.bmax;
	u8  nb, b, w, i;

	if (ytop >= (s16)PF_LOG_BOT || ytop + rows <= (s16)PF_LOG_TOP - 8) { return; }
	if (vx <= -40 || vx >= 232) { return; }

	/* the tile columns each band's opaque pixels touch */
	tr0 = ytop >> 3;
	nb  = kfb_snb;
	{	/* (the flip's base chosen once; vx + 64 keeps the sums positive: unsigned shifts) */
		const s8* sa = kfb_sa;
		u16 base = (u16)(vx + 64 + (flip ? 15 : 0));
		for (b = 0U; b < nb; b++, sa += 2) {
			s8 a0 = sa[0], a1 = sa[1], lo, hi;
			if (a0 > a1) { bmin[b] = 127; bmax[b] = -128; continue; }
			if (flip) { lo = (s8)((u8)((base - a1) >> 3) - 8U); hi = (s8)((u8)((base - a0) >> 3) - 8U); }
			else      { lo = (s8)((u8)((base + a0) >> 3) - 8U); hi = (s8)((u8)((base + a1) >> 3) - 8U); }
			bmin[b] = lo; bmax[b] = hi;
			if (lo < c0) { c0 = lo; }
			if (hi > c1) { c1 = hi; }
		}
	}
	if (c0 > c1) { return; }
	w = (u8)(c1 - c0 + 1);
	if (w > KFB_MAXW) { kfm_dropped++; return; }

	/* cost estimate, in scanlines (interrupts included: 1400 main cycles a line). Measured in
	** cuzebox-ex: allocation ~230 cycles a new cell + ~150 a band, streaming 18 + KFB_SPI_PAD
	** cycles a byte + ~20 a row. Cells already allocated by another sprite make it pessimistic. */
	{
		u16 n = ((u8)vx & 1U) ? kfb_md.n1 : kfb_md.n0;
		u16 cells = 0U;                         /* cells still to allocate (others are reused) */
		for (b = 0U; b < nb; b++) {
			u8 tr = (u8)(tr0 + b);
			s8 lo = bmin[b], hi = bmax[b];
			if (lo <= hi && tr >= 4U && tr < 4U + PF_ROWS) {
				const u8* d;
				u8 k;
				if (lo < 0) { lo = 0; }                  /* the bounds clipped once, then a pointer walk */
				if (hi > 24) { hi = 24; }
				if (lo > hi) { continue; }
				d = pfrow[tr - 4U].data + (u8)lo;
				for (k = (u8)(hi - lo) + 1U; k; k--) { cells += (u8)(~*d++ >> 7); }
			}
		}
		/* calibrated against measured phase times (cuzebox-ex, interrupts included) */
		/* in lines, rounded up: multiplied by a reciprocal a little above 1/1800 and 1/1600 (the
		** estimates can only come out equal or higher: the deadline stays safe), not divided: the
		** two 32-bit divisions per pose cost about 3 lines a frame in a busy scene. The values
		** stay below 2^16 (cells <= 72, nb <= 24, n <= 2048, rows <= 96). */
		est_alloc  = (u8)(((u32)(u16)((u16)cells * 230U + (u16)nb * 150U + 400U + 1799U) * 37U) >> 16);
		est_stream = (u8)(((u32)(u16)(n * (18U + KFB_SPI_PAD) + (u16)rows * 20U + 300U + 1599U) * 41U) >> 16);
	}
#ifdef KFB_FIXED_DEADLINE
	if (bench_deadline != 0U && lines_left() < bench_deadline) { kfm_dropped++; return; }
#else
	if (bench_deadline != 0U && lines_left() < est_alloc + est_stream + 1U) { kfm_dropped++; return; }
#endif

	/* allocate the grid */
	kfb_safe(est_alloc + 1U);
#ifdef KFB_PROFILE
	{ when_t pa = now();
#endif
	if (bench_calloc) {
		for (i = 0U; i < nb * w; i++) { kfb_grid[i] = (u16)kfb_scratch; }
		for (b = 0U; b < nb; b++) {
			if (bmin[b] <= bmax[b] && tr0 + b >= 0) {
				kfb_band((u8)(tr0 + b), bmin[b], bmax[b], &kfb_grid[b * w + (u8)(bmin[b] - c0)]);
			}
		}
	} else {
		kfb_actx.tr0   = (u8)tr0;
		kfb_actx.nb    = nb;
		kfb_actx.w     = w;
		kfb_actx.c0    = c0;
		kfb_actx.trmax = 3U + PF_ROWS;
		kfb_alloc();
	}
#ifdef KFB_PROFILE
	{ u8 dd; when_t pb = now(); prof[0] += est_alloc; prof[1] += cpu_lines_between(pa, pb, &dd); } }
#endif

	/* stream the whole pose (one SPI burst) */
	{
		u8  p   = (u8)vx & 1U;
		s8  off = p ? kfb_md.off1 : kfb_md.off0;
		u16 a   = p ? kfb_md.a1 : kfb_md.a0;
		s16 xb  = flip ? ((vx + 14 - off) >> 1) : ((vx + off) >> 1);
		kfb_ctx.rows    = rows;
		kfb_ctx.line4   = ((u8)ytop & 7U) << 2;
		kfb_ctx.xbase   = (u8)(xb - (s16)c0 * 4);
		kfb_ctx.flip    = flip;
		kfb_ctx.ptr     = (u16)(&kfb_grid[0]);
		kfb_ctx.stride2 = w * 2U;
		kfb_ctx.dmax    = w * 4U - 1U;
		kfb_safe(est_stream + 1U);
#ifdef KFB_PROFILE
		{ when_t pa = now();
#endif
		kfs_stream(kfb_md.bank, a);
		kfb_rows();
		kfs_end();
#ifdef KFB_PROFILE
		{ u8 dd; when_t pb = now(); prof[2] += est_stream; prof[3] += cpu_lines_between(pa, pb, &dd); prof[4]++; } }
#endif
		blit_count++;
	}
}

/* ---- game logic -------------------------------------------------------------------- */

#ifndef KFM_GAME   /* benchmark only */
static void grip_spawn(u8 i, s16 tx)
{
	grip_t* g = &grip[i];
	g->right = i & 1U;
	g->kind  = (i >> 1) & 1U;
	g->x     = g->right ? (cam + SCREEN_W + 8 + 24 * (i >> 1)) : (cam - 24 - 24 * (i >> 1));
	g->state = 1U;
	g->anim  = i * 3U;
	g->timer = 0U;
	(void)tx;
}
#endif

#ifndef KFM_GAME   /* benchmark only */
static void update(u16 pad)
{
	u8 i;
	s16 tx;
	s8 dir = 0;

	{	/* held by a gripper: Thomas can't walk (as in the arcade) */
		u8 k;
		held = 0U;
		for (k = 0U; k < NGRIP && k < bench_ngrip; k++) { if (grip[k].state == 2U) { held = 1U; } }
	}
	if (held) {
		dir = 0;
	} else if (bench_auto) {
		if (facing_left) { dir = -1; if (cam <= 0) { facing_left = 0U; } }
		else             { dir =  1; if (cam >= CAM_MAX) { facing_left = 1U; } }
	} else {
		if (pad & BTN_LEFT)  { dir = -1; facing_left = 1U; }
		if (pad & BTN_RIGHT) { dir =  1; facing_left = 0U; }
	}
	if (dir != 0) {
		sub = (sub + 1U) & 3U;                     /* 1.25 px a frame, as the arcade */
		cam += dir * ((sub == 0U) ? 2 : 1);
		if (cam < 0) { cam = 0; }
		if (cam > CAM_MAX) { cam = CAM_MAX; }
		walk_phase++;
		if (walk_phase >= KFM_THOMAS_SEQ_N) { walk_phase = 0U; }
	}
	tx = cam + THOMAS_X;

	for (i = 0U; i < NGRIP; i++) {
		grip_t* g = &grip[i];
		if (i >= bench_ngrip) { g->state = 0U; continue; }
		if (g->state == 0U) { grip_spawn(i, tx); }
		if (g->state == 1U) {
			s16 target = g->right ? (tx + 8 + 4 * (i >> 1)) : (tx - 8 - 4 * (i >> 1));
			g->anim++;
			if (g->right) { g->x--; if (g->x <= target) { g->x = target; g->state = 2U; } }
			else          { g->x++; if (g->x >= target) { g->x = target; g->state = 2U; } }
			/* a gripper left far behind comes again (it used to be parked off screen,
			** where they piled up and all walked in together when Thomas turned) */
			if (g->x < cam - 64 || g->x > cam + SCREEN_W + 48) { g->state = 0U; }
		} else {
			/* holding: stay on Thomas, let go after a while and come again */
			g->x = g->right ? (tx + 8 + 4 * (i >> 1)) : (tx - 8 - 4 * (i >> 1));
			if (++g->timer >= 150U) { g->state = 0U; }
		}
	}
}
#endif

#ifndef KFM_GAME
static void draw_grips_from(u8 first);
#endif

#ifndef KFM_GAME   /* benchmark only */
static void draw(void)
{
	u8 pose;
	blit_count = 0U;
	if (!bench_draw) { return; }
	pose = pgm_read_byte(&kfm_thomas_seq[walk_phase]);
	if (bench_thomas) draw_pose((facing_left ? KFM_POSE_THOMAS_L : KFM_POSE_THOMAS_R) + pose, THOMAS_X, THOMAS_Y);
	/* Draw order is also the stacking order (later on top), so it must stay fixed while
	** everything fits: rotating it every frame made overlapping grippers swap places and
	** flicker. Priority rotates only after an update that had to leave poses out. */
	{
		static u8  drop_last;
		static u16 d0;
		if (drop_last) { rot = (rot + 1U) % NGRIP; }
		d0 = kfm_dropped;
		draw_grips_from(rot);
		drop_last = (kfm_dropped != d0) ? 1U : 0U;
	}
}
#endif

#ifndef KFM_GAME   /* benchmark only */
static void draw_grips_from(u8 first)
{
	u8 i;
	for (i = 0U; i < NGRIP; i++) {
		grip_t* g = &grip[(i + first) % NGRIP];
		u8 p;
		if (g->state == 0U) { continue; }
		if (g->state == 2U) { p = 6U; }
		else { p = (g->kind ? 3U : 0U) + (u8)((g->anim >> 3) % 3U); }
		draw_pose((g->right ? KFM_POSE_GRIP_M : KFM_POSE_GRIP) + p, g->x - cam, GRIP_Y);
	}
}
#endif

/* ---- timing ---------------------------------------------------------------------------- */

/* Scanline positions within a field: L = 0 at the vertical sync, the display covers
** L = DISP_A .. DISP_B - 1, the main program only runs outside it. */
#define FIELD_LINES 262U
#define DISP_A      (VSYNC_LINES + FIRST_LINE)
#define DISP_B      (DISP_A + KFM_LINES)

static volatile u8 vcount;
static void on_vsync(void) { vcount++; }


static when_t now(void)
{
	/* no cli: on real hardware even a short interrupt hold-off jitters the sync pulses.
	** Re-read until the counters are consistent (they change once a scanline). */
	when_t w;
	u8 v, ph, pu;
	do {
		v  = vcount;
		ph = sync_phase;
		pu = sync_pulse;
	} while (v != vcount || pu != sync_pulse || ph != sync_phase);
	w.v = v;
	w.l = (ph == 0U) ? (u16)((18U - pu) >> 1) : (u16)(VSYNC_LINES + (HSYNC_LINES - pu));
	return w;
}

/* CPU scanlines between two moments: elapsed lines minus the displayed ones */
static u16 cpu_lines_between(when_t a, when_t b, u8* displays)
{
	u8  dv = b.v - a.v;
	s32 A = a.l, B = (s32)dv * FIELD_LINES + b.l;
	s32 n = B - A;
	u8  k;
	*displays = 0U;
	for (k = 0U; k <= dv; k++) {
		s32 d0 = (s32)k * FIELD_LINES + DISP_A, d1 = (s32)k * FIELD_LINES + DISP_B;
		s32 lo = (A > d0) ? A : d0, hi = (B < d1) ? B : d1;
		if (hi > lo) { n -= hi - lo; (*displays)++; }
	}
	return (n < 0) ? 0U : (u16)n;
}

static bool post_display(void)
{
	return (sync_phase != 0U) && (sync_pulse < RENDER_END);
}

/* blank lines left before the next display starts */
static u8 lines_left(void)
{
	u8 ph = sync_phase, pu = sync_pulse, n;
	if (ph == 0U) { n = (pu >> 1) + FIRST_LINE; }
	else if (pu >= RENDER_TOP) { n = pu - RENDER_TOP; }
	else if (pu < RENDER_END) { n = pu + VSYNC_LINES + FIRST_LINE; }
	else { n = 0U; }
	return (n > SAFETY_LINES) ? (n - SAFETY_LINES) : 0U;
}

/* ---- controls and on-screen statistics ------------------------------------------- */
/*
 * Select: grippers 0-6        Start: autoplay on / off (off: Left / Right walk)
 * A: blitter: Mode 748 -> custom with C allocator -> custom with kfb_alloc
 * B: HUD page: statistics -> diagnostics -> arcade panel     X: deadline mode on / off
 * Settings are kept in EEPROM block 0x4B46 (if the EEPROM is formatted) with a boot counter.
 *
 * Diagnostics page:
 *   BOOT nnnnn SPI eeee Ppp   boots so far, SPI RAM self-check errors, self-check passes
 *   BAD nnnnn MCUSR xx EE OK  pose rows rejected for bad data, reset flags, EEPROM usable
 *
 * Statistics, once a second:
 *   CPU aaa/bbb MAX ccc Udd   average / available scanlines of CPU per update (waits for
 *                             the display excluded), the worst
 *                             in that second, updates per second
 *   RT ee/ff MISS ggg Gh ijk  RAM tiles used by the last update / available, cells
 *                             left undrawn for lack of RAM tiles (Mode 748: updates that
 *                             ran out), grippers, blitter (M / C / A), P autoplay or
 *                             J joypad, D deadline mode
 */
#define KFB_DEADLINE_LINES 16U

#ifndef KFM_GAME
static u16 pad_prev;
#endif
static u8  hud_stats = 1U;   /* HUD page: 0 arcade panel, 1 statistics, 2 diagnostics */
static u8  sec_v0, sec_updates, sec_max, hud_dirty;
static u16 sec_cpu, sec_miss0;
static u8  shown_avg, shown_max, shown_ups;
static u16 shown_miss;

#ifndef KFM_GAME   /* benchmark only */
static void controls(u16 pad)
{
	u16 hit = pad & ~pad_prev;
	pad_prev = pad;
	if (hit & BTN_SELECT) { bench_ngrip = (bench_ngrip + 1U) % (NGRIP + 1U); hud_dirty = 1U; }
	if (hit & BTN_START)  { bench_auto ^= 1U; hud_dirty = 1U; }
	if (hit & BTN_A) {
		if (bench_engine == 0U)      { bench_engine = 1U; bench_calloc = 1U; }
		else if (bench_calloc != 0U) { bench_calloc = 0U; }
		else                         { bench_engine = 0U; }
		hud_dirty = 1U;
	}
	if (hit & BTN_X) { bench_deadline = (bench_deadline != 0U) ? 0U : KFB_DEADLINE_LINES; hud_dirty = 1U; }
	if (hit & BTN_B) {
		u8 i;
		hud_stats = (hud_stats + 1U) % 3U;
		for (i = 0U; i < 2U; i++) { hudrow[i].bg_addr = KFM_SPI_HUD + 32U * (i + 2U * hud_stats); }
		hud_dirty = 1U;
	}
	if (hit & (BTN_SELECT | BTN_START | BTN_A | BTN_X)) { ee_timer = 120U; }   /* save 2 s after the last change */
}
#endif

static u8 font_tile(char ch, u8 cyan)
{
	u8 i;
	if (ch >= '0' && ch <= '9')      { i = 1U + (u8)(ch - '0'); }
	else if (ch >= 'A' && ch <= 'Z') { i = 11U + (u8)(ch - 'A'); }
	else if (ch == '/')              { i = 37U; }
	else if (ch == '-')              { i = 38U; }
	else if (ch == '.')              { i = 39U; }
	else                             { i = 0U; }
	return pgm_read_byte(&kfm_font[(cyan ? KFM_FONT_N : 0U) + i]);
}

static void hud_text_P(const char* t)        /* red labels, text in flash */
{
	char ch;
	while ((ch = (char)pgm_read_byte(t++)) != 0) { SpiRamSeqWriteU8(font_tile(ch, 0U)); }
}
#define hud_text(s) hud_text_P(PSTR(s))

static void hud_num(u16 v, u8 digits)        /* cyan, zero padded */
{
	char d[5];
	u8 i;
	for (i = 0U; i < digits; i++) { d[digits - 1U - i] = (char)('0' + (v % 10U)); v /= 10U; }
	for (i = 0U; i < digits; i++) { SpiRamSeqWriteU8(font_tile(d[i], 1U)); }
}

static void hud_char(char ch) { SpiRamSeqWriteU8(font_tile(ch, 1U)); }

static void hud_write(void)
{
	SpiRamSeqWriteStart(0U, KFM_SPI_HUD + 64U);
	hud_text("CPU ");  hud_num(shown_avg, 3U); hud_char('/'); hud_num(VBLANK_LINES, 3U);
	hud_text(" MAX "); hud_num(shown_max, 3U);
	hud_text(" U");    hud_num(shown_ups, 2U); hud_text(" ");
	SpiRamSeqWriteEnd();
	SpiRamSeqWriteStart(0U, KFM_SPI_HUD + 96U);
	hud_text("RT ");    hud_num(kfm_stats.rt_used, 2U); hud_char('/'); hud_num(kfm_stats.rtmax, 2U);
	hud_text(" MISS "); hud_num((shown_miss > 999U) ? 999U : shown_miss, 3U);
	hud_text(" G");     hud_num(bench_ngrip, 1U); hud_text(" ");
	hud_char((bench_engine == 0U) ? 'M' : ((bench_calloc != 0U) ? 'C' : 'A'));
	hud_char(bench_auto ? 'P' : 'J');
	hud_char((bench_deadline != 0U) ? 'D' : ' ');
	SpiRamSeqWriteEnd();
}

static void hud_hex(u8 v)
{
	u8 h = v >> 4, l = v & 15U;
	hud_char((char)((h < 10U) ? ('0' + h) : ('A' + h - 10U)));
	hud_char((char)((l < 10U) ? ('0' + l) : ('A' + l - 10U)));
}

extern volatile u16 kfb_bad;

static void hud_write_diag(void)
{
	SpiRamSeqWriteStart(0U, KFM_SPI_HUD + 128U);
	hud_text("BOOT "); if (ee_ok) { hud_num(boots % 10000U, 4U); } else { hud_text("----"); }
	if (last_ok) {        /* where the last run's per-line sound routine last was */
		hud_text(" Z"); hud_hex(last_zl);
		hud_text(" P"); hud_num(last_pulse, 3U);
		hud_text(" L"); hud_num((last_lines > 9999U) ? 9999U : last_lines, 4U);
	} else {
		hud_text(" SPI"); hud_num((chk_bad > 999U) ? 999U : chk_bad, 3U);
#ifdef KFM_GAME
		hud_text(" @");   hud_hex((u8)(fix_addr >> 8)); hud_hex((u8)fix_addr);
#else
		hud_text("      ");
#endif
		hud_text("   ");
	}
	SpiRamSeqWriteEnd();
	SpiRamSeqWriteStart(0U, KFM_SPI_HUD + 160U);
	hud_text("STK");   hud_num(stk_free(), 3U);
	if (last_ok) {        /* the run before the reset: its phase, seconds, and stack low point (S) */
		hud_text(" P"); hud_num(last_phase, 1U);
		hud_text(" T"); hud_num((last_secs > 9999U) ? 9999U : last_secs, 4U);
	} else {
		hud_text(" NONE   ");
	}
	hud_text(" I"); hud_num((bad_irqs > 99U) ? 99U : bad_irqs, 2U);   /* unexpected interrupts this run */
	if (last_ok) { hud_text(" S"); hud_num((last_stk > 99U) ? 99U : last_stk, 2U); }
	(void)mcusr_boot;
	SpiRamSeqWriteEnd();
}

/* after each update: per-second figures, and the HUD when there is time before the display */
static void stats_tick(u8 cpu, u8 vnow)
{
	u8 f;
	sec_updates++;
	sec_cpu += cpu;
	if (cpu > sec_max) { sec_max = cpu; }
	f = (u8)(vnow - sec_v0);
	if (f >= 60U) {
		u16 miss = bench_engine ? kfb_rt_drop : kfm_stats.rt_full;
		shown_avg  = (u8)(sec_cpu / sec_updates);
		shown_max  = sec_max;
		shown_ups  = (u8)(((u16)sec_updates * 60U + f / 2U) / f);
		shown_miss = miss - sec_miss0;
		sec_miss0  = miss;
		sec_v0 = vnow;
		sec_updates = 0U;
		sec_cpu = 0U;
		sec_max = 0U;
		hud_dirty = 1U;
	}
	if (hud_dirty && hud_stats != 0U && lines_left() >= 6U) {
		if (hud_stats == 1U) { hud_write(); } else { hud_write_diag(); }
		hud_dirty = 0U;
	}
#ifndef KFG_NO_CHK
#ifdef KFM_DEBUG
	if (lines_left() >= 5U) { PHASE(7); chk_step(); PHASE(4); }   /* the SPI RAM self-check */
#endif
#endif
	if (ee_timer != 0U && --ee_timer == 0U) { ee_save(); }   /* blocks ~0.1 s: only after changes */
}

#ifdef KFM_SOUND
/* the mixer's encoded-data reads (at most 32 bytes): only from the foreground, in blank time */
static void kfm_mix_read(uint32_t pos, uint8_t* d, uint8_t n)
{
	SpiRamSeqReadStart((u8)(pos >> 16), (u16)pos);
	while (n--) { *d++ = SpiRamSeqReadU8(); }
	SpiRamSeqReadEnd();
}

/* the floor's music: its stream (KFA_MUSIC_POS) gives each frame's changes to the generator
** in update_sound: the melody's phase step and level, the drums' level */
extern volatile uint16_t mus_step, mus_step2;
extern volatile uint8_t  mus_tvol, mus_dvol, mus_tvol2;
static u16 mus_pos;
static u8  mus_on;

static u8  mus_vc;                 /* the vertical sync count the music has been played to */
static u16 mus_base;               /* the stream playing: the floor's music (loops) or the jingle (once) */
static u8  mus_loop;
static u8  mus_ramp;               /* fade-in (KFM_FADE only): the levels rise over the first 32 frames */
static u8  mus_tv, mus_dv, mus_tv2; /* the stream's levels, before the fade-in */
#ifdef KFM_FADE
#define MUS_RAMP0 0U               /* (make EXTRA=-DKFM_FADE: the music fades in over half a second) */
#else
#define MUS_RAMP0 32U              /* the music starts at full level, as the arcade's */
#endif
#ifdef KFM_NO_MUSIC
static void mus_play(u16 pos, u8 loop) { (void)pos; (void)loop; }   /* bisection build: effects only */
#else
static void mus_play(u16 pos, u8 loop) { mus_base = mus_pos = pos; mus_loop = loop; mus_on = 1U; mus_ramp = MUS_RAMP0; mus_tv = mus_dv = mus_tv2 = 0U; mus_vc = (u8)((u8)vcount - 1U); }
#endif
static void mus_start(void) { mus_play((u16)KFA_MUSIC_POS, 1U); }
static void mus_stop(void)  { mus_on = 0U; mus_tvol = 0U; mus_dvol = 0U; mus_tvol2 = 0U; }

/* The vertical sync, or the 6 lines before it: the sound's foreground work stays out of it. Every
** reset seen on an Omega (sound builds only) came at its first pulse with this work under way. */
static u8 near_vsync(void)
{
	u8 ph = sync_phase, pu = sync_pulse;
	return (ph == 0U) || (pu < 6U) ? 1U : 0U;
}

static void mus_record(void)          /* one frame's record of the music stream */
{
	u8 f, n = 1U;
	SpiRamSeqReadStart(0U, mus_pos);
	f = SpiRamSeqReadU8();
	if (f > 31U && f != KFA_MUSIC_END) {                 /* not music data: silence, never noise */
		SpiRamSeqReadEnd(); mus_stop(); return;
	}
	if (f == KFA_MUSIC_END) {                            /* the end: from the start, or stop */
		SpiRamSeqReadEnd();
		if (!mus_loop) { mus_stop(); return; }
		mus_pos = mus_base;
		SpiRamSeqReadStart(0U, mus_pos);
		f = SpiRamSeqReadU8();
	}
	if (f & 1U) { u8 lo = SpiRamSeqReadU8(); mus_step = (u16)lo | ((u16)SpiRamSeqReadU8() << 8); n += 2U; }
	/* the levels (KFM_VOLUME), capped at the tune's own maximum (30 and 28 at full) */
	if (f & 2U) { u8 v = SpiRamSeqReadU8(); if (v > 30U) { v = 30U; } mus_tv = v >> (2 - KFM_VOLUME); n++; }
	if (f & 4U) { u8 v = SpiRamSeqReadU8(); if (v > 28U) { v = 28U; } mus_dv = v >> (2 - KFM_VOLUME); n++; }
	if (f & 8U) { u8 lo = SpiRamSeqReadU8(); mus_step2 = (u16)lo | ((u16)SpiRamSeqReadU8() << 8); n += 2U; }
	if (f & 16U) { u8 v = SpiRamSeqReadU8(); if (v > 30U) { v = 30U; } mus_tv2 = v >> (2 - KFM_VOLUME); n++; }
	if (mus_ramp < 32U) { mus_ramp++; }
	mus_tvol = (u8)(((u16)mus_tv * mus_ramp) >> 5);
	mus_dvol = (u8)(((u16)mus_dv * mus_ramp) >> 5);
	mus_tvol2 = (u8)(((u16)mus_tv2 * mus_ramp) >> 5);
	SpiRamSeqReadEnd();
	mus_pos += n;
}

/* The music keeps its own time: the frames since the last record played are counted by the vertical
** sync (vcount), and as many records are played (up to 4: a game frame running late, an effect being
** decoded, does not hold a note or a drum's noise any more). Called once a frame, at its start, well
** clear of the display: a second call at the frame's end (just before the display) reset an Omega. */
static void mus_frame(void)
{
	u8 due;
	if (!mus_on) { return; }
	due = (u8)((u8)vcount - mus_vc);
	if (due > 4U) { mus_vc = (u8)((u8)vcount - 4U); due = 4U; }
	/* each record is one SPI RAM read: only with time to finish it well before the display (which
	** reads SPI RAM itself); what is left waits for the next call */
	while (due-- && mus_on && lines_left() >= 3U && !near_vsync()) { mus_record(); mus_vc++; }
}

/* The story borrows the sample queue's page for RAM tiles: it is silent (as the arcade's attract),
** and its scenes need more tiles than there are (the knife thrower's lower half went missing).
** The refill stops; once the queue is empty, its last byte is made a silent sample (128) and both
** indices 0, so the per-line routine (which repeats MixerSamples[R - 1] when empty) holds that byte,
** and the page's other 7 blocks (0x0E00-0x0EDF) extend the RAM tiles, which end at the page.
** At the story's end the tiles shrink back first, and the refill resumes a frame later.
** (Normal builds with sound: debug builds keep the crash log between the tiles and the page.) */
#if defined(KFM_SOUND) && !defined(KFM_DEBUG)
#define SND_BORROW 7U
static u8 snd_hold;                       /* 1 draining, 2 borrowed, 3 a frame before the refill */
static void snd_borrow_begin(void) { if (!snd_hold) { snd_hold = 1U; } }
static void snd_borrow_end(void)
{
	if (snd_hold) { m74_rtmax = kfm_stats.rtmax; snd_hold = 3U; }
}
static void snd_borrow_resume(void)      /* the story again: borrow once the effects have finished */
{
	if (!snd_hold && !MixerVoiceActive(0U) && !MixerVoiceActive(1U)) { snd_hold = 1U; }
}
static void snd_borrow_step(void)        /* once a frame */
{
	if (snd_hold == 1U && MixerReadIndex == MixerWriteIndex) {
		MixerSamples[0xFFU] = 128U;
		MixerWriteIndex = 0U;
		MixerReadIndex = 0U;
		m74_rtmax = kfm_stats.rtmax + SND_BORROW;
		snd_hold = 2U;
	} else if (snd_hold == 3U) {
		snd_hold = 0U;                      /* (the tiles shrank a frame ago: the page is free) */
	}
}
#else
#define snd_hold 0U
static void snd_borrow_begin(void) {}
static void snd_borrow_end(void) {}
static __attribute__((unused)) void snd_borrow_step(void) {}
static void snd_borrow_resume(void) {}
#endif

/* top up the sample queue, 32 samples at a time, while there is safe time before the display */
static void snd_fill(void)
{
	if (snd_hold) { return; }                /* (the story has the queue's page as RAM tiles) */
#ifdef KFM_NO_FILL
	return;                                  /* test build: no queue refill at all */
#endif
	while (lines_left() >= 4U && !near_vsync()) {
		if (MixerRenderAvailable(32U) < 32U) { break; }
	}
}
#endif

#ifdef KFM_GAME
static m74_mode0_vram_t* intro_row(u8 k) { return (k < 2U) ? &hudrow[k] : &pfrow[k - 2U]; }

static void intro_map(u16 map)             /* show a screen map on the intro's rows */
{
	u8 k;
	for (k = 0U; k < KFI_ROWS; k++) { intro_row(k)->bg_addr = map + 32U * k; }
}

/* Mode 748's row-table and row-selector pointers are 16-bit: written a byte at a time, so they are
** switched only between the end of the display and the vertical sync, when neither the display nor
** the sync's interrupt work reads them (a half-written pointer sends the display to the wrong rows). */
static void m74_switch(u16 vaddr, u16 rows)
{
	while (!(post_display() && sync_pulse >= 8U)) {}
	m74_vaddr = vaddr;
	m74_rows  = rows;
}

static void intro_enter(u16 map)
{
	u8 k;
	cam = 0;                                  /* (no scroll: the sprites are placed for xshift 0) */
	for (k = 0U; k < KFI_ROWS; k++) {
		m74_mode0_vram_t* r = intro_row(k);
		r->config = 0U;
		r->t0_addr_h = KFM_INTRO_TILES >> 8;
		r->t1_addr_h = (KFM_INTRO_TILES >> 8) + 0x10U;
	}
	intro_map(map);
	for (k = 0U; k < 16U; k++) { ((volatile u8*)M74_PAL_OFF)[k] = pgm_read_byte((const u8*)KFM_PAL_PF + k); }
	m74_switch((u16)(&vrow_intro[0]), (u16)(&rowsel_intro[0]));
	intro_on = 1U;
}

static void intro_leave(void)
{
	u8 k;
	m74_switch((u16)(&vrow_rom[0]), (u16)(&rowsel[0]));
	for (k = 0U; k < 16U; k++) { ((volatile u8*)M74_PAL_OFF)[k] = pgm_read_byte((const u8*)KFM_PAL_HUD + k); }
	for (k = 0U; k < 2U; k++) {
		hudrow[k].config = 0U;
		hudrow[k].bg_addr = KFM_SPI_HUD + 32U * (k + 2U);
		hudrow[k].t0_addr_h = KFM_HUD_TILES >> 8;
		hudrow[k].t1_addr_h = (KFM_HUD_TILES >> 8) + 0x10U;
	}
	for (k = 0U; k < PF_ROWS; k++) {
		u8 t = pgm_read_byte(&kfm_row_t0[k]);
		pfrow[k].t0_addr_h = t;
		pfrow[k].t1_addr_h = (u8)(t + 0x10U);
	}
	intro_on = 0U;
}

#include "game.c"
#endif

/* ---- main ------------------------------------------------------------------------------- */

int main(void)
{
	u8  i;
	u8* pal = (u8*)(m74_paddr);
	u16 rtstart;

	stk_prev = stk_free();             /* the previous run's paint, if it survived: its true lowest point */
	{	/* paint the free main stack for the watermark */
		volatile u8* q = (volatile u8*)STK_LO;
		u16 sp = (u16)SP;
		while ((u16)q < sp - 24U) { *q++ = 0xA5U; }
	}
	if (CRASH_LOG->magic == 0xC0DEU) {          /* the previous run's crash log */
		last_ok = 1U; last_phase = CRASH_LOG->phase; last_secs = CRASH_LOG->secs; last_stk = stk_prev;
		last_pad1 = CRASH_LOG->pad1; last_pad2 = CRASH_LOG->pad2;
		last_zl = CRASH_LOG->zl; last_pulse = CRASH_LOG->pulse; last_lines = CRASH_LOG->lines;
	}
	CRASH_LOG->magic = 0xC0DEU; CRASH_LOG->phase = 0U; CRASH_LOG->secs = 0U; CRASH_LOG->stk = 255U;
	CRASH_LOG->pad1 = 0U; CRASH_LOG->pad2 = 0U;
	mcusr_boot = MCUSR;                         /* reset cause (the bootloader may have cleared it) */
	UCSR0B = 0U; EIMSK = 0U; PCICR = 0U;        /* interrupts the game doesn't use: off (see BADISR_vect) */
	MCUSR = 0U;
	m74_config = 0U;
	SetRenderingParameters(FIRST_LINE, KFM_LINES);

	/* RAM tiles: from the end of .bss up to the palette buffer at 0x0F00 */
	rtstart = ((u16)(&__bss_end) + 31U) & ~31U;
	kfm_stats.rtbase = (u8)((rtstart - 0x100U) >> 5);
	kfm_stats.rtmax  = (u8)((KFM_SRAM_TOP - rtstart) >> 5);   /* above: the crash log (and the sound queue) */
	kfm_stats.vblank_lines = VBLANK_LINES;
	SetUserPostVsyncCallback(&on_vsync);
#ifdef KFM_SOUND
	MixerInit(&kfm_mix_read);
#endif
	for (i = 0U; i < KFB_MAXB * KFB_MAXW; i++) { kfb_grid[i] = (u16)kfb_scratch; }

	/* the blob must sit where gen/kfm_rom.h says (this reference also keeps it from being collected) */
	if ((u16)(&kfm_rom[0]) != KFM_ROM_BASE) { fail(7U, 0xC0U); }   /* blue */

	load_spiram((u8*)rtstart);      /* the RAM tile area serves as the sector buffer */
	ee_boot();
#ifdef KFM_GAME
	bench_engine = 1U; bench_calloc = 0U; bench_deadline = 16U;   /* the custom blitter, deadline mode */
	hud_stats = 0U;                                               /* the game's HUD (Select: statistics) */
#endif

	/* VRAM rows (the row table itself is in flash) */
	for (i = 0U; i < 2U; i++) {
		hudrow[i].config    = 0U;
		hudrow[i].bg_addr   = KFM_SPI_HUD + 32U * (i + 2U);   /* statistics page (B cycles pages) */
		hudrow[i].t0_addr_h = KFM_HUD_TILES >> 8;
		hudrow[i].t1_addr_h = (KFM_HUD_TILES >> 8) + 0x10U;
	}
	for (i = 0U; i < PF_ROWS; i++) {
		pfrow[i].t0_addr_h = pgm_read_byte(&kfm_row_t0[i]);
		pfrow[i].t1_addr_h = pgm_read_byte(&kfm_row_t0[i]) + 0x10U;
	}
	m74_rows   = (u16)(&rowsel[0]);
	m74_vaddr  = (u16)(&vrow_rom[0]);
	m74_rtbase = kfm_stats.rtbase;
	m74_rtmax  = kfm_stats.rtmax;
	for (i = 0U; i < 16U; i++) { pal[i] = pgm_read_byte(KFM_PAL_HUD + i); }   /* frame starts with the HUD's */

	bg_move(cam);
	m74_config = 0U;                     /* row table in flash (VramRestore reads this bit) */
	M74_VramRestore();
	m74_config = M74_CFG_ENABLE;
	WaitVsync(2U);

	while (1) {
		u16 pad;
		u16 cl;
		u8  late;
		u16 wait0, waited;
		when_t t0, t1;
		static u8 vlast;
		PHASE(6);
		while (!post_display()) {}           /* the display of this field is over */
		CRASH_LOG->lines = 0U;               /* (update_sound counts the lines since) */
		ClearVsyncFlag();
#ifdef KFM_SOUND
		PHASE(8);
		snd_borrow_step();
		mus_frame();                         /* the music's changes (first: the effects' decoding can be long) */
		snd_fill();                          /* refill what the display used (not near the vertical sync) */
#endif
		t0 = now();
		wait0 = kfb_wait_lines;

		pad = ReadJoypad(0U);
		CRASH_LOG->pad1 = pad;                   /* both controllers' buttons, for the crash log */
		CRASH_LOG->pad2 = ReadJoypad(1U);
		{
			const u16 sr = BTN_START | BTN_SELECT | BTN_Y | BTN_B;
			if (CRASH_LOG->pad1 == sr || CRASH_LOG->pad2 == sr) { if (combo_frames < 999U) { combo_frames++; } }
		}
		M74_VramRestore();
		kfb_rtno = 0U;
#ifdef KFM_GAME
		PHASE(1);
		game_update(pad);
		if (!intro_on) { bg_move(cam); vram_heal(); }
		PHASE(2);
		game_draw();
		PHASE(4);
#else
		controls(pad);
		update(pad);
		bg_move(cam);
		draw();
#endif
		{ u8 d; for (d = 0U; d < bench_delay; d++) { __builtin_avr_delay_cycles(10000UL); } }

		t1 = now();
		cl = cpu_lines_between(t0, t1, &late);
		kfm_stats.updates++;
		kfm_stats.fields += (u8)(t0.v - vlast);
		vlast = t0.v;
		kfm_stats.cpu_lines = (cl > 255U) ? 255U : (u8)cl;
		if (kfm_stats.cpu_lines > kfm_stats.cpu_max) { kfm_stats.cpu_max = kfm_stats.cpu_lines; }
		kfm_stats.cpu_sum += cl;
		kfm_stats.late = late;
		if (late != 0U) { kfm_stats.late_count++; }
		kfm_stats.blits = blit_count;
		if (blit_count > kfm_stats.blits_max) { kfm_stats.blits_max = blit_count; }
		waited = kfb_wait_lines - wait0;       /* lines spent waiting for a display to pass */
		stats_tick((waited >= cl) ? 0U : (u8)(((cl - waited) > 255U) ? 255U : (cl - waited)), t1.v);
#ifdef KFM_SOUND
		snd_fill();                          /* and top up before the next display */
#endif
#ifdef KFM_GAME
		PHASE(5);
		game_hud();
		if (++run_secs_sub >= 60U) {             /* seconds alive, stack low point: for the crash log */
			u8 f = stk_free();
			run_secs_sub = 0U;
			if (lines_left() >= 2U) { kfs_mode(); }   /* keep the SPI RAM in sequential mode (insurance) */
			CRASH_LOG->secs++;
			if (f < CRASH_LOG->stk) { CRASH_LOG->stk = f; }
		}
#endif
		{	/* RAM tiles in use: VRAM overlay bytes with bit 7 set (outside the timed part) */
			u8 r, c, n = 0U;
			for (r = 0U; r < PF_ROWS; r++) {
				for (c = 0U; c < 25U; c++) { if (pfrow[r].data[c] & 0x80U) { n++; } }
			}
			kfm_stats.rt_used = n;
			if (n > kfm_stats.rt_max) { kfm_stats.rt_max = n; }
			if (n >= kfm_stats.rtmax) { kfm_stats.rt_full++; }
		}

		while (post_display() && !GetVsyncFlag()) {}   /* leave this blank before waiting again */
	}
}

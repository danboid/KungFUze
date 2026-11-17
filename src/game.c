/*
 * Kung-Fu Master, Uzebox Mode 748: a playable first floor.
 *
 * Included into main.c when KFM_GAME is defined (make game); uses its renderer
 * (kfg_pose), timing, loader and HUD text routines.
 *
 * Controls: Left / Right walk (and turn), Down crouch, Up jump,
 *           B or Y punch, A or X kick, Start start / pause, Select: statistics HUD.
 *
 * Thomas's moves are the arcade's own animations, captured frame by frame from the
 * reference machine (tools/kfm_capture.py); each hits on the frame where it reaches
 * furthest. Enemies: grippers (grab and drain energy until shaken off with Left /
 * Right), knife throwers (high knives: crouch; low knives: jump), and the floor's
 * boss, the stick fighter, at the stairs.
 */

#include "kfm_game.h"

/* ---- tuning ------------------------------------------------------------------------ */

#define G_FLOOR       KFG_FLOOR_Y   /* logical line of the floor (feet) */
#define G_TX          THOMAS_X      /* Thomas's screen x (the camera follows him) */
#define G_ENERGY      40U
#define G_TIME        2000U
#define G_LIVES       3U
#define G_GRIP_MAX    4U            /* grippers on screen at once */
#define G_SLOTS       8U
#define G_BOSS_HP     16U
/* The boss, as the arcade's (watched in its emulation): he hovers out of reach of Thomas's
** crouching kick, 9 px beyond it, wandering about a 32 px band and feinting with his staff, and
** gives ground (more slowly than Thomas walks) when Thomas walks at him. Ducking and kicking, on
** the spot or cornering him, never reaches him: he comes in only to strike, a quick lunge started
** while Thomas isn't attacking and given up if Thomas attacks first, and reels back out of reach
** when hit. So Thomas wins by dodging the thrust and countering as he comes in. Each of his hits
** takes 16 of the arcade's 63 energy: here 10 of 40, so four hits empty the bar, as there.
** (Distances: dist(), anchor to anchor; Thomas's moves reach their kfg_moves reach + 20.) */
#define G_BOSS_NEAR   54            /* the crouching kick (reach 25) lands to 45 */
#define G_BOSS_BAND   32
#define G_BOSS_STRIKE 36            /* where a lunge stops to thrust: inside Thomas's kicks */
#define G_BOSS_REACH  40            /* his thrust lands to here */
#define G_BOSS_HIT    10U
#define G_BOSS_CAM    64            /* the boss appears from here: the room before the stairs */
#define G_STAIR_CAM   53            /* Thomas stops at the right edge of the stairs' bottom step */
#define G_WALL_X      (221 * 8)     /* the floor's right wall (its left edge): map column 221 */
#define G_TH_WALL     (G_WALL_X - 24 - (CAM_MAX + G_TX))   /* Thomas's furthest step past his screen
                                       spot, the camera at its end: 3 px short of the wall, as the arcade */

enum { T_NONE = 0, T_GRIP, T_KNIFER, T_KNIFE, T_BOSS, T_POP };
#ifndef KFM_SOUND
#define snd_borrow_begin() ((void)0)       /* (no sound: no queue page to borrow) */
#define snd_borrow_end()   ((void)0)
#define snd_borrow_resume() ((void)0)
#endif
enum { S_WALK = 0, S_HOLD, S_FALL, S_WAIT, S_THROW, S_ATTACK, S_DOWN, S_LEAVE };
enum { G_TITLE = 0, G_PLAY, G_DEAD, G_CLEAR, G_OVER, G_PAUSE, G_READY, G_LETTER };

typedef struct {
	u8  type;
	u8  state;
	s16 x;          /* world x of the pose anchor */
	s8  dir;        /* -1 moves / faces left, +1 right */
	u8  t;          /* state timer */
	u8  hp;
	s8  y;          /* knives: height above the floor */
} ent_t;

KFM_LEND static ent_t ent[G_SLOTS];   /* (after pfrow: lent to the story's RAM tiles, see src/main.c) */
static u8  g_mode = G_TITLE;
static u8  g_t;
static u32 g_score;
static u16 g_time;
static u8  g_tsub;
static u8  g_energy, g_boss_energy, g_lives;
static u8  g_boss_on;
static u8  g_spawn_t, g_knife_t;
static u8  g_hud_t, g_hud_page;
static u8  g_time_dirty;                  /* the time changed: its 4 digits are rewritten on their own */     /* 0: game HUD, 1: statistics */
static u16 g_rnd = 0xACE1U;
static u8  g_invincible;                  /* (INVINCIBLE=1 builds) Select on the title screen: no damage, the timer stops */
static u8  g_boss_kill;                   /* Thomas died in the boss fight: 1 the laugh is due, 2 playing */

/* Thomas */
static u8  th_move = 0xFFU;         /* KFG_MV_*, 0xFF: none */
static u8  th_frame;
static u8  th_walk;
static u8  th_hurt;                 /* flashing after a hit */
static u8  th_holders;              /* grippers holding him */
static u8  th_shake;                /* Left / Right alternations while held */
static s8  th_last_dir;
static u8  th_crouch;
static u8  th_hit_done;             /* this move has connected */
static s16 th_cx, th_cy;            /* Thomas's offset from his screen spot: > 0 walking to the right
                                       wall (the camera at its end); < 0 climbing the stairs; y in 1/8 */

#ifdef KFM_SOUND
/* sound effects: voice 0 Thomas (shout, hurt), voice 1 impacts and the knife */
#define SFX_GAIN (16U << KFM_VOLUME)       /* 64 full (the default), 32 half, 16 a quarter */
static __attribute__((unused)) void sfx_play(u8 voice, u32 pos, u32 bytes, u32 samples)
{
	MixerClip c;
	c.position = pos; c.bytes = bytes; c.samples = samples; c.accumulator = 128U; c.state = 0U;
	MixerPlay(voice, &c, SFX_GAIN);
}
#ifdef KFM_NO_SFX
#define SFX(v, n) do { } while (0)         /* bisection build: music only */
#else
#define SFX(v, n) sfx_play((v), KFA_##n##_POS, KFA_##n##_BYTES, KFA_##n##_SAMPLES)
#endif
#else
#define SFX(v, n) do { } while (0)
#define mus_start() do { } while (0)
#define mus_stop()  do { } while (0)
#define mus_play(p, l) do { } while (0)
#define KFA_JINGLE_POS 0
#define KFA_GAMEOVER_POS 0
#endif

static u8 rnd8(void)
{
	g_rnd = (g_rnd >> 1) ^ (-(g_rnd & 1U) & 0xB400U);
	return (u8)g_rnd;
}

/* ---- helpers -------------------------------------------------------------------------- */

static s16 th_x(void) { return cam + G_TX + th_cx; }   /* (th_cx: at the right wall, or climbing) */

static s8 th_yoff(void)
{
	if (th_move >= 0xFEU) { return 0; }
	return (s8)pgm_read_byte(&kfg_move_yoff[pgm_read_word(&kfg_moves[th_move].start) + th_frame]);
}

static bool th_low(void)          /* crouching: high knives and staff thrusts miss */
{
	return th_crouch || th_move == KFG_MV_CROUCH_KICK || th_move == KFG_MV_CROUCH_PUNCH;
}

static bool th_high(void)         /* in the air: low knives and sweeps miss */
{
	return (th_move == KFG_MV_JUMP || th_move == KFG_MV_JUMP_KICK) && th_yoff() < -10;
}

static void th_damage(u8 n)
{
	if (g_mode != G_PLAY || th_hurt || g_invincible) { return; }
	g_energy = (g_energy > n) ? g_energy - n : 0U;
	th_hurt = 24U;
	SFX(0, HURT);                                    /* hurt ($03 + $11, as the arcade) */
	g_hud_t = 0U;
}

static void add_score(u16 n) { g_score += n; g_hud_t = 0U; }

static ent_t* ent_new(u8 type)
{
	u8 i;
	for (i = 0U; i < G_SLOTS; i++) {
		if (ent[i].type == T_NONE) {
			ent_t* e = &ent[i];
			e->type = type; e->state = S_WALK; e->t = 0U; e->hp = 1U; e->y = 0;
			return e;
		}
	}
	return 0;
}

static u8 count(u8 type)
{
	u8 i, n = 0U;
	for (i = 0U; i < G_SLOTS; i++) { if (ent[i].type == type) { n++; } }
	return n;
}

/* the score for a hit, and its pop-up where the hit connected */
static void pop_score(const ent_t* at, u16 n)
{
	ent_t* p;
	s16 x = at->x;
	add_score(n);
	p = ent_new(T_POP);
	if (p) { p->x = x; p->hp = (n >= 800U) ? 8U : ((n >= 500U) ? 5U : ((n >= 200U) ? 2U : 1U)); p->dir = 1; }
}

/* distance from Thomas's centre to an entity's centre (> 0: to his right) */
static s16 dist(const ent_t* e) { return (e->x + 8) - (th_x() + 7); }

/* ---- the intro: the story (the title) and the letter ------------------------------------------- */

#define ST_MAX_OBJ 9U
static u16 st_pos, st_frame;
static u8  st_screen = 0xFFU;
static u8  st_typed[KFI_N_SCREENS];      /* characters shown on each screen */
static u8  st_want;                      /* this frame's count on the current screen */
static u8  st_n;
static u8  st_obj[ST_MAX_OBJ][4];        /* pose (bit 7: the intro's own), flip, x + 32, y */

static void story_start(void)
{
	st_pos = KFI_STREAM;
	st_frame = 0U;
	st_screen = 0xFFU;
	intro_enter(pgm_read_word(&kfi_screens[0].map), 1U);
}

/* type (or, as the story loops, erase) characters towards this frame's count, a few a frame */
static void story_type(void)
{
	u8 s, k;
	for (s = 0U; s < KFI_N_SCREENS; s++) {
		u8 want = (s == st_screen) ? st_want : 0U;
		u16 map = pgm_read_word(&kfi_screens[s].map), typ = pgm_read_word(&kfi_screens[s].typing);
		for (k = 0U; k < 8U && st_typed[s] != want && lines_left() >= 3U; k++) {
			u8 i = (st_typed[s] < want) ? st_typed[s] : (u8)(st_typed[s] - 1U);
			u16 off; u8 tile;
			SpiRamSeqReadStart(0U, typ + 3U * i);
			off = SpiRamSeqReadU8();
			off |= (u16)SpiRamSeqReadU8() << 8;
			tile = SpiRamSeqReadU8();
			SpiRamSeqReadEnd();
			if (st_typed[s] > want) { tile = pgm_read_byte(&kfi_screens[s].blank); }
			SpiRamSeqWriteStart(0U, map + off);
			SpiRamSeqWriteU8(tile);
			SpiRamSeqWriteEnd();
			if (st_typed[s] < want) { st_typed[s]++; } else { st_typed[s]--; }
		}
	}
}

static void story_frame(void)
{
	u8 i, j, sc;
	if (lines_left() < 3U) { return; }               /* (a frame late, at worst) */
	if (st_frame >= KFI_N_FRAMES) { st_pos = KFI_STREAM; st_frame = 0U; }
	SpiRamSeqReadStart(0U, st_pos);
	sc = SpiRamSeqReadU8();
	st_want = SpiRamSeqReadU8();
	st_n = SpiRamSeqReadU8();
	if (st_n > ST_MAX_OBJ) { st_n = ST_MAX_OBJ; }
	for (i = 0U; i < st_n; i++) { for (j = 0U; j < 4U; j++) { st_obj[i][j] = SpiRamSeqReadU8(); } }
	SpiRamSeqReadEnd();
	st_pos += 3U + 4U * (u16)st_n;
	st_frame++;
	if (sc != st_screen) { st_screen = sc; intro_map(pgm_read_word(&kfi_screens[sc].map)); }
	story_type();
}

static u8  g_demo;                       /* the attract mode: a demo game */
static u16 g_demo_t;

static void to_letter(void)                /* the letter (and the start jingle), then the game */
{
	snd_borrow_end();                      /* (the queue's page back from the story's RAM tiles) */
	SFX(0, COIN);                          /* the arcade's coin voice greets Start */
	st_want = 0U;
	st_n = 0U;
	if (!intro_on) { intro_enter(KFI_LETTER_MAP, 0U); } else { intro_to_letter(); }
	intro_map(KFI_LETTER_MAP);
	g_mode = G_LETTER; st_frame = 0U;
	mus_play((u16)KFA_JINGLE_POS, 0U);
}

/* the demo's player: walks one way (mostly left) for a random while, then attacks (a random move);
** attacks what comes near in front; shakes grippers off */
static u8  demo_walk_t;                    /* frames left of this walk */
static u16 demo_dir = BTN_LEFT;
static u8  demo_hold;                      /* frames to hold this attack's buttons */
static u16 demo_btn;

static u16 demo_attack(void)
{
	static const u16 moves[5] PROGMEM = { BTN_B, BTN_A, BTN_DOWN | BTN_B, BTN_DOWN | BTN_A, BTN_UP };
	u16 m = pgm_read_word(&moves[rnd8() % 5U]);
	return m;
}

static u16 demo_pad(void)
{
	u8 i;
	if (th_holders) { return ((g_demo_t >> 1) & 1U) ? BTN_LEFT : BTN_RIGHT; }
	if (demo_hold) { demo_hold--; return demo_btn; }
	if (th_move != 0xFFU) {                       /* (a jump becomes a flying kick near its top) */
		return (th_move == KFG_MV_JUMP && th_frame == 4U) ? BTN_A : 0U;
	}
	for (i = 0U; i < G_SLOTS; i++) {             /* something close in front: hit it */
		const ent_t* e = &ent[i];
		s16 d = dist(e) * (facing_left ? -1 : 1);
		if (e->type != T_NONE && e->type != T_POP && e->state != S_FALL && d > 0 && d < 34) {
			demo_btn = demo_attack(); demo_hold = 3U;
			return demo_btn;
		}
	}
	if (demo_walk_t) { demo_walk_t--; return demo_dir; }
	/* a walk is over: an attack, then the next walk (0.5-2.5 s, left three times in four) */
	demo_dir = (rnd8() < 192U) ? BTN_LEFT : BTN_RIGHT;
	demo_walk_t = 30U + (rnd8() % 120U);
	demo_btn = demo_attack(); demo_hold = 3U;
	return demo_btn;
}

static void story_draw(void)
{
	/* small objects (the knives: a cell or two) first, then the rest in the capture's order. Drawn
	** last, a knife lost its only cells whenever the RAM tiles ran out (it vanished beside Sylvia);
	** first, it costs the others at most those cells. (Largest-first was tried: it moved the whole
	** shortage onto Sylvia.) */
	u8 i, pass;
	for (pass = 0U; pass < 2U; pass++) {
		for (i = 0U; i < st_n; i++) {
			u8 ref = st_obj[i][0];
			const kfg_meta_t* mt = (ref & 0x80U) ? &kfi_meta[ref & 0x7FU] : &kfg_meta[ref];
			u8 small = (pgm_read_word(&mt->n0) <= 96U) ? 1U : 0U;
			if (small != (pass == 0U ? 1U : 0U)) { continue; }
			kfg_pose_m(mt, st_obj[i][1], (s16)st_obj[i][2] - 32, st_obj[i][3]);
		}
	}
}

/* ---- floor flow ------------------------------------------------------------------------ */

static void floor_start(void)
{
	u8 i;
	for (i = 0U; i < G_SLOTS; i++) { ent[i].type = T_NONE; }
	cam = CAM_START;
	facing_left = 1U;
	th_move = 0xFFU; th_hurt = 0U; th_holders = 0U; th_crouch = 0U;
	th_cx = 0; th_cy = 0;
	g_energy = G_ENERGY;
	g_boss_energy = G_BOSS_HP;
	g_boss_on = 0U;
	g_time = G_TIME;
	g_spawn_t = 90U;
	g_knife_t = 200U;
	g_hud_t = 0U;
	g_mode = G_READY;                  /* "1-PLAYER 1-FLOOR READY": Thomas walks in */
	g_t = 0U;
	mus_stop();
}

static void game_reset(void)
{
	g_score = 0U;
	g_lives = G_LIVES;
	floor_start();
}

/* ---- Thomas ------------------------------------------------------------------------------ */

static void th_die(void)
{
	u8 i;
	g_mode = G_DEAD; g_t = 0U; g_hud_t = 0U;
	mus_stop();                                         /* the arcade: $00 (stop all), then $07 */
	SFX(0, DEATH);
	g_boss_kill = g_boss_on;                            /* (the boss's laugh follows the cry) */
	th_move = KFG_MV_DEATH; th_frame = 0U; th_crouch = 0U; th_hurt = 0U;
	th_holders = 0U;
	for (i = 0U; i < G_SLOTS; i++) {                    /* as the arcade: they turn and walk off the screen */
		ent_t* e = &ent[i];
		if ((e->type == T_GRIP || e->type == T_KNIFER) && e->state != S_FALL) {
			e->state = S_LEAVE; e->t = 0U;
			e->dir = (dist(e) > 0) ? 1 : -1;              /* away from where Thomas fell */
		}
	}
}

static void th_start(u8 mv)
{
	if (mv != KFG_MV_CROUCH && mv != KFG_MV_JUMP) { SFX(0, KIAI); }   /* the shout ($02) */
	th_move = mv;
	th_frame = 0U;
	th_hit_done = 0U;
}

static bool th_is_punch(void)
{
	return th_move == KFG_MV_PUNCH || th_move == KFG_MV_CROUCH_PUNCH;
}

/* the moment a move connects: knock down what is in front and in reach */
static void th_hit_test(void)
{
	u8 i, reach = pgm_read_byte(&kfg_moves[th_move].reach);
	if (reach < 14U) { reach = 14U; }
	for (i = 0U; i < G_SLOTS; i++) {
		ent_t* e = &ent[i];
		s16 d = dist(e) * (facing_left ? -1 : 1);       /* > 0: in front */
		if (th_hit_done) { break; }
		if (e->type == T_NONE || e->type == T_KNIFE || e->type == T_POP || e->state == S_FALL || e->state == S_DOWN) { continue; }
		if (d <= 0 || d > (s16)reach + 20) { continue; }
		th_hit_done = 1U;
		SFX(1, IMPACT);                              /* the hit lands ($11) */
		if (e->type == T_BOSS) {
			if (g_boss_energy > 1U) { g_boss_energy--; e->x += facing_left ? -6 : 6; e->state = S_WAIT; e->t = 20U; }
			else {                                  /* defeated: knocked down off the screen */
				g_boss_energy = 0U; e->state = S_FALL; e->t = 0U; e->y = 0; e->hp = (u8)(s8)-3;
				e->dir = facing_left ? -1 : 1;
				add_score(2000U); g_mode = G_CLEAR; g_t = 0U; mus_stop();
			}
			add_score(100U);
			g_hud_t = 0U;
		} else {
			if (e->state == S_HOLD && th_holders) { th_holders--; }
			e->state = S_FALL;
			e->t = 0U;
			e->dir = facing_left ? -1 : 1;              /* knocked away from Thomas */
			e->y = 0;                                   /* and down off the screen */
			e->hp = (u8)(s8)-3;
			if (e->type == T_KNIFER) { pop_score(e, th_is_punch() ? 800U : 500U); }   /* as the arcade */
			else { pop_score(e, th_is_punch() ? 200U : 100U); }
		}
	}
}

static void th_update(u16 pad, u16 hit)
{
	s8 dir = 0;
	if (th_hurt) { th_hurt--; }

	/* held: no walking (Down still ducks, as the arcade); Left / Right alternations shake the grippers off */
	if (th_holders) {
		th_crouch = (pad & BTN_DOWN) ? 1U : 0U;
		s8 d = (pad & BTN_LEFT) ? -1 : ((pad & BTN_RIGHT) ? 1 : 0);
		if (d != 0 && d != th_last_dir) { th_last_dir = d; th_shake++; }
		if (th_shake >= 8U) {
			u8 i;
			for (i = 0U; i < G_SLOTS; i++) {
				if (ent[i].type == T_GRIP && ent[i].state == S_HOLD) {
					ent[i].state = S_FALL; ent[i].t = 0U; ent[i].dir = (dist(&ent[i]) > 0) ? 1 : -1;
					ent[i].y = 0; ent[i].hp = (u8)(s8)-3;
					pop_score(&ent[i], 100U);
				}
			}
			th_holders = 0U;
			th_shake = 0U;
		}
		th_move = 0xFFU;
		return;
	}
	th_shake = 0U;

	if (th_move != 0xFFU) {                               /* a move plays out */
		{	/* the attack is live for 4 frames from its furthest reach, and hits once */
			u8 h = pgm_read_byte(&kfg_moves[th_move].hit);
			if (!th_hit_done && th_frame >= h && th_frame <= h + 3U &&
			    th_move != KFG_MV_CROUCH && th_move != KFG_MV_JUMP) { th_hit_test(); }
		}
		if (th_move == KFG_MV_CROUCH) {                   /* hold the crouch while Down is held */
			if ((pad & BTN_DOWN) && th_frame >= 4U) { th_frame = 4U; }
		}
		if (++th_frame >= pgm_read_byte(&kfg_moves[th_move].len)) { th_move = 0xFFU; }
		/* the jump keeps its momentum; the others stand */
		if (th_move == KFG_MV_JUMP || th_move == KFG_MV_JUMP_KICK) {
			if (pad & BTN_LEFT) { dir = -1; } else if (pad & BTN_RIGHT) { dir = 1; }
		}
		if (th_move == KFG_MV_JUMP && (hit & (BTN_A | BTN_X | BTN_B | BTN_Y))) {
			/* the flying kick: its capture follows the jump's path until it kicks */
			const kfg_move_t* m = &kfg_moves[KFG_MV_JUMP_KICK];
			u8 pr = pgm_read_byte(&m->press), h = pgm_read_byte(&m->hit);
			if (th_frame < h) {
				th_move = KFG_MV_JUMP_KICK;
				SFX(0, KIAI);
				if (th_frame < pr) { th_frame = pr; }
				th_hit_done = 0U;
			}
		}
	} else {
		th_crouch = (pad & BTN_DOWN) ? 1U : 0U;
		if (hit & (BTN_B | BTN_Y)) { th_start(th_crouch ? KFG_MV_CROUCH_PUNCH : KFG_MV_PUNCH); }
		else if (hit & (BTN_A | BTN_X)) { th_start(th_crouch ? KFG_MV_CROUCH_KICK : KFG_MV_KICK); }
		else if (hit & BTN_UP) { th_start(KFG_MV_JUMP); th_crouch = 0U; }
		else if (!th_crouch) {
			if (pad & BTN_LEFT)  { dir = -1; facing_left = 1U; }
			if (pad & BTN_RIGHT) { dir =  1; facing_left = 0U; }
		}
	}
	if (dir != 0) {
		s8 step;
		sub = (sub + 1U) & 3U;
		step = (sub == 0U) ? 2 : 1;                   /* 1.25 px a frame, as the arcade */
		if (dir > 0) {
			if (cam < CAM_MAX) {                      /* the camera follows him to its end, */
				cam += step;
				if (cam > CAM_MAX) { th_cx += cam - CAM_MAX; cam = CAM_MAX; }
			} else {
				th_cx += step;                        /* then he walks across the screen to the wall */
			}
			if (th_cx > G_TH_WALL) { th_cx = G_TH_WALL; }
		} else {
			if (th_cx > 0) {                          /* back to his screen spot first, */
				th_cx -= step;
				if (th_cx < 0) { cam += th_cx; th_cx = 0; }
			} else {
				cam -= step;                          /* then the camera follows him */
			}
			if (cam < G_STAIR_CAM) { cam = G_STAIR_CAM; }    /* the stairs: climbed only after the boss */
		}
		if (th_move == 0xFFU) { if (++th_walk >= KFG_THOMAS_WALK_N) { th_walk = 0U; } }
	}
}

/* ---- enemies ------------------------------------------------------------------------------- */

static void spawn(void)
{
#ifdef KFG_NO_ENEMIES
	return;                                     /* bisection build: Thomas alone */
#endif
	/* newcomers walk in from just off the screen; while the floor's right wall is on the screen,
	** only from the left (as the arcade: none come out of the wall) */
	u8  wall = (cam + SCREEN_W > G_WALL_X) ? 1U : 0U;
	s16 front = (facing_left || wall) ? cam - 24 : cam + SCREEN_W + 8;
	s16 back  = (facing_left && !wall) ? cam + SCREEN_W + 8 : cam - 24;
	if (g_boss_on) { return; }
	if (cam <= G_BOSS_CAM) {                         /* the boss's room: he always gets a slot */
		u8 i;
		ent_t* e;
		for (i = 0U; i < G_SLOTS; i++) {            /* (all but grippers holding Thomas make way) */
			if (ent[i].type != T_NONE && !(ent[i].type == T_GRIP && ent[i].state == S_HOLD)) { ent[i].type = T_NONE; }
		}
		e = ent_new(T_BOSS);
		if (e) {
			e->x = cam - 30; e->dir = 1; e->hp = G_BOSS_HP;
			g_boss_on = 1U;
			g_hud_t = 0U;
		}
		return;
	}
	if (cam <= G_BOSS_CAM + 100) { return; }          /* no newcomers in the approach to the boss */
	if (g_spawn_t) { g_spawn_t--; }
	else if (count(T_GRIP) < G_GRIP_MAX) {
		ent_t* e = ent_new(T_GRIP);
		if (e) {
			u8 behind = 0U, i;
			for (i = 0U; i < G_SLOTS; i++) {
				if (ent[i].type == T_GRIP && ((facing_left && ent[i].x > th_x()) || (!facing_left && ent[i].x < th_x()))) { behind++; }
			}
			e->x = (rnd8() < 180U || behind >= 1U) ? front : back;
			e->x += (s16)(rnd8() & 15U) * ((e->x < th_x()) ? -1 : 1);
			e->dir = (e->x < th_x()) ? 1 : -1;
			e->hp = 3U + (rnd8() & 3U);                 /* speed, in 1/4 px a frame: 0.75 - 1.5 */
		}
		g_spawn_t = 40U + (rnd8() & 63U);
	}
	if (cam < 1300 && count(T_KNIFER) == 0U) {
		if (g_knife_t) { g_knife_t--; }
		else {
			ent_t* e = ent_new(T_KNIFER);
			if (e) { e->x = front; e->dir = (e->x < th_x()) ? 1 : -1; e->hp = 0U; }
			g_knife_t = 250U + rnd8();
		}
	}
}

static void ent_update(ent_t* e)
{
	s16 d = dist(e);
	e->t++;
	if (e->state == S_FALL && e->type == T_KNIFER) {  /* the arcade's death, frame by frame (drawn from */
		if (e->t >= KFG_KNIFE_DIE_N) { e->type = T_NONE; }   /* kfg_knife_die), then gone */
		return;
	}
	if (e->state == S_FALL) {                         /* knocked down: up a little, then down off the screen */
		s8 vy = (s8)e->hp;
		if (e->t & 1U) { e->x += e->dir; }
		e->y += vy;
		if ((e->t & 3U) == 0U && vy < 6) { e->hp = (u8)(vy + 1); }
		if (e->y > 70) { e->type = T_NONE; }
		return;
	}
	if (e->type == T_POP) {                           /* score pop-ups 3/4 s, the strike 1/5 s */
		if (e->t > ((e->hp == 0x0FU) ? 12U : 45U)) { e->type = T_NONE; }
		return;
	}
	if (e->state == S_LEAVE) {                        /* Thomas died: off the screen, then gone */
		if (e->t & 1U) { e->x += e->dir; }
		if (e->x < cam - 40 || e->x > cam + SCREEN_W + 24) { e->type = T_NONE; }
		return;
	}
	if (e->x < cam - 72 || e->x > cam + SCREEN_W + 56) { e->type = T_NONE; return; }   /* left behind */
	switch (e->type) {
	case T_GRIP:
		if (e->state == S_WALK) {
			e->dir = (d > 0) ? -1 : 1;
			e->y += e->hp;                              /* sub-pixel speed accumulator */
			while (e->y >= 4) { e->y -= 4; e->x += e->dir; }
			if (d > -10 && d < 10 && !th_high() && g_mode == G_PLAY) {
				e->state = S_HOLD; e->t = 0U; th_holders++;
			}
		} else if (e->state == S_HOLD) {
			u8 k, nth = 0U;                              /* stack holders beside one another */
			for (k = 0U; k < G_SLOTS && &ent[k] != e; k++) {
				if (ent[k].type == T_GRIP && ent[k].state == S_HOLD && ent[k].dir == e->dir) { nth++; }
			}
			e->x = th_x() + ((e->dir > 0) ? -(10 + 7 * nth) : (10 + 7 * nth));
			if ((e->t & 15U) == 0U && !g_invincible) { g_energy = (g_energy > 1U) ? g_energy - 1U : 0U; g_hud_t = 0U; }
		}
		break;
	case T_KNIFER:
		/* e->y: this throw is low (1) or high (0); e->hp: 1 while his knife is in flight */
		e->dir = (d > 0) ? -1 : 1;
		if (e->state == S_WALK) {
			if (d > 72 || d < -72) { if (e->t & 1U) { e->x += e->dir; } }
			else if (!e->hp && e->t > 24U) { e->state = S_THROW; e->t = 0U; e->y = (rnd8() & 1U) ? 1 : 0; }
		} else if (e->state == S_THROW) {
			/* as the arcade: stand (30 frames), raise the knife overhead / crouch (13),
			** release: the knife flies (frame 43), arm forward (15 more) */
			if (e->t == 43U) {
				ent_t* k = ent_new(T_KNIFE);
				if (k) {
					k->x = e->x + ((e->dir > 0) ? 16 : -8);
					k->dir = e->dir;
					k->y = e->y ? 29 : 59;                 /* the knife's top, above the floor */
					k->hp = (u8)(e - ent);                 /* whose knife */
					e->hp = 1U;
					SFX(1, THROW);                         /* the throw ($15) */
				}
			}
			if (e->t > 58U) { e->state = S_WALK; e->t = 0U; if (d < 40 && d > -40) { e->x -= e->dir * 8; } }
		}
		break;
	case T_KNIFE:
		e->x += e->dir * ((e->t % 3U == 0U) ? 3 : 2);     /* 2 1/3 px a frame, as the arcade */
		if (d > -8 && d < 8 && g_mode == G_PLAY) {
			bool miss = (e->y > 40) ? th_low() : th_high();   /* high: crouch under; low: jump over */
			if (!miss) {
				s8 h = e->y;
				if (ent[e->hp].type == T_KNIFER) { ent[e->hp].hp = 0U; }   /* the thrower may throw again */
				th_damage(6U); e->type = T_NONE;
				e = ent_new(T_POP);                          /* the red strike where it hit */
				if (e) { e->x = th_x() + 4; e->y = h; e->hp = 0x0FU; e->dir = 1; }
				return;
			}
		}
		if (e->type == T_NONE || e->x < cam - 16 || e->x > cam + SCREEN_W + 8) {
			e->type = T_NONE;
			if (ent[e->hp].type == T_KNIFER) { ent[e->hp].hp = 0U; }   /* the thrower may throw again */
		}
		break;
	case T_BOSS: {
		s16 ad = (d < 0) ? -d : d;
		bool back_ok, th_atk;
		if (e->state == S_DOWN) { return; }
		e->dir = (d > 0) ? -1 : 1;                       /* (towards Thomas) */
		back_ok = (e->dir > 0) ? (e->x > cam) : (e->x < cam + SCREEN_W - 24);   /* (not off the screen) */
		th_atk = th_move < KFG_N_MOVES && th_move != KFG_MV_CROUCH && th_move != KFG_MV_JUMP;
		if (e->state == S_WALK) {
			/* e->y: how far past G_BOSS_NEAR he means to stand, picked anew after each thrust */
			s16 want = G_BOSS_NEAR + e->y;
			if (e->t == 1U) { e->y = (s8)(rnd8() % G_BOSS_BAND); }
			if (ad < G_BOSS_NEAR) {                       /* Thomas closes in: he gives ground, slowly */
				if ((e->t & 1U) && back_ok) { e->x -= e->dir; }
			} else if (ad > want + 1) {
				if (e->t & 1U) { e->x += e->dir; }
			} else if (ad < want - 1 && back_ok) {
				if (e->t & 1U) { e->x -= e->dir; }
			}
			if (ad <= G_BOSS_REACH && e->t > 20U) {      /* Thomas walked into reach: a thrust */
				e->state = S_ATTACK; e->t = 0U; e->hp = (rnd8() & 1U) ? 1U : 2U;
			} else if (e->t > 60U + (rnd8() & 127U)) {    /* every 2 s or so: a lunge, or a feint */
				if (!th_atk && (rnd8() & 1U)) { e->state = S_HOLD; e->t = 0U; }
				else { e->state = S_ATTACK; e->t = 0U; e->hp = (rnd8() & 1U) ? 1U : 2U; }
			}
		} else if (e->state == S_HOLD) {                  /* the lunge: in fast, to thrust */
			if (th_atk && ad > G_BOSS_REACH) { e->state = S_WALK; e->t = 0U; }   /* Thomas struck first */
			else if (ad > G_BOSS_STRIKE) { e->x += 2 * e->dir; }
			else { e->state = S_ATTACK; e->t = 0U; e->hp = (rnd8() & 1U) ? 1U : 2U; }
			if (e->t > 50U) { e->state = S_WALK; e->t = 0U; }
		} else if (e->state == S_ATTACK) {
			if (e->t == 18U && ad <= G_BOSS_REACH && g_mode == G_PLAY) {
				bool miss = (e->hp == 1U) ? th_low() : th_high();
				if (!miss) { th_damage(G_BOSS_HIT); }
			}
			if (e->t > 36U) { e->state = S_WALK; e->t = 0U; }
		} else if (e->state == S_WAIT) {                  /* hit: he reels back out of reach */
			if (back_ok) { e->x -= e->dir; }
			if (e->t > 20U) { e->state = S_WALK; e->t = 0U; }
		}
		break;
	}
	}
}

static __attribute__((unused)) void hud_page_set(u8 page)
{
	u8 k;
	g_hud_page = page;
	hud_stats = page;
	for (k = 0U; k < 2U; k++) { hudrow[k].bg_addr = KFM_SPI_HUD + 32U * (k + ((page == 2U) ? 4U : 2U)); }
	hud_dirty = 1U; g_hud_t = 0U;
}

/* ---- per frame ------------------------------------------------------------------------------- */

static u16 g_pad_prev, g_pad_prev2;

static void game_update(u16 pad)
{
	u16 hit = pad & ~g_pad_prev;
	u8 i;
	g_pad_prev = pad;
	if (g_demo) {                          /* the attract mode's demo game */
		if ((hit & BTN_START) || ++g_demo_t > 1800U || g_mode == G_DEAD || g_mode == G_OVER) {
			u8 start = (hit & BTN_START) ? 1U : 0U;
			g_demo = 0U; mus_stop();
			for (i = 0U; i < G_SLOTS; i++) { ent[i].type = T_NONE; }
			if (start) { to_letter(); } else { g_mode = G_TITLE; }
			return;
		}
		pad = demo_pad();
		hit = pad & ~g_pad_prev2;
		g_pad_prev2 = pad;
	}
	if (0) {
#ifdef KFM_INVINCIBLE
	} else if ((hit & BTN_SELECT) && (g_mode == G_TITLE || g_mode == G_OVER)) {
		g_invincible ^= 1U; g_hud_t = 0U;               /* title / game over: invincibility (a build option) */
		if (intro_on) { snd_borrow_end(); }           /* (the story's tiles hand the sound queue back) */
		if (g_invincible) { SFX(0, KIAI); } else { SFX(1, STEP); }   /* (heard: the HUD is not shown) */
#endif
#ifdef KFM_DEBUG
	} else if ((hit & BTN_SELECT) && !intro_on && g_mode != G_TITLE && g_mode != G_OVER) {
		hud_page_set((u8)((g_hud_page + 1U) % 3U));   /* in play: game HUD -> statistics -> diagnostics */
#endif                                                /* (not during the intro: its rows are the HUD's) */
	}

	switch (g_mode) {
	case G_TITLE:
#ifdef KFM_BOOT_DIAG
		/* test build: no intro; the game starts at once, on the diagnostics page (the crash log of
		** the run before a reset is on screen straight away) */
		if (intro_on) { intro_leave(); }
		game_reset(); bench_deadline = 16U;
		hud_page_set(2U);
		return;
#endif
		bench_deadline = 0U;                         /* the story: every sprite drawn (no flicker) */
		if (!intro_on) { story_start(); snd_borrow_begin(); }
		else { snd_borrow_resume(); }                /* (after an effect: Select) */
		story_frame();
		if (hit & BTN_START) { to_letter(); return; }        /* the letter, then the game */
		if (st_frame >= KFI_N_FRAMES) {                      /* the story once through: the demo */
			snd_borrow_end(); intro_leave(); game_reset(); bench_deadline = 16U;
			g_demo = 1U; g_demo_t = 0U; g_pad_prev2 = 0U;
		}
		return;
	case G_LETTER:
		st_screen = 0xFFU;
		story_type();                               /* (erases the story's text meanwhile) */
		if (++st_frame >= KFI_LETTER_FRAMES) { intro_leave(); mus_stop(); game_reset(); bench_deadline = 16U; }
		return;
	case G_OVER:                                    /* GAME OVER for 4 s, then the intro and attract mode */
		if (hit & BTN_START) { to_letter(); return; }
		if (++st_frame >= 326U) {                   /* (the arcade's game over: about 5.4 s) */
			for (i = 0U; i < G_SLOTS; i++) { ent[i].type = T_NONE; }
			mus_stop();
			g_mode = G_TITLE;
			return;
		}
		break;
	case G_PAUSE:
		if (hit & BTN_START) { g_mode = G_PLAY; g_hud_t = 0U; mus_start(); }
		return;
	case G_READY:
		/* as the arcade: Thomas walks left by himself (36 frames, a footstep every 6), then
		** waits; play and the floor's music begin 133 frames in */
		if (g_t < 36U) {
			facing_left = 1U;
			sub = (sub + 1U) & 3U;
			cam -= (sub == 0U) ? 2 : 1;
			if (cam < 0) { cam = 0; }
			if (++th_walk >= KFG_THOMAS_WALK_N) { th_walk = 0U; }
			if (g_t % 6U == 0U) { SFX(1, STEP); }
		}
		if (++g_t >= 133U) { g_mode = G_PLAY; g_hud_t = 0U; mus_start(); }
		break;
	case G_DEAD:
		if (g_boss_kill == 1U && g_t == 62U) { SFX(0, LAUGH); g_boss_kill = 2U; }   /* after the 1 s cry */
		if (th_move == KFG_MV_DEATH && th_frame + 1U < pgm_read_byte(&kfg_moves[KFG_MV_DEATH].len)) { th_frame++; }
		else { th_move = 0xFEU; }                       /* fallen off the screen */
		if (++g_t > ((g_boss_kill == 2U) ? 200U : 120U)) {   /* (the laugh's 2 s to finish) */
			g_boss_kill = 0U;
			if (g_lives > 1U) { g_lives--; floor_start(); }
			else { g_lives = 0U; g_mode = G_OVER; st_frame = 0U; mus_play((u16)KFA_GAMEOVER_POS, 0U); }
			g_hud_t = 0U;
		}
		break;
	case G_CLEAR:
		if (g_time >= 10U) { g_time -= 10U; add_score(10U); }   /* time bonus */
		g_time_dirty = 1U;
		if (g_t < 30U) { g_t++; break; }              /* a pause, then: */
		th_move = 0xFFU; th_crouch = 0U; facing_left = 1U;
		if (cam > G_STAIR_CAM) {                      /* to the bottom of the stairs, walking */
			cam--;
			if (++th_walk >= KFG_THOMAS_WALK_N) { th_walk = 0U; }
		} else if (th_cy > -8 * 170) {                /* up them: a pixel left, ~1.2 lines up a frame */
			th_cx--; th_cy -= 10;
			if (++th_walk >= KFG_THOMAS_WALK_N) { th_walk = 0U; }
		} else if (++g_t > 70U) {                     /* off the top: a moment, and the floor again */
			floor_start();
		}
		break;
	case G_PLAY:
		if (hit & BTN_START) { g_mode = G_PAUSE; g_hud_t = 0U; mus_stop(); return; }
		th_update(pad, hit);
		spawn();
		if (!g_invincible && ++g_tsub >= 3U) { g_tsub = 0U; if (g_time) { g_time--; } g_time_dirty = 1U; }
		if (g_energy == 0U || g_time == 0U) { th_die(); }
		break;
	}
	for (i = 0U; i < G_SLOTS; i++) { if (ent[i].type != T_NONE) { ent_update(&ent[i]); } }
}

/* flip: 1 = draw facing left (each pose's own facing is in kfg_native_left); Thomas's face left */
static void ent_draw(const ent_t* e)
{
	s16 sx = e->x - cam;
	u8 flip = (e->dir < 0) ? 1U : 0U;
	u8 p;
	switch (e->type) {
	case T_GRIP:
		if (e->state == S_FALL) { p = (e->t < 6U) ? KFG_GRIP_HIT : ((e->t < 16U) ? KFG_GRIP_FALL0 : KFG_GRIP_FALL1); flip ^= 1U; }
		else if (e->state == S_HOLD) { p = KFG_GRIP_HOLD; }
		else { static const u8 w[4] PROGMEM = { KFG_GRIP_WALK0, KFG_GRIP_WALK1, KFG_GRIP_WALK2, KFG_GRIP_WALK1 };
		       p = pgm_read_byte(&w[(e->t >> 3) & 3U]); }
		break;
	case T_KNIFER:
		if (e->state == S_FALL) {                   /* the arcade's death: captured falling to the left, */
			u8 k = (e->t < KFG_KNIFE_DIE_N) ? e->t : (u8)(KFG_KNIFE_DIE_N - 1U);   /* facing right */
			s8 dx = (s8)pgm_read_byte(&kfg_knife_die[k][1]);
			p = (u8)pgm_read_byte(&kfg_knife_die[k][0]);
			flip = ((e->dir < 0) ? 0U : 1U) ^ pgm_read_byte(&kfg_native_left[p]);
			kfg_pose(p, flip, sx + ((e->dir < 0) ? dx : -dx), G_FLOOR + (s8)pgm_read_byte(&kfg_knife_die[k][2]));
			return;
		}
		else if (e->state == S_THROW) {
			if (e->t < 30U)      { p = KFG_KNIFE_WALK1; }                                   /* stand */
			else if (e->t < 43U) { p = e->y ? KFG_KNIFE_THROW_LO0 : KFG_KNIFE_RAISE; }       /* crouch / knife overhead */
			else                 { p = e->y ? KFG_KNIFE_THROW_LO1 : KFG_KNIFE_THROW_HI; }    /* release */
		}
		else { static const u8 w[4] PROGMEM = { KFG_KNIFE_WALK0, KFG_KNIFE_WALK1, KFG_KNIFE_WALK2, KFG_KNIFE_WALK1 };
		       p = pgm_read_byte(&w[(e->t >> 3) & 3U]); }
		break;
	case T_KNIFE:
		kfg_pose(KFG_BLADE, flip, sx, G_FLOOR - e->y);
		return;
	case T_POP:
		if (e->hp == 0x0FU) { kfg_pose(KFG_STRIKE, 0U, sx, G_FLOOR - e->y); return; }
		kfg_pose((e->hp == 8U) ? KFG_POP_800 : ((e->hp == 5U) ? KFG_POP_500 : ((e->hp == 2U) ? KFG_POP_200 : KFG_POP_100)),
		         0U, sx, G_FLOOR - 64);
		return;
	case T_BOSS:
		if (e->state == S_DOWN || e->state == S_FALL) { p = KFG_BOSS_DOWN; if (e->state == S_FALL) { flip ^= 1U; } }
		else if (e->state == S_ATTACK) {
			if (e->hp == 1U) { p = (e->t < 18U) ? KFG_BOSS_RAISE : KFG_BOSS_THRUST_HI; }
			else { p = (e->t < 18U) ? KFG_BOSS_CROUCH0 : KFG_BOSS_THRUST_LO; }
		} else if (e->state == S_WAIT) { p = KFG_BOSS_CROUCH1; }
		else { static const u8 w[4] PROGMEM = { KFG_BOSS_WALK0, KFG_BOSS_WALK1, KFG_BOSS_WALK2, KFG_BOSS_WALK1 };
		       p = pgm_read_byte(&w[(e->t >> 3) & 3U]); }
		break;
	default:
		return;
	}
	/* the poses face either way: flip those that face the other way natively */
	flip ^= pgm_read_byte(&kfg_native_left[p]);
	kfg_pose(p, flip, sx, G_FLOOR + ((e->state == S_FALL) ? e->y : 0));
}

static void game_draw(void)
{
	u8 i, p;
	blit_count = 0U;
	if (g_mode == G_TITLE) { story_draw(); return; }
	if (g_mode == G_LETTER) { return; }
	/* Thomas first (the grippers hold on in front of him); PHASE 3: the enemies */
	if (!(th_hurt & 4U) && th_move != 0xFEU) {
		if (th_move != 0xFFU) {
			p = pgm_read_byte(&kfg_move_shape[pgm_read_word(&kfg_moves[th_move].start) + th_frame]);
		} else if (th_crouch) {
			const kfg_move_t* m = &kfg_moves[KFG_MV_CROUCH];
			p = pgm_read_byte(&kfg_move_shape[pgm_read_word(&m->start) + 4U]);
		} else {
			p = pgm_read_byte(&kfg_walk_seq[th_walk]);
		}
		if (th_move == KFG_MV_DEATH) {           /* the fall drifts sideways: each frame's own offset */
			s8 dx = (s8)pgm_read_byte(&kfg_death_x[th_frame]);
			kfg_pose(p, facing_left ? 0U : 1U, G_TX + th_cx + (facing_left ? dx : -dx), G_FLOOR + th_yoff());
		} else {
			kfg_pose(p, facing_left ? 0U : 1U, G_TX + th_cx, G_FLOOR + th_yoff() + (th_cy >> 3));
		}
	}
	PHASE(3);
	{	/* fixed order while everything fits; rotate the priority after a frame that dropped poses */
		static u8 drop_last, first;
		u16 d0 = kfm_dropped;
		if (drop_last) { first = (first + 1U) % G_SLOTS; }
		u8 pass;
		for (pass = 0U; pass < 2U; pass++) {          /* the knife throwers and the boss first: the */
			for (i = 0U; i < G_SLOTS; i++) {          /* largest sprites, the most RAM tiles (drawn */
				const ent_t* e = &ent[(i + first) % G_SLOTS];   /* last, their lower rows went missing) */
				u8 big = (e->type == T_KNIFER || e->type == T_BOSS) ? 1U : 0U;
				if (e->type != T_NONE && big == (pass == 0U ? 1U : 0U)) { ent_draw(e); }
			}
		}
		drop_last = (kfm_dropped != d0) ? 1U : 0U;
	}
}

/* ---- HUD: two rows, written into the HUD map in SPI RAM when something changed ----------------- */

static void hud_bar(u8 v, u8 boss)
{
	u8 i;
	u8 full = (v + 3U) >> 2;                       /* 10 segments of 4 */
	for (i = 0U; i < 10U; i++) {
		SpiRamSeqWriteU8((i < full) ? pgm_read_byte(&kfm_font[2U * KFM_FONT_N + boss]) : font_tile(' ', 0U));
	}
}

static void game_hud(void)
{
#ifdef KFG_NO_HUD
	return;                                     /* bisection build: no SPI RAM writes in play */
#endif
	if (g_hud_page != 0U) { return; }
	if (g_time_dirty && lines_left() >= 3U) {  /* the time, ticking as the arcade's: its digits only */
		g_time_dirty = 0U;                         /* (columns 15-18: "SC nnnnnn TIME nnnn") */
		SpiRamSeqWriteStart(0U, KFM_SPI_HUD + 64U + 15U);
		hud_num(g_time, 4U);
		SpiRamSeqWriteEnd();
	}
	if (g_hud_t) { g_hud_t--; return; }
	if (lines_left() < 6U) { return; }
	g_hud_t = 30U;
	SpiRamSeqWriteStart(0U, KFM_SPI_HUD + 64U);
	hud_text("SC "); hud_num((u16)(g_score / 100U), 4U); hud_num(0U, 2U);
	hud_text(" TIME "); hud_num(g_time, 4U);
	hud_text(" L"); hud_num(g_lives, 1U);
	if (g_invincible) { hud_text(" INV"); } else { hud_text("    "); }
	SpiRamSeqWriteEnd();
	SpiRamSeqWriteStart(0U, KFM_SPI_HUD + 96U);
	switch (g_mode) {
#ifdef KFM_INVINCIBLE
	case G_TITLE: if (g_invincible) { hud_text(" START  SELECT:INV ON  "); } else { hud_text(" START  SELECT:INV OFF "); } break;
#else
	case G_TITLE: hud_text("         START          "); break;
#endif
	case G_PAUSE: hud_text("         PAUSE          "); break;
	case G_READY: hud_text(" 1-PLAYER 1-FLOOR READY "); break;
	case G_OVER:  hud_text("       GAME  OVER       "); break;
	case G_CLEAR: hud_text("      FLOOR CLEAR       "); break;
	default:
		hud_text("P"); hud_bar(g_energy, 0U);
		hud_text(" E"); hud_bar(g_boss_on ? (u8)((u16)g_boss_energy * G_ENERGY / G_BOSS_HP) : 0U, 1U);
		hud_text(" ");
		break;
	}
	SpiRamSeqWriteEnd();
}

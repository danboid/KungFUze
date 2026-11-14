# Kung-Fu Master Uzebox port

A port of Irem's *Kung-Fu Master* (1984) to the Uzebox by Dan MacDonald using video mode 748 and the coding assistance of Claude Opus 5.5.

Kung-Fu Master requires 128 KB SPI RAM to run.

The graphics, moves and sound are taken from **your own** arcade ROM set at build time; no ROM data is included here.

## Building

You need the Uzebox repository (kernel, `packrom`), avr-gcc, Python 3 with NumPy and Pillow, and the MAME ROM set `kungfum.zip`. The set used in development has this SHA-256:

```
0a583b52503ce3aeef4698a0473bd5bc4c908503c4bbdb18886523a1da53ac22  kungfum.zip
```

```sh
make assets ROM=/path/to/kungfum.zip     # once (a few minutes): extracts, captures and builds the assets
make clean
make UZEBOX=/path/to/uzebox LINES=184 GAME=1 SOUND=1
```

Copy `_bin_/kungfum.uze` to the SD card: that one file is the whole game.

**The data is inside the `.uze`,** as in IK748 and stuntcar-uzebox:
- **The layout.** packrom writes its 512-byte header and the program; the Makefile pads that with zeros to a whole 512-byte sector and appends the game's data (`KFMG.DAT`, about 124 KB). The bootloader flashes only the program, whose size is in the header.
- **Loading.** At start-up the game opens its own `KUNGFUM.UZE`, checks the header's `UZEBOX` marker, skips the header and program to the next whole sector (113 at present), and loads the data into SPI RAM from there.
- **The checksum.** `tools/kfm_pack.py` stamps the data's length and its CRC-16 (CCITT/XMODEM) into the data's header (bytes 16–21). The game computes the same CRC while copying the data, in the time each byte takes to go out to SPI RAM, which costs about 0.1 s.
- **The fallback.** If `KUNGFUM.UZE` isn't on the card (renamed, say), the game looks for a separate `KFMG.DAT` instead (`_obj_/KFMG.DAT`, stamped). Keep the `.uze`'s name for the single-file game.

Error screens at start-up:

| Colour | Error | Meaning |
|---|---|---|
| Red | 1 | The SD card did not initialise |
| Red | 2 | Neither `KUNGFUM.UZE` nor `KFMG.DAT` was found |
| Yellow | 3 | Bad SPI RAM data (no `KFM7` marker) |
| Cyan | 4 | The data is from another asset build than the program |
| White | 5 | The data is damaged: its CRC-16 or length is wrong |
| Magenta | 6 | No SPI RAM answered |
| Blue | 7 | The graphics blob is not where the build put it (a build fault) |

| Option | Effect |
|---|---|
| `DEBUG=1` | The debugging aids: the statistics and diagnostics pages (Select in play), a crash log that survives a reset, the SPI RAM self-check. Without it the game has one more RAM tile. |
| `EXTRA="-DKFB_SPI_PAD=n"` | Extra cycles on each SPI RAM read (default 1; 0 gave drawing errors on an Omega) |
| `EXTRA="-DKFM_VOLUME=n"` | 0 quarter, 1 half (default), 2 full volume |
| `EXTRA="-DKFM_FADE"` | The music fades in over half a second |
| `C_MIXER=1` | The hand-off's C mixer instead of the assembly one (for comparison) |
| `QUEUE=0x0D00` | The sample queue's page (default 0x0E00) |
| Test builds | `STOCK_ISR=1`, `-DKFM_NO_MUSIC`, `-DKFM_NO_SFX`, `-DKFM_NO_FILL`, `-DKFM_BOOT_DIAG` (see below) |

## Controls

| Input | Action |
|---|---|
| Left / Right | Walk (and turn); shake off grippers |
| Down | Crouch |
| Up | Jump |
| B or Y | Punch |
| A or X | Kick |
| Start | Start a game / pause |
| Select | On the title or game-over screen: invincibility on / off (no damage, the timer stops; INV in the HUD). With `DEBUG=1`, in play: game HUD → statistics → diagnostics |

## Features

- **The intro:** the story typed out on two screens, the kidnapping played back from the arcade's own sprites, the attract demo after it, and on Start the coin voice, the letter screen with the start jingle, and the walk-on (READY).
- **Floor 1:** the scrolling floor with the arcade's tiles and palettes, and the HUD (score, time, energy bars, lives).
- **Thomas:** the arcade's animations, captured frame by frame. He walks, crouches, jumps, punches and kicks (standing, crouching, flying), shakes off grippers, and dies with the arcade's fall and cry.
- **The grippers** walk in from both sides, grab and drain Thomas's energy. While the floor's right wall is on the screen, enemies come only from the left, as in the arcade.
- **The right wall:** walking right at the start, the camera follows Thomas to its end, then he walks across the screen until he is up against the wall.
- **The knife throwers** throw high and low knives with the arcade's timing; a knife that hits leaves the arcade's red strike. Beaten, they die as in the arcade: hit, stagger, and fall backwards through the floor.
- **The boss**, the stick fighter, appears in front of the stairs, with his laugh when he kills Thomas. Defeated, he falls; Thomas walks to the stairs and climbs them, and the floor clears with a time bonus.
- **Scoring:** the arcade's points and pop-ups (100, 200, 500, 800), the timer ticking down, 3 lives, game over (with its jingle, then back to the intro) and pause.
- **When Thomas dies,** the enemies turn and walk off the screen.
- **Sound:** the arcade's sampled voices and effects, and the floor's music and jingles, rendered from the ROM's sound board.
- **Invincibility,** for practice.

Not yet: floors 2 to 5, with their tom-tom men, dragons, snakes and other bosses.

## Third-party code (`third_party/`)

| Directory | What it is | Licence | Used by |
|---|---|---|---|
| `stearcade/` | Eight files of STEArcade (AnimaInCorpore): its Z80, its M6801/M6803, and the Kung-Fu Master board and sound board models | GPL v2 or later | `tools/kfm_extract.py`, `tools/kfm_capture.py` and `tools/kfm_audio_build.py`, which run the user's ROM set on them |
| `kfa_mixer/` | The KungFuMasterAudio hand-off's 2-voice 2-bit ADPCM mixer and encoder, used with the author's permission | GPL v3 or later, through the Uzebox `dconvert` code it follows | The `SOUND=1` build and `tools/kfm_audio_build.py` |

Each directory has a `NOTICE.md` (origin, revision, files) and the licence text. The copies are unchanged; kfm748's adaptations are in `src/kfm_mixrt.c` and `src/kfm_sound.s`.

The bundled copies reproduce the earlier assets and firmware byte for byte. `STEARCADE=` and `MIXERS=` still accept other copies.

**ROM data is never included.** That covers the ROM set itself and everything generated from it under `build/` and `gen/`, including the sound clips.


## Playable floor 1 (`make GAME=1`)

The build is described under **Building** above. The benchmark build (without `GAME=1`) uses `KFM.DAT`.

| Input | Action |
|---|---|
| Left / Right | Walk (and turn) |
| Down | Crouch |
| Up | Jump |
| B or Y | Punch |
| A or X | Kick |
| Start | Start / pause |
| Select | On the title or game-over screen: invincibility on / off (no damage, the timer stops; INV in the HUD). In play: game HUD → statistics → diagnostics |

**Thomas.** His moves are the arcade's own animations, captured frame by frame by `tools/kfm_capture.py`:
- punch and kick, standing or crouching;
- jump, and jump with an attack.

Each move hits on the frame where it reaches furthest.

**Enemies:**
- **Grippers** walk in from both sides and grab. Each one holding Thomas drains his energy until he shakes them off with Left / Right.
- **Knife throwers** stop at a distance and throw high or low knives. Crouch under the high ones, jump over the low ones.
- **The boss**, the stick fighter, waits at the stairs, with high and low staff attacks and an energy bar.

**Rules.** Score, the timer, 3 lives, floor clear with a time bonus (the floor then starts again), game over and pause.

**Fixed after the first Omega test:**
- **Flying kick and crouching punch.**
  - The capture now presses the right buttons (0x80 kicks, 0x20 punches), records when each was pressed, and takes all of Thomas's sprites (some moves put parts of him in separate slots).
  - Crouch attacks start crouched and end back in the crouch.
  - The flying kick follows the jump's path.
  - Attacks are live for 4 frames and hit once.
- **Knocked-down enemies** pop up, then drop down off the screen, as in the arcade.
- **Enemies always face Thomas.** The capture records which side of Thomas each enemy pose was seen on, which gives each pose's own facing. The arcade's artwork is drawn both ways, so the flip bit doesn't tell.

**Score pop-ups.** "200" for a punch and "100" for a kick (crouching and flying versions included) appear where a hit connects, for 3/4 s. Attacks ignore pop-ups; that used to make them linger.

**Knife throwers** throw as the arcade does, as measured in the reference machine:
1. Stand for 30 frames.
2. Raise the knife overhead (or crouch, for a low throw) for 13 frames.
3. Release: the knife flies at 2⅓ px a frame, its top 59 (high) or 29 (low) pixels above the floor.

The flying knife is the arcade's code 1 in colour 1. Code 264 in colour 7 is the knife held overhead, part of the raised-knife pose. A new throw waits until the previous knife has gone.

**Points:**
- Grippers: punch 200, kick 100.
- Knife throwers: punch 800, kick 500.

The pop-ups come straight from the sprite ROM's pop-up strip (codes 752–760). They are the arcade's own sprites, captured as colour 12, and cost about a line of drawing time each.

**The build rejects** any chosen pose seen fewer than 10 times in the capture. Such a pose is usually two enemies merged into one object, as the knife thrower's old "aim" pose was.

## Sound effects (`make … GAME=1 SOUND=1`)

The sound effects use the KungFuMasterAudio hand-off's foreground 2-voice 2-bit ADPCM mixer, profile `pcm_adpcm2_2v`, bundled in `third_party/kfa_mixer`. The sounds themselves are made from the user's ROM set: `make assets` runs the sound ROMs on STEArcade's model of the Kung-Fu Master sound board. That model is bundled in `third_party/stearcade` and covers the M6803 program in a-3h, two AY-3-8910s and two MSM5205s, which play the ADPCM samples in a-3e and a-3f.
- **Voices** are the MSM5205 output, resampled to the line rate.
- **AY effects** are rendered from the board's timed register writes.

Checked against MAME recordings of the same commands, the voices match closely: loudness envelope 0.95–0.98, spectrum 0.98–0.99. The only difference is MAME's 40–50 ms lead-in.

```sh
make assets ROM=../kungfum.zip
make clean
make UZEBOX=~/src/uzebox LINES=184 GAME=1 SOUND=1
```

**What plays.** These are the sound commands the arcade sends in play, logged on the reference machine:

| Sound | Arcade command | When | Voice |
|---|---|---|---|
| Thomas's shout | $02 | every punch and kick | 0 |
| The hit landing | $11 | an AY effect, rendered to a sample at build time; it ends once it has decayed below 10 % | 1 |
| A knife thrown | $15 | an AY effect, rendered | 1 |
| Thomas hurt | $03 + $11 | the hurt voice and the impact, which the arcade sends together on damage | 0 |

`tools/kfm_audio_build.py` encodes the four clips (about 3 KB) and places them in the unused end of SPI RAM bank 0.

**How it fits Mode 748:**
- **The per-line routine.** The mixer decodes in the foreground into a 256-byte queue at SRAM 0x0E00. `src/kfm_sound.s` replaces the kernel's `update_sound` (the stock one is renamed in its object by `objcopy`) and plays one queued sample a line. It is padded to the stock inline mixer's exact contract: SYNC edges at 62 and 129 cycles, 189 in all. The stock code was measured and the new code checked by a static walk of both disassemblies.
- **Filling the queue.** The game tops the queue up twice a frame, in 32-sample chunks, only while there is safe time before the display. Encoded data is read from SPI RAM then, never during the display.
- **Silence is cheap.** When both voices are idle, the queue is filled with silence in a tight loop.

**Cost:**
- **CPU:** about 86 cycles a sample while a sound plays (about 15 lines a frame at 262 samples), about 23 when idle.
- **Pile-ups:** in busy fights deadline mode skips some poses. In a 600-frame test, 75 poses were skipped with sound, against none without.
- **SRAM:** about 400 bytes, leaving 54 RAM tiles instead of 67.
- **Flash:** about 4 KB, so the graphics data now starts at 0x9000.

**Thomas's death** is the arcade's own, captured from the ROM: he staggers, then falls down off the bottom of the screen. The capture stores its two shapes, plus the sideways drift of each frame separately.

**Grabbed:** Thomas can still duck and stand while held, as in the arcade.

**Padded SPI RAM access (`src/kfs.s`).** Everything except the sprite blitter, which pads its own, now uses these: loading, the self-check, the HUD, the pose tables and the sound. They wait about 31 cycles after each byte, where the kernel's routines use the 17-cycle minimum. On an Omega, minimum-timing HUD writes went now and then to the wrong address, leaving a row of the floor map garbled for the rest of the game. The SPI RAM's sequential mode is also re-asserted once a second.

**SPI RAM is nearly full.** `KFM.DAT` is now about 126 KB; the self-check's sums start at 0x1F000.

**READY.** As in the arcade, each life begins with "1-PLAYER 1-FLOOR READY". Thomas walks left by himself for 36 frames (a footstep, $10, every 6), then play and the music begin 133 frames in.

**Music.** The floor's music is the arcade's command $24, recorded from the ROM on the sound board model. It uses one AY chip's channel B (a square-wave melody) and channel A (noise under the decaying envelope: the drums). It is one 15.36 s loop, found in its register-write sequence and stored as a per-frame change stream of about 2.2 KB in SPI RAM bank 0.

`update_sound` generates both channels every scanline, in time that used to be padding, and mixes them with the effects' sample with saturation. The stock cycle contract is kept on every branch path, checked by a static walk of the disassembly. A once-a-frame player (`mus_frame`) reads each frame's changes. The music stops on death, on pause and when the floor is cleared, as the arcade's $00 does.

**Death cry.** On death the arcade sends $07, Thomas's death cry, which is now played.

**Crash log trail (sound builds).** Every scanline, `update_sound` writes into the crash log, in time that was padding:
- its call flags (`Z`, the kernel's ZL);
- the kernel's sync position (`P`);
- the lines since the main loop last came round (`L`).

After a reset, the diagnostics page's first row reads `BOOT nnnn Zxx Pnnn Lnnnn`.

**Sound bisection switches** (add to `EXTRA`):

| Switch | What it does |
|---|---|
| `-DKFM_QUIET` | Every sound at half level |
| `-DKFM_NO_MUSIC` | Effects only |
| `-DKFM_NO_SFX` | Music only |

**The boss** is captured as all his sprites together (colours 8 and 9), since grouping by distance split his wider poses. He is knocked down off the screen when defeated, in his kneeling pose. The capture never records the arcade's own defeat.

**Display self-repair.** On an Omega, a row of the playfield sometimes switched to the wrong tiles for the rest of a game. It scrolled with the background and spanned the full width, which points at that row's tile-window byte in SRAM. Every frame the game now checks the display's row structures (tile windows, the HUD rows, the row selector, the separator) and repairs any that differ. The diagnostics page shows the number of repairs (`F`) and the last damaged address (`@`).

**The intro.** The arcade's own intro, captured from the ROM by `tools/kfm_intro_capture.py`:

1. **The story (the title).** Two screens of text that type out as in the arcade, re-wrapped to 24 columns with each word keeping its colours. The kidnapping scene is replayed frame by frame from the arcade's sprite list: Thomas and Silvia attacked, Silvia carried off; then Thomas finding the letter. The 192-pixel window showing the most of the scene is chosen automatically (arcade x 64–255).
2. **Start:** the letter, with its hands cropped at the sides.
3. **READY.**

On the title, Select still toggles invincibility; a shout means on, a footstep off (the HUD is not shown during the intro).

**How the intro is built:**
- **Tiles:** its 173 tile combinations are one 256-tile window in the flash blob (`KFM_INTRO_TILES`), in the playfield palette.
- **Maps and playback:** the screen maps, the typing lists and the playback (about 12.6 KB) are in `KFMG.DAT` bank 0.
- **Sprites:** 36 of the scene's 55 poses are the game's own (some mirrored); the 13 it adds are in bank 0.
- **Display:** the HUD and playfield rows show 23 rows of picture from line 0, with their own row table and row selector and the playfield palette at the top of the frame. Leaving restores the gameplay set-up.

**Title additions:**
- **Credit.** "© 1984 IREM CORP." sits two rows higher, with "UZEBOX PORT BY / DAN MACDONALD 2026" under it. Its letters are added to the intro tile set, in the story's white.
- **Two grippers.** The kidnapping shows at most two grippers a frame; more of them, and the knife thrower beside them, flickered.

**Start jingle.** The arcade's $20, built from the ROM like the floor music, plays once over the letter.

**Attract mode.** After the story has played once, a demo game runs with automatic input:
- walks for a random 0.5–2.5 s, left three times in four, otherwise right;
- then makes a random attack: a punch, a kick, a crouching punch or kick, or a jump that becomes a flying kick;
- attacks whatever comes close in front;
- shakes grippers off. It ends after 30 s or when Thomas dies, and returns to the story. Start during the demo goes to the letter and a real game.

**Flash limit: 0xE000.** The program and its data end below 0xE000; `kfm_build.py` refuses a blob that would go past it. Writing above it corrupted an Omega's bootloader font: some Uzebox bootloaders keep data there. To fit, the benchmark-only code and tables are left out of the game build (compiled with `-fdata-sections`, so unused tables are dropped). The asset blob is at 0x8C00, and all builds are free of warnings.

**When Thomas dies,** the grippers and knife throwers turn and walk away from where he fell, off the screen, as in the arcade. Grippers from opposite sides used to meet there and walk in place.

**Game over** plays the arcade's game-over jingle ($21, built from the ROM like the start jingle). It shows for about 5.4 s, as the arcade does, while the enemies leave, then returns to the intro and attract mode.

**Firmware and data must match.** (Since the data is packed into `kungfum.uze`, the two always match; the check still guards the `KFMG.DAT` fallback.) Each asset build writes a random build ID into `KFMG.DAT` (bytes 4–7) and into the firmware (`KFG_BUILD_ID`). At start-up the game checks them and stops with a **cyan** screen (error 4) if they differ. A `KFMG.DAT` from another build has its sound, poses and intro at other addresses. With such a file, the music read the laugh's samples as notes, at full scale, which reset an Omega.

The music player also stops on anything that is not music data, and caps the levels at the tune's own. The music starts at full level, as in the arcade. `-DKFM_FADE` fades it in over its first half second instead: an experiment for the resets, which turned out not to depend on it.

**Sound work stays out of the vertical sync.** Every reset logged on an Omega with sound came at the same point: the first pulse of the vertical sync (`Z01 P017`), with the game in its sound work (the mixer's SPI RAM reads, the music). Neither the queue refill nor the music player now runs during the vertical sync or the 6 lines before it; the refill at the end of the frame catches up.

**The music keeps its own time.** The vertical sync counts frames, and the music player plays as many of its per-frame records as are due, up to 4. It runs once a frame, at its start, before the effects are decoded and well clear of the display. Each record (one SPI RAM read) is played only with at least 3 lines left before the display, and any left over wait for the next frame. A second call at the frame's end, just before the display, reset an Omega as the music began. Before, a late frame (an effect being decoded) skipped a record, holding the melody's note and the drums' noise: heard as the effects interrupting the music, and as a hiss on hits. The diagnostics page shows the crashed run's lowest stack reading (`S`) when there is a crash log.

**Switching the display between the intro and the game.** `m74_vaddr` and `m74_rows` are 16-bit pointers written a byte at a time. They are now switched only between the end of the display and the vertical sync, when neither the display nor the sync's interrupt reads them. A half-written pointer would send the display to the wrong rows.

**Finding a hardware-only sound fault.** On an Omega, sound builds reset at the start of play, even without the music. Two test builds separate the two halves of the sound system:

| Build | What runs |
|---|---|
| `make … SOUND=1 STOCK_ISR=1` | The foreground sound: mixer, effects and SPI RAM reads. The kernel's own per-line routine stays, so the game is silent. |
| `make … SOUND=1 EXTRA="… -DKFM_NO_MUSIC -DKFM_NO_SFX"` | The per-line routine, `kfm_sound.s`, with no music and no effects. |
| `make … SOUND=1 STOCK_ISR=1 EXTRA="… -DKFM_NO_MUSIC -DKFM_NO_SFX -DKFM_NO_FILL"` | Neither: only what every sound build has in common (the memory layout, the extra code linked in). |
| `make … SOUND=1 STOCK_ISR=1 EXTRA="… -DKFM_NO_MUSIC -DKFM_NO_SFX"` | The queue refill alone (silence, no SPI RAM reads), with the kernel's per-line routine. |
| `make … SOUND=1 EXTRA="… -DKFM_NO_MUSIC -DKFM_NO_SFX -DKFM_NO_FILL"` | `kfm_sound.s` alone, with no refill, music or effects. |

**Select during the intro** is ignored; it used to switch the HUD rows, which show the intro's picture then, to a HUD page. On the title screen it toggles invincibility as before.

**The diagnostics page's second line** reads `STKnnn Pn Tnnnn Inn Snn`:
- **`STK`:** this run's stack low point.
- **`P`, `T`:** the crashed run's phase and seconds.
- **`I`:** unexpected interrupts this run.
- **`S`:** the crashed run's true stack low point. It is measured at the next start-up from what is left of that run's stack paint, before repainting, so it includes the moment of the crash; 0 means the stack overflowed.

**`-DKFM_BOOT_DIAG`** (test build): no intro. The game starts at once on the diagnostics page, so the crash log of the run before a reset is on screen straight away.

**Unexpected interrupts.** At start-up the game switches off the UART, external and pin-change interrupts; it uses only the kernel's timer interrupt. A catch-all handler (`BADISR_vect`) replaces avr-libc's default jump to address 0, which would restart the game. It switches those sources off again, counts the event, and lets the game carry on; the count is `I` on the diagnostics page. A Uzenet ESP8266 on the board sends its serial output to the AVR's UART: with the receive interrupt left on by a bootloader, every byte would have restarted the game.

**The sample queue's page: `QUEUE=`** (default 0x0E00). The crash log and the end of the RAM tiles move with it. On an Omega, the queue refill alone (writing silence to 0x0E00–0x0EEF, with the kernel's own per-line routine) reset the console within seconds, while the per-line routine alone ran fine. Something on the real system seems to use that page; `QUEUE=0x0D00` tests that, at the cost of 8 RAM tiles.

**Two melody channels.** The start and game-over jingles ($20, $21) play tones on two channels, A and B; the floor's music uses A for noise drums. `update_sound` now generates both tones and the drums. The music data carries channel A's tone step and level: flags bits 3 and 4. Against a full rendering of every AY channel, the game-over jingle's spectrum now matches at 0.99; with channel B alone it was 0.86.

**The boss always appears.** Everything that moves shares 8 slots: enemies, knives, score pop-ups and strikes. Near the end of the floor they could all be in use, and the boss, who appears at camera 40, then never did; Thomas reached the floor's end with no one there. Now ordinary enemies stop arriving 100 pixels before the boss's room, and when the boss appears everything but grippers holding Thomas makes way for him.

**The mixer's per-sample work is in assembly** (`src/kfm_mix2.s`, with `src/kfm_mixvar.c`). The hand-off's 2-voice 2-bit ADPCM variant is compiled unchanged, and the assembly renders batches with each voice's state in registers.
- **A batch** is as long as both voices can go without a buffer refill and the queue has room. Voice 0 writes 128 + its sample, which is always within 0–255; voice 1 adds its sample, clamped.
- **The values are those of the C** (`MixerSample`), checked against it in the emulator:
  - with one voice, every overlapping stretch of the decoded sequence identical;
  - in a fight, 857 of 860, the other 3 a second voice starting a few samples apart.
- **Speed:** in a fight with effects playing, 13,200 cycles a frame against 27,400 for the C, about 8 scan lines a frame freed for drawing.
- **`C_MIXER=1`** builds with the hand-off's C instead, through `select.c`.

**The story borrows the sample queue's page as RAM tiles: 65 instead of 58.** The story is silent, as the arcade's attract mode.
- **Borrowing.** The refill stops; once the queue is empty, its last byte becomes a silent sample (128) and both indices are set to 0. The per-line routine, which repeats `MixerSamples[R - 1]` when the queue is empty, then holds that byte. The page's other 7 blocks (0x0E00–0x0EDF) extend the RAM tiles, which end at the page in a normal build.
- **Handing back.** At the story's end (the demo, or Start for the letter), the tiles shrink back first, and the refill resumes a frame later; the coin voice is about 2 frames later than before.
- **Measured over the whole intro:** dropped cells went from 1,893 to 353. In the worst frame, the knife thrower is now complete.
- **Not in debug builds,** whose crash log sits between the tiles and the page.

**The knife thrower's death** is captured from the ROM by `tools/kfm_capture.py` (`capture_knife_death`). A bot plays the knife throwers' stretch and records the first death with the camera still, from the hit (his 500 or 800 pop-up) until he has gone: 29 frames, 3 shapes.
- **Separating shape from motion.** Each shape gets its own offset from a smooth anchor, which drifts away from Thomas (about 1¾ px a frame) and drops ever faster (71 px). Kept in one orientation (falling to the left), the death is mirrored by the game for the other side.
- **Where it lives.** The shapes are poses in SPI RAM bank 0, after the floor and HUD maps, because bank 1 is full. The table `kfg_knife_die` (pose, x, feet below the floor) is 87 bytes of flash.
- **Shared with the intro.** The story's kidnapping matches two of the shapes, so the intro now shares them.

**Select during the story** hands the borrowed sample queue back, so its sound (the kiai, invincibility on; a step, off) is heard; the story borrows the queue again once both effect voices have finished.

**The story's knives fly off the screen.**
- **Drawing order.** The story draws its small objects (the knives, a cell or two) first, then the rest in the capture's order. Drawn last, the low knife lost its cells whenever the RAM tiles ran out, and vanished beside Sylvia.
- **Extended flight.** The arcade's program clears the low knife's sprite at x 224, before its screen's right edge. `tools/kfm_intro_build.py` continues any knife whose track ends inside the window, at its own speed, until it has left the screen (27 frames added).

**Debugging aids only with `DEBUG=1`.** In a normal build, the crash log is an ordinary variable instead of 32 bytes reserved above the RAM tiles, so the game has 58 RAM tiles instead of 57. Select in play does nothing, and the SPI RAM self-check is off.

**Drawing order and the knife thrower.** RAM tiles run out in busy scenes: one intro scene with the knife thrower, Thomas and Sylvia needs about 73 cells, and there are 58 tiles. Whatever is drawn last loses its lower rows. That was usually the knife thrower: the largest character, so his lower half went missing. In play, knife throwers and the boss are now drawn first, so the shortage falls on the smaller grippers. Drawing the intro's largest poses first was tried, and moved the whole shortage onto Sylvia, so the intro keeps the capture's order.

**Faster sprite set-up, profiled in a busy scene** (several grippers holding Thomas):
- **No divisions.** Each sprite's cost estimate used two 32-bit divisions, nearly 3 lines a frame. They are now multiplications by a reciprocal a little larger than the exact one, so the estimates can only round up.
- **The band and cell loops** choose the flip's base once a sprite and clip the bounds once a band, then walk a pointer. The two loops' time went from about 5,100 to 2,900 cycles a frame.
- Drawing is still byte-exact (`verify_blit.py`, both facings).

**More RAM tiles: 57, from 53.** With `kfm_sound.s`, the kernel's own sound engine (its music player, 5 tracks, patches) is no longer linked; this game has its own mixer and music. `src/kfm_nokernsnd.s` stands in for the little the kernel's core still refers to, at the same sizes: the state it initialises, and an empty `process_music`. This frees 144 bytes. The no-sound and `STOCK_ISR=1` builds keep the kernel's engine.

**SPI padding.** `KFB_SPI_PAD` now defaults to 1. On an Omega, 0 gave drawing artefacts; 1 works and is a cycle a byte faster than 2. `EXTRA="-DKFB_SPI_PAD=2"` is no longer needed.

**The time ticks** as in the arcade. It is updated 20 times a second, and its 4 digits are rewritten on their own whenever it changes; the whole HUD line is still redrawn only every 30 frames.

**The right wall.**
- **Walking to it.** The camera's right end (`CAM_MAX`, the map's end) leaves the wall at screen x 176, too far right for Thomas at his fixed screen spot to reach. Past the camera's end, Thomas walks across the screen instead (`th_cx`, up to 64 px), stopping 3 px short of the wall as the arcade does. Walking left brings him back to his spot before the camera moves.
- **No enemies from the wall.** The wall is map column 221 (`G_WALL_X`). The arcade's camera does not move here at all, and its enemies only come from the left; here, while the wall is on the screen, new enemies only come from the left.

**The stairs.**
- Thomas can walk left only as far as the right edge of the staircase's bottom step (camera 53), and the boss appears from camera 64.
- When the boss is defeated, Thomas climbs the stairs after a short pause: a pixel left and about 1.2 lines up a frame, walking, until he is off the top of the screen. Then the floor clears as before.

**The boss's laugh.** When the boss kills Thomas, his laugh ($04, 2 s) follows Thomas's death cry, as in the arcade. It plays on the effects channel the cry has finished with, not the one the READY footsteps use, and the next life waits until it has finished.

**Start's voice.** The arcade's coin voice ($01) greets Start, from the title, the demo or game over.

**The red strike.** When a knife hits Thomas, the arcade's red strike (code 268, colour 11) shows where it hit, for 12 frames. Start during it begins a new game.

**The story draws every sprite.** Deadline mode is off while the story plays, so the scene doesn't flicker; it is on again for play and the demo.

**`KFMG.DAT`.** The game now has its own data file. Bank 0 keeps only the floor and HUD maps (below 0x1800), then the sound and the intro; bank 1 holds the game's poses. The benchmark keeps `KFM.DAT`, bank 0 only.

**Sound level:** `-DKFM_VOLUME=n`.

| Value | Level |
|---|---|
| 2 | Full (also `-DKFM_LOUD`) |
| 1 | Half, the default: full levels browned out an Omega's supply |
| 0 | A quarter |

**Not there yet:**
- tom-toms;
- the hold and death animations;
- a title screen;
- the other floors;
- an arcade-accurate knife thrower death.

**SPI RAM.** The game's 47 poses (about 53 KB) live in SPI RAM bank 1, so `KFM.DAT` is about 119 KB. Spans are read per pose from SPI RAM; flash holds about 12 bytes of metadata per pose.

**Testing.** `tools/play_test.py --script "start*4;*30;left*900:a/16" --shots 300,600 [--cam N]` plays it headless in cuzebox-ex.

**Default asset address.** The asset blob now defaults to 0x8000 (`ROM_BASE`), because the game's code goes past 0x7000.


A measurement prototype for porting Irem's Kung-Fu Master (1984) to the Uzebox with Video Mode 748 and 128 KB of SPI RAM.

The prototype does the following:

- **Floor 1:** it scrolls, under joypad control or autoplay. The background is the arcade's own map.
- **HUD:** two tile rows recomposed from the arcade panel, with their own palette.
- **Sprites:** Thomas's 4-pose walk and up to six grippers walking in and holding on. All of it is drawn through the real Mode 748 sprite blitter, with masking against the arcade's foreground tiles.
- **Measurement:** it records how much of each vertical blank the frame's work uses.

There is no game logic beyond that: no fighting, collisions, scoring or sound.

## You need

- **avr-gcc and avr-libc**, plus an Uzebox source tree (`kernel/` and `tools/packrom`).
- **[cuzebox-ex](https://github.com/weber21w/cuzebox-ex)**, built with its default (debugger/API) feature set. It is used for running and measuring.
- **Python 3** with numpy and Pillow.
- **An [STEArcade](https://github.com/AnimaInCorpore/STEArcade) checkout.** Its Kung-Fu Master reference machine is imported at build time, not copied; STEArcade has no licence file, so it is not redistributed here.
- **Your own MAME `kungfum.zip`.**

## Build

```sh
make assets ROM=/path/to/kungfum.zip
make UZEBOX=/path/to/uzebox            # LINES=192 for a shorter display
```

This produces `_bin_/kfm748.uze` and `_bin_/KFM.DAT`. Put both in the same directory (the SD card on hardware); the game loads `KFM.DAT` into SPI RAM at boot.

Everything under `gen/`, `build/` and `_bin_/` is derived from the ROM set. Don't distribute it; each user builds their own.

### How the assets are made

`tools/kfm_extract.py` boots the reference machine to the start of floor 1. It then calls the game's own column generator at `$5765` for each of the floor's 224 columns, which yields the whole floor map (1,792 × 208 px). It also records the sprite poses from sprite RAM while Thomas walks into the first grippers.

`tools/kfm_build.py` then does the conversion:

- **Palettes:** it fits a 16-colour 3:3:2 palette for the playfield and another for the HUD.
- **ROM tiles:** it converts the 329 tile/colour combinations into ROM tiles, split into two row bands so that each row's tiles fit Mode 748's 256-tile window. 42 of these tiles get a full mask, because the arcade draws them over sprites.
- **Sprite tiles:** it cuts the poses into 8×8 sprite tiles (190 tiles, 6 KB in SPI RAM), using recolour tables for the arcade's colour sets.

Resource use:

| Resource | Used |
|---|---|
| Flash: kernel + Mode 748 + sprite engine + prototype code | 18.4 KB |
| Flash: tiles, palettes, masks | 12.1 KB |
| SPI RAM | 12 KB (floor map 5.6 KB) |
| RAM tiles available | 75 (SRAM from the end of .bss to the palette buffer) |

## Running it on hardware

Copy `_bin_/kfm748.uze` and `_bin_/KFM.DAT` to the root of the SD card, and flash the `.uze` with the bootloader. You need a Uzebox with the 128 KB SPI RAM fitted: Mode 748 keeps the map and all sprite data there.

### Controls

| Button | Action |
|---|---|
| Select | Number of grippers: 0, 1, … 6, then back to 0 |
| Start | Autoplay on / off. With autoplay off, Left and Right walk |
| A | Blitter: Mode 748 → custom with C allocator → custom with `kfb_alloc` |
| X | Custom blitter deadline mode on / off (see below) |
| B | HUD page: statistics → diagnostics → arcade panel |

**Deadline mode:** when a pose won't fit in the blank time that's left, it is skipped instead of being drawn late. That keeps 60 Hz but makes busy scenes flicker.

### Statistics HUD (updated once a second; the default HUD)

```
CPU 043/046 MAX 048 U37
RT 46/67 MISS 264 G6 APD
```

- **`CPU`:** the average scanlines of CPU time an update took in the last second, out of the blank lines available per frame. Waits for the display to pass are excluded, so these figures match `tools/run_test.py`.
- **`MAX`:** the worst single update in that second.
- **`U`:** updates per second.
- **`RT`:** RAM tiles used by the last update, out of those available.
- **`MISS`:** cells per second left undrawn because the RAM tiles ran out. With the Mode 748 blitter, it counts updates per second that ran out instead.
- **`G`:** number of grippers.
- **Last three letters:**
  - blitter: `M` = Mode 748, `C` = custom with C allocator, `A` = custom with `kfb_alloc`;
  - mode: `P` = autoplay, `J` = joypad;
  - `D` = deadline mode on.

The text is written into the HUD map in SPI RAM after the timed part of each update, so it doesn't affect the measurements. The emulator interface (`bench_*` variables, `kfm_stats`) is unchanged.

### Settings aligned with IK748

These follow IK748, a Mode 748 game known to run on an Omega:

- **Kernel sprite masking is off** (`M74_MSK_ENABLE 0`). With it on, the Mode 748 blitter keeps a mask byte per RAM tile at 0x1020 onwards, inside the main stack.
  - The custom blitter still hides sprites behind foreground tiles, using its own table.
  - The Mode 748 blitter (`M`) now draws over them.
- **Blank-time estimates allow 2 extra lines**, as IK748's `wait_render_done` does.
- **`make LINES=192` uses IK748's exact geometry:** 192 lines from line 32.

### Hardware timing options (after the first Omega tests)

On the Omega, these worked: `M` (slow) and `C` with deadline mode (`D`) best. `A` was garbled, and `C` without `D` flickered. Both of the custom blitter's assembly routines read SPI RAM directly, and `D` keeps all SPI work well away from the display start. So two things in this build can now be adjusted, each with a more cautious default:

| Option | Default | Meaning |
|---|---|---|
| `KFB_SPI_PAD` | 4 | Extra cycles before every raw SPI read and address byte, on top of the kernel's 17-cycle minimum. Each cycle costs about 6% of the pose streaming time. |
| `KFM_SAFETY` | 4 | Lines subtracted from every blank-time estimate. |
| `KFB_ALLOC_LINES` | 9 | Blank lines required before a pose's allocation phase starts. |
| `KFB_STRIP_LINES` | 12 | Blank lines required before a pose's stream starts. |

For example: `make UZEBOX=… LINES=192 EXTRA="-DKFB_SPI_PAD=8 -DKFM_SAFETY=6"`.

**Fixed draw order.** Overlapping sprites no longer flicker. The draw order is also the stacking order, and it used to rotate every frame (for fairness when poses are dropped), which made overlapping grippers swap places. The order is now fixed, and priority rotates only after an update that actually dropped something.

**Deadline mode uses a per-pose cost estimate.** It is built from the pose's stream length and row count, plus the cells it still has to allocate (cells another sprite already allocated are nearly free). It replaces a fixed 16-line threshold, and the same estimate also sets the waits that keep SPI work away from the display start. Calibrated against measured phase times, it is within about 0.2 lines. `-DKFB_FIXED_DEADLINE` restores the old threshold for comparison.

**More arcade-like test scene:**
- A gripper holding Thomas stops him walking, as in the arcade.
- Grippers left far behind re-spawn instead of being parked just off screen. Parked grippers piled up and all walked in together after a turnaround.
- With four or more grippers, the classic pile-up now happens: Thomas plus every gripper overlapping. That exceeds both the CPU time and the RAM tiles.

**More RAM tiles.** Several changes freed SRAM for RAM tiles: 72 at 184 lines, 71 at 192.
- The VRAM row table is in flash.
- Only the displayed playfield rows have VRAM rows.
- The sprite grid is sized for 9 bands.
- Two leftover buffers were removed.

**Pile-ups of four or more grippers** (2,900 emulated frames, APD):

| Build | 4 grippers: poses skipped | 6 grippers: poses skipped |
|---|---|---|
| `LINES=192`, padding 4 | 24% | 16% |
| `LINES=192`, padding 1 | 11% | 8% |
| `LINES=184`, padding 4 | 1.9% | 1.4% |
| `LINES=184`, padding 2 | 0% | 0% |

At 184 lines the floor and the characters' feet are fully visible; only 8 more lines of the decorative base are cropped. The remaining flicker comes from running out of RAM tiles (about 2–4 cells a frame), which shows as small gaps rather than whole sprites disappearing.

**Settings persist:** the blitter and deadline settings are kept in EEPROM, so the program starts in whatever mode was last selected.

### Diagnostics page (press B once)

```
BOOT 00012 SPI 0000 P07
BAD 00000 MCUSR 00 EE OK
```

- **`BOOT`:** how many times the program has started. It is kept in EEPROM block 0x4B46 together with the current settings, which are saved 2 seconds after the last change.
  - If `BOOT` climbs on its own, the console is crashing and restarting.
  - Because the settings survive a reset, you can switch the blitter with `A` and see whether the restarts stop.
  - If the EEPROM isn't formatted, `EE NO` is shown and nothing is written. The program never formats it, because that would erase other games' saves.
- **`SPI`:** SPI RAM self-check errors. At load, the 8-bit sum of each 64-byte chunk of the image goes into the otherwise unused upper 64 KB of SPI RAM. Spare blank time then re-reads one chunk per frame and compares.
  - **`P`:** completed passes over the whole image; one pass takes about 8 seconds.
  - Any non-zero `SPI` means SPI RAM reads aren't reliable on that board.
- **`BAD`:** pose rows the custom blitter rejected because their data would have written outside the sprite's tile grid. Bad data then only causes a visual glitch, where before it could crash.
- **Crash log (game build).** The second row reads `STK nnn LST Pn Tnnnn Snn`. It shows the run before the last reset, from a few bytes at 0x0EE0 that the next start-up reads back. `LST NONE` means no log survived, or there was no earlier run.
  - **`P`:** the phase the game was in.
    - 0: starting up.
    - 1: input and game logic.
    - 2: drawing Thomas.
    - 3: drawing the enemies.
    - 4: after drawing.
    - 5: the HUD.
    - 6: waiting for the display.
    - 7: the SPI RAM self-check.
  - **`T`:** seconds it ran.
  - **`R`:** in the current run, the number of frames in which either controller read exactly Start + Select + Y + B.
  - **`M`** (in the place of `R`, in this build): the MCUSR reset flags at start-up. 01 is power-on, 02 the reset button, 04 a brown-out, 08 the watchdog. The bootloader may clear them.
  - **Bisection builds**, if the game resets on hardware: add any of these to `EXTRA`.
    - `-DKFG_NO_HUD`: no HUD writes to SPI RAM during play.
    - `-DKFG_NO_CHK`: no SPI RAM self-check.
    - `-DKFG_NO_ENEMIES`: Thomas alone.
  - **`make … NOSOFTRESET=1`** compiles the kernel's controller code without the restart that combination triggers, so a phantom reading only counts in `R`.
  - **When a log survived, the first row** shows the buttons held on controllers 1 and 2 in hex: `BOOT nnnn 1:xxxx 2:xxxx`. The Uzebox kernel deliberately restarts into the bootloader when Start + Select + Y + B (0x010F) are held on either controller.
- **`STK`:** the fewest bytes ever left free on the main stack above 0x1020, below which Mode 748 keeps its own data. At 0, the stack has overflowed. The free stack is painted with a marker pattern at start-up, and this figure is a watermark of it.
- **`MCUSR`:** the reset-cause flags at start-up. The bootloader may clear them first. 01 = power-on, 02 = reset button, 04 = brown-out, 08 = watchdog.

### What to look out for

- **Solid-colour screen at boot:** the program stopped itself, and the colour says why.
  - **Red:** SD init failed or `KFM.DAT` was not found.
  - **Magenta:** no SPI RAM answered.
  - **Yellow:** the data read back from SPI RAM is wrong. That suggests no or faulty SPI RAM, or a `KFM.DAT` from a different build; always copy the pair.
  - **Blue:** the asset blob is not where the build expects it.
- **Sprites wrong:** sprites shifted 2 pixels right, pixels doubled and smeared, or garbage rows. The custom blitter's SPI timing was tuned in cuzebox-ex: every read comes 17+ cycles after the byte was clocked out, as in the kernel's own routines. A difference on real hardware would show up like this. Compare with the emulator screenshots, and with `A` set to `M`.
- **Occasional garbage sprites or a reset into the bootloader:** the deadline margins (`KFB_ALLOC_LINES`, `KFB_STRIP_LINES` in `src/main.c`) are too tight for the hardware.
- **Compare the numbers with the emulator.** Expect `CPU` about 15 (Thomas alone) and 25 (two grippers) on `A`, and about 38 on `M` (Thomas alone, scrolling). Six grippers at 216 lines average about 44 at about 36 updates/s.
- **Expected and harmless:**
  - no sound;
  - grippers vanishing now and then in 216-line builds with many grippers (an update spans two frames);
  - small gaps in sprites when `MISS` is non-zero.
- **Display:** a `LINES=192` build gives 24 more blank lines and should hold 60 Hz with six grippers. Mode 748 draws 216 or 192 lines, not Mode 3's 224, which may matter for the OSSC.

## Screen

| Lines | Content | Source |
|---|---|---|
| 0–15 | HUD | VRAM rows 0–1 |
| 16 | Mode 2 separator; loads the playfield palette | VRAM row 2 |
| 17–215 | Playfield: 199 lines × 192 px | Arcade lines 57–255, x 32–223 |

With `LINES=192`, the playfield loses its bottom 24 lines (the decorative base) in exchange for a 24-line-longer blank.

## Measuring

```sh
python3 tools/run_test.py --frames 600 --grips 6 [--budget N] [--nomask] [--nothomas] [--shots 100,200]
```

`run_test.py` runs cuzebox-ex headless through its TCP API. It writes the `bench_*` variables in SRAM and reads `kfm_stats`, then prints the results as JSON. Screenshots go to `build/shots/`, sampled at 7 cycles per pixel from cuzebox-ex's cycle-exact capture.

The program times each update with a post-vsync frame counter and the kernel's scanline counter. Display lines are subtracted, because the main program does not run while Mode 748 renders.

**Calibration.** A 60,000-cycle busy-wait takes 41–43 blank lines. So the main program gets about **1,450 of each blank line's 1,820 cycles**; the sync interrupt and the inline audio mixer take the rest.

### Results (autoplay, floor 1)

216-line display: 46-line blank, about 62k usable cycles.

| Load | 8×8 blits per update | CPU lines per update (avg) | Updates/s | Notes |
|---|---|---|---|---|
| Scrolling and logic only | 0 | 1.4 | 60 | |
| Thomas | 15–21 | 37 | 55 | The 21-blit poses overrun |
| Thomas + 1 gripper | 31–44 | 66 | 30 | |
| Thomas + 4 grippers | 55–70 | 94 | 24 | Out of RAM tiles in 16% of updates |
| Thomas + 6 grippers | 60–83 | 116 | 20 | Out of RAM tiles in 15% |
| 6 grippers, 20-blit budget | ≤ 20 | 33 | 59.4 | About 5.7 of 7 objects left out per update |

192-line display: 70-line blank, about 100k usable cycles.

| Load | Updates/s | Notes |
|---|---|---|
| Thomas | 60 | |
| Thomas + 6 grippers | 30 | Out of RAM tiles in 21% of updates |
| 6 grippers, 40-blit budget | 53 | About 4.6 objects left out per update |

**Cost per blit.** One 8×8 sprite blit, including RAM-tile allocation, costs about 2,500–3,000 cycles in this use. Masking makes little difference. That limits a 60 Hz frame to roughly 20 blits at 216 lines, or about 33 at 192.

**Updates that span fields.** When an update takes more than one blank, Mode 748 displays the half-rebuilt sprite overlay in between. Grippers vanish for a field; you can see this in the screenshots. A clean 30 Hz would need double-buffered VRAM overlays and RAM tiles, and there isn't the SRAM for that.

## The specialised blitter (`src/kfb.s`, `kfb_pose()` in `src/main.c`)

`bench_engine` (`--engine 0|1`) switches between the Mode 748 sprite blitter and a blitter written for this game. The custom blitter is the default; its shared helpers in `src/main.c` are prefixed `kfb_`.

- **Data:** each sprite pose is composed at build time from its arcade sprite parts into one stream in SPI RAM. Colours are final palette indices; nybble 0 is transparent, so black sprite pixels use the darkest other palette entry. Each row is trimmed to its opaque span with a start/length header.
- **Two variants:** each pose is stored twice, pre-shifted for even and odd x. A flip reuses the same data, with swapped nybbles and a reversed write order.
- **Allocation (`kfb_alloc` in `src/kfb.s`):** a per-pose table, precomputed for each vertical phase (y mod 8), gives the tile columns each 8-line band touches. One assembly routine then initialises the pose's cell grid and allocates exactly those cells:
  - a cell that is already a RAM tile is reused without reading the background;
  - the other cells' background indices come in one SPI burst per band, and each next index is clocked in while the current RAM tile is being filled;
  - a new RAM tile is filled from its ROM tile in 160 cycles, or with 32 stores of a constant (64 cycles) when the build tool has marked that ROM tile as a flat colour (`KFB_FILL`; 58% of the background cells in the characters' rows);
  - foreground tiles are skipped.
- **C allocator:** the original C allocator is kept for comparison (`--calloc`).
- **Streaming:** the pose is then one SPI burst. The inner loop overlaps each byte's SPI transfer with a masked write through a 256-byte nybble-transparency table, at 18 cycles per byte (two pixels). Clipped cells point at a scratch tile.
- **SPI timing:** every read must come at least **17 cycles** after the previous clock-out. At 16, the read returns the previous byte. That cost me two bugs, which the tools now catch.
- **Display deadline:** the display deselects SPI RAM at frame start and never restores it. Each allocation pass and each stream therefore checks the blank lines left first, and waits for the display to pass when too few remain. A stream cut off mid-read misaligns its row headers and writes through bad pointers.
- **Verification:** `tools/verify_blit.py [--warm N] [--right]` freezes the prototype and reads back the VRAM rows and RAM tiles. It rebuilds every cell Thomas covers (ROM tile plus decoded pose) in Python and compares them byte for byte. It is exact across all four walk poses, both facings and every x alignment.

### Results (CPU lines per update, and updates/s)

Columns: the Mode 748 blitter, then the custom blitter with the C allocator, then with `kfb_alloc`.

At 216 lines (46-line blank):

| Load | Mode 748 | Custom, C allocator | Custom, `kfb_alloc` |
|---|---|---|---|
| Thomas | 37.8 (54/s) | 18.7 (60/s) | **14.7 (60/s)** |
| Thomas + 2 grippers | 66.8 (30/s) | 31.1 (59/s) | **25.4 (60/s)** |
| Thomas + 4 | 93.2 (25/s) | 42.6 (40/s) | **34.3 (56/s)** |
| Thomas + 6 | 116.3 (20/s) | 54.0 (30/s) | **43.8 (36/s)** |

At 192 lines (70-line blank):

| Load | Mode 748 | Custom, C allocator | Custom, `kfb_alloc` |
|---|---|---|---|
| Thomas | 37.3 (60/s) | 18.0 (60/s) | **14.3 (60/s)** |
| Thomas + 2 grippers | 65.0 (53/s) | 30.7 (60/s) | **24.2 (60/s)** |
| Thomas + 4 | 91.0 (34/s) | 41.5 (60/s) | **33.7 (60/s)** |
| Thomas + 6 | 113.9 (30/s) | 51.1 (60/s) | **41.9 (60/s)** |

The custom blitter with the assembly allocator is **2.5–2.7× faster** than the Mode 748 blitter. The assembly allocator alone saves about 20% of each update.

- **At 192 lines:** Thomas plus six grippers runs at 60 Hz with about 28 lines to spare.
- **At 216 lines:** Thomas plus four runs at 56/s; six averages 43.8 of the 46 lines, so many updates still run over.

**RAM tiles become the next limit.** With six grippers, the 68 RAM tiles run out in about 6–11% of updates, and some cells go undrawn.

**Where a Thomas pose's ~25k cycles (including interrupts) go:**

| Part | Cycles |
|---|---|
| Stream (about 450 bytes) | ~15k |
| Allocation (about 25 RAM tiles over 9 bands; ~11.5k with the C allocator) | ~7k |
| C setup | ~3k |

`make EXTRA=-DKFB_PROFILE` keeps the pose and allocator functions out of line, so their cycle costs can be timed with breakpoints.

**What was tried and didn't help:**

- **Row splitting:** splitting rows at interior transparent gaps would save under 1%, because the arcade art is dense.
- **The stream:** it is near the floor for sprite data streamed from SPI RAM.

**Remaining gains are small:**

- **C setup in assembly:** about 1–2k cycles per pose.
- **Sprite data in flash:** about 30% faster per byte, but flash can't hold the full game's sprites.

## What this means for a port

The prototype's results add up to the following:

- **Background:** the floor map, palettes, masks and HUD are solved, and scrolling costs almost nothing.
- **Sprites:**
  - The Mode 748 blitter manages roughly 20 tiles a frame; the specialised blitter handles about 2.5 times the sprite load.
  - With a 192-line display (playfield cropped by 24 lines of decorative base), the floor 1 load used here runs at 60 Hz.
  - Busier scenes (knife throwers and their knives, tom-toms, the bosses) will need a per-frame budget with rotating priority on top.
  - RAM tiles (about 69 in SRAM) are the next ceiling, more than CPU.
- **Not covered yet:** game logic, sound, and the other floors.

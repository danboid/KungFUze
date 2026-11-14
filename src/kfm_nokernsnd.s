/*
 * Sound builds with kfm_sound.s: the Uzebox kernel's sound engine (uzeboxSoundEngine.c and
 * uzeboxSoundEngineCore.s: its music player, 5 tracks, patches) is not linked; this game has
 * its own mixer and music. What the kernel's core still refers to is here, the same sizes:
 * the state it initialises (mixer, sound_enabled, the noise channel's) and process_music,
 * called once a frame by the video core, which has nothing to do. Freed: about 140 bytes
 * of RAM, 4 more RAM tiles.
 */
.section .bss.kfm_nokernsnd
.global sound_enabled
.global mixer
.global tr4_params
.global tr4_barrel_lo
.global tr4_barrel_hi
sound_enabled: .space 1
mixer:         .space 19
tr4_params:    .space 1
tr4_barrel_lo: .space 1
tr4_barrel_hi: .space 1

.section .text.kfm_nokernsnd
.global process_music
process_music:
	ret

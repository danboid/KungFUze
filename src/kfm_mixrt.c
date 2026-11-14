/*
 * The KungFuMasterAudio hand-off's mixer runtime (mixers/runtime.c), with its queue at a
 * fixed SRAM page (MixerSamples = 0x0E00, given to the linker) instead of a 256-byte
 * aligned array, which could waste up to 255 bytes of SRAM in padding. The mixer itself
 * (mixers/select.c with MIXER_PCM_ADPCM2_2V) is compiled from the hand-off unchanged.
 * Copied Uzebox/hand-off code: GPL, as the original.
 */
#include "mixer.h"

/* The floor's music: its generator runs in update_sound (src/kfm_sound.s) every scanline;
** mus_frame (here, once a frame) sets its pitch and levels from the music stream in SPI RAM. */
volatile uint16_t mus_phase, mus_step, mus_lfsr = 1U, mus_phase2, mus_step2;
volatile uint8_t  mus_tvol, mus_dvol, mus_tvol2;
volatile int8_t   mus_next;
volatile uint8_t MixerReadIndex, MixerWriteIndex, MixerLastSample = 128;

void MixerRender(uint16_t samples)
{
	uint8_t write = MixerWriteIndex;
	while (samples--) {
		while ((uint8_t)(write - MixerReadIndex) >= 240) {}
		MixerSamples[write] = MixerSample(); write++; MixerWriteIndex = write;
	}
}

extern uint16_t kfm_render_active(uint16_t maxSamples);    /* src/kfm_mixvar.c (assembly inside) */

uint16_t MixerRenderAvailable(uint16_t maxSamples)
{
	uint8_t write = MixerWriteIndex; uint16_t produced = 0;
	if (!MixerVoiceActive(0) && !MixerVoiceActive(1)) {
		/* kfm748: both voices idle (most of the time): queue silence in a tight loop instead
		** of running the mixer for it (about 86 cycles a sample) */
		while (produced < maxSamples && (uint8_t)(write - MixerReadIndex) < 240) {
			MixerSamples[write] = 128U; write++; produced++;
		}
		MixerWriteIndex = write;
		return produced;
	}
#ifndef KFM_C_MIXER
	return kfm_render_active(maxSamples);
#endif
	while (produced < maxSamples && (uint8_t)(write - MixerReadIndex) < 240) {
		uint8_t value = MixerSample(); MixerSamples[write] = value;
		write++; MixerWriteIndex = write; produced++;
	}
	return produced;
}

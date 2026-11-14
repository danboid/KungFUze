/*
 * The KungFuMasterAudio mixer's 2-voice 2-bit ADPCM variant (third_party/kfa_mixer, unchanged),
 * compiled here instead of through select.c, with the per-sample render in assembly
 * (src/kfm_mix2.s): the same values as the C (MixerSample), about half the time.
 * This file exports the variant's voices and step table to the assembly, and refills a voice's
 * 32-byte buffer from SPI RAM (CodecByte's refill) when the assembly asks for it.
 */
#include <stddef.h>
#include "variants/pcm_adpcm2_2v.c"

/* the assembly's view of the voices and the table (same objects) */
extern CodecVoice kfm_voices[2] __attribute__((alias("codecVoice")));
extern const uint8_t kfm_adpcm2_tab[90][8] __attribute__((alias("codecTable_adpcm2")));

/* the field offsets src/kfm_mix2.s uses */
_Static_assert(sizeof(CodecVoice) == 57, "CodecVoice layout");
_Static_assert(offsetof(CodecVoice, samplesLo) == 8 && offsetof(CodecVoice, samplesHi) == 10, "CodecVoice layout");
_Static_assert(offsetof(CodecVoice, packed) == 12 && offsetof(CodecVoice, bits) == 14, "CodecVoice layout");
_Static_assert(offsetof(CodecVoice, accumulator) == 15 && offsetof(CodecVoice, state) == 16, "CodecVoice layout");
_Static_assert(offsetof(CodecVoice, gain) == 17 && offsetof(CodecVoice, index) == 18, "CodecVoice layout");
_Static_assert(offsetof(CodecVoice, count) == 19 && offsetof(CodecVoice, active) == 20, "CodecVoice layout");
_Static_assert(offsetof(CodecVoice, buffer) == 21, "CodecVoice layout");

extern uint8_t kfm_mix2(uint8_t n);      /* src/kfm_mix2.s: up to n samples; fewer: refill or queue full */

/* render up to maxSamples with at least one voice active */
uint16_t kfm_render_active(uint16_t maxSamples)
{
	uint16_t done = 0U;
	while (done < maxSamples) {
		uint8_t n = (maxSamples - done > 32U) ? 32U : (uint8_t)(maxSamples - done);
		uint8_t got = kfm_mix2(n), v, refilled = 0U;
		done += got;
		if (got) { continue; }               /* (a batch: as long as both voices and the queue allow) */
		for (v = 0U; v < 2U; v++) {           /* a voice waiting for bytes: CodecByte's refill */
			CodecVoice *p = &codecVoice[v];
			if (p->active && !p->bits && p->index == p->count) {
				uint8_t m = p->bytes > 32U ? 32U : (uint8_t)p->bytes;
				if (!m) { p->active = 0U; }
				else { codecRead(p->position, p->buffer, m); p->position += m; p->bytes -= m; p->index = 0U; p->count = m; }
				refilled = 1U;
			}
		}
		if (!refilled) { break; }              /* the queue is full */
	}
	return done;
}

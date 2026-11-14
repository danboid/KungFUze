#pragma once
#include <stdint.h>
/* Exactly one MIXER_<PROFILE>=1 selects a ready-made implementation.
 * mixer_config.h rejects zero or multiple selections at compile time. */
#include "mixer_config.h"
typedef void (*MixerReadFn)(uint32_t position,uint8_t *destination,uint8_t bytes);
typedef struct {
    uint32_t position,bytes,samples;
    uint8_t accumulator,state;
} MixerClip;
void MixerInit(MixerReadFn read);
/* gain:128=unity;28 gives one voice headroom;14 each for two voices.
 * Clip codec must match the selected mixer. All counts exclude byte padding.
 * Calls and register writes run in foreground, before rendering their sample. */
uint8_t MixerPlay(uint8_t voice,const MixerClip *clip,uint8_t gain);
void MixerStopVoice(uint8_t voice);
void MixerStop(void);
uint8_t MixerVoiceActive(uint8_t voice);
/* Queues complete samples. Call during blanking; do not call from an ISR.
 * Every262 lines require262 samples. Caller schedules exact register events
 * by rendering up to that sample before writing the new registers. */
void MixerRender(uint16_t samples);
/* Fill up to maxSamples, returning the exact number produced. Returns when
 * the queue is full so the caller can run other foreground jobs. This only
 * avoids queue-space waits; a game's SPI callback may still have its own. */
uint16_t MixerRenderAvailable(uint16_t maxSamples);
uint8_t MixerSample(void); /* Single-sample entry point, useful for host QA. */
void MixerAyWrite(uint8_t reg,uint8_t value);
void MixerAyEffectStart(void);
void MixerAyEffectWrite(uint8_t reg,uint8_t value);
void MixerAyEffectStop(void);
/* Native2A03 pulse1/pulse2/triangle/noise. External ADPCM replaces DMC;
 * this API does not interpret6502 code or implement NES DMC DMA/IRQs. */
void MixerApuWrite(uint16_t address,uint8_t value);
extern volatile uint8_t MixerReadIndex,MixerWriteIndex,MixerLastSample;
extern volatile uint8_t MixerSamples[256];

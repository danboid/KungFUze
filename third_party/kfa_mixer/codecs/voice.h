#pragma once
#include "../mixer.h"
typedef struct {
    uint32_t position,bytes;
    uint16_t samplesLo,samplesHi;
    /* Byte access for1/2/4-bit codecs avoids16-bit shifts and high-byte
     * stores. Only3-bit symbols need the full cross-byte reservoir. */
    union {uint16_t reservoir;uint8_t packed;};
    uint8_t bits,accumulator,state,gain,index,count,active;
    uint8_t buffer[32];
    uint8_t slope,up,down,neutral;
} CodecVoice;
/* Exact signed8 * unsigned8 gain, shifted7. The result fits signed8 for
 * gain<=128. Portable C below is smaller source, but AVR expands its signed
 *16-bit multiply/shift into substantially more per-sample work. */
static inline __attribute__((always_inline)) int16_t CodecScale(uint8_t sample,uint8_t gain){
#ifdef __AVR__
    int8_t centered=sample^128,result;
    asm("mulsu %1,%2\n\tlsl r0\n\trol r1\n\tmov %0,r1\n\tclr r1"
        : "=&r"(result) : "a"(centered),"a"(gain) : "r0","r1","cc");
    return result;
#else
    return ((int16_t)sample-128)*gain>>7;
#endif
}
/* Most samples only need the low16 counter. Touch the high word on a
 * borrow or final-zero test, instead of loading/storing all32 bits every
 * sample. Exact for lengths above65536 as well as short voice clips. */
static inline __attribute__((always_inline)) void CodecAdvance(CodecVoice *p){
    if(p->samplesLo){
        if(!--p->samplesLo&&!p->samplesHi)p->active=0;
    }else{
        p->samplesLo=UINT16_MAX;p->samplesHi--;
    }
}
static CodecVoice codecVoice[MIXER_VOICES];
static MixerReadFn codecRead;
static void CodecInit(MixerReadFn read){codecRead=read;for(uint8_t v=0;v<MIXER_VOICES;v++)codecVoice[v].active=0;}
uint8_t MixerPlay(uint8_t voice,const MixerClip *clip,uint8_t gain){
    if(voice>=MIXER_VOICES||!codecRead||!clip||!clip->samples||gain>128)return 0;
    uint8_t bits=voice?MIXER_SECOND_BITS:MIXER_BITS;
    /* Split the ceil(samples*bits/8) calculation before multiplying. This
     * avoids overflow for long streams and keeps the small tail arithmetic
     *8-bit; no division helper is needed for the byte boundary. */
    uint32_t required=(clip->samples>>3)*bits+(((clip->samples&7)*bits+7)>>3);
    if(clip->state>=(voice?MIXER_SECOND_STATES:MIXER_CODEC_STATES)||clip->bytes<required)return 0;
    CodecVoice *p=&codecVoice[voice];p->active=0;
    p->position=clip->position;p->bytes=clip->bytes;p->samplesLo=clip->samples;p->samplesHi=clip->samples>>16;
    p->accumulator=clip->accumulator;p->state=clip->state;p->gain=gain;
    p->reservoir=0;p->bits=p->index=p->count=0;
    p->slope=2;p->up=p->down=p->neutral=0;p->active=1;return 1;
}
void MixerStopVoice(uint8_t voice){if(voice<MIXER_VOICES)codecVoice[voice].active=0;}
uint8_t MixerVoiceActive(uint8_t voice){return voice<MIXER_VOICES&&codecVoice[voice].active;}
static inline uint8_t CodecByte(CodecVoice *p){
    if(p->index==p->count){
        uint8_t n=p->bytes>32?32:p->bytes;
        /* Explicit byte/sample lengths, no reserved adaptive terminator. */
        if(!n){p->active=0;return 0;}
        codecRead(p->position,p->buffer,n);p->position+=n;p->bytes-=n;
        p->index=0;p->count=n;
    }
    return p->buffer[p->index++];
}

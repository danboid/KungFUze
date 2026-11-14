/* Generated explicit adpcm2 decoder. See tools/mixer_codecs.py for packing.
 * Decode runs in foreground into the complete waveform queue. */
#include <avr/pgmspace.h>
#include "voice.h"
static const uint8_t codecTable_adpcm2[90][8] PROGMEM={
#include "adpcm2_table.inc"
};
static inline __attribute__((always_inline)) int16_t CodecSample_adpcm2(CodecVoice *p){
    if(!p->active)return 0;
    if(!p->bits){p->packed=CodecByte(p);p->bits=8;}
    uint8_t code=p->packed&3;p->packed>>=2;p->bits-=2;
    const uint8_t *entry=codecTable_adpcm2[p->state]+code*2;
    p->accumulator+=pgm_read_byte(entry);p->state=pgm_read_byte(entry+1);
    CodecAdvance(p);
    return CodecScale(p->accumulator,p->gain);
}

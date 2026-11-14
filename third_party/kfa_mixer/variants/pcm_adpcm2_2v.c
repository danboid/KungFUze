/* Ready-made PCM + adpcm2 + adpcm2.
 * Generated, deliberately explicit; no runtime codec dispatch. */
#include "../codecs/adpcm2.c"
#include "../cores/pcm.c"
void MixerInit(MixerReadFn read){CodecInit(read);ChipInit();MixerLastSample=128;}
void MixerStop(void){MixerStopVoice(0);MixerStopVoice(1);ChipStop();}
uint8_t MixerSample(void){
    int16_t sample=128+ChipSample();
    sample+=CodecSample_adpcm2(&codecVoice[0]);
    sample+=CodecSample_adpcm2(&codecVoice[1]);
    if(sample<0)sample=0;else if(sample>255)sample=255;
    return sample;
}

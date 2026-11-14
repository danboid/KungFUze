#include "mixer.h"
#if defined(MIXER_PCM_ADPCM1_1V) && MIXER_PCM_ADPCM1_1V
#include "variants/pcm_adpcm1_1v.c"
#elif defined(MIXER_PCM_ADPCM1_2V) && MIXER_PCM_ADPCM1_2V
#include "variants/pcm_adpcm1_2v.c"
#elif defined(MIXER_PCM_ADPCM1_ADPCM2_2V) && MIXER_PCM_ADPCM1_ADPCM2_2V
#include "variants/pcm_adpcm1_adpcm2_2v.c"
#elif defined(MIXER_PCM_ADPCM1_ADPCM3_2V) && MIXER_PCM_ADPCM1_ADPCM3_2V
#include "variants/pcm_adpcm1_adpcm3_2v.c"
#elif defined(MIXER_PCM_ADPCM1_ADPCM4_2V) && MIXER_PCM_ADPCM1_ADPCM4_2V
#include "variants/pcm_adpcm1_adpcm4_2v.c"
#elif defined(MIXER_PCM_ADPCM1_FIXED4_2V) && MIXER_PCM_ADPCM1_FIXED4_2V
#include "variants/pcm_adpcm1_fixed4_2v.c"
#elif defined(MIXER_PCM_ADPCM2_1V) && MIXER_PCM_ADPCM2_1V
#include "variants/pcm_adpcm2_1v.c"
#elif defined(MIXER_PCM_ADPCM2_ADPCM1_2V) && MIXER_PCM_ADPCM2_ADPCM1_2V
#include "variants/pcm_adpcm2_adpcm1_2v.c"
#elif defined(MIXER_PCM_ADPCM2_2V) && MIXER_PCM_ADPCM2_2V
#include "variants/pcm_adpcm2_2v.c"
#elif defined(MIXER_PCM_ADPCM2_ADPCM3_2V) && MIXER_PCM_ADPCM2_ADPCM3_2V
#include "variants/pcm_adpcm2_adpcm3_2v.c"
#elif defined(MIXER_PCM_ADPCM2_ADPCM4_2V) && MIXER_PCM_ADPCM2_ADPCM4_2V
#include "variants/pcm_adpcm2_adpcm4_2v.c"
#elif defined(MIXER_PCM_ADPCM2_FIXED4_2V) && MIXER_PCM_ADPCM2_FIXED4_2V
#include "variants/pcm_adpcm2_fixed4_2v.c"
#elif defined(MIXER_PCM_ADPCM3_1V) && MIXER_PCM_ADPCM3_1V
#include "variants/pcm_adpcm3_1v.c"
#elif defined(MIXER_PCM_ADPCM3_ADPCM1_2V) && MIXER_PCM_ADPCM3_ADPCM1_2V
#include "variants/pcm_adpcm3_adpcm1_2v.c"
#elif defined(MIXER_PCM_ADPCM3_ADPCM2_2V) && MIXER_PCM_ADPCM3_ADPCM2_2V
#include "variants/pcm_adpcm3_adpcm2_2v.c"
#elif defined(MIXER_PCM_ADPCM3_2V) && MIXER_PCM_ADPCM3_2V
#include "variants/pcm_adpcm3_2v.c"
#elif defined(MIXER_PCM_ADPCM3_ADPCM4_2V) && MIXER_PCM_ADPCM3_ADPCM4_2V
#include "variants/pcm_adpcm3_adpcm4_2v.c"
#elif defined(MIXER_PCM_ADPCM3_FIXED4_2V) && MIXER_PCM_ADPCM3_FIXED4_2V
#include "variants/pcm_adpcm3_fixed4_2v.c"
#elif defined(MIXER_PCM_ADPCM4_1V) && MIXER_PCM_ADPCM4_1V
#include "variants/pcm_adpcm4_1v.c"
#elif defined(MIXER_PCM_ADPCM4_ADPCM1_2V) && MIXER_PCM_ADPCM4_ADPCM1_2V
#include "variants/pcm_adpcm4_adpcm1_2v.c"
#elif defined(MIXER_PCM_ADPCM4_ADPCM2_2V) && MIXER_PCM_ADPCM4_ADPCM2_2V
#include "variants/pcm_adpcm4_adpcm2_2v.c"
#elif defined(MIXER_PCM_ADPCM4_ADPCM3_2V) && MIXER_PCM_ADPCM4_ADPCM3_2V
#include "variants/pcm_adpcm4_adpcm3_2v.c"
#elif defined(MIXER_PCM_ADPCM4_2V) && MIXER_PCM_ADPCM4_2V
#include "variants/pcm_adpcm4_2v.c"
#elif defined(MIXER_PCM_ADPCM4_FIXED4_2V) && MIXER_PCM_ADPCM4_FIXED4_2V
#include "variants/pcm_adpcm4_fixed4_2v.c"
#elif defined(MIXER_PCM_FIXED4_1V) && MIXER_PCM_FIXED4_1V
#include "variants/pcm_fixed4_1v.c"
#elif defined(MIXER_PCM_FIXED4_ADPCM1_2V) && MIXER_PCM_FIXED4_ADPCM1_2V
#include "variants/pcm_fixed4_adpcm1_2v.c"
#elif defined(MIXER_PCM_FIXED4_ADPCM2_2V) && MIXER_PCM_FIXED4_ADPCM2_2V
#include "variants/pcm_fixed4_adpcm2_2v.c"
#elif defined(MIXER_PCM_FIXED4_ADPCM3_2V) && MIXER_PCM_FIXED4_ADPCM3_2V
#include "variants/pcm_fixed4_adpcm3_2v.c"
#elif defined(MIXER_PCM_FIXED4_ADPCM4_2V) && MIXER_PCM_FIXED4_ADPCM4_2V
#include "variants/pcm_fixed4_adpcm4_2v.c"
#elif defined(MIXER_PCM_FIXED4_2V) && MIXER_PCM_FIXED4_2V
#include "variants/pcm_fixed4_2v.c"
#elif defined(MIXER_AY_ADPCM1_1V) && MIXER_AY_ADPCM1_1V
#include "variants/ay_adpcm1_1v.c"
#elif defined(MIXER_AY_ADPCM1_2V) && MIXER_AY_ADPCM1_2V
#include "variants/ay_adpcm1_2v.c"
#elif defined(MIXER_AY_ADPCM1_ADPCM2_2V) && MIXER_AY_ADPCM1_ADPCM2_2V
#include "variants/ay_adpcm1_adpcm2_2v.c"
#elif defined(MIXER_AY_ADPCM1_ADPCM3_2V) && MIXER_AY_ADPCM1_ADPCM3_2V
#include "variants/ay_adpcm1_adpcm3_2v.c"
#elif defined(MIXER_AY_ADPCM1_ADPCM4_2V) && MIXER_AY_ADPCM1_ADPCM4_2V
#include "variants/ay_adpcm1_adpcm4_2v.c"
#elif defined(MIXER_AY_ADPCM1_FIXED4_2V) && MIXER_AY_ADPCM1_FIXED4_2V
#include "variants/ay_adpcm1_fixed4_2v.c"
#elif defined(MIXER_AY_ADPCM2_1V) && MIXER_AY_ADPCM2_1V
#include "variants/ay_adpcm2_1v.c"
#elif defined(MIXER_AY_ADPCM2_ADPCM1_2V) && MIXER_AY_ADPCM2_ADPCM1_2V
#include "variants/ay_adpcm2_adpcm1_2v.c"
#elif defined(MIXER_AY_ADPCM2_2V) && MIXER_AY_ADPCM2_2V
#include "variants/ay_adpcm2_2v.c"
#elif defined(MIXER_AY_ADPCM2_ADPCM3_2V) && MIXER_AY_ADPCM2_ADPCM3_2V
#include "variants/ay_adpcm2_adpcm3_2v.c"
#elif defined(MIXER_AY_ADPCM2_ADPCM4_2V) && MIXER_AY_ADPCM2_ADPCM4_2V
#include "variants/ay_adpcm2_adpcm4_2v.c"
#elif defined(MIXER_AY_ADPCM2_FIXED4_2V) && MIXER_AY_ADPCM2_FIXED4_2V
#include "variants/ay_adpcm2_fixed4_2v.c"
#elif defined(MIXER_AY_ADPCM3_1V) && MIXER_AY_ADPCM3_1V
#include "variants/ay_adpcm3_1v.c"
#elif defined(MIXER_AY_ADPCM3_ADPCM1_2V) && MIXER_AY_ADPCM3_ADPCM1_2V
#include "variants/ay_adpcm3_adpcm1_2v.c"
#elif defined(MIXER_AY_ADPCM3_ADPCM2_2V) && MIXER_AY_ADPCM3_ADPCM2_2V
#include "variants/ay_adpcm3_adpcm2_2v.c"
#elif defined(MIXER_AY_ADPCM3_2V) && MIXER_AY_ADPCM3_2V
#include "variants/ay_adpcm3_2v.c"
#elif defined(MIXER_AY_ADPCM3_ADPCM4_2V) && MIXER_AY_ADPCM3_ADPCM4_2V
#include "variants/ay_adpcm3_adpcm4_2v.c"
#elif defined(MIXER_AY_ADPCM3_FIXED4_2V) && MIXER_AY_ADPCM3_FIXED4_2V
#include "variants/ay_adpcm3_fixed4_2v.c"
#elif defined(MIXER_AY_ADPCM4_1V) && MIXER_AY_ADPCM4_1V
#include "variants/ay_adpcm4_1v.c"
#elif defined(MIXER_AY_ADPCM4_ADPCM1_2V) && MIXER_AY_ADPCM4_ADPCM1_2V
#include "variants/ay_adpcm4_adpcm1_2v.c"
#elif defined(MIXER_AY_ADPCM4_ADPCM2_2V) && MIXER_AY_ADPCM4_ADPCM2_2V
#include "variants/ay_adpcm4_adpcm2_2v.c"
#elif defined(MIXER_AY_ADPCM4_ADPCM3_2V) && MIXER_AY_ADPCM4_ADPCM3_2V
#include "variants/ay_adpcm4_adpcm3_2v.c"
#elif defined(MIXER_AY_ADPCM4_2V) && MIXER_AY_ADPCM4_2V
#include "variants/ay_adpcm4_2v.c"
#elif defined(MIXER_AY_ADPCM4_FIXED4_2V) && MIXER_AY_ADPCM4_FIXED4_2V
#include "variants/ay_adpcm4_fixed4_2v.c"
#elif defined(MIXER_AY_FIXED4_1V) && MIXER_AY_FIXED4_1V
#include "variants/ay_fixed4_1v.c"
#elif defined(MIXER_AY_FIXED4_ADPCM1_2V) && MIXER_AY_FIXED4_ADPCM1_2V
#include "variants/ay_fixed4_adpcm1_2v.c"
#elif defined(MIXER_AY_FIXED4_ADPCM2_2V) && MIXER_AY_FIXED4_ADPCM2_2V
#include "variants/ay_fixed4_adpcm2_2v.c"
#elif defined(MIXER_AY_FIXED4_ADPCM3_2V) && MIXER_AY_FIXED4_ADPCM3_2V
#include "variants/ay_fixed4_adpcm3_2v.c"
#elif defined(MIXER_AY_FIXED4_ADPCM4_2V) && MIXER_AY_FIXED4_ADPCM4_2V
#include "variants/ay_fixed4_adpcm4_2v.c"
#elif defined(MIXER_AY_FIXED4_2V) && MIXER_AY_FIXED4_2V
#include "variants/ay_fixed4_2v.c"
#elif defined(MIXER_APU_ADPCM1_1V) && MIXER_APU_ADPCM1_1V
#include "variants/apu_adpcm1_1v.c"
#elif defined(MIXER_APU_ADPCM1_2V) && MIXER_APU_ADPCM1_2V
#include "variants/apu_adpcm1_2v.c"
#elif defined(MIXER_APU_ADPCM1_ADPCM2_2V) && MIXER_APU_ADPCM1_ADPCM2_2V
#include "variants/apu_adpcm1_adpcm2_2v.c"
#elif defined(MIXER_APU_ADPCM1_ADPCM3_2V) && MIXER_APU_ADPCM1_ADPCM3_2V
#include "variants/apu_adpcm1_adpcm3_2v.c"
#elif defined(MIXER_APU_ADPCM1_ADPCM4_2V) && MIXER_APU_ADPCM1_ADPCM4_2V
#include "variants/apu_adpcm1_adpcm4_2v.c"
#elif defined(MIXER_APU_ADPCM1_FIXED4_2V) && MIXER_APU_ADPCM1_FIXED4_2V
#include "variants/apu_adpcm1_fixed4_2v.c"
#elif defined(MIXER_APU_ADPCM2_1V) && MIXER_APU_ADPCM2_1V
#include "variants/apu_adpcm2_1v.c"
#elif defined(MIXER_APU_ADPCM2_ADPCM1_2V) && MIXER_APU_ADPCM2_ADPCM1_2V
#include "variants/apu_adpcm2_adpcm1_2v.c"
#elif defined(MIXER_APU_ADPCM2_2V) && MIXER_APU_ADPCM2_2V
#include "variants/apu_adpcm2_2v.c"
#elif defined(MIXER_APU_ADPCM2_ADPCM3_2V) && MIXER_APU_ADPCM2_ADPCM3_2V
#include "variants/apu_adpcm2_adpcm3_2v.c"
#elif defined(MIXER_APU_ADPCM2_ADPCM4_2V) && MIXER_APU_ADPCM2_ADPCM4_2V
#include "variants/apu_adpcm2_adpcm4_2v.c"
#elif defined(MIXER_APU_ADPCM2_FIXED4_2V) && MIXER_APU_ADPCM2_FIXED4_2V
#include "variants/apu_adpcm2_fixed4_2v.c"
#elif defined(MIXER_APU_ADPCM3_1V) && MIXER_APU_ADPCM3_1V
#include "variants/apu_adpcm3_1v.c"
#elif defined(MIXER_APU_ADPCM3_ADPCM1_2V) && MIXER_APU_ADPCM3_ADPCM1_2V
#include "variants/apu_adpcm3_adpcm1_2v.c"
#elif defined(MIXER_APU_ADPCM3_ADPCM2_2V) && MIXER_APU_ADPCM3_ADPCM2_2V
#include "variants/apu_adpcm3_adpcm2_2v.c"
#elif defined(MIXER_APU_ADPCM3_2V) && MIXER_APU_ADPCM3_2V
#include "variants/apu_adpcm3_2v.c"
#elif defined(MIXER_APU_ADPCM3_ADPCM4_2V) && MIXER_APU_ADPCM3_ADPCM4_2V
#include "variants/apu_adpcm3_adpcm4_2v.c"
#elif defined(MIXER_APU_ADPCM3_FIXED4_2V) && MIXER_APU_ADPCM3_FIXED4_2V
#include "variants/apu_adpcm3_fixed4_2v.c"
#elif defined(MIXER_APU_ADPCM4_1V) && MIXER_APU_ADPCM4_1V
#include "variants/apu_adpcm4_1v.c"
#elif defined(MIXER_APU_ADPCM4_ADPCM1_2V) && MIXER_APU_ADPCM4_ADPCM1_2V
#include "variants/apu_adpcm4_adpcm1_2v.c"
#elif defined(MIXER_APU_ADPCM4_ADPCM2_2V) && MIXER_APU_ADPCM4_ADPCM2_2V
#include "variants/apu_adpcm4_adpcm2_2v.c"
#elif defined(MIXER_APU_ADPCM4_ADPCM3_2V) && MIXER_APU_ADPCM4_ADPCM3_2V
#include "variants/apu_adpcm4_adpcm3_2v.c"
#elif defined(MIXER_APU_ADPCM4_2V) && MIXER_APU_ADPCM4_2V
#include "variants/apu_adpcm4_2v.c"
#elif defined(MIXER_APU_ADPCM4_FIXED4_2V) && MIXER_APU_ADPCM4_FIXED4_2V
#include "variants/apu_adpcm4_fixed4_2v.c"
#elif defined(MIXER_APU_FIXED4_1V) && MIXER_APU_FIXED4_1V
#include "variants/apu_fixed4_1v.c"
#elif defined(MIXER_APU_FIXED4_ADPCM1_2V) && MIXER_APU_FIXED4_ADPCM1_2V
#include "variants/apu_fixed4_adpcm1_2v.c"
#elif defined(MIXER_APU_FIXED4_ADPCM2_2V) && MIXER_APU_FIXED4_ADPCM2_2V
#include "variants/apu_fixed4_adpcm2_2v.c"
#elif defined(MIXER_APU_FIXED4_ADPCM3_2V) && MIXER_APU_FIXED4_ADPCM3_2V
#include "variants/apu_fixed4_adpcm3_2v.c"
#elif defined(MIXER_APU_FIXED4_ADPCM4_2V) && MIXER_APU_FIXED4_ADPCM4_2V
#include "variants/apu_fixed4_adpcm4_2v.c"
#elif defined(MIXER_APU_FIXED4_2V) && MIXER_APU_FIXED4_2V
#include "variants/apu_fixed4_2v.c"
#endif

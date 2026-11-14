/* PCM-only profile: all output headroom belongs to the selected ADPCM voices. */
static void ChipInit(void){}
static void ChipStop(void){}
static inline int16_t ChipSample(void){return 0;}

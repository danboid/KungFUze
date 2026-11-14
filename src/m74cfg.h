/* Mode 748 configuration for the Kung-Fu Master prototype (included into every
** kernel and game source with -include). Addresses come from the generated
** gen/kfm_rom.h (tools/kfm_build.py). */
#ifndef M74CFG_H
#define M74CFG_H
#include "kfm_rom.h"

#define M74_SPR_ENABLE     1
/* Kernel sprite masking off (as in IK748, which is known to run on an Omega): with it on, the
** Mode 748 blitter keeps a mask index per RAM tile at M74_RAMMASKIDX_OFF (0x1020 + tile number),
** inside the main stack's 224 bytes. The custom blitter uses the ROM mask index table below on
** its own, so foreground tiles still cover sprites there; the Mode 748 blitter ('M') now draws
** sprites over them. */
#define M74_MSK_ENABLE     0
#define M74_RECTB_OFF      KFM_RECTB
/* one mask index byte per 32-byte ROM tile slot, starting at the first tile */
#define M74_ROMMASKIDX_OFF (KFM_MASKIDX - (KFM_TILES / 32))
#define M74_ROMMASK_OFF    0      /* only zero (0xFE) and full (0xFF) masks are used */
/* the custom blitter's flat-colour table, addressed like the mask indices */
#define KFB_FILL_OFF       (KFB_FILL - (KFM_TILES / 32))
/* Extra cycles before every raw SPI RAM read of the custom blitter (see src/kfb.s); 1 is
** what fit the Omega best. Override with make EXTRA=-DKFB_SPI_PAD=n. */
/* Sound level: KFM_VOLUME 2 full (the default, the arcade's level), 1 half, 0 a quarter.
** Override with make EXTRA=-DKFM_VOLUME=n. */
#ifndef KFM_VOLUME
#define KFM_VOLUME 2
#endif

#ifndef KFB_SPI_PAD
#define KFB_SPI_PAD        1
#endif
#endif

/* KFM_DEBUG (make DEBUG=1): the statistics and diagnostics pages, the crash log at a fixed
** address that survives a reset, the SPI RAM self-check. KFM_BOOT_DIAG implies it. */
#if defined(KFM_BOOT_DIAG) && !defined(KFM_DEBUG)
#define KFM_DEBUG
#endif
